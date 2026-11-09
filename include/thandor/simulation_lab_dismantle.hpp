#pragma once
#include "thandor/simulation_lab.hpp"
#include <bit>
#include <span>
namespace thandor {
struct SimulationLabDismantleStep { uint32_t refund_q4{};bool completed{};bool advanced{}; };
// The shared model callback advances this clock even before dismantling starts.
inline SimulationLabDismantleStep simulation_lab_advance_component_dismantling(
        SimulationLabModelComponentState& c,uint32_t xenite_value_q4,uint32_t ticks=1U) noexcept {
    c.dismantle_countdown=std::bit_cast<int32_t>(uint32_t(c.dismantle_countdown)-ticks);
    if(c.dismantle_countdown>=0)return {};
    c.dismantle_countdown=std::bit_cast<int32_t>(uint32_t(c.dismantle_countdown)+12U);
    if(!(c.runtime_flags_ec&0x10U)||!xenite_value_q4||!c.maximum_integrity||c.integrity<=0)return {};
    SimulationLabDismantleStep result{xenite_value_q4>>5,false,true};
    const auto drain=uint32_t(c.maximum_integrity)>>4;
    const auto previous=c.integrity;
    c.integrity=std::bit_cast<int32_t>(uint32_t(previous)-drain);
    if(previous<=int32_t(drain)){
        c.integrity=0;c.runtime_flags_ec^=0x210U;c.runtime_flags_ec|=0x20U;
        result.completed=true;
    }
    return result;
}
// ArmyRuntime_ResetMovementStateFromModel cancels dismantling on a live root,
// recursively clearing 0x218 while retaining the no-regeneration flag 0x400.
inline void simulation_lab_cancel_dismantling_on_stop(SimulationLabEntity& e) noexcept {
    if(e.root_model_component_index>=e.model_components.size())return;
    const auto& root=e.model_components[e.root_model_component_index];
    if(!(root.runtime_flags_ec&0x10U)||root.integrity==0)return;
    for(auto& c:e.model_components)if(!c.runtime_removed){
        c.runtime_flags_ec&=~0x218U;c.terminal_runtime_flag8=false;
    }
    e.runtime_flags&=~0x218U;e.root_model_terminal=false;
}
// ModelRuntimeHierarchy_MarkDestroyedRecursive leaves the model linked until
// the dismantling callback consumes its remaining integrity.
inline void simulation_lab_mark_destroyed_hierarchy(SimulationLabEntity& entity) noexcept {
    for(size_t index=0;index<entity.model_components.size();++index){
        auto& component=entity.model_components[index];
        const bool root=index==entity.root_model_component_index;
        const auto flags=component.runtime_flags_ec|(component.terminal_runtime_flag8?8U:0U)|
            (root?entity.runtime_flags:0U);
        if(component.runtime_removed||(flags&8U))continue;
        component.runtime_flags_ec|=0x418U;
        component.terminal_runtime_flag8=true;
        if(root){
            entity.runtime_flags|=0x418U;
            entity.root_model_terminal=true;
        }
    }
}
// AiFactionPlanning_UpdateActiveEntityPressureFlag: once established, a
// faction below the structure/armed-unit threshold dismantles admitted owners.
inline void simulation_lab_update_active_entity_pressure_flag(SimulationLabRun& run,uint32_t faction,
        std::span<const size_t> structures,std::span<const size_t> units) {
    if(faction>=run.ai_active_entity_pressure_latched.size())return;
    int needed=2;
    for(const auto i:structures)if(i<run.entities.size()){
        const auto arm=run.entities[i].army_asset_id;
        if(arm<340U||(arm<380U&&--needed==0)){
            run.ai_active_entity_pressure_latched[faction]=true;return;
        }
    }
    ++needed;
    for(const auto i:units)if(i<run.entities.size()){
        const auto& e=run.entities[i];size_t attachment=0;bool armed=false;
        for(const auto& c:e.model_components)if(c.parent_component_index==e.root_model_component_index){
            if(attachment<2U&&!c.runtime_removed)armed=true;
            ++attachment;
        }
        if(armed&&--needed==0){run.ai_active_entity_pressure_latched[faction]=true;return;}
    }
    if(!run.ai_active_entity_pressure_latched[faction])return;
    const auto mark=[&](size_t i){
        if(i<run.entities.size())simulation_lab_mark_destroyed_hierarchy(run.entities[i]);
    };
    for(const auto i:structures)mark(i);
    for(const auto i:units)mark(i);
}
} // namespace thandor
