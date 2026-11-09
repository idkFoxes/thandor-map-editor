#!/usr/bin/env python3
from pathlib import Path
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True)
args = parser.parse_args()
root = Path(args.source)
worker = (root / 'src/core/simulation_lab_worker.cpp').read_text()
lab_h = (root / 'include/thandor/simulation_lab.hpp').read_text()
app = (root / 'src/editor/editor_app.cpp').read_text()
worker_h = (root / 'include/thandor/simulation_lab_worker.hpp').read_text()
writer = (root / 'src/editor/simulation_lab_diagnostic_writer.hpp').read_text()

def req(cond, message):
    if not cond:
        raise SystemExit('FAIL: ' + message)

req('void step_run(const FieldGrid& field, std::size_t run_index' in lab_h,
    'SimulationLabExperiment must expose independent per-run stepping')
req('state->next_run.fetch_add' in worker and 'simulation_lab_worker_thread_count' in worker,
    'persistent pool must claim each independent run once per generation')
req('state->experiment.step_run(state->field, run_index, ticks);' in worker,
    'each run worker must advance only its independent run')
req('state->dispatch_completed == state->run_workers.size()' in worker,
    'coordinator must publish only after the whole parallel generation completes')
req('constexpr std::uint32_t kWorkerChunkTicks = 1U;' in worker,
    'one scheduler tick must map to one parallel generation')
req('unpublished_generations+=chunk;' in worker and
    'completed_to_publish=unpublished_generations;' in worker and
    'completed_generations.fetch_add(completed_to_publish' in worker,
    'published progress must include every intervening one-tick generation')
req(worker.index('state->last_generation_duration_us.store(generation_us') <
    worker.index('state->completed_generations.fetch_add(completed_to_publish'),
    'generation-complete barrier must publish the current timing sample')
req('state->experiment.step(state->field, chunk);' not in worker,
    'coordinator must not reintroduce serialized all-run stepping')
req('run_worker_count()' in worker_h and 'simulation_lab_config_.worker_thread_limit=' in app and '##Threads (0 = automatic)' in app,
    'worker count API or configurable thread limit is missing')
req('sample.generation_us=simulation_lab_worker_.last_generation_duration_us()' in app and 'worker_generation_us=' in writer and 'if(!logging_enabled_||!simulation_lab_diagnostic_writer_' in app,
    'optional diagnostics must retain generation latency without per-frame UI formatting')
print('PASS: Simulation Lab parallel worker source contract')
