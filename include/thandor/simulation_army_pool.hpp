#pragma once
#include "thandor/field.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
namespace thandor {
// Native ArmyRuntimeSlot addresses survive release. Neither projectile ownership
// nor temporary placement probes carry an entity generation check.
struct SimulationArmyPoolSlot {
    bool occupied{};
    uint32_t faction{},army{},command_flags{};
    int32_t command_generation{};
    std::optional<size_t> entity_index,target_slot;
    Vec3f position{};
    uint32_t retained_waypoint_count{};
    std::array<std::array<int32_t,2>,8> retained_waypoints{};
};
struct SimulationArmyPool {
    bool enabled{};
    uint32_t tick{};
    std::vector<SimulationArmyPoolSlot> slots;
    static constexpr size_t capacity=1024;
    std::optional<size_t> begin_create(uint32_t army,uint32_t faction) {
        if(!enabled)return {};
        const auto free=std::find_if(slots.begin(),slots.end(),[](auto& s){return !s.occupied;});
        const size_t index=size_t(free-slots.begin());
        if(index==capacity)return {};
        if(free==slots.end())slots.emplace_back();
        // Native writes these before model allocation. An allocation failure
        // retains the previous command fields in this still-vacant record.
        slots[index].army=army;slots[index].faction=std::min(faction,7U);
        if(std::getenv("THANDOR_SIMLAB_ARMY_POOL_TRACE"))std::fprintf(stderr,"ARMY_POOL_CREATE %u %zu %u %u\n",tick,index,army,faction);
        return index;
    }
    void finish_create(size_t index,Vec3f position={}) {
        auto& slot=slots.at(index);const auto faction=slot.faction,army=slot.army;
        const auto count=slot.retained_waypoint_count;
        const auto waypoints=slot.retained_waypoints;
        slot={true,faction,army,0U,0,{}, {},position};
        // ArmyRuntime_CreateInstance leaves the waypoint tail untouched.
        slot.retained_waypoint_count=count;slot.retained_waypoints=waypoints;
        // The constructor writes the linked-child coordinate aliases at C0/C4,
        // which are also the first queued waypoint. The count and tail survive.
        slot.retained_waypoints[0]={float_to_q12(position.x),float_to_q12(position.y)};
    }
    std::optional<size_t> create(uint32_t army,uint32_t faction,Vec3f position={}) {
        const auto index=begin_create(army,faction);
        if(index)finish_create(*index,position);
        return index;
    }
    void release(size_t index) {
        if(index>=slots.size())return;
        if(std::getenv("THANDOR_SIMLAB_ARMY_POOL_TRACE"))std::fprintf(stderr,"ARMY_POOL_FREE %u %zu %u %u\n",tick,index,slots[index].army,slots[index].faction);
        slots[index].occupied=false;slots[index].entity_index.reset();
    }
};
struct SimulationArmyPlacementSeed {
    size_t source_placement_index{};
    uint32_t army{},faction{};
};
}
