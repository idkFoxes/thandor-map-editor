#include "thandor/simulation_lab_phase7_preview.hpp"
#include "thandor/simulation_lab_phase7_owner.hpp"
#include "thandor/fog_preview.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>

using namespace thandor;
int main() {
    SimulationLabRun run{};
    constexpr std::size_t cells=64;
    auto retained=std::make_shared<SimulationLabRetailScratchImage>();
    retained->field_occupancy_masks.assign(cells,0);
    retained->field_occupancy_masks[8]=0x80ULL<<16U;
    retained->presence_update_tick=7;
    run.retail_scratch=retained;
    run.current_visibility_occupancy.assign(cells,0);
    run.current_visibility_occupancy[8]=0x88ULL<<16U; // stale marker
    run.current_visibility_occupancy_tick=7;
    run.tick=7;
    assert(!simulation_lab_phase7_preview_has_world_owner(run));
    assert(simulation_lab_phase7_preview_uses_retained_field(run,cells));
    // Replayed field has persistent exploration, but no false current visibility.
    assert(((run.retail_scratch->field_occupancy_masks[8]>>16U)&0xFFU)==0x80U);
    assert(!simulation_lab_phase7_preview_uses_retained_field(run,cells+1));
    retained->presence_update_tick=6;
    assert(!simulation_lab_phase7_preview_uses_retained_field(run,cells));
    retained->presence_update_tick=7;
    run.static_placement_owner_count=1; // mission scenery remains linked
    assert(simulation_lab_phase7_preview_has_world_owner(run));
    assert(!simulation_lab_phase7_preview_uses_retained_field(run,cells));
    run.static_placement_owner_count=0;
    run.entities.resize(1);
    auto& actor=run.entities[0];
    actor.root_model_component_index=0;
    actor.model_components.resize(1);
    actor.root_model_terminal=true;
    assert(simulation_lab_phase7_preview_has_world_owner(run));
    assert(!simulation_lab_phase7_preview_uses_retained_field(run,cells));
    actor.model_components[0].runtime_removed=true;
    assert(!simulation_lab_phase7_preview_has_world_owner(run));
    assert(simulation_lab_phase7_preview_uses_retained_field(run,cells));
    actor.model_components[0].runtime_removed=false;
    actor.runtime_removed=true;
    assert(simulation_lab_phase7_preview_uses_retained_field(run,cells));
    // Native callback frequency is 2 at phase 3 with owners, 1 without;
    // phase 7 is one with owners and zero when the last linked owner is gone.
    // Test the exact gate used by the renderer, independent of its UI state.
    assert(retail_fog_clamp_calls_for_simulation_tick(3,true)==2);
    assert(retail_fog_clamp_calls_for_simulation_tick(3,false)==1);
    assert(retail_fog_clamp_calls_for_simulation_tick(7,true)==1);
    assert(retail_fog_clamp_calls_for_simulation_tick(7,false)==0);
    std::cout << "V734_PHASE7_PREVIEW PASS\n";
}
