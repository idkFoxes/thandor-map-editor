from pathlib import Path
src=Path(__file__).resolve().parents[1]
lab=(src/'src/core/simulation_lab.cpp').read_text()
segment=lab[lab.index('void service_pioneer_behavior('):lab.index('void SimulationLabExperiment::apply_player_commands')]
assert 'admitted_selector_tick=selector_tick&&' in segment
assert 'lab_pioneer_workspace01_admitted_v743(run,index)' in segment
assert 'scheduled_callback=admitted_selector_tick&&pioneer_ai_dispatch_ready(' in segment
assert 'if(scheduled_callback || (s.flags&0x100U))' in segment
assert 's.flags|=4U;const auto movement=pioneer_movement_update(s,deps);' in lab
assert '#include <algorithm>' in (src/'include/thandor/simulation_command_markers.hpp').read_text()
# Marker colours and destruction behavior belong to other, fixed-scope paths.
assert 'simulation_lab_spawn_terminal_replacement' not in lab
print('V743_PIONEER_SCOPE PASS')
