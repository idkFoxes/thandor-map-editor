#include "thandor/simulation_lab_phase7_owner.hpp"
#include <cassert>
#include <memory>
#include <vector>
using namespace thandor;

int main() {
    SimulationLabRun run;
    run.level_runtime_enabled=true;
    run.active_view_faction_index=1U;
    SimulationLabEntity owner;
    owner.faction_index=2U;
    owner.construction_complete=false; // retail does not filter incomplete roots
    owner.root_model_terminal=true;     // terminal effects retain linked owner
    owner.root_model_component_index=0U;
    owner.model_components.resize(1);
    run.faction_packed_relation_states[2]=(0x8U << 4U);
    assert(simulation_lab_phase7_sector_owner_eligible(run,owner));
    // The gate reads the OWNER relation row, not the view faction row.
    run.faction_packed_relation_states[1]=(0x8U << 8U);
    run.faction_packed_relation_states[2]=0U;
    assert(!simulation_lab_phase7_sector_owner_eligible(run,owner));
    run.faction_packed_relation_states[2]=(0x8U << 4U);
    run.active_view_faction_index=3U;
    assert(!simulation_lab_phase7_sector_owner_eligible(run,owner));
    run.active_view_faction_index=1U;
    owner.model_components[0].runtime_removed=true;
    assert(!simulation_lab_phase7_sector_owner_eligible(run,owner));
    owner.model_components[0].runtime_removed=false;
    owner.runtime_removed=true;
    assert(!simulation_lab_phase7_sector_owner_eligible(run,owner));
    owner.runtime_removed=false;
    owner.faction_index=0U;
    assert(!simulation_lab_phase7_sector_owner_eligible(run,owner));
    owner.faction_index=2U;
    run.active_view_faction_index=8U;
    assert(!simulation_lab_phase7_sector_owner_eligible(run,owner));
    run.active_view_faction_index=1U;
    // No-authored-runtime fixtures retain historical zero-row fallback.
    // Level runtime uses its authored radius, including an explicit zero.
    run.level_runtime_enabled=true;
    owner.occupancy_mark_radius_units=0U;
    owner.runtime_class_specific_19c_raw=4608;
    assert(simulation_lab_phase7_owner_occupancy_radius(run,owner)==0U);
    owner.occupancy_mark_radius_units=1152U;
    assert(simulation_lab_phase7_owner_occupancy_radius(run,owner)==1152U);
    run.level_runtime_enabled=false;
    owner.occupancy_mark_radius_units=0U;
    assert(simulation_lab_phase7_owner_occupancy_radius(run,owner)==4608U);
    owner.runtime_class_specific_19c_raw=-1;
    assert(simulation_lab_phase7_owner_occupancy_radius(run,owner)==0U);
    run.faction_packed_relation_states.fill(0U);
    owner.root_model_terminal=false;
    owner.construction_complete=true;
    assert(simulation_lab_phase7_sector_owner_eligible(run,owner));
    owner.root_model_terminal=true;
    assert(!simulation_lab_phase7_sector_owner_eligible(run,owner));
    owner.root_model_terminal=false;
    owner.construction_complete=false;
    assert(!simulation_lab_phase7_sector_owner_eligible(run,owner));
}
