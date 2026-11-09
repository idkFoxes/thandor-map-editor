#!/usr/bin/env python3
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True)
args = parser.parse_args()
root = Path(args.source)
header = (root / 'include/thandor/simulation_lab.hpp').read_text()
core = (root / 'src/core/simulation_lab.cpp').read_text()
editor = (root / 'src/editor/editor_app.cpp').read_text()

failures = []
def req(cond, msg):
    if not cond:
        failures.append(msg)

req('primary_queue_overrides' in header and 'kSimulationLabPrimaryQueueCapacity = 64U' in header,
    'per-faction Simulation Lab primary queue override disappeared')
req('primary_queue_overridden' in core and 'lab_primary_queue_override_assets' in core and
    'if (primary_queue_overridden[faction]) continue;' in core,
    'override no longer replaces the retail-derived startup queue')
req('production_asset(army_asset_id) == nullptr' in core,
    'unresolved override ARM ids can poison the primary queue again')
req('ImGui::CollapsingHeader("Starting inventory")' in editor and
    'simulation_lab_config_.primary_queue_overrides[faction]' in editor and
    'ImGui::Checkbox(faction_label.c_str(),&override.enabled)' in editor and
    'catalog_.army_preset(override.army_asset_ids[index])' in editor,
    'Lab starting-inventory override controls no longer bind to the experiment configuration')

if failures:
    for failure in failures:
        print('FAIL:', failure)
    raise SystemExit(1)
print('V569_PRIMARY_QUEUE_OVERRIDE_SOURCE_CONTRACT PASS')
