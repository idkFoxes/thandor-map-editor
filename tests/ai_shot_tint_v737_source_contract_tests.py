from pathlib import Path
root=Path(__file__).resolve().parents[1]
source=(root/'src/core/simulation_lab.cpp').read_text()
state=(root/'include/thandor/simulation.hpp').read_text()
helper=(root/'include/thandor/simulation_lab_shot_terrain.hpp').read_text()
assert 'simulation_lab_resolve_shot_terrain_state(' in source
assert 'run.active_view_faction_index' in source
for name in ('terrain_runtime_class_mask','terrain_runtime_flags','terrain_tint_argb'):
    assert name in state and f'shot.{name}=' in source
assert 'retail_ui_state_tint_argb(flags)' in helper
assert 'retail_modulate_argb_mmx(' in helper
assert 'simulation_lab_shot_neighborhood_mask(' in source
assert 'V737' in (root/'include/thandor/editor_version.hpp').read_text()
print('V737_SHOT_TINT_SOURCE PASS')
