#!/usr/bin/env python3
"""Narrow static source-contract gate for V585; no compiler, game assets or GUI needed."""
from pathlib import Path
import argparse

p=argparse.ArgumentParser()
p.add_argument('--source',type=Path,default=Path('.'))
r=p.parse_args().source
checks={
'include/thandor/simulation_lab.hpp':(
    'bool simulate_all_active_factions_as_ai{};',
),
'src/core/simulation_lab.cpp':(
    'scenario_runtime.simulate_all_active_factions_as_ai ||',
    'faction > run.assignable_faction_count',
),
'src/editor/editor_app.cpp':(
    'scenario_runtime.simulate_all_active_factions_as_ai=true;',
    'fog_persistent_explored=',
    'fog_origin_barrier=',
    'owner_disabled=',
    'self_relation=',
    'self_occupancy=',
    'last_planning_tick=',
    'vary_seed_per_run=',
),
'tests/scenario_level_runtime_tests.cpp':(
    'Simulation Lab must mark factions 1 and 2 AI even in an assignable LEV',
),
'tools/analyze_simlab_forensics.py':(
    'def analyze(path: Path) -> str:',
    'fog_persistent_explored',
),
}
for rel, snippets in checks.items():
    text=(r/rel).read_text()
    for text_fragment in snippets:
        if text_fragment not in text:
            raise SystemExit(f'V585 SOURCE CONTRACT FAIL: {rel}: {text_fragment}')
print('V585_AI_FOG_SOURCE_CONTRACT PASS (static only; compilation and CTest not run)')
