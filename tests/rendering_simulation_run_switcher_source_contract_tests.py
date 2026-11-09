#!/usr/bin/env python3
"""Full 3D run switching must not render/allocate a full framebuffer per worker."""
from pathlib import Path
import re
root=Path(__file__).resolve().parent.parent
source=(root/'src/editor/editor_app.cpp').read_text()
header=(root/'src/editor/editor_app.hpp').read_text()

def require(ok,reason):
    if not ok: raise AssertionError(reason)
require('draw_simulation_lab_run_switcher();' in source, 'missing viewport run selector')
require(source.index('draw_simulation_lab_run_switcher();')<source.index('update_canvas_interaction();'),
        'selector must precede canvas editing to capture clicks')
require('void EditorApp::draw_simulation_lab_run_switcher()' in source, 'no viewport selector body')
require('bool EditorApp::select_simulation_lab_run(std::size_t index)' in source, 'no shared selection handler')
selector=source.split('bool EditorApp::select_simulation_lab_run(std::size_t index)',1)[1].split('void EditorApp::draw_simulation_lab_run_switcher()',1)[0]
for key in ('simulation_lab_selected_run_', 'sync_selected_simulation_lab_run()',
            'simulation_static_replacement_dirty_=true', 'last_dynamic_scene_tick_=',
            'selected_simulation_lab_entity_.reset()'):
    require(key in selector, 'switching run did not invalidate: '+key)
require('select_simulation_lab_run(row);' in source, 'inspector and viewport must share selection logic')
require('select_simulation_lab_run(index);' in source, 'viewport selector must actually switch run')
require('if(simulation_lab_run_selector_hovered_&&!right_pointer_capture_active_)return;' in source,
        'canvas editing must not consume viewport selector clicks')
require('Checkbox("Show other runs",&simulation_lab_show_all_runs_)' in source,
        'comparison overlay must remain an explicit opt-in control')
require('if(selected){' in source and 'if(!simulation_lab_show_all_runs_)continue;' in source,
        'only selected run should use full model rendering')
require('bool simulation_lab_run_selector_hovered_' in header, 'pointer capture field not declared')

ui_toggle=source.split('if(ImGui::Checkbox("Show other runs"',1)[1].split('}',1)[0]
require('rebuild_objects();' not in ui_toggle and 'simulation_static_replacement_dirty_=true' in ui_toggle,
        'switching ghost comparison mode must not synchronously rebuild and then rebuild again')

print("SIMLAB_SINGLE_VIEWPORT_SWITCHER_PASS")
