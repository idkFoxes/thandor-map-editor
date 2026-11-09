#!/usr/bin/env python3
"""V716: preload native combat terminal channels only; construction EFF is separate."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
app=(root/'src/editor/editor_app.cpp').read_text()
preload=app.split('const auto preload_simulation_lab_preset_textures=',1)[1].split('struct ShadowAtlasTile',1)[0]
assert 'simulation_lab_terminal_residency_effect_ids(' in preload
assert 'preload_effect_textures(effect_id);' in preload
assert preload.index('if(preload_runtime_resources)')<preload.index('simulation_lab_terminal_residency_effect_ids(')
assert 'for_each_selected_lab_runtime_hierarchy(runs,selected,' in preload
assert 'for(const auto& run:runs)' not in preload
assert 'simulation_lab_terminal_residency_effect_ids(hierarchy)' in preload
assert 'is_building?preset.construction_destruction_effect_id:0U' not in preload
assert 'for(const auto id:simulation_lab_resident_effect_ids_)preload_effect_textures(id);' in app
print('RENDERING_LAB_TERMINAL_RESIDENCY_SOURCE_CONTRACT PASS')
