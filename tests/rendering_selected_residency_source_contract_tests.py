#!/usr/bin/env python3
"""V694 source contract: inactive simulations cannot populate the active atlas.
This is NOT a Vulkan runtime screenshot or GPU validation test.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
app=(root/'src/editor/editor_app.cpp').read_text()
helper=(root/'src/editor/simulation_lab_selected_residency.hpp').read_text()
preload=app.split('const auto preload_simulation_lab_preset_textures=',1)[1].split('struct ShadowAtlasTile',1)[0]
selection=app.split('bool EditorApp::select_simulation_lab_run(',1)[1].split('void EditorApp::draw_simulation_lab_run_switcher(',1)[0]
for x in ('for_each_selected_lab_synthetic_asset(runs,selected',
          'lab_selected_render_variants_=',
          '&run.faction_technology_masks[faction],true);'):
    assert x in preload,x
assert 'for(const auto& run:runs)' not in preload,'preloads synthetic textures for all runs'
for x in ('entity.runtime_removed','entity.faction_index==0U',
          'if(selected_index>=runs.size())return 0;', 'visited_assets.insert(key)'):
    assert x in helper,x
# The renderer does not filter root_model_terminal; neither selected-run
# preload visitor may discard it while that hierarchy is still draw-eligible.
assert 'entity.runtime_removed||entity.root_model_terminal' not in helper
assert '!entity.synthetic||entity.runtime_removed||entity.root_model_terminal' not in helper
assert selection.index('movement_ground_effects_.clear();') < selection.index('sync_selected_simulation_lab_run();')
assert selection.index('simulation_lab_resident_effect_ids_.clear();') < selection.index('sync_selected_simulation_lab_run();')
assert 'simulation_static_replacement_dirty_=true;' in selection
assert 'lab_selected_render_variants=' in (root/'src/editor/editor_performance_log.hpp').read_text()
print('RENDER_SELECTED_RESIDENCY_SOURCE_CONTRACT PASS selected-only assets effects reset run switch')
