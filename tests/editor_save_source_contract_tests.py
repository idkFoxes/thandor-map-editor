#!/usr/bin/env python3
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, required=True)
args = parser.parse_args()
app = (args.source / 'src/editor/editor_app.cpp').read_text()
hdr = (args.source / 'src/editor/editor_app.hpp').read_text()
map_cpp = (args.source / 'src/core/map_document.cpp').read_text()
map_h = (args.source / 'include/thandor/map_document.hpp').read_text()

checks = {
    'stock source identity is explicit': 'current_stock_group_' in hdr,
    'stock load records exact group': 'current_stock_group_=group_index' in app,
    'non-stock loads clear stock identity': app.count('current_stock_group_.reset()') >= 2,
    'file menu exposes in-place stock save': 'Save current stock map in place' in app,
    'ctrl-s routes stock map to in-place save': 'if(current_stock_group_)save_current_map();' in app,
    'editor calls core transactional stock save': 'save_stock_map_in_place(' in app,
    'save path explicitly excludes simulation state': 'Simulation state was not saved.' in app,
    'core API documents authored-only persistence': 'Runtime simulation state is' in map_h,
    'core save rewrites backing archives': 'catalog.backing_archive_path(*level_record)' in map_cpp,
    'core save verifies exact rewritten entries': 'stock in-place save verification failed' in map_cpp,
    'core save rolls back archive bytes on failure': 'write_file_atomic(archive.path, archive.original)' in map_cpp,
}

save_begin = app.index('void EditorApp::save_current_map()')
# Do not couple stock-save isolation to the return type or ordering of the
# subsequent New Map command. Only the save_current_map body is relevant.
save_open = app.index('{', save_begin)
depth = 0
save_end = None
for offset in range(save_open, len(app)):
    if app[offset] == '{':
        depth += 1
    elif app[offset] == '}':
        depth -= 1
        if depth == 0:
            save_end = offset + 1
            break
if save_end is None:
    raise RuntimeError('unterminated save_current_map() body')
save_body = app[save_begin:save_end]
for forbidden in (
    'simulation_actor_states_', 'simulation_projectiles_', 'simulation_integrity_states_',
    'simulation_produced_entities_', 'simulation_linked_child_entities_',
    'simulation_attack_placement_targets_', 'simulation_attack_point_targets_',
):
    checks[f'stock save does not serialize {forbidden}'] = forbidden not in save_body

failed = [name for name, ok in checks.items() if not ok]
if failed:
    for name in failed:
        print('FAIL:', name)
    raise SystemExit(1)
print(f'V557_EDITOR_SAVE_SOURCE_CONTRACT PASS checks={len(checks)}')
