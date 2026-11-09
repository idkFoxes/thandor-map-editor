from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
app = (root / 'src/editor/editor_app.cpp').read_text()

begin = app.find('std::optional<EditorApp::SimulationModelRaycastHit> EditorApp::simulation_model_raycast_nearest(')
end = app.find('\nvoid EditorApp::simulation_refresh_mode0_projectile_raycast(', begin)
if begin < 0 or end < 0:
    print('V550 destroyed-model raycast source contract could not isolate model raycast function', file=sys.stderr)
    raise SystemExit(1)
raycast = app[begin:end]

checks = {
    'authored raycast filters completed destruction':
        'building_destruction_states_.find(index)' in raycast and
        'catalog_.effect_definition(destruction->second.effect_id)' in raycast and
        'thandor::effect_lifetime_ticks(*definition)' in raycast and
        'if(lifetime==0U||age>=lifetime)continue;' in raycast,
    'destruction age uses caller collision timeline':
        'uint32_t query_tick' in raycast and
        'query_tick>=destruction->second.start_tick' in raycast and
        '?query_tick-destruction->second.start_tick:0U' in raycast,
    'live destruction hierarchy remains collidable until completion':
        raycast.find('if(lifetime==0U||age>=lifetime)continue;') <
        raycast.find('consider(placement_model_hulls_[index],SimulationModelRaycastHit{'),
    'editor selection hulls remain independent':
        'placement_pick_hulls_' not in raycast and
        'for(const auto& hull:placement_pick_hulls_[index])' in app,
    'retail owner-list destruction rationale retained':
        'keeps the hierarchy in the WorldRuntime owner list only until' in raycast and
        'stop the removed model hierarchy from blocking simulation LOS/projectiles' in raycast,
}
failed = [name for name, ok in checks.items() if not ok]
for name, ok in checks.items():
    print(f"{'PASS' if ok else 'FAIL'} {name}")
if failed:
    print('V550 destroyed-model raycast source contract failed: ' + ', '.join(failed), file=sys.stderr)
    raise SystemExit(1)
print('V550_DESTROYED_MODEL_RAYCAST_SOURCE_CONTRACT PASS')
