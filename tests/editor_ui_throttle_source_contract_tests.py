from pathlib import Path
import sys

root=Path(__file__).resolve().parents[1]
app=(root/'src/editor/editor_app.cpp').read_text(encoding='utf-8')
header=(root/'src/editor/editor_app.hpp').read_text(encoding='utf-8')
cadence=(root/'src/editor/simulation_lab_ui_cadence.hpp').read_text(encoding='utf-8')

def req(cond,msg):
    if not cond:
        print('FAIL:',msg,file=sys.stderr)
        raise SystemExit(1)

req('kSimulationLabUiRefreshIntervalSeconds = 0.5' in cadence,
    'UI/debug refresh interval is not fixed at 500 ms')
req('simulation_lab_ui_snapshot_' in header,
    'separate immutable UI/debug snapshot is missing')
req('SimulationLabUiSelectedSummary' in header,
    'selected-run debug summaries are not cached at the throttled cadence')
req('simulation_lab_ui_runs()' in app,
    'UI/debug snapshot accessor is not used')

poll=app[app.index('bool EditorApp::poll_simulation_lab_snapshot()'):
         app.index('void EditorApp::update_simulation_lab',app.index('bool EditorApp::poll_simulation_lab_snapshot()'))]
req('append_simulation_lab_diagnostics' not in poll,
    'forensic diagnostics are still emitted on every live render snapshot')
req('sync_selected_simulation_lab_run();' in poll,
    'live render/actor synchronization must remain on the normal snapshot cadence')

refresh=app[app.index('void EditorApp::refresh_simulation_lab_ui_snapshot'):
            app.index('bool EditorApp::poll_simulation_lab_snapshot()',app.index('void EditorApp::refresh_simulation_lab_ui_snapshot'))]
req('simulation_lab_ui_refresh_due' in refresh,
    'UI/debug snapshot refresh is not cadence-gated')
req('append_simulation_lab_diagnostics(force);' in refresh,
    'forensic diagnostics do not share the 500 ms UI/debug cadence')
req('refresh_simulation_lab_ui_text();' in refresh and 'for(const auto& entity:run.entities)' in app,
    'selected-run entity summaries are not computed at refresh time')
req('simulation_lab_worker_.retire_snapshot' in refresh,
    'superseded UI snapshots are not retired on the worker')

panel=app[app.index('void EditorApp::draw_simulation_lab_panel()'):
          app.index('void EditorApp::draw_simulation_lab_overlay()',app.index('void EditorApp::draw_simulation_lab_panel()'))]
req('auto runs=simulation_lab_ui_runs();' in panel,
    'large Simulation Lab text panel still reads the live per-generation snapshot')
req('simulation_lab_ui_text_[row]' in panel and 'std::to_string(run.tick)' not in panel,
    'run text is not cached at the snapshot cadence')
req('simulation_lab_objects_text_.c_str()' in panel and 'std::size_t live=0U' not in panel,
    'always-visible selected-run entity summaries are still recomputed every frame')

overlay=app[app.index('void EditorApp::draw_simulation_lab_overlay()'):
            app.index('void EditorApp::ensure_water_selection_size()',app.index('void EditorApp::draw_simulation_lab_overlay()'))]
req('const auto runs=simulation_lab_ui_runs();' in overlay,
    'Simulation Lab text overlay still reads live data')

render=app[app.index('void EditorApp::rebuild_dynamic_scene_cpu('):
           app.index('void EditorApp::',app.index('void EditorApp::rebuild_dynamic_scene_cpu(')+10)]
req('simulation_lab_runs()' in render and 'simulation_lab_ui_runs()' not in render,
    '3D dynamic scene was accidentally throttled with the UI')

print('V609 Simulation Lab UI throttle source contract PASS')
