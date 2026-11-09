from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
app = (root / 'src/editor/editor_app.cpp').read_text()
hdr = (root / 'src/editor/editor_app.hpp').read_text()

checks = {
    'dedicated model hull storage': 'placement_model_hulls_' in hdr,
    'model hull storage reset': 'placement_model_hulls_.clear();' in app and 'placement_model_hulls_.assign(placements.size(),{});' in app,
    'ui fallback remains pick-only': 'pick_hulls.push_back(TransformedBoundsHull::from_axis_aligned(fallback_pick_bounds));' in app and 'model_hulls.push_back(TransformedBoundsHull::from_axis_aligned(fallback_pick_bounds));' not in app,
    'header bounds shared to simulation hulls': 'pick_hulls.push_back(node_hull);' in app and 'model_hulls.push_back(node_hull);' in app,
    'radius bounds shared to simulation hulls': 'pick_hulls.push_back(radius_hull);' in app and 'model_hulls.push_back(radius_hull);' in app,
    'simulated placement updates collision hulls': 'transform_hulls(placement_model_hulls_[placement_index],nullptr);' in app,
    'simulation raycast uses model-only hulls': 'for(std::size_t index=0;index<placement_model_hulls_.size()&&index<placements.size();++index)' in app and 'consider(placement_model_hulls_[index],SimulationModelRaycastHit{' in app,
    'selection still uses pick hulls': 'for(const auto& hull:placement_pick_hulls_[index])' in app,
}
failed = [name for name, ok in checks.items() if not ok]
for name, ok in checks.items():
    print(f"{'PASS' if ok else 'FAIL'} {name}")
if failed:
    print('V549 model collision hull source contract failed: ' + ', '.join(failed), file=sys.stderr)
    raise SystemExit(1)
print('V549_MODEL_COLLISION_HULL_SOURCE_CONTRACT PASS')
