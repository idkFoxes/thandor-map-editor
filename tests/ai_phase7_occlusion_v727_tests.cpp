#include "thandor/simulation_lab_phase7_owner.hpp"
#include <cassert>

using namespace thandor;

int main() {
    SimulationLabRun run;
    run.level_runtime_enabled=true;
    run.active_view_faction_index=1U;
    SimulationLabEntity owner;
    owner.faction_index=2U;
    owner.root_model_component_index=0U;
    owner.model_components.resize(1);
    owner.fog_uncover_radius_q12=0;
    owner.occupancy_mark_radius_units=0U;
    owner.construction_complete=false;
    owner.root_model_terminal=true;
    owner.model_components[0].runtime_flags_ec=0x10U;
    // An owned, linked model emits LOS even when unfinished, disabled for
    // orders, or waiting for a terminal effect. The zero radius still scans.
    assert(simulation_lab_phase7_occlusion_owner_eligible(run,owner));
    assert(simulation_lab_phase7_occlusion_preview_radius(run,owner)==1U);
    // The active-view relationship applies ONLY to independent sector bit-1.
    run.faction_packed_relation_states[2]=0x8U<<8U; // self-visible but not allied to active view
    assert(!simulation_lab_phase7_sector_owner_eligible(run,owner));
    assert(simulation_lab_phase7_occlusion_owner_eligible(run,owner));
    // An allied relation enables the sector callback without changing LOS.
    run.faction_packed_relation_states[2]=0x8U<<4U;
    assert(simulation_lab_phase7_sector_owner_eligible(run,owner));
    assert(simulation_lab_phase7_occlusion_owner_eligible(run,owner));
    owner.model_components[0].runtime_removed=true;
    assert(!simulation_lab_phase7_occlusion_owner_eligible(run,owner));
    owner.model_components[0].runtime_removed=false;
    owner.runtime_removed=true;
    assert(!simulation_lab_phase7_occlusion_owner_eligible(run,owner));
    owner.runtime_removed=false;
    owner.faction_index=0U;
    assert(!simulation_lab_phase7_occlusion_owner_eligible(run,owner));
    owner.faction_index=2U;
    owner.fog_uncover_radius_q12=0x480;
    assert(simulation_lab_phase7_occlusion_preview_radius(run,owner)==0x480U);
    // The reduced synthetic fixtures preserve the older fog-preview gate.
    run.level_runtime_enabled=false;
    assert(!simulation_lab_phase7_occlusion_owner_eligible(run,owner));
    owner.root_model_terminal=false;
    owner.construction_complete=true;
    assert(!simulation_lab_phase7_occlusion_owner_eligible(run,owner));
    owner.model_components[0].runtime_flags_ec=0U;
    assert(simulation_lab_phase7_occlusion_owner_eligible(run,owner));
    owner.fog_uncover_radius_q12=0;
    assert(!simulation_lab_phase7_occlusion_owner_eligible(run,owner));
    assert(simulation_lab_phase7_occlusion_preview_radius(run,owner)==0U);
}
