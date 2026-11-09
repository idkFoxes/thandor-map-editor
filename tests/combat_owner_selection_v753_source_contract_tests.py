from source_contract_support import require_editor_version_at_least
from pathlib import Path
root=Path(__file__).resolve().parents[1]
lab=(root/'src/core/simulation_lab.cpp').read_text()
target=(root/'src/core/simulation_lab_targeting.cpp').read_text()
header=(root/'include/thandor/simulation_lab.hpp').read_text()
markers=(root/'include/thandor/simulation_command_markers.hpp').read_text()
schedule=(root/'include/thandor/simulation_lab_owner_schedule.hpp').read_text()
assert 'TargetArmySelection' in header and 'TargetPositionSelection' in header
assert 'combat_command_target_position_world' in header
assert 'simulation_lab_apply_selection_army_target' in target
assert 'combat_command_mode_flags=0x15U' in target
assert 'simulation_lab_apply_selection_target_position' in target
assert 'combat_command_mode_flags=0x16U' in target
assert 'combat_command_generation=4096' in target
assert 'simulation_lab_combat_command_class(source)' not in target[target.index('void simulation_lab_service_combat_command'):target.index('void simulation_lab_clear_released_target')]
assert 'position_command_aim' in lab and 'aim_available' in lab
assert 'TargetArmySelection' in lab and 'TargetPositionSelection' in lab
assert 'simulation_lab_visit_captured_owner_chain' in lab
assert 'is_linked(owner)' in schedule
assert 'combat_command_target_position_world' in markers
require_editor_version_at_least(root, 758)
print('V753_COMBAT_OWNER_SELECTION_SOURCE_CONTRACT PASS')
