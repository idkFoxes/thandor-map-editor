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
producer=(root/'include/thandor/ai_unit_producibility_v601.hpp').read_text()

fail=[]
def req(ok,msg):
    if not ok: fail.append(msg)

req('secondary_purchase_requests' in header and 'pending_asset_requests' in header,
    'independent retail secondary/primary faction queues are missing')
req('SimulationLabProductionAsset' in header and 'xenite_cost_q4' in header and
    'build_duration_ticks' in header and 'energy_demand_q4' in header,
    'producer ARM cost/build/Energy metadata is missing')
req('return ai_v601_producer_accepts(producer.runtime_model_class_id,' in core and
    'producer.production_compatibility_mask, candidate.army_runtime_flags14_raw)' in core and
    'if ((flags & 0x10U) != 0U)' in producer and
    'if ((flags & 0x08U) != 0U)' in producer and
    '(class13_factory_mask & asset_flags & 0xEEU) != 0U' in producer and
    'producer_runtime_class == 11U' in producer and 'producer_runtime_class == 22U' in producer,
    'class 11/13/22 helper acceptance gates or live call site are missing')
req('economy.xenite_current_q4 -= candidate->xenite_cost_q4;' in core and
    'run.secondary_purchase_requests.erase' in core,
    'Xenite is not charged at producer acceptance / secondary dequeue')
req('faction_active_production_energy_q4' in core and
    'production_active_energy_demand_q4' in core,
    'temporary production Energy load is missing')
req('class11_primary_completions' in core and 'run.pending_asset_requests.push_back' in core,
    'class 11 does not route completed products to the primary placement queue')
req('class13_units_spawned' in core and 'production_parent_entity_index' in core and
    'simulation_lab_set_factory_exit_route(run,field,child_index,producer_index,' in core and
    'const auto rally=producer.production_drive_target_world.value_or(exit);' in core and
    'producer.production_behavior_state == 2U' in core,
    'class 13 does not create/link/route a physical produced unit')
req('production_linked_child_asset_ids[slot]=completed_asset_id' in core and
    'production_linked_child_count' in core,
    'class 22 does not route completion into linked-child storage')
req('return true; // retail stops after one committed placement per planner pass' in core and
    'run.pending_asset_requests.erase' in core,
    'primary construction planner one-commit/remove-after-create contract is missing')
req('production_assets' in worker and
    'faction_economy,production_assets,technology_assets,faction_technology_masks,' in app and
    'faction_score_seeds,document_->field(),scenario_runtime)' in app,
    'editor/worker handoff does not carry the production catalog through the expanded V563 snapshot boundary')
req('append_runtime_transient_model' in app and 'entity.synthetic' in app,
    'selected-run renderer does not expose physically produced synthetic units')

if fail:
    for item in fail: print('FAIL:', item)
    raise SystemExit(1)
print('V562_SIMULATION_LAB_PRODUCER_SOURCE_CONTRACT PASS')
