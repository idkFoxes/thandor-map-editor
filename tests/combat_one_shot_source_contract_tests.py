#!/usr/bin/env python3
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True)
args = parser.parse_args()
root = Path(args.source)
app = (root / 'src/editor/editor_app.cpp').read_text()
sim = (root / 'src/core/simulation_lab.cpp').read_text()
simh = (root / 'include/thandor/simulation_lab.hpp').read_text()

sync_start = app.index('void EditorApp::sync_selected_simulation_lab_run()')
sync_end = app.find('\nvoid EditorApp::', sync_start + 10)
if sync_end < 0:
    sync_end = len(app)
sync = app[sync_start:sync_end]

checks = {
    'snapshot carries immutable one-shot chain':
        'std::shared_ptr<const SimulationLabOneShotEventNode> one_shot_event_tail' in simh,
    'journal captures historical actor pose':
        'SimulationActorState actor{};' in simh and 'event.actor = entity.actor;' in sim,
    'articulated completions are appended in worker core':
        'SimulationLabOneShotEventKind::ArticulatedContact' in sim and
        'append_one_shot_event(run, std::move(event));' in sim,
    'movement-ground pulses are appended in worker core':
        'SimulationLabOneShotEventKind::MovementGroundEffect' in sim and
        'movement_ground_effect_events' in sim,
    'selected Lab run consumes journal by sequence cursor':
        'simulation_lab_one_shot_events_after(run,' in sync and
        'simulation_lab_last_observed_one_shot_sequence_' in sync,
    'historical hierarchy reconstruction uses event actor':
        'auto historical_actor=event.actor;' in sync and
        'simulation_hierarchy_transforms(event.source_placement_index,*preset,' in sync,
    'UI no longer infers one-shot from latest serial delta':
        'articulated_contact_event_serial>previous' not in sync and
        'articulated_contact_event_serial > previous' not in sync,
    'all runs advance observation cursor to prevent backlog replay':
        'simulation_lab_latest_one_shot_event_sequence(runs[run_index])' in sync,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print('FAIL:', name)
    raise SystemExit(1)
print('V544_SIMULATION_LAB_ONE_SHOT_SOURCE_CONTRACT PASS')
