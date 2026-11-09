#!/usr/bin/env python3
import argparse
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('--source',required=True)
args=parser.parse_args()
root=Path(args.source)
app=(root/'src/editor/editor_app.cpp').read_text()
app_h=(root/'src/editor/editor_app.hpp').read_text()
sim_h=(root/'include/thandor/simulation_lab.hpp').read_text()
sim=(root/'src/core/simulation_lab.cpp').read_text()

failures=[]
def req(ok,msg):
    if not ok: failures.append(msg)

req('uint32_t simulation_lab_tick_duration_ms_{50U};' in app_h,
    'Simulation Lab does not retain a configurable millisecond tick duration')
req('InputInt("##Tick interval (ms)"' in app and 'std::clamp(tick_duration_ms,1,5000)' in app,
    'Simulation Lab UI does not expose the millisecond tick-duration control')
req('std::floor(simulation_lab_tick_accumulator_ms_/static_cast<double>(tick_ms))' in app,
    'wall cadence is not converted into bounded worker ticks')
req('simulation_lab_tick_accumulator_ms_' in app and
    'simulation_lab_worker_.request_advance(requested)' in app,
    'Simulation Lab wall-clock scheduler does not translate milliseconds into worker tick requests')
req('presentation_tick>simulation_lab_last_tick_' not in app,
    'Simulation Lab is still driven by the shared 20 Hz presentation tick')
req('construction_start_tick' in sim_h and 'entity.construction_start_tick = run.tick;' in sim,
    'proxy construction does not retain a visible start tick')
req('simulation_lab_construction_progress' in sim_h and 'simulation_lab_construction_progress' in sim,
    'proxy construction does not expose a testable visual progress contract')
req('if(!entity.synthetic)continue;' in app and
    'simulation_lab_construction_progress(runs[run_index],entity)' in app,
    'selected-run synthetic construction is still hidden until completion')
req('simulation_lab_objects_text_.c_str()' in app and 'summary.entity_count' in app,
    'selected-run UI lost its cached entity summary')
req('simulation_lab_worker_.start(simulation_lab_config_,ai_knowledge_->parameters(),seeds,' in app and
    'faction_economy,production_assets,technology_assets,faction_technology_masks,' in app and
    'simulation_lab_one_shot_events_after' in app,
    'worker seed handoff or runtime visual event integration is missing')

if failures:
    for failure in failures: print('FAIL:',failure)
    raise SystemExit(1)
print('V539_SIMULATION_LAB_VISUAL_TIMING_SOURCE_CONTRACT PASS')
