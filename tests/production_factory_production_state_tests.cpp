#include "thandor/simulation.hpp"

#include <cassert>
#include <iostream>

int main(){
    using namespace thandor;
    assert(simulation_class13_candidate_compatible(0x20U,0x24U));
    assert(!simulation_class13_candidate_compatible(0x20U,0x04U));

    SimulationClass13ProductionState state;
    simulation_class13_begin_production(state,0x155U,3U,0x90);
    assert(state.phase==SimulationClass13ProductionPhase::Building);
    assert(state.animated_subnode_ticks==0U);
    assert(!simulation_class13_tick_build(state));
    assert(state.animated_subnode_ticks==1U);
    assert(!simulation_class13_tick_build(state));
    assert(simulation_class13_tick_build(state));
    assert(state.animated_subnode_ticks==3U);
    // Spawn failure retries state 1; a successful create explicitly promotes it.
    assert(simulation_class13_tick_build(state));
    assert(state.animated_subnode_ticks==4U);
    simulation_class13_mark_spawned(state,17U);
    assert(state.phase==SimulationClass13ProductionPhase::Opening);
    assert(state.active_energy_demand_q4==0);
    const auto frozen_animation_tick=state.animated_subnode_ticks;

    // 0x20000/tick reaches the exact 0x80000 clamp on tick four.
    assert(!simulation_class13_tick_open(state,0x20000));
    assert(!simulation_class13_tick_open(state,0x20000));
    assert(!simulation_class13_tick_open(state,0x20000));
    assert(simulation_class13_tick_open(state,0x20000));
    assert(state.primary_texture_offset_v==0x80000);
    assert(state.phase==SimulationClass13ProductionPhase::WaitingForProducedUnitClear);
    assert(!simulation_class13_observe_produced_unit_link(state,true));
    assert(simulation_class13_observe_produced_unit_link(state,false));
    assert(state.phase==SimulationClass13ProductionPhase::Closing);
    assert(!simulation_class13_tick_close(state,0x20000));
    assert(!simulation_class13_tick_close(state,0x20000));
    assert(!simulation_class13_tick_close(state,0x20000));
    assert(simulation_class13_tick_close(state,0x20000));
    assert(state.phase==SimulationClass13ProductionPhase::Idle);
    assert(state.primary_texture_offset_v==0);
    assert(state.animated_subnode_ticks==frozen_animation_tick);

    std::cout << "V543_FACTORY_PRODUCTION_STATE PASS\n";
    return 0;
}
