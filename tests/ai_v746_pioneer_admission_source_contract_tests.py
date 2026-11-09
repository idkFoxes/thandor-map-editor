#!/usr/bin/env python3
from pathlib import Path
import argparse

parser=argparse.ArgumentParser()
parser.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[1])
args=parser.parse_args()
src=args.source.resolve()

def window(text, marker, size):
    assert marker in text
    start=text.index(marker)
    return text[start:start+size]

lab=(src/'src/core/simulation_lab.cpp').read_text()
ws=(src/'include/thandor/simulation_lab_ai_workspaces.hpp').read_text()
placement=(src/'src/core/simulation_lab_site_placement.cpp').read_text()
trace=(src/'include/thandor/pioneer_site_selector_trace.hpp').read_text()
model=(src/'include/thandor/simulation_lab.hpp').read_text()

follow=(src/'src/core/simulation_lab_follow.cpp').read_text()
group=window(follow,'void simulation_lab_set_group_target_route(',3300)
assert 'simulation_lab_set_group_target_route(run,field,idx,best' in lab
assert 'army_start_fallback_follow(state,point(target_entity.actor.position),deps)' in group
assert 'state.flags&=~0x200U' in group
assert 'e.combat_command_mode_flags=5U' in group
assert 'e.combat_command_generation=32' in group

segment=lab[lab.index('void service_pioneer_behavior('):lab.index('void SimulationLabExperiment::apply_player_commands')]
selector_start=lab.index('std::optional<Vec3f> class18_special_site_target(')
selector_end=lab.index('\nbool run_has_asset_presence(',selector_start)
selector=lab[selector_start:selector_end]
assert 'simulation_lab_pioneer_workspace_counts(run,entity.faction_index)' in segment
assert 'workspace_counts.workspace00,workspace_counts.workspace04' in segment
assert 'for (const auto& owned : run.entities)' not in segment
assert 'lab_nearest_entity_distance_q12(\n                run,entity.faction_index,field,candidate.world,3U' in selector
assert 'simulation_lab_pioneer_site_admission(' in selector
assert 'PioneerSiteAdmission::StopSelector' in selector
assert 'simulation_lab_ai_assigned_structure_count(' in selector
assert 'simulation_lab_ai_own_workspaces(run, faction)' in ws
assert 'pioneer_site_admission(run.ai_parameters[28U]' in placement
assert 'pioneer_reserve_separated_site_chain<size_t>' in placement
assert 'retail_admission_evaluated=true' in selector
assert 'last_selector_stops' in trace
assert 'simulation_lab_has_live_root_model_owner' in model
assert '!simulation_lab_has_live_root_model_owner(entity)' in lab
assert 'kSimulationLabAiWorkspace11Capacity = 1024U' in ws
assert 'kSimulationLabAiWorkspace13Capacity = 128U' in ws
assert 'workspace11.size() == kSimulationLabAiWorkspace11Capacity' in lab
assert 'workspace.rbegin(), workspace.rend()' in ws
assert 'workspace.size() >= kSimulationLabAiWorkspace13Capacity' in ws
assert 'simulation_lab_ai_candidate_add_or_accumulate(candidates,id,kind,delta)' in lab
assert '#include <algorithm>' in (src/'include/thandor/simulation_command_markers.hpp').read_text()
assert 'simulation_lab_spawn_terminal_replacement' not in lab
print('V746_COMBAT_WORKSPACE_SOURCE_CONTRACT PASS')
