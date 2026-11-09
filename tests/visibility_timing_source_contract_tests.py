#!/usr/bin/env python3
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True)
args = parser.parse_args()
root = Path(args.source)
app = (root / 'src/editor/editor_app.cpp').read_text(encoding='utf-8')
core = (root / 'src/core/simulation_lab.cpp').read_text(encoding='utf-8')
pathing = (root / 'src/core/v528_pathing.cpp').read_text(encoding='utf-8')
worker = (root / 'src/core/simulation_lab_worker.cpp').read_text(encoding='utf-8')
header = (root / 'include/thandor/simulation_lab.hpp').read_text(encoding='utf-8')
app_header = (root / 'src/editor/editor_app.hpp').read_text(encoding='utf-8')

def req(condition, message):
    if not condition:
        raise SystemExit(message)

start = app[app.index('void EditorApp::start_simulation_lab()'):app.index('void EditorApp::clear_simulation_lab()')]
# Runtime ownership follows MODEL class/hierarchy, including class 18,
# independently of the editor's browser category.
owner_gate = (root / 'include/thandor/simulation_lab_catalog.hpp').read_text()
req('simulation_lab_faction_placement_has_runtime_owner(*preset,placement.faction_index)' in start and
    'preset.runtime_model_class_id<24U' in owner_gate and
    '(!preset.resolved_model_hierarchy.empty()||!preset.model_hierarchy.empty())' in owner_gate and
    'PlacementVisualKind' not in owner_gate,
    'editor seed handoff lost the shared native runtime-owner gate')

req('entity.building = true;' in core and 'process_pending_construction' in core,
    'primary construction completion no longer forces authoritative building runtime state')

renderer = app[app.index('const auto append_simulation_lab_synthetic_model'):
               app.index('constexpr std::array<uint32_t,8> kRunGhostRgb')]
req('preset==nullptr)return false;' in renderer and
    'preset->browser_kind!=thandor::PlacementBrowserKind::Building' not in renderer,
    'synthetic building renderer still rejects retail special structures by editor browser category')

selected = app[app.index('if(selected){', app.index('constexpr std::array<uint32_t,8> kRunGhostRgb')):
               app.index('if(!simulation_lab_show_all_runs_)continue;', app.index('constexpr std::array<uint32_t,8> kRunGhostRgb'))]
req('if(entity.building)' in selected and 'bool rendered=append_simulation_lab_synthetic_model' in selected and
    'rendered=append_runtime_transient_model' in selected and 'if(!rendered)' in selected and
    'append_ghost_body' in selected,
    'selected run lacks the real-model-first visibility chain and last-resort marker')

req('simulation_lab_render_asset_signature_' in app_header and
    'render_signature!=simulation_lab_render_asset_signature_' in app and
    'simulation_static_replacement_dirty_=true' in app,
    'late synthetic assets no longer force one static texture residency rebuild')

preload = app[app.index('const auto preload_simulation_lab_preset_textures'):
              app.index('struct ShadowAtlasTile', app.index('const auto preload_simulation_lab_preset_textures'))]
req('for_each_selected_lab_synthetic_asset(runs,selected' in preload and
    'catalog_.army_preset(entity.army_asset_id)' in preload and 'run.faction_technology_masks' in preload,
    'Lab texture preload is not driven by actual synthetic runtime assets and live research state')

req('segment_rejected_on_demand' in pathing and 'nearest_unblocked_source_on_demand' in pathing,
    'V528 direct-route path no longer performs on-demand scratch evaluation')
req('terrain_cache->state_at(static_cast<uint32_t>(column), static_cast<uint32_t>(row))' in pathing and
    'field.sample_terrain' not in pathing[pathing.index('uint32_t scratch_state_at'):pathing.index('ScratchImage build_scratch')],
    'path classification stopped reading the shared native lattice or resumed interpolating normals per query')

resolver = pathing[pathing.index('V528RouteResult v528_resolve_route'):
                  pathing.index('V528RouteResult v565_resolve_route_fast_for_simulation_lab')]
req(resolver.index('segment_rejected_on_demand') < resolver.index('auto image = build_scratch'),
    'V528 resolver rebuilt the full 4x scratch image before trying its exact direct-line fast path')
req('propagate_costs(image' in resolver and 'find_routed_path_on_demand' not in resolver,
    'machine-backed V528 resolver was replaced by the Simulation Lab bounded-latency search')
lab_resolver = pathing[pathing.index('V528RouteResult v565_resolve_route_fast_for_simulation_lab'):
                       pathing.index('const char* v528_route_disposition_name')]
req('find_routed_path_on_demand' in lab_resolver and 'compressed_primary_on_demand' in lab_resolver,
    'Simulation Lab no longer uses its bounded-latency routed search')
req('kOpenBucketCount = 16U' in pathing and 'mixed_axial_distance(neighbor, source)' in pathing,
    'bounded-latency A* lost the exact consistent-heuristic bucket queue')
req('blocker_rows' in pathing and 'dynamic_blocked' in pathing and 'IndexedBlocker' in pathing,
    'dynamic path blockers are again rescanned in full for every visited scratch record')
req('navigation_recheck_tick' in header and 'navigation_direct_route_valid' in header and
    'kNavigationRecheckTicks = 8U' in core and 'navigation_cached_ticks' in core,
    'Lab movement no longer caches deterministic route state between 20 Hz ticks')
req('kMaximumRoutedSolvesPerTick = 1U' in core and
    'v565_resolve_route_fast_for_simulation_lab' in core and
    'navigation_deferred_route_solves' in header and 'navigation_routed_solves' in header,
    'Lab lost the one-detour-per-tick 20 Hz route budget or its diagnostics')
req('if (entity.target_entity_index && *entity.target_entity_index == j) continue;' in core,
    'combat destination is again being inserted as a dynamic blocker against its own route')
req('SimulationLabExperiment snapshot_copy() const' in header and
    'state->experiment.snapshot_copy()' in worker,
    'worker publication resumed copying immutable seed/ARM/TEC catalogs every generation')

print('v565 simulation lab empty-base visibility/timing source contract passed')
