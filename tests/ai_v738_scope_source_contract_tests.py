from pathlib import Path
root=Path(__file__).resolve().parent.parent
core=(root/'src/core/simulation_lab.cpp').read_text()
render=(root/'src/editor/editor_app.cpp').read_text()
types=(root/'include/thandor/simulation_lab_environment_types.hpp').read_text()
assert core.count('lab_refresh_linked_effect_terrain_states(run,field,terrain_image') == 2
assert 'simulation_lab_resolve_effect_terrain_state(' in core
assert 'terrain_runtime_flags{}, terrain_tint_argb{}, terrain_refresh_tick{};' in types
assert 'selected_lab_shot_model_tint.reset();' in render
assert 'emitting_command_overlay=true;' in render
assert 'if(selected_lab_shot_model_tint && !emitting_command_overlay && recursion_depth==0U &&' in render
assert '(definition.definition_id&0x80000000U)!=0U' in render
assert 'packed_color=*selected_lab_shot_model_tint;' in render
assert 'append_effect_visual_recursive(flight,position,age,yaw' in render
assert 'V739' in (root/'include/thandor/editor_version.hpp').read_text()
assert '#include <algorithm>' in (root/'include/thandor/simulation_command_markers.hpp').read_text()
print('V738 source-contract PASS')
