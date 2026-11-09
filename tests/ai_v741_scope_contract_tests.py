from pathlib import Path
root=Path(__file__).resolve().parents[1]
src=(root/'src/core/simulation_lab.cpp').read_text()
assert 'x<field.width(); ++x' in src
assert 'run.level_runtime_enabled?field.width():field.width()-1U' not in src
assert 'simulation_lab_workspace05_native_score(' in src
assert 'simulation_lab_workspace05_score.hpp' in src
assert '#include <algorithm>' in (root/'include/thandor/simulation_command_markers.hpp').read_text()
print('V741 source-contract PASS')
