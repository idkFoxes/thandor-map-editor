#!/usr/bin/env python3
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True)
args = parser.parse_args()
root = Path(args.source)
app = (root / 'src/editor/editor_app.cpp').read_text()
app_h = (root / 'src/editor/editor_app.hpp').read_text()
simulation_h = (root / 'include/thandor/simulation.hpp').read_text()
path_cpp = (root / 'src/core/v528_pathing.cpp').read_text()
effect_cpp = (root / 'src/core/effect.cpp').read_text()
simulation_cpp = (root / 'src/core/simulation.cpp').read_text()
ki_cpp = (root / 'src/core/ai_knowledge.cpp').read_text()
ki_panel = (root / 'src/editor/ai_knowledge_panel.cpp').read_text()

failures = []
def require(condition, message):
    if not condition:
        failures.append(message)

require('simulation_choose_local_detour' not in app + app_h,
        'superseded V451 left/right detour helper survived')
require('simulation_resolve_v528_route(' in app and 'v528_resolve_route(' in app,
        'V530-authority route resolver is not integrated into movement')
require('active primary route segment reconstructed from V530' in simulation_h,
        'move_waypoint is not documented as the V530 primary solver segment')
require('queuedWaypoints[8]' in simulation_h and 'separately' in simulation_h,
        'V528 command queue is not explicitly separated from solver segments')
require('Checkbox("Show route / 4x pathing cells",&v519_path_diagnostics_)' in app,
        'V530 route diagnostic control missing')
require('if(!v519_path_diagnostics_||index>=placements.size())continue;' in app and 'live_influence_bands_complete{}' in (root/'include/thandor/v528_pathing.hpp').read_text(),
        'path overlay gate or explicit incomplete live-influence state missing')
require('placement_pick_hulls_' in app and 'ray_transformed_bounds_distance(ray,hull)' in app,
        'V530 LOS does not test transformed model hierarchy bounds')
require('exclude_shooter_placement?std::optional<std::size_t>{shooter_index}:std::nullopt' in app and
        'intended_static=model_hit->placement_index&&intended_target_index' in app and
        'intended_dynamic=model_hit->transient&&intended_transient' in app,
        'V530 LOS does not preserve shooter exclusion and intended static/transient target handling')
require('Show 62 sourced KI parameters' in ki_panel,
        'V528 62-entry KI frontier is not exposed')
for token in [
    'AI_KNOWLEDGE_STRATEGIC_12D_AND_141_TO_143_ADDITIONAL_PLANNING_CAPACITY',
    'AI_KNOWLEDGE_STRATEGIC_12F_TO_132_ADDITIONAL_PLANNING_CAPACITY',
]:
    require(token in ki_cpp, f'missing V528 KI semantic {token}')
require('std::array<AiKnowledgeParameterSemantic, 62>' in ki_cpp,
        'V528 KI semantic dictionary is not fixed at 62 sourced entries')
require('equivalent axial/cube hex-line coverage' not in path_cpp and
        'V530 resolves the retail helper' in path_cpp,
        'V530 exact integer path-line walker did not replace cube interpolation')
require('nearest_unblocked_source' in path_cpp and 'source_cell_relocated' in
        (root/'include/thandor/v528_pathing.hpp').read_text(),
        'V530 source-side nearest-unblocked recovery is missing')
require('kRetailPassBoundaryEntries = 0x10000U' in path_cpp and
        'remaining_passes = 6U' in path_cpp,
        'V530 bounded six-pass weighted propagation is missing')
require('requested_cell_retargeted' in app and 'requested_cell_snapped' not in app,
        'movement integration still treats source recovery as destination snapping')
require('shot_line_of_fire_policy(shot.trajectory_kind())' in app and
        'if(!policy.test_terrain&&!policy.test_models)return true;' in app,
        'V530 trajectory-mode line-of-fire policy is not integrated')
require('shot_line_of_fire_range_q12(shot)' in app and
        'projectile_lifetime_ticks) - 1' in effect_cpp and
        'target_radius_q12=target_preset->simulation_motion_profile.collision_radius_q12' in app and
        '(compared_distance_q12*2-static_cast<int64_t>(target_radius_q12))/2' in app,
        'V530 direct/lead line-of-fire range and static/transient target-radius clearance are not integrated')
require('std::abs(dx)<=1.0F&&std::abs(dy)<=1.0F' in simulation_cpp and
        'if(!using_waypoint' in simulation_cpp,
        'V530 final-target +/-0x1000 Q12 arrival window is missing or applied to route waypoints')
require('pitch_in_retail_window' in simulation_cpp and
        'state.target_aligned=yaw_aligned&&pitch_aligned&&pitch_in_retail_window' in simulation_cpp,
        'V530 elevation-window rejection still allows clamp-and-fire behavior')
require('v565_path_segment_clear_static_cached(document_->field(),start,end' in app and
        'distance/0.20F' not in app[app.find('bool EditorApp::simulation_path_segment_clear'):app.find('thandor::V528RouteResult EditorApp::simulation_resolve_v528_route')],
        'pre-route movement validation still uses the pre-V530 sampled segment approximation')
require('live_influence_bands_complete{}' in (root/'include/thandor/v528_pathing.hpp').read_text(),
        'V528 route result can no longer disclose incomplete live influence state')

if failures:
    for failure in failures:
        print('FAIL:', failure)
    raise SystemExit(1)
print('V530_EDITOR_SOURCE_CONTRACT PASS')
