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
app_h=(root/'src/editor/editor_app.hpp').read_text()

fail=[]
def req(ok,msg):
    if not ok: fail.append(msg)

req('SimulationLabFactionEconomySeed' in header and 'xenite_storage_limit_q4' in header and 'tritium_storage_limit_q4' in header,
    'run-local Xenite/Tritium economy snapshot is missing')
req('SimulationLabPendingAssetRequest' in header and 'pending_asset_requests' in header,
    'pending construction workspace is missing')
req('process_pending_construction' in core and 'return true; // retail stops after one committed placement per planner pass' in core,
    'pending requests are not processed before candidate generation with one-commit stop semantics')
req('storage < run.ai_parameters[20U]' in core and 'free_storage < run.ai_parameters[22U]' in core and
    'storage < run.ai_parameters[21U]' in core and 'free_storage < run.ai_parameters[23U]' in core,
    'strict KI[20..23] Q4 storage/free-space comparisons are missing')
req('weight *= 4' in core and 'xenite_full_weight_boosts' in core,
    'full-Xenite 14B x4 branch is missing')
req('run.ai_parameters[weight_index]' in core and
    'try_site(0x14AU,0U,5U)' in core and 'try_site(0x14CU,1U,6U)' in core and
    'derived14d_weight_events' in core,
    '14D workspace08-derived KI[5]/KI[6] path is missing')
req('ws.base09.reserve(1024U)' in core and 'ws.large10.reserve(256U)' in core and
    'ws.base09.size()<1024U' in core and 'ws.large10.size()<256U' in core,
    'workspace09/workspace10 are not represented as distinct retail-bounded placement domains')
req('faction_economy' in worker and 'production_assets' in worker and
    'state->experiment.reset(config, base_parameters, seed_entities, faction_economy, production_assets,' in worker and
    'technology_assets, faction_technology_masks, faction_score_seeds, scenario_runtime)' in worker,
    'parallel worker does not snapshot faction economy/production/research/score inputs into each experiment')
req('faction_xenite_q4(faction)' in app and 'faction_tritium_q4(faction)' in app,
    'Simulation Lab does not seed exact LEV current Xenite/Tritium values')
req('ImGui::Checkbox("Override storage capacity"' in app and
    '&simulation_lab_storage_limit_overrides_enabled_' in app and
    'derive_storage_limits_from_components=true' in app,
    'UI/editor seed does not disclose or enable V537-derived storage capacity with experiment-only overrides')
req('simulation_lab_storage_limit_overrides_enabled_' in app_h,
    'storage override state is not retained by EditorApp')
req('simulation_lab_worker_.start(simulation_lab_config_,ai_knowledge_->parameters(),seeds,' in app and
    'faction_economy,production_assets,technology_assets,faction_technology_masks,' in app,
    'recovered construction/production inputs no longer reach the worker')

if fail:
    for item in fail: print('FAIL:',item)
    raise SystemExit(1)
print('V542_AI_CONSTRUCTION_ECONOMY_SOURCE_CONTRACT PASS')
