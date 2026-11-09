from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
source=(root/'src/core/simulation_lab.cpp').read_text()
header=(root/'include/thandor/simulation_lab_terrain_refresh.hpp').read_text()
assert 'simulation_lab_terrain_refresh_root_linked(entity)' in source
assert 'simulation_lab_terrain_refresh_source(' in source
assert 'run,cell_count,(run.tick&7U)==3U' in source
assert 'run,field.cells().size(),true' in source
assert 'if(run.level_runtime_enabled && (run.tick==1U||(run.tick&7U)==3U))' in source
assert 'if (run.level_runtime_enabled && (run.tick&7U)==7U)' in source
step=source.index('        update_exploration_and_points(run, field);')
rebuild=source.index('        lab_advance_persistent_scratch_world_job(run,field,runtime_catalog_.get(),static_placement_owners_);',step)
callback=source.index('            lab_refresh_army_model_terrain_states(run,field,',rebuild)
assert step<rebuild<callback
assert 'field_occupancy_masks.size()==cell_count' in header
assert 'entity.root_model_terminal' not in header # terminal means still linked
assert 'return {};' in header # never treat a stale projection as a completed field
from source_contract_support import require_editor_version_at_least
require_editor_version_at_least(root,735)
assert '#include <algorithm>' in (root/'include/thandor/simulation_command_markers.hpp').read_text()
print('V735_TERRAIN_REFRESH_SOURCE_CONTRACT PASS')
