from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
src = (root / 'src/core/simulation_lab.cpp').read_text()
start = src.index('void lab_rebuild_phase7_field_presence(')
end = src.index('// FieldGrid is mutable', start)
phase7 = src[start:end]
assert 'simulation_lab_phase7_projection_available(is_world_job,has_world_owner)' in phase7
assert 'if (!is_world_job || has_world_owner)' in phase7
assert 'return !is_world_job || has_world_owner;' in (root/'include/thandor/simulation_lab_phase7_owner.hpp').read_text()
# Later snapshots may select a fresh model-owner projection for catalog-backed
# runs, but the metadata fallback must still read the cached preview only behind
# the same world-owner projection gate.
assert 'run.current_visibility_occupancy.size()==cell_count' in phase7
assert 'run.current_visibility_occupancy[cell]' in phase7
assert 'const auto current=projected' in phase7
assert 'simulation_lab_phase7_ended_faction_mask(' in phase7
assert 'simulation_lab_phase7_merge_occupancy_mask(' in phase7
assert 'simulated' not in phase7.lower() # avoid test-only branches
from source_contract_support import require_editor_version_at_least
require_editor_version_at_least(root,733)
print('V733_PHASE7_NO_OWNER_SOURCE_CONTRACT PASS')
