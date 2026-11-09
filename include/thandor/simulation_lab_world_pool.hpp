#pragma once

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace thandor {
inline constexpr std::size_t kSimulationLabNoWorldSlot = SIZE_MAX;

// MODEL, SHT and EFF share this first-free arena. Releasing a node clears
// allocation flags; the pose remains available to a subsequent initializer.
struct SimulationLabWorldPose {
    std::array<std::int32_t, 3> position{};
    std::uint32_t yaw{};
};

struct SimulationLabWorldObjectPool {
    enum class Kind : std::uint32_t { Model, Shot, Effect };
    struct Record {
        SimulationLabWorldPose pose;
        bool allocated{};
    };
    // Handles are identities of individual allocations. The numerical slot
    // is resolved when the tick's operations are committed in owner order.
    struct Handle {
        std::size_t slot{kSimulationLabNoWorldSlot};
        std::uint64_t owner_order{};
        SimulationLabWorldPose retained;
        Kind kind{};
        std::uint32_t definition{};
    };
    struct Operation {
        std::uint64_t owner_order{};
        std::size_t handle{};
        bool claim{};
        SimulationLabWorldPose pose;
        int callback_phase{};
        std::size_t component_order{};
    };
    class OwnerScope {
    public:
        OwnerScope(SimulationLabWorldObjectPool& pool,std::uint64_t owner_order,bool force=false) noexcept
            :pool_(pool),previous_(pool.owner_order_),previous_component_(pool.component_order_),
             previous_phase_(pool.callback_phase_) {
            if(owner_order||force){
                if(owner_order!=pool_.owner_order_||force){
                    pool_.component_order_=0;pool_.callback_phase_=0;
                }
                pool_.owner_order_=owner_order;
            }
        }
        OwnerScope(const OwnerScope&)=delete;
        OwnerScope& operator=(const OwnerScope&)=delete;
        ~OwnerScope(){pool_.owner_order_=previous_;pool_.component_order_=previous_component_;
            pool_.callback_phase_=previous_phase_;}
    private:
        SimulationLabWorldObjectPool& pool_;
        std::uint64_t previous_;
        std::size_t previous_component_;
        int previous_phase_;
    };
    class ComponentScope {
    public:
        ComponentScope(SimulationLabWorldObjectPool& pool,std::size_t component,int phase) noexcept
            :pool_(pool),previous_(pool.component_order_),previous_phase_(pool.callback_phase_){
            pool_.component_order_=component;pool_.callback_phase_=phase;
        }
        ComponentScope(const ComponentScope&)=delete;
        ComponentScope& operator=(const ComponentScope&)=delete;
        ~ComponentScope(){pool_.component_order_=previous_;pool_.callback_phase_=previous_phase_;}
    private:
        SimulationLabWorldObjectPool& pool_;
        std::size_t previous_;
        int previous_phase_;
    };

    std::size_t claim(Kind kind,std::uint32_t definition,bool before_owner_body=false,bool after_owner_body=false) {
        const auto handle=handles_.size();
        handles_.push_back({kSimulationLabNoWorldSlot,0,{},kind,definition});
        schedule({owner_order_,handle,true,{},before_owner_body?1:(after_owner_body?-1:callback_phase_)});
        return handle;
    }

    void release(std::size_t handle, SimulationLabWorldPose last_pose) {
        if(handle<handles_.size())schedule({owner_order_,handle,false,last_pose,callback_phase_});
    }

    const SimulationLabWorldPose& retained_pose(std::size_t handle) const noexcept {
        return handles_[handle].retained;
    }
    std::size_t slot(std::size_t handle) const noexcept {
        return handle<handles_.size()?handles_[handle].slot:kSimulationLabNoWorldSlot;
    }
    std::uint64_t owner_order(std::size_t handle) const noexcept {
        return handle<handles_.size()?handles_[handle].owner_order:0;
    }
    OwnerScope enter_owner(std::size_t handle) noexcept {
        return OwnerScope(*this,owner_order(handle));
    }
    OwnerScope enter_commands() noexcept {return OwnerScope(*this,UINT64_MAX);}
    OwnerScope enter_world_job(bool active) noexcept {return OwnerScope(*this,0,active);}
    ComponentScope enter_component(std::size_t component,int phase=0) noexcept {
        return ComponentScope(*this,component,phase);
    }
    void begin_tick(std::uint32_t tick) noexcept {
        tick_=tick;batching_=true;owner_order_=0;component_order_=0;callback_phase_=0;
    }
    void commit_tick() {
        // The editor stages production and effects separately. The native
        // arena sees their allocations inside the newest-first owner walk.
        std::stable_sort(operations_.begin(),operations_.end(),[](const auto& a,const auto& b){
            if(a.owner_order!=b.owner_order)return a.owner_order>b.owner_order;
            if(a.component_order!=b.component_order)return a.component_order<b.component_order;
            return a.callback_phase>b.callback_phase;
        });
        for(const auto& op:operations_)apply(op);
        operations_.clear();batching_=false;
    }

private:
    void schedule(Operation op){
        op.component_order=component_order_;
        if(batching_)operations_.push_back(op);
        else apply(op);
    }
    void apply(const Operation& op){
        auto& handle=handles_[op.handle];
        auto pose=op.pose;
        std::size_t slot=handle.slot;
        if(op.claim){
            slot=first_free_;
            if(slot==records_.size())records_.emplace_back();
            handle.slot=slot;
            handle.owner_order=next_owner_order_++;
            handle.retained=pose=records_[slot].pose;
            records_[slot].allocated=true;
            while(first_free_<records_.size()&&records_[first_free_].allocated)++first_free_;
        }else{
            if(slot>=records_.size()||!records_[slot].allocated)return;
            records_[slot]={pose,false};
            handle.slot=kSimulationLabNoWorldSlot;
            first_free_=std::min(first_free_,slot);
        }
        if(std::getenv("THANDOR_SIMLAB_WORLD_POOL_TRACE"))
            std::fprintf(stderr,"%s tick=%u slot=%zu class=%u def=%u yaw=%u xyz=%d,%d,%d owner=%llu\n",
                op.claim?"WALLOC":"WFREE",tick_,slot,unsigned(handle.kind),handle.definition,
                pose.yaw,pose.position[0],pose.position[1],pose.position[2],
                static_cast<unsigned long long>(op.owner_order));
    }

    std::vector<Record> records_;
    std::vector<Handle> handles_;
    std::vector<Operation> operations_;
    std::size_t first_free_{};
    std::uint64_t owner_order_{},next_owner_order_{1};
    std::size_t component_order_{};
    int callback_phase_{};
    std::uint32_t tick_{};
    bool batching_{};
};
}
