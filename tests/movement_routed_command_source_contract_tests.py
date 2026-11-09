#!/usr/bin/env python3
from pathlib import Path
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1])
args = parser.parse_args()
root = args.source.resolve()

header = (root / 'include/thandor/simulation_lab_follow.hpp').read_text()
follow = (root / 'src/core/simulation_lab_follow.cpp').read_text()
lab = (root / 'src/core/simulation_lab.cpp').read_text()
markers = (root / 'include/thandor/simulation_command_markers.hpp').read_text()

signature = '''void simulation_lab_set_site_route(SimulationLabRun& run,const FieldGrid& field,\n    size_t source_index,MovementPoint target,'''
assert signature in header, 'routed AI command still lacks explicit run/field/source/target context'

start = follow.index('void simulation_lab_set_site_route(')
end = follow.index('bool simulation_lab_primary_follow_weapon', start)
body = follow[start:end]
assert body.index('if(!e.motion.movable())return;') < body.index('simulation_lab_sync_follow(e);'), \
    'immobile routed command is not a strict pre-state-mutation early exit'
assert 'pioneer_command_chain(s,target,0,deps);' in body, \
    'routed command does not use the recovered native command chain'
assert 'const bool locked=(s.flags&2U)!=0U;' in body and 'if(locked)return;' in body, \
    'locked waypoint branch no longer preserves the active actor/navigation route'
assert 'e.actor.move_target=fallback;' in body, \
    'resolved fallback is not published to the actor target at issue time'
assert 'e.actor.move_waypoint=Vec3f' in body and 's.stored' in body, \
    'resolved primary route point is not published as the actor waypoint'
assert 'e.navigation_target_cell_valid=deps.last_resolved_cell_valid;' in body, \
    'resolved route cell is not published to the bounded Lab navigation cache'
assert 'e.combat_follow_owned=false;' in body, \
    'new routed order does not revoke target-follow ownership'

site = lab[lab.index('if (choice.kind==1U'):lab.index('// AiUnitGroup_AssignCollectedEntitiesToBestTarget')]
assert site.count('simulation_lab_set_site_route(run,field,i,') == 2, \
    'site and anchor AI branches are not both routed through immediate resolution'
assert 'army.actor.move_target=Vec3f' not in site, \
    'AI branch still overwrites the raw actor target before route resolution'

assert '#include <algorithm>' in markers, 'required command-marker algorithm include was removed'
print('MOVEMENT_ROUTED_COMMAND_SOURCE_CONTRACT PASS')
