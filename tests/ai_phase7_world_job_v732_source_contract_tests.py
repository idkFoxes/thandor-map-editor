#!/usr/bin/env python3
"""Source-level check that the phase-7 semantics are actually wired to the run."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
src = (root / 'src/core/simulation_lab.cpp').read_text()
mask = (root / 'include/thandor/simulation_lab_phase7_occupancy.hpp').read_text()
owner = (root / 'include/thandor/simulation_lab_phase7_owner.hpp').read_text()
public = (root / 'include/thandor/simulation_lab.hpp').read_text()
checks = {
    'scenario replay flag passed to run': 'run.local_faction_ended=scenario_runtime.local_faction_ended;' in src,
    'scenario and run flag present': public.count('bool local_faction_ended{};') == 2,
    'native owner gate integrated': 'simulation_lab_phase7_maybe_has_world_owner(run,run.static_placement_owner_count)' in src,
    'world job keeps startup distinct': 'if (!is_world_job || has_world_owner)' in src,
    'local ended flag gated to world tick': 'const auto ended_faction=is_world_job' in src,
    'selected faction writer': 'simulation_lab_phase7_ended_faction_mask(' in src,
    'retail projected high bits retained': '(previous_mask & persistent_bits) | projected_mask' in mask,
    'no universal ruins spawned': 'simulation_lab_phase7_maybe_has_world_owner(' in owner,
}
for label, ok in checks.items():
    if not ok:
        raise AssertionError(label)
print('V732_PHASE7_WORLD_JOB_SOURCE_CONTRACT PASS')
