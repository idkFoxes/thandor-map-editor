#!/usr/bin/env python3
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True)
args = parser.parse_args()
root = Path(args.source)
catalog = (root / 'src/core/catalog.cpp').read_text()
core = (root / 'src/core/simulation_lab.cpp').read_text()
gate = (root / 'include/thandor/pioneer_movement_gate.hpp').read_text()
workspaces = (root / 'include/thandor/simulation_lab_ai_workspaces.hpp').read_text()
cmake = (root / 'CMakeLists.txt').read_text()

failures = []
def req(condition, message):
    if not condition:
        failures.append(message)

req('preset.visual_kind == PlacementVisualKind::PlayerStart' in catalog and
    'preset.runtime_model_class_id == 18U' in catalog and
    'preset.simulation_motion_profile.speed_q12_per_tick' in catalog,
    'PlayerStart/class-18 no longer receives the root MDL movement profile')
req('inside_structure_extent=false' in core and 'own.army_asset_id<300U' in core,
    'primary build-area extent machine source is no longer documented')
req('inside_structure_extent=true' in core and 'dx<reach && dy<reach' in core and
    '!inside_structure_extent' in core,
    'workspace09/10 strict assigned ARM>=300 extent gate disappeared')
req('ArmyPlacementCandidate_TestOffsetClearanceCf' in core and
    'source.runtime_model_class_id == 18U' in core and '(placement_mode & 1U)' in core and
    '(source.runtime_flags & 0x18U)' in core and
    'source.runtime_class_specific_19c_raw' in core and
    'reachable_from_build_area' in core,
    'mode-0 Pioneer build radius / mode-1 exclusion contract disappeared')
req('retail_direction_scaled(0U,heading,0x2d05)' in core and
    'in.workspace00_count==in.workspace04_count' in gate and
    'producer->production_building &&' in core and 'producer->production_asset_id >= 300U' in core and
    'kWorkspace00Capacity = 0x80U' in core and 'kSimulationLabAiWorkspace04Capacity = 8U' in workspaces,
    'class-18 retail workspace00/workspace04 bootstrap comparison disappeared')
req('placement_pioneer_build_area_tests' in cmake,
    'V569 screenshot-shaped Pioneer/build-area regression is not registered')

if failures:
    for failure in failures:
        print('FAIL:', failure)
    raise SystemExit(1)
print('V569_PIONEER_MOTION_SOURCE_CONTRACT PASS')
