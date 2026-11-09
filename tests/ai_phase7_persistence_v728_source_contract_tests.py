"""Guard the integrated native phase-7 old occupancy bit-7 preservation."""
from pathlib import Path
import argparse
parser=argparse.ArgumentParser()
parser.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[1])
args=parser.parse_args()
root=args.source
cpp=(root/'src/core/simulation_lab.cpp').read_text()
h=(root/'include/thandor/simulation_lab_phase7_occupancy.hpp').read_text()
v=(root/'include/thandor/editor_version.hpp').read_text()
assert '#include "thandor/simulation_lab_phase7_occupancy.hpp"' in cpp
begin=cpp.index('void lab_rebuild_phase7_field_presence(')
end=cpp.index('void lab_initialize_persistent_scratch(',begin)
body=cpp[begin:end]
assert 'auto previous_occupancy_masks=std::move(scratch.field_occupancy_masks);' in body
assert 'previous_occupancy_masks.size()==cell_count' in body
assert 'simulation_lab_phase7_merge_occupancy_mask(' in body
assert 'simulation_lab_phase7_present_factions(mask)' in body
assert 'lab_propagate_field_presence_into_scratch(scratch,field)' in body
assert '0x8080808080808080ULL' in h
assert '0xF9U' in h
# This is a V728 feature contract, not a requirement that subsequent
# snapshots retain the V728 editor window title.
from source_contract_support import require_editor_version_at_least
require_editor_version_at_least(root,728)
assert 'simulation_lab_phase7_merge_occupancy_mask' in (root/'tests/ai_phase7_persistence_v728_tests.cpp').read_text()
print('V728 phase-7 integration source contract PASS')
