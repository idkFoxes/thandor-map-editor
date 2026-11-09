from pathlib import Path
root=Path(__file__).resolve().parents[1]
core=(root/'src/core/simulation_lab.cpp').read_text()
shot=(root/'include/thandor/simulation_lab_shot_terrain.hpp').read_text()
types=(root/'include/thandor/simulation.hpp').read_text()
version=(root/'include/thandor/editor_version.hpp').read_text()
assert 'lab_refresh_shot_terrain_samples(run,field,terrain_image,runtime_catalog_.get())' in core
assert core.count('lab_refresh_shot_terrain_samples(run,field,terrain_image,runtime_catalog_.get())')==2
assert 'simulation_lab_shot_neighborhood_mask(' in shot
assert 'mask|=classify(middle)' in shot and 'mask|=classify(beam_end)' in shot
assert 'terrain_neighborhood_mask' in types and 'terrain_refresh_tick' in types
assert 'V737' in version
assert '#include <algorithm>' in (root/'include/thandor/simulation_command_markers.hpp').read_text()
print('V736_SHOT_TERRAIN_SOURCE_CONTRACT PASS')
