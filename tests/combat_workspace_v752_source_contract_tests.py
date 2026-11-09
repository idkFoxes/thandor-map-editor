from pathlib import Path

root = Path(__file__).resolve().parents[1]
target_h = (root / "include/thandor/simulation_lab_targeting.hpp").read_text()
workspaces_h = (root / "include/thandor/simulation_lab_ai_workspaces.hpp").read_text()
target_cpp = (root / "src/core/simulation_lab_targeting.cpp").read_text()
lab_cpp = (root / "src/core/simulation_lab.cpp").read_text()

# Existing command targets and direct combat candidates have different native
# lifetimes: terminal MODEL owners remain on the world list, but destroyed
# owners are not new combat candidates.
assert "simulation_lab_command_target_owner_available" in target_h
assert "simulation_lab_combat_target_owner_available" in target_h
assert "!entity.root_model_terminal" in target_h

select_start = target_cpp.index("SimulationLabCombatTargetChoice simulation_lab_select_combat_target")
select_end = target_cpp.index("bool simulation_lab_weapon_callback_active", select_start)
select = target_cpp[select_start:select_end]
assert "run.entities.size()-1U-visit" in select
assert "source_metrics.class_sum==0" in select
assert "source.root_model_terminal" not in select
assert "!source.construction_complete" not in select
assert "(root_flags(source)&0x18U)" not in select
assert "faction_capability_flags" in select
assert "line_of_fire_blocked" in select
assert "static_cast<std::uint32_t>(score.value)>>2U" in select
assert "source.motion.ground_acceleration_q12!=0U" in select
assert "if(!accelerates)score.value=0" in select
assert "simulation_lab_model_visibility_bits" in select

service_start = target_cpp.index("void simulation_lab_service_combat_command")
service_end = target_cpp.index("void simulation_lab_clear_released_target", service_start)
service = target_cpp[service_start:service_end]
assert "simulation_lab_command_target_owner_available" in service
assert "source.root_model_terminal" not in service
assert "(source.runtime_model_class_id==4U&&!source.construction_complete)" in service
assert "!source.construction_complete ||" not in service
assert "metrics[source_index].class_sum>=0" in service
assert "source.combat_command_mode_flags=8U" in service

perception_start = lab_cpp.index("SimulationLabHostilePerception simulation_lab_ai_hostile_perception")
perception_end = lab_cpp.index("float simulation_lab_construction_progress", perception_start)
perception = lab_cpp[perception_start:perception_end]
assert "simulation_lab_command_target_owner_available" in perception
assert "faction_capability_flags" in perception
assert "construction_complete" not in perception
assert "root_model_terminal" not in perception

# Native RebuildFactionWorkspaces tests the whole owner-list head before it
# appends pending and in-production records; static LEV owners count.
assert "static_placement_owner_count != 0U" in workspaces_h
assert "simulation_lab_ai_world_owner_list_nonempty" in workspaces_h
assert "run.entities.size() - 1 - visit" in workspaces_h
assert "if (!world_has_owners)" in workspaces_h
assert "? static_cast<std::uint32_t>(std::count_if" in workspaces_h

military_start = lab_cpp.index("void SimulationLabExperiment::step_military_ai")
military_end = lab_cpp.index("void SimulationLabExperiment::step_run", military_start)
military = lab_cpp[military_start:military_end]
assert "for (const auto i:owner_ws.units01)" in military
assert "army.production_linked_to_parent?2U:0U" in military
eligibility_prefix = military[:military.index("const auto flags=army.combat_movement.flags")]
assert "!army.construction_complete" not in eligibility_prefix
assert "army.root_model_terminal" not in eligibility_prefix
assert "scenario_entity_disabled(army)" not in eligibility_prefix

planner_start = lab_cpp.index("void SimulationLabExperiment::plan_autonomous_army_purchases")
planner_end = lab_cpp.index("void SimulationLabExperiment::complete_construction", planner_start)
planner = lab_cpp[planner_start:planner_end]
assert "faction_present=run.faction_lifecycle_states[faction]==kFactionLifecycleActive" in planner
assert "if (!run.level_runtime_enabled &&" in planner
assert "simulation_lab_ai_hostile_perception(run,field,faction,entity)" in planner

# User constraints retained.
markers = (root / "include/thandor/simulation_command_markers.hpp").read_text()
assert "#include <algorithm>" in markers


print("V752_COMBAT_WORKSPACE_SOURCE_CONTRACT PASS")
