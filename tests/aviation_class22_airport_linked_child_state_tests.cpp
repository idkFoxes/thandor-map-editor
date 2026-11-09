#include "thandor/simulation.hpp"

#include <cassert>
#include <iostream>

int main(){
    using namespace thandor;

    assert(simulation_class22_candidate_compatible(0x08U));
    assert(simulation_class22_candidate_compatible(0x18U));
    assert(!simulation_class22_candidate_compatible(0x04U));

    SimulationClass22AirportState airport;
    simulation_class22_configure_storage(airport,3U,0);
    assert(airport.storage_capacity==3U);

    // Defensive editor-state hardening: stale occupied bits must not underflow the derived
    // availability count when a snapshot is reconfigured before its stored ids are restored.
    SimulationClass22AirportState malformed;
    malformed.occupied_slot_mask=0x0007U;
    simulation_class22_configure_storage(malformed,3U,0);
    assert(malformed.stored_slot_count==0U);
    assert(malformed.available_stored_count==0U);
    simulation_class22_configure_storage(malformed,0U,0);
    assert(malformed.occupied_slot_mask==0U);

    assert(simulation_class22_begin_storage_build(airport,240U,2U,0x30));
    assert(!simulation_class22_tick_storage_build(airport));
    assert(simulation_class22_tick_storage_build(airport));
    assert(simulation_class22_complete_storage_build(airport,true));
    // Retail completion scans capacity-1 down to zero and picks the highest free slot.
    assert(airport.stored_army_asset_ids[2]==240U);
    assert(airport.stored_slot_count==1U&&airport.available_stored_count==1U);

    assert(simulation_class22_begin_storage_build(airport,242U,1U,0x20));
    assert(simulation_class22_tick_storage_build(airport));
    assert(simulation_class22_complete_storage_build(airport,true));
    assert(airport.stored_army_asset_ids[1]==242U);
    assert(airport.stored_slot_count==2U&&airport.available_stored_count==2U);

    // Completion clears the active build even when the executable's final energy test rejects
    // storage of the completed id.
    assert(simulation_class22_begin_storage_build(airport,244U,1U,0x10));
    assert(simulation_class22_tick_storage_build(airport));
    assert(!simulation_class22_complete_storage_build(airport,false));
    assert(airport.build_phase==SimulationClass22BuildPhase::Idle);
    assert(airport.active_energy_demand_q4==0);
    assert(airport.stored_slot_count==2U);

    const auto world_command=simulation_class22_world_launch_command(0x111U,0x222,0x333);
    assert(world_command.value_a==0x111&&world_command.value_b==0x222&&world_command.value_c==0x333);
    simulation_class22_queue_launch_mask(airport,3U,world_command.value_a,world_command.value_b,world_command.value_c);
    assert(airport.pending_launch_counts[0]==1U);
    assert(airport.pending_launch_counts[1]==1U);
    assert(airport.pending_launch_counts[2]==0U);

    auto request=simulation_class22_take_next_pending_launch(airport);
    assert(request&&request->lane==0U&&request->army_asset_id==240U);
    assert(request->command.value_a==0x111&&request->command.value_b==0x222&&
           request->command.value_c==0x333);
    const auto slot=simulation_class22_find_free_stored_slot(airport,request->army_asset_id);
    assert(slot&&*slot==2U);
    assert(simulation_class22_mark_linked_child_spawned(airport,*slot));
    assert(airport.dispatch_phase==SimulationClass22DispatchPhase::Opening);
    assert((airport.occupied_slot_mask&(1U<<2U))!=0U);
    assert(airport.available_stored_count==1U);

    // Selection counts the persistent stored id even while its transient child instance occupies
    // the slot; the spawner's free-bit test is a later, separate gate in the executable.
    simulation_class22_queue_launch_mask(airport,1U,7,8,9);
    assert(airport.pending_launch_counts[0]==1U);

    assert(!simulation_class22_tick_open(airport,0x20000));
    assert(!simulation_class22_tick_open(airport,0x20000));
    assert(!simulation_class22_tick_open(airport,0x20000));
    assert(simulation_class22_tick_open(airport,0x20000));
    assert(airport.primary_texture_offset_v==0x80000);
    assert(airport.dispatch_phase==SimulationClass22DispatchPhase::RaisingPlatform);

    // State 2 uses a strict 'ceiling < z' edge, so exact equality remains state 2 for one tick.
    assert(!simulation_class22_tick_raise_platform(airport,0x1000,0x3000));
    assert(!simulation_class22_tick_raise_platform(airport,0x1000,0x3000));
    assert(!simulation_class22_tick_raise_platform(airport,0x1000,0x3000));
    assert(simulation_class22_tick_raise_platform(airport,0x1000,0x3000));
    assert(airport.platform_local_z_q12==0x3000);
    assert(airport.dispatch_phase==SimulationClass22DispatchPhase::WaitingForChildRelease);

    SimulationClass21LinkedChildState child;
    assert(simulation_class21_begin_outbound_when_platform_raised(
        child,airport.dispatch_phase,2,4));
    assert(!simulation_class21_tick_outbound_release(child));
    assert(simulation_class21_tick_outbound_release(child));
    // The child, not a parent timer, owns the external 3->4 transition.
    assert(simulation_class22_signal_child_release(airport));
    assert(airport.dispatch_phase==SimulationClass22DispatchPhase::LoweringPlatform);

    // State 4 likewise crosses strictly below the model-lookup baseline before clamping.
    assert(!simulation_class22_tick_lower_platform(airport,0x1000,0));
    assert(!simulation_class22_tick_lower_platform(airport,0x1000,0));
    assert(!simulation_class22_tick_lower_platform(airport,0x1000,0));
    assert(simulation_class22_tick_lower_platform(airport,0x1000,0));
    assert(airport.platform_local_z_q12==0);
    assert(airport.dispatch_phase==SimulationClass22DispatchPhase::Closing);

    assert(!simulation_class22_tick_close(airport,0x20000));
    assert(!simulation_class22_tick_close(airport,0x20000));
    assert(!simulation_class22_tick_close(airport,0x20000));
    assert(simulation_class22_tick_close(airport,0x20000));
    assert(airport.primary_texture_offset_v==0);
    assert(airport.dispatch_phase==SimulationClass22DispatchPhase::Reset);
    assert(simulation_class22_reset_dispatch_cycle(airport));
    assert(airport.dispatch_phase==SimulationClass22DispatchPhase::Idle);

    // Finish the outbound lifetime: the state changes offmap only when the countdown becomes
    // negative, then a later mission-completion path enters OffmapReturn.
    assert(!simulation_class21_tick_outbound_release(child));
    assert(!simulation_class21_tick_outbound_release(child));
    assert(!simulation_class21_tick_outbound_release(child));
    assert(child.phase==SimulationClass21LinkedChildPhase::OffmapOutbound);
    assert(simulation_class21_begin_mission_pass(child,2));
    assert(child.phase==SimulationClass21LinkedChildPhase::MissionPass);
    assert(!simulation_class21_tick_mission_pass(child));
    assert(!simulation_class21_tick_mission_pass(child));
    assert(simulation_class21_tick_mission_pass(child));
    assert(child.phase==SimulationClass21LinkedChildPhase::OffmapReturn);
    assert(simulation_class21_begin_return_when_parent_idle(child,airport,2));
    assert(airport.dispatch_phase==SimulationClass22DispatchPhase::Opening);
    assert(child.phase==SimulationClass21LinkedChildPhase::ReturningToPlatform);
    assert(!simulation_class21_tick_return_to_platform(child,airport));
    assert(!simulation_class21_tick_return_to_platform(child,airport));
    assert(simulation_class21_tick_return_to_platform(child,airport));
    assert(child.phase==SimulationClass21LinkedChildPhase::Attached);
    assert(airport.dispatch_phase==SimulationClass22DispatchPhase::LoweringPlatform);

    // Returned child runtime is transient. Its later destruction releases only the occupied bit;
    // the persistent stored aircraft id remains reusable.
    simulation_class22_release_linked_child_slot(airport,*slot);
    assert((airport.occupied_slot_mask&(1U<<2U))==0U);
    assert(airport.stored_army_asset_ids[2]==240U);
    assert(airport.available_stored_count==2U);

    std::cout<<"V544_CLASS22_AIRPORT_LINKED_CHILD_STATE PASS\n";
    return 0;
}
