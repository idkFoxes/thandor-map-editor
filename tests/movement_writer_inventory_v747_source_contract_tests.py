#!/usr/bin/env python3
import argparse
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('--source',required=True)
args=parser.parse_args()
root=Path(args.source)
follow_h=(root/'include/thandor/simulation_lab_follow.hpp').read_text()
follow=(root/'src/core/simulation_lab_follow.cpp').read_text()
lab_h=(root/'include/thandor/simulation_lab.hpp').read_text()
lab=(root/'src/core/simulation_lab.cpp').read_text()
cmake=(root/'CMakeLists.txt').read_text()

checks={
 'site/anchor routed writer':
  'void simulation_lab_set_site_route(' in follow and
  'pioneer_command_chain(s,target,0,deps);' in follow,
 'group target immediate writer':
  'void simulation_lab_set_group_target_route(' in follow and
  'army_start_fallback_follow(state,point(target_entity.actor.position),deps)' in follow and
  'state.flags&=~0x200U;' in follow,
 'factory auxiliary writer declaration':
  'void simulation_lab_set_factory_exit_route(' in follow_h,
 'factory auxiliary native flags and queue':
  'state.flags|=0x24BU;' in follow and
  'state.flags&=~0xB0U;' in follow and
  'state.queue_count=1U;' in follow and
  'state.queue[0]=queued_rally;' in follow,
 'factory door issue-tick call':
  'simulation_lab_set_factory_exit_route(run,field,child_index,producer_index,' in lab,
 'selection waypoint writer':
  'void simulation_lab_append_waypoint_or_start_route(' in follow and
  'pioneer_command_chain(state,target,2,deps);' in follow,
 'selection command surface':
  'MoveSelection, QueueSelectionWaypoint' in lab_h and
  'case SimulationLabPlayerCommandKind::MoveSelection:' in lab and
  'case SimulationLabPlayerCommandKind::QueueSelectionWaypoint:' in lab,
 'selection formation limits':
  'extent_x>0x5000||extent_y>0x5000||' in lab and
  'extent_x+extent_y>0x7000' in lab,
 'single class13 rally special case':
  'entity.runtime_model_class_id==13U' in lab and
  'entity.production_drive_target_world=Vec3f{' in lab,
 'V747 runtime tests registered':
  'movement_factory_auxiliary_route_v747_tests' in cmake and
  'movement_selection_commands_v747_tests' in cmake,
}
failed=[name for name,ok in checks.items() if not ok]
if failed:
    for name in failed: print('FAIL:',name)
    raise SystemExit(1)
print(f"V747_MOVEMENT_WRITER_INVENTORY_SOURCE_CONTRACT PASS checks={len(checks)}")
