#pragma once
#include "thandor/simulation_lab.hpp"
namespace thandor {
inline void simulation_lab_claim_army_slot(SimulationLabRun& run,SimulationLabEntity& e,size_t entity_index){
    if(!run.army_pool.enabled||e.native_army_pool_slot!=SIZE_MAX)return;
    // A paid factory job can complete after defeat. Native construction still
    // allocates its ArmyRuntimeSlot; faction lifecycle controls AI, not creation.
    if(e.faction_index>=8U)return;
    if(const auto slot=run.army_pool.create(e.army_asset_id,e.faction_index,e.actor.position)){
        e.native_army_pool_slot=*slot;run.army_pool.slots[*slot].entity_index=entity_index;
        const auto& retained=run.army_pool.slots[*slot];
        e.combat_movement.queue_count=e.pioneer_movement.queue_count=retained.retained_waypoint_count;
        for(size_t q=0;q<8;++q){
            const auto& point=retained.retained_waypoints[q];
            e.combat_movement.queue[q]=e.pioneer_movement.queue[q]={point[0],point[1]};
        }
    }
}
inline void simulation_lab_capture_army_slot(SimulationLabRun& run,SimulationLabEntity& e){
    const auto index=e.native_army_pool_slot;
    if(!run.army_pool.enabled||index>=run.army_pool.slots.size())return;
    auto& slot=run.army_pool.slots[index];
    if(!slot.occupied||!slot.entity_index||*slot.entity_index>=run.entities.size()||
       &run.entities[*slot.entity_index]!=&e)return;
    slot.faction=e.faction_index;slot.army=e.army_asset_id;
    slot.command_flags=e.combat_command_mode_flags;slot.command_generation=e.combat_command_generation;
    slot.position=e.actor.position;slot.target_slot.reset();
    const auto& movement=e.pioneer_movement_initialized&&!e.production_linked_to_parent?
        e.pioneer_movement:e.combat_movement;
    slot.retained_waypoint_count=movement.queue_count;
    for(size_t q=0;q<8;++q)slot.retained_waypoints[q]={movement.queue[q].x,movement.queue[q].y};
    if(e.target_entity_index&&*e.target_entity_index<run.entities.size()){
        const auto target=run.entities[*e.target_entity_index].native_army_pool_slot;
        if(target!=SIZE_MAX)slot.target_slot=target;
    }
    for(auto& p:run.projectiles)if(p.owner_kind!=SimulationProjectileOwnerKind::Environmental&&
        p.owner_placement_index==*slot.entity_index&&p.owner_army_pool_slot==SIZE_MAX)
        p.owner_army_pool_slot=index;
}
inline void simulation_lab_release_army_slot(SimulationLabRun& run,SimulationLabEntity& e){
    simulation_lab_capture_army_slot(run,e);
    if(run.army_pool.enabled)run.army_pool.release(e.native_army_pool_slot);
    e.native_army_pool_slot=SIZE_MAX;
}
struct SimulationLabArmyOwnerView {
    bool present{};
    uint32_t faction{},command_flags{};
    std::optional<uint32_t> target_faction;
    size_t entity_index{SIZE_MAX};
};
inline SimulationLabArmyOwnerView simulation_lab_army_owner(const SimulationLabRun& run,size_t index){
    if(!run.army_pool.enabled||index>=run.army_pool.slots.size())return {};
    const auto& slot=run.army_pool.slots[index];
    SimulationLabArmyOwnerView view{true,slot.faction,slot.command_flags,{},SIZE_MAX};
    if(slot.occupied&&slot.entity_index&&*slot.entity_index<run.entities.size()){
        const auto& e=run.entities[*slot.entity_index];
        if(!e.runtime_removed&&e.native_army_pool_slot==index){
            view.faction=e.faction_index;view.command_flags=e.combat_command_mode_flags;
            view.entity_index=*slot.entity_index;
            if(e.target_entity_index&&*e.target_entity_index<run.entities.size())
                view.target_faction=run.entities[*e.target_entity_index].faction_index;
            return view;
        }
    }
    if(slot.target_slot&&*slot.target_slot<run.army_pool.slots.size())
        view.target_faction=run.army_pool.slots[*slot.target_slot].faction;
    return view;
}
}
