#!/usr/bin/env python3
from pathlib import Path
import argparse

ap=argparse.ArgumentParser(); ap.add_argument('--source', default='.'); args=ap.parse_args()
root=Path(args.source)
hpp=(root/'src/editor/editor_app.hpp').read_text()
app=(root/'src/editor/editor_app.cpp').read_text()
sim=(root/'src/core/simulation.cpp').read_text()
checks={
    'transient linked child state':'SimulationLinkedChildEntity' in hpp and 'simulation_linked_child_entities_' in hpp,
    'command triple is heading xy':'simulation_class22_world_launch_command' in sim and
        'mission_heading_turn16' in app and 'mission_world_x_q12' in app and 'mission_world_y_q12' in app,
    'persistent slot occupied not consumed':'simulation_class22_mark_linked_child_spawned' in app and
        'simulation_class22_release_linked_child_slot(parent,child.storage_slot)' in app,
    'parent switch has no same tick fallthrough':'if(spawned_linked_child_this_tick)return;' in app,
    'parent door open':'simulation_class22_tick_open(state,producer_preset.runtime_class_step_q12)' in app,
    'parent platform raise':'simulation_class22_tick_raise_platform(state' in app,
    'child owns release edge':'simulation_class22_signal_child_release(parent)' in app,
    'parent platform lower':'simulation_class22_tick_lower_platform(state' in app,
    'parent door close':'simulation_class22_tick_close(state,producer_preset.runtime_class_step_q12)' in app,
    'mission pass state':'simulation_class21_begin_mission_pass(child.runtime' in app and
        'simulation_class21_tick_mission_pass_events(' in app,
    'return state':'simulation_class21_begin_return_when_parent_idle(child.runtime,parent' in app and
        'simulation_class21_tick_return_to_platform(child.runtime,parent)' in app,
    'class21 child is rendered':'append_runtime_transient_model(child.army_asset_id,child.faction_index' in app,
    'class21 child0 z is rendered':'linked_child->child0_local_z_q12' in app,
    'class21 child0 flight angles':'angle0=0x8000U' in app and 'linked_child->child0_pitch_turn16' in app,
    'class21 exact fixed pitch helper':'simulation_class21_flight_pitch_turn16' in sim and 'simulation_class21_flight_pitch_turn16(' in app,
    'offmap child not rendered':'if(!child.visible)continue;' in app,
    'resolved faction family':'graphics_family_for_faction(faction_index)' in app,
    'launch requires target and free slot':'simulation_target_point_.has_value()&&free_count>0U' in app,
}
failed=[name for name,ok in checks.items() if not ok]
if failed: raise SystemExit('V545_CLASS21_AIRPORT_SORTIE_SOURCE_CONTRACT FAIL: '+', '.join(failed))
print('V545_CLASS21_AIRPORT_SORTIE_SOURCE_CONTRACT PASS')
