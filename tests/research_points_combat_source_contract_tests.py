#!/usr/bin/env python3
import argparse
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('--source',required=True)
args=parser.parse_args()
root=Path(args.source)
header=(root/'include/thandor/simulation_lab.hpp').read_text()
core=(root/'src/core/simulation_lab.cpp').read_text()
worker=(root/'src/core/simulation_lab_worker.cpp').read_text()
app=(root/'src/editor/editor_app.cpp').read_text()

fail=[]
def req(ok,msg):
    if not ok: fail.append(msg)

req('SimulationLabTechnologyAsset' in header and 'SimulationLabFactionScoreState' in header and
    'WeaponLaunchEffect' in header and 'WeaponImpactEffect' in header,
    'V563 research/score/combat state is not represented in the Lab API')
req('score.primary_resource_component) * 0x10' in core and
    'score.secondary_resource_component) * 8' in core and
    'score.unlocked_technology_count_beyond_baseline) * 0xA000' in core and
    'score.explored_terrain_percent) * 0x1000' in core and
    'score.points = score.economy_progress_score + score.relation_score;' in core,
    'retail Points formula constants/sum are not preserved')
for token in ['score.relation_counter_b) * 0x28000','score.relation_counter_d) * -0x14000',
              'score.relation_counter_a) * 0x14000','score.relation_counter_c) * -0xA000',
              'score.relation_counter_e) * 0x14000','score.relation_counter_f) * 0x28000']:
    req(token in core, f'missing retail relation-score term: {token}')
req('set_research_job_flags(source,0x80U);' in core and
    'economy.xenite_current_q4 -= entity.research_xenite_due_q4;' in core and
    'set_research_job_flags(entity,0x40U);' in core and
    'run.faction_active_research_energy_q4[faction] += entity.research_energy_demand_q4;' in core and
    'unlock_technology_recursive(run, faction, entity.research_technology_id);' in core,
    'pending -> paid active -> unlock research chain is incomplete')
# Factory linkage locks ground movement, not every ground MODEL callback.
# Aircraft stay excluded by the shared owner gate; newborn models do not fire.
targeting=(root/'include/thandor/simulation_lab_targeting.hpp').read_text()
req('!simulation_lab_command_target_owner_available(entity)' in core and
    'entity.synthetic&&entity.construction_start_tick>=run.tick' in core and
    '(!entity.production_linked_to_parent||factory_ground)' in targeting and
    '!entity.production_parked_on_pad' in targeting,
    'factory ground-owner, parked-aircraft or newborn callback gate disappeared')
req('SimulationProjectileOwnerKind::ProducedClass13' in core and
    'SimulationLabOneShotEventKind::WeaponLaunchEffect' in core and
    'SimulationLabOneShotEventKind::WeaponImpactEffect' in core,
    'produced-unit projectile/effect ownership is not represented')
req('faction_economy,production_assets,technology_assets,faction_technology_masks,' in app and
    'faction_score_seeds,document_->field(),scenario_runtime)' in app and
    'state->experiment.reset(config, base_parameters, seed_entities, faction_economy, production_assets,' in worker and
    'technology_assets, faction_technology_masks, faction_score_seeds, scenario_runtime)' in worker,
    'worker snapshot does not carry production/research/score state with the FLD')
req('event.kind==thandor::SimulationLabOneShotEventKind::WeaponLaunchEffect||' in app and
    'event.kind==thandor::SimulationLabOneShotEventKind::WeaponImpactEffect' in app and
    'movement_ground_effects_.push_back(MovementGroundEffectInstance{' in app,
    'selected 3D Lab run does not consume combat EFF events')
req('ImGui::TreeNode("Faction results")' in app and
    'graphics_family_single_player_faction_name(family)' in app and
    'run.faction_score[faction].points' in app,
    'retail Points calculation is not visible in the Simulation Lab UI')
req('simulation_lab_results_text_.c_str()' in app and
    'std::to_string(run.metrics.research_jobs_completed)' in app and
    'append_simulation_lab_diagnostics(force)' in app,
    'cached research results or optional runtime diagnostic sampling is missing')

if fail:
    for item in fail: print('FAIL:',item)
    raise SystemExit(1)
print('V563_SIMULATION_LAB_RESEARCH_POINTS_COMBAT_SOURCE_CONTRACT PASS')
