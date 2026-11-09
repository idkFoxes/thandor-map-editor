from pathlib import Path
source = (Path(__file__).resolve().parents[1] / 'src/core/simulation_lab.cpp').read_text()
start=source.index('void SimulationLabExperiment::step_military_ai(')
end=source.index('void SimulationLabExperiment::step_run(',start)
block=source[start:end]
assert 'const auto owner_ws=lab_ai_owner_workspaces_v718(run,field,faction);' in block
assert 'for (const auto i:owner_ws.units01)' in block
assert 'for (std::size_t i=0;i<run.entities.size();++i)' not in block
assert block.count('const auto owner_ws=') == 1
assert 'const auto& visible_entities=owner_ws.targets07' in block
assert 'if(!simulation_lab_ai_unit_behavior_ready(' in block
assert '#include <algorithm>' in (Path(__file__).resolve().parents[1] / 'include/thandor/simulation_command_markers.hpp').read_text()
print('V742_WS01_DISPATCH_SOURCE PASS')
