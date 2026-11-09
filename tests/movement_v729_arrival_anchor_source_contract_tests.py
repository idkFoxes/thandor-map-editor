#!/usr/bin/env python3
"""Source contract: ordinary arrival does not invent StopMovement coordinates."""
from pathlib import Path
import argparse

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1])
    root = ap.parse_args().source
    code = (root/'src/core/simulation_lab.cpp').read_text()
    selection_stop = code[code.index('// Native ArmyRuntime_UpdateMovementAndWaypoints consumes'):]
    assert 'ordinary arrival must not make' in selection_stop
    assert 'entity.ai_damage_tracked_coordinate_q12=anchor;' not in code
    assert 'entity.ai_path_coordinate_q12=anchor;' not in code
    assert 'entity.ai_path_coordinate_q12=lab_ai_model_point_v719(entity.actor.position);' in code
    assert '#include <algorithm>' in (root/'include/thandor/simulation_command_markers.hpp').read_text()
    print('V729_ARRIVAL_ANCHOR_SOURCE_CONTRACT PASS')

if __name__=='__main__':
    main()
