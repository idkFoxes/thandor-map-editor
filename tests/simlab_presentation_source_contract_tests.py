#!/usr/bin/env python3
import argparse
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('--source',required=True)
args=parser.parse_args()
root=Path(args.source)
app=(root/'src/editor/editor_app.cpp').read_text()
sim=(root/'src/core/simulation_lab.cpp').read_text()
simh=(root/'include/thandor/simulation_lab.hpp').read_text()
audio=(root/'src/core/simulation_lab_audio.cpp').read_text()

checks={
    'catalog contact rate is seeded into Lab': 'seed.articulated_contact_interpolation_rate_q12=' in app,
    'tank sprite chord baseline is seeded into Lab': 'seed.tank_track_local_bounds_y0=cached_sprite' in app,
    'Lab invokes class-2 track callback': 'simulation_update_tank_track_offsets(entity.actor' in sim,
    'Lab invokes class-3 terrain-contact transition': 'simulation_update_articulated_contact_transition(entity.actor' in sim,
    'Lab does not substitute movement speed for contact rate': 'std::max(entity.motion.speed_q12_per_tick, 1)' not in sim,
    'Lab services class-17 lifecycle pose': 'simulation_tick_class17_pose(actor, moved)' in sim,
    'Lab snapshot publishes runtime sound gate': 'runtime_sound_profile' in simh and 'simulation_runtime_looping_sound_indices(' in audio and
        'simulation_lab_audio_frame(run,*simulation_lab_audio_catalog_,one_shots,' in app and
        'simulation_lab_loop_sound_requests_=std::move(audio.ordered_loops)' in app and
        'simulation_lab_audible_loops(requests,' in app,
    'Lab completion contact sound is spatial': (
        'SimulationLabOneShotEventKind::ArticulatedContact' in app and
        'play_runtime_spatial_sound(request.sound_table_index,request.world_position,' in app and
        'event.sound_gain_and_distance' in audio and 'frame.one_shots.push_back' in audio),
    'selected-run tick drives runtime visual animation': 'runtime_visual_tick=runs[run_index].tick' in app,
    'building timed effects are enabled in simulation/Lab': (
        'thandor::timed_effect_preview_enabled(' in app and
        'animate_timed_effects_,animate_buildings_,simulation_mode_||simulation_lab_active())' in app),
    'Lab dynamic rebuild is snapshot-driven': 'const bool dynamic_rebuild_due=simulation_lab_active()' in app,
}
failed=[name for name,ok in checks.items() if not ok]
if failed:
    for name in failed: print('FAIL:',name)
    raise SystemExit(1)
print('V543_SIMULATION_LAB_PRESENTATION_SOURCE_CONTRACT PASS')
