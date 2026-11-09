#!/usr/bin/env python3
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True)
args = parser.parse_args()
app = (Path(args.source) / 'src/editor/editor_app.cpp').read_text(encoding='utf-8')

def req(condition, message):
    if not condition:
        raise SystemExit(message)

rebuild_start = app.index('void EditorApp::rebuild_objects()')
static = app[rebuild_start:app.index('bool EditorApp::placement_visible_in_canvas', rebuild_start)]
player = static[static.index('if(preset->visual_kind==thandor::PlacementVisualKind::PlayerStart'):
                static.index('if(preset->sprite_path.empty()', static.index('if(preset->visual_kind==thandor::PlacementVisualKind::PlayerStart'))]
req('++player_start_count_' in player and
    'simulation_lab_suppressed_placements_.contains(placement_index)' in player,
    'PlayerStart rendering is no longer unconditional before the conversion-only suppression gate')
req('const bool retail_runtime_unit=' in static and
    'preset->runtime_model_class_id==18U' in static and
    'preset->visual_kind==thandor::PlacementVisualKind::PlayerStart' in static and
    'retail_runtime_unit,' in static,
    'static replacement policy still excludes the General-browser class-18 Pioneer')

dynamic_start = app.index('dynamic_placements.reserve(dynamic_placements.size()+simulation_actor_placements_.size())')
dynamic_end = app.index('for(const auto& [placement_index,is_building]:dynamic_placements)', dynamic_start)
dynamic = app[dynamic_start:dynamic_end]
req('const bool is_unit=preset->browser_kind==thandor::PlacementBrowserKind::Unit ||' in dynamic and
    'preset->runtime_model_class_id==18U' in dynamic and
    'preset->visual_kind==thandor::PlacementVisualKind::PlayerStart' in dynamic,
    'live selected-run replacement still filters class-18 Pioneer by browser category')

selected_sync_start = app.index('std::unordered_map<std::size_t,thandor::SimulationActorState> selected_states;')
selected_sync_end = app.index('simulation_actor_states_=std::move(selected_states);', selected_sync_start)
selected_sync = app[selected_sync_start:selected_sync_end]
req('if(entity.synthetic||entity.runtime_removed||entity.source_placement_index>=placement_count)continue;' in selected_sync and
    'selected_states[entity.source_placement_index]=entity.actor;' in selected_sync,
    'selected-run persisted actors no longer publish class-18 runtime state to the world renderer')

print('v567 pioneer render source contract passed')
