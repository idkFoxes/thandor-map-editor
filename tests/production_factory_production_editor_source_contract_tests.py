#!/usr/bin/env python3
import argparse
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('--source',required=True)
a=p.parse_args()
root=Path(a.source)
app=(root/'src/editor/editor_app.cpp').read_text(encoding='utf-8')
simh=(root/'include/thandor/simulation.hpp').read_text(encoding='utf-8')
sim=(root/'src/core/simulation.cpp').read_text(encoding='utf-8')

checks={
    'class13 queue start does not fall through into build tick':
        'if(started_this_tick)return;' in app,
    'class13 spawn lookup is key class5 index0 and exit is index1':
        'ModelLookupKeyIndex{0U}' in app and 'ModelLookupKeyIndex{1U}' in app and
        'simulation_class13_spawn_and_exit_points' in app,
    'class13 completion sound resolves producer root transform':
        'sound_position=transforms[root_index].translation' in app and
        'play_runtime_spatial_sound(producer_preset.class13_completion_sound_asset_id' in app,
    'class13 authored child animation has a state-owned cumulative clock':
        'animated_subnode_ticks' in simh and 'state.animated_subnode_ticks' in sim and
        'class13_factory->second.animated_subnode_ticks' in app,
    'produced class2 track animation services translation or turning':
        'preset->runtime_model_class_id==2U&&(translated||turned)' in app,
    'produced class3 services recovered contact transition':
        'simulation_update_articulated_contact_transition(entity.actor' in app and
        'preset->articulated_contact_interpolation_rate_q12' in app,
    'produced class3 contact sound is positioned at solved root':
        'instances[root_index].transform.translation' in app and
        'play_runtime_spatial_sound(preset->articulated_contact_sound_asset_id' in app,
    'produced class3 footprint chooses terminal alternating foot':
        'articulated_walker_detail::leg_chain(hierarchy,root_index' in app and
        'entity.actor.articulated_last_completed_side&1U' in app,
    'produced movement effects resolve land water at emitted world location':
        'retail_nearest_water_delta_q12' in app and
        'root->simulation.timed_water_effect_id:root->simulation.timed_effect_id' in app,
}
failed=[name for name,ok in checks.items() if not ok]
if failed:
    for name in failed: print('FAIL:',name)
    raise SystemExit(1)
print('V543_FACTORY_PRODUCTION_EDITOR_SOURCE_CONTRACT PASS')
