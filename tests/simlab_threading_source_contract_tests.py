#!/usr/bin/env python3
import argparse
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('--source',required=True)
args=parser.parse_args()
root=Path(args.source)
app=(root/'src/editor/editor_app.cpp').read_text()
app_h=(root/'src/editor/editor_app.hpp').read_text()
worker_h=(root/'include/thandor/simulation_lab_worker.hpp').read_text()
worker=(root/'src/core/simulation_lab_worker.cpp').read_text()

failures=[]
def req(ok,msg):
    if not ok: failures.append(msg)

req('SimulationLabWorker simulation_lab_worker_' in app_h,
    'EditorApp does not own a dedicated Simulation Lab worker')
req('std::shared_ptr<const thandor::SimulationLabExperiment> simulation_lab_snapshot_' in app_h,
    'EditorApp does not consume immutable Simulation Lab snapshots')
req('simulation_lab_worker_.start(simulation_lab_config_,ai_knowledge_->parameters(),seeds,' in app and
    'faction_economy,production_assets,technology_assets,faction_technology_masks,' in app and
    'faction_score_seeds,document_->field(),scenario_runtime)' in app,
    'Simulation Lab start does not snapshot production/research/score assets and the active FLD into the worker')
req('simulation_lab_worker_.request_advance(' in app,
    'frame update does not enqueue Simulation Lab ticks')
req('simulation_lab_.step(' not in app,
    'editor/render thread still steps SimulationLabExperiment directly')
req('simulation_lab_worker_.retire_snapshot(std::move(previous))' in app,
    'superseded snapshots are not retired on the worker')
req('lab_snapshot_rebuild=simulation_lab_active()&&simulation_lab_snapshot_dirty_' in app and
    '(!simulation_lab_active()&&dynamic_geometry_present)' in app,
    'dynamic scene is still forced to rebuild every presentation tick merely because the lab is active')
req('bool simulation_lab_show_all_runs_{};' in app_h and
    'Checkbox("Show other runs",&simulation_lab_show_all_runs_)' in app,
    'all-run lightweight marker rendering is not explicit opt-in')
req('std::jthread thread_' in worker_h and 'std::stop_token' in worker,
    'worker is not owned/stopped with C++20 jthread semantics')
req('FieldGrid field;' in worker and 'state->field = field;' in worker,
    'worker does not own an FLD snapshot, leaving terrain reads race-prone')
req(('kWorkerChunkTicks = 2U' in worker or 'kWorkerChunkTicks = 1U' in worker) and 'kMaximumPendingTicks = 256U' in worker,
    'worker does not bound cancellation latency/backlog')
req(('unknown exception in Simulation Lab worker' in worker or
     'unknown exception in Simulation Lab run worker' in worker) and
    'Simulation Lab worker stopped:' in app,
    'worker exceptions are not surfaced safely to the UI')

if failures:
    for failure in failures: print('FAIL:',failure)
    raise SystemExit(1)
print('V538_SIMULATION_LAB_THREADING_SOURCE_CONTRACT PASS')
