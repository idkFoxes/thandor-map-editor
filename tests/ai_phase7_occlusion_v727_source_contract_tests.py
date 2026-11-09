from pathlib import Path
root=Path(__file__).resolve().parents[1]
src=(root/'src/core/simulation_lab.cpp').read_text()
header=(root/'include/thandor/simulation_lab_phase7_owner.hpp').read_text()
assert src.count('simulation_lab_phase7_occlusion_owner_eligible(run,entity)') >= 2
assert src.count('simulation_lab_phase7_occlusion_preview_radius(run,entity)') >= 4
assert 'if (run.level_runtime_enabled) return true;' in header
assert 'owner.model_components[owner.root_model_component_index].runtime_removed' in header
assert 'if (run.level_runtime_enabled && owner.fog_uncover_radius_q12 == 0) return 1U;' in header
assert 'simulation_lab_phase7_sector_owner_eligible(run,own)' in src
assert 'simulation_lab_phase7_owner_occupancy_radius(run,own)' in src
print('PASS: independent native owner LOS and sector callback source contract')
