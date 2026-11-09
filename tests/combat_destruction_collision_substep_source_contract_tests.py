from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
app = (root / 'src/editor/editor_app.cpp').read_text()
hdr = (root / 'src/editor/editor_app.hpp').read_text()

ray_begin = app.find('std::optional<EditorApp::SimulationModelRaycastHit> EditorApp::simulation_model_raycast_nearest(')
ray_end = app.find('\nvoid EditorApp::simulation_refresh_mode0_projectile_raycast(', ray_begin)
mode0_begin = ray_end
mode0_end = app.find('\n\nvoid EditorApp::simulation_refresh_non_direct_projectile_model_collision(', mode0_begin)
non_begin = mode0_end
non_end = app.find('\nbool EditorApp::simulation_line_of_fire_clear(', non_begin)
los_begin = non_end
los_end = app.find('\nstd::optional<std::pair<Vec3f,Vec3f>> EditorApp::simulation_class13_spawn_and_exit_points(', los_begin)
if min(ray_begin, ray_end, mode0_begin, mode0_end, non_begin, non_end, los_begin, los_end) < 0:
    print('V551 substep contract could not isolate collision/LOS functions', file=sys.stderr)
    raise SystemExit(1)
ray = app[ray_begin:ray_end]
mode0 = app[mode0_begin:mode0_end]
non = app[non_begin:non_end]
los = app[los_begin:los_end]

checks = {
    'raycast requires explicit query tick':
        'const ViewRay& ray,float maximum_distance,uint32_t query_tick' in ray and
        'float maximum_distance,uint32_t query_tick' in hdr,
    'destruction age uses raycast substep':
        'const uint32_t age=query_tick>=destruction->second.start_tick' in ray and
        '?query_tick-destruction->second.start_tick:0U' in ray and
        'current_presentation_tick_>=destruction->second.start_tick' not in ray,
    'mode0 refresh receives and forwards current tick':
        'uint32_t current_tick' in mode0 and
        'maximum_distance,current_tick,excluded_placement,excluded_transient' in mode0 and
        'simulation_refresh_mode0_projectile_raycast(projectile,*shot,tick);' in app,
    'non-direct collision forwards current tick':
        'segment_distance,current_tick,excluded_placement,excluded_transient' in non,
    'LOS receives and forwards query tick':
        'bool support_or_repair_command,uint32_t query_tick' in los and
        'query_distance,query_tick,' in los,
    'authored weapon LOS uses simulation substep':
        'index,intended_target_index,retail_los_origin,*weapon_target,*shot,recovery_weapon,tick' in app,
    'class21 candidate LOS uses simulation substep':
        'candidate.placement_index,source.actor.position,position,source_shot,false,tick,' in app,
    'catchup loop remains bounded multi-step':
        'const uint32_t steps=std::min<uint32_t>(presentation_tick-last_simulation_tick_,128U);' in app and
        'const uint32_t tick=first_tick+step;' in app,
}
failed = [name for name, ok in checks.items() if not ok]
for name, ok in checks.items():
    print(f"{'PASS' if ok else 'FAIL'} {name}")
if failed:
    print('V551 destruction collision substep contract failed: ' + ', '.join(failed), file=sys.stderr)
    raise SystemExit(1)
print('V551_DESTRUCTION_COLLISION_SUBSTEP_SOURCE_CONTRACT PASS')
