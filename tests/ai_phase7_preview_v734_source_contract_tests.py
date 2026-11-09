from pathlib import Path
import re
root = Path(__file__).resolve().parents[1]
editor=(root/'src/editor/editor_app.cpp').read_text()
core=(root/'src/core/simulation_lab.cpp').read_text()
header=(root/'include/thandor/simulation_lab_phase7_preview.hpp').read_text()
run=(root/'include/thandor/simulation_lab.hpp').read_text()
assert 'simulation_lab_phase7_preview_uses_retained_field(worker_run,cell_count)' in editor
assert 'simulation_lab_phase7_preview_has_world_owner(' in editor
assert 'simulation_lab_phase7_occlusion_owner_eligible(run,entity)' in editor
assert 'simulation_lab_phase7_occlusion_preview_radius(run,entity)' in editor
assert 'static_placement_owner_count' in run
assert 'run.static_placement_owner_count = static_placement_owners_.size();' in core
assert 'run.static_placement_owner_count' in core
assert 'presence_update_tick>=run.current_visibility_occupancy_tick' in header
from source_contract_support import require_editor_version_at_least
require_editor_version_at_least(root,734)
print('V734_PHASE7_PREVIEW_SOURCE_CONTRACT PASS')
