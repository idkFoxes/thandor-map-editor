#!/usr/bin/env python3
"""Guard the recovered world-owner and anchor lifecycle against accidental shortcuts."""
from pathlib import Path
import argparse


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1])
    root = ap.parse_args().source
    body = (root/'src/core/simulation_lab.cpp').read_text()
    lead = (root/'include/thandor/simulation_lab_shot_lead.hpp').read_text()
    owner = (root/'include/thandor/simulation_lab_ai_owner_list_v718.hpp').read_text()
    model = (root/'include/thandor/simulation_lab.hpp').read_text()
    assert 'lab_ai_has_world_model_owner_v722(entity.runtime_removed,entity.faction_index)' in body
    assert 'return !runtime_removed && faction_index != 0U;' in owner
    # Retail arrival does not call the selection StopMovement coordinate writer.
    # Do not silently reinstate the V722 inferred end-of-route anchor.
    assert 'entity.ai_damage_tracked_coordinate_q12=anchor;' not in body
    assert 'entity.ai_path_coordinate_q12=anchor;' not in body
    assert 'ordinary arrival must not make' in body
    assert 'std::optional<MovementPoint> ai_damage_tracked_coordinate_q12;' in model
    assert 'target.ai_path_coordinate_q12 && target.ai_damage_tracked_coordinate_q12' in lead
    assert 'target.ai_path_coordinate_q12 && target.ai_damage_tracked_coordinate_q12' in lead
    assert 'if (root_aim_z) current_aim.z=*root_aim_z;' in lead
    assert 'if (x2<=lead_squared && y2<=lead_squared-x2)' in lead
    assert '#include <algorithm>' in (root/'include/thandor/simulation_command_markers.hpp').read_text()
    print('V722_OWNER_ANCHOR_SOURCE_CONTRACT PASS')


if __name__ == '__main__':
    main()
