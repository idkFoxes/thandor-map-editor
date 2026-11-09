#!/usr/bin/env python3
from pathlib import Path
import argparse
ap=argparse.ArgumentParser()
ap.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[1])
r=ap.parse_args().source
core=(r/'src/core/simulation_lab.cpp').read_text()
hdr=(r/'include/thandor/simulation_lab_ai_base_extents_v724.hpp').read_text()
assert 'lab_ai_inside_primary_extents_v724(' in core
assert 'if (entity.faction_index==0U ||\n            !simulation_lab_has_live_root_model_owner(entity)) continue;' in core
assert 'if (simulation_lab_has_live_root_model_owner(entity))\n                retail_placement_stamp_influence(scratch,' in core
assert 'owner_workspaces.structures00' in core
assert 'if (own.root_model_terminal' not in core[core.index('const bool inside_structure_extent'):core.index('const bool inside_structure_extent')+850]
assert 'std::int64_t(point.x)-entry->root_position_q12.x' in hdr
assert 'dx > -extent && dx < extent && dy > -extent && dy < extent' in hdr
assert '#include <algorithm>' in (r/'include/thandor/simulation_command_markers.hpp').read_text()
assert 'kEditorVersion' in (r/'include/thandor/editor_version.hpp').read_text()
print('V724_WORKSPACE_BASE_EXTENTS_CONTRACT PASS')
