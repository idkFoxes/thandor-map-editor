#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(sys.argv[1] if len(sys.argv)>1 else Path(__file__).resolve().parents[1])
source=(root/'src/editor/editor_app.cpp').read_text()
header=(root/'src/editor/simulation_lab_selected_residency.hpp').read_text()
preload=source.split('const auto preload_simulation_lab_preset_textures=',1)[1].split('struct ShadowAtlasTile',1)[0]
sync=source.split('void EditorApp::sync_selected_simulation_lab_run()',1)[1].split('void EditorApp::update_simulation_lab(',1)[0]
menu=source.split('void EditorApp::draw_main_menu()',1)[1].split('void EditorApp::draw_retail_debug_overlay()',1)[0]
assert 'for_each_selected_lab_runtime_hierarchy(runs,selected' in preload
assert 'entity.runtime_model_hierarchy.get());' in preload
assert 'runtime_hierarchy!=nullptr' in preload
assert 'for(const auto& node:*entity.runtime_model_hierarchy)' in sync
assert 'node.sprite_path' in sync
assert 'for_each_selected_lab_runtime_hierarchy' in header
assert 'ImGui::SameLine(right_x)' in menu and 'FPS' in menu
assert 'draw_measured_fps_overlay' not in source
print('PASS: selected concrete runtime model texture residency + navbar FPS contract')
