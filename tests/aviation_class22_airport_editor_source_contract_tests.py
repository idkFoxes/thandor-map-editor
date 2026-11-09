#!/usr/bin/env python3
from pathlib import Path
import argparse

ap=argparse.ArgumentParser()
ap.add_argument('--source',default='.')
args=ap.parse_args()
root=Path(args.source)
hpp=(root/'src/editor/editor_app.hpp').read_text()
app=(root/'src/editor/editor_app.cpp').read_text()
checks={
    'class22 state map':'simulation_airport_states_' in hpp,
    'class22 candidate selection':'simulation_airport_candidate_selection_' in hpp,
    'shared faction secondary queue':'simulation_secondary_production_queues_[faction]' in app,
    'bit3 compatibility':'simulation_class22_candidate_compatible(candidate->army_runtime_flags14_raw)' in app,
    'xenite deduction':'simulation_faction_xenite_q4_[faction]-=candidate->retail_xenite_cost_q4' in app,
    'build uses recovered ARM build field':'candidate->retail_build_time_q5' in app,
    'build uses recovered ARM energy field':'candidate->retail_energy_demand_q4' in app,
    'highest-slot store helper reachable':'simulation_class22_complete_storage_build(state,true)' in app,
    'capacity comes from class22 MDL c4':'producer_preset.runtime_class_specific_c4_raw' in app,
    'platform baseline uses lookup class1 index0':'ModelLookupKeyClass{1U}' in app and 'ModelLookupKeyIndex{0U}' in app,
    'airport door texture channel rendered':'airport->second.primary_texture_offset_v' in app,
    'airport lift channel rendered':'class22_airport->second.platform_local_z_q12' in app,
    'build animation uses class22 clock':'class22_airport->second.animated_subnode_ticks' in app,
    'class22 launch remains a linked-child path':'simulation_linked_child_entities_' in hpp and 'simulation_class22_world_launch_command' in app,
}
failed=[name for name,ok in checks.items() if not ok]
if failed:
    raise SystemExit('V544_CLASS22_AIRPORT_EDITOR_SOURCE_CONTRACT FAIL: '+', '.join(failed))
print('V544_CLASS22_AIRPORT_EDITOR_SOURCE_CONTRACT PASS')
