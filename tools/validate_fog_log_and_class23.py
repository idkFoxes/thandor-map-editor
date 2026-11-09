#!/usr/bin/env python3
"""Narrow static regression gate for V584. This does not require editor/Vulkan dependencies."""
from pathlib import Path
import argparse
parser=argparse.ArgumentParser()
parser.add_argument('--source',required=True)
root=Path(parser.parse_args().source)
header=(root/'src/editor/editor_app.hpp').read_text()
app=(root/'src/editor/editor_app.cpp').read_text()
core=(root/'src/core/simulation_lab.cpp').read_text()
labheader=(root/'include/thandor/simulation_lab.hpp').read_text()
problems=[]
def must(condition,label):
    if not condition: problems.append(label)
must((root/'CMakeLists.txt').exists(),'cumulative ZIP missing CMakeLists.txt')
must('void advance_class23_collision_state(SimulationLabRun& run);' in labheader and
     'static void advance_class23_collision_state(' not in labheader and
     'config_.class23_callback_step_ticks' in core,
     'class23 callback must be an instance method to access config_')
must(app.count('const uint32_t active_factions=fog_preview_active_faction_count();')>=2,
     'fog union and entity visibility must share the same active faction count')
must('void EditorApp::begin_simulation_lab_diagnostics()' in app and
     '    begin_simulation_lab_diagnostics();\n}\n\nvoid EditorApp::clear_simulation_lab()' in app and
     'void EditorApp::append_simulation_lab_diagnostics(bool force)' in app and
     'append_simulation_lab_diagnostics(force);' in app and
     'refresh_simulation_lab_ui_snapshot' in app,
     'automatic diagnostic log missing from Start or throttled UI/debug refresh')
for text in ('PENDING run=', 'FACTION run=', 'ENTITY run=',
             'conversions_started=', 'conversions_completed=', 'fog_current=',
             'WORKER_ERROR ', 'LOG_LIMIT 64 MiB'):
    must(text in app,'missing forensic log field '+text)
must('std::ofstream simulation_lab_diagnostic_stream_;' in header and
     'simulation_lab_diagnostic_path_' in header,
     'GUI log stream/path not retained')
if problems:
    for p in problems: print('FAIL:',p)
    raise SystemExit(1)
print('V584_FOG_LAB_LOG_CLASS23_SOURCE_CONTRACT PASS')
