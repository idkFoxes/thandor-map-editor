from pathlib import Path
root=Path(__file__).resolve().parents[1]
source=(root/'src/core/simulation_lab.cpp').read_text()
header=(root/'include/thandor/simulation_lab_phase7_owner.hpp').read_text()
assert 'simulation_lab_phase7_sector_owner_eligible(run,own)' in source
assert 'simulation_lab_phase7_owner_occupancy_radius(run,own)' in source
assert 'run.level_runtime_enabled || owner.occupancy_mark_radius_units != 0U' in header
assert '(relation & 0x8U) != 0U' in header
assert 'owner.model_components[owner.root_model_component_index].runtime_removed' in header
assert 'return !owner.root_model_terminal && owner.construction_complete;' in header
assert 'kEditorVersion' in (root/'include/thandor/editor_version.hpp').read_text()
print('V726 phase-7 source contract passed')
