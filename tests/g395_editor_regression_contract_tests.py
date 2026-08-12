#!/usr/bin/env python3
import argparse, pathlib, subprocess, tempfile, os
ap=argparse.ArgumentParser(); ap.add_argument('--source',required=True); ns=ap.parse_args()
r=pathlib.Path(ns.source)
app=(r/'src/editor/editor_app.cpp').read_text(); hdr=(r/'src/editor/editor_app.hpp').read_text(); ctx=(r/'include/thandor/context_order.hpp').read_text(); walker=(r/'src/editor/articulated_walker_pose.hpp').read_text()
for name,token in [('fog menu','BeginMenu("Fog of war preview")'),('scaled timeline','animation_preview_multiplier_'),('shared simulation tick','update_simulation(current_presentation_tick_)'),('faction research editor','draw_faction_research_mask_editor'),('direct placement','add_asset_placement(picked->first,picked->second,drag.registry_id)'),('selection brackets','const ImU32 selection=IM_COL32(75,255,105,245)'),('production order','SetProductionRallyPoint'),('production marker state','simulation_production_rally_points_'),('near sphere fallback','eye_distance<=std::max(2.0F,radius*2.5F)'),('near AABB fallback','distance_to_bounds<=std::max(3.0F,radius*1.5F)')]:
    if token not in app+hdr+ctx: raise SystemExit(f'missing {name}: {token}')
if 'building_animation_preview_multiplier_' in app+hdr: raise SystemExit('building-only speed domain survived')
for rejected in ['popup_screen_position','SetNextWindowPos(ImVec2(pending_technology_placement_', 'PendingTechnologyPlacement','technology_level_index','placement_technology_level']:
    if rejected in app+hdr: raise SystemExit(f'superseded per-placement technology preview survived: {rejected}')

for name,token in [
    ('recursive MDL animation helper','model_runtime_child_yaw_step_turn16'),
    ('recursive animation owner','animation_owner=hierarchy[node.parent_index]')]:
    if token not in app+hdr: raise SystemExit(f'missing {name}: {token}')
for rejected in ['64x inspection','256x inspection','animation_preview_multiplier_=256U',
                 'animation_preview_rate_user_selected_',
                 'animation_preview_rate_auto_selected_for_buildings_']:
    if rejected in app+hdr: raise SystemExit(f'superseded G397 workaround survived: {rejected}')
if 'rotate_basis_from_to(transforms[upper]' in walker or 'transforms[foot].basis=transforms[root].basis' in walker: raise SystemExit('detaching world-space walker IK survived')
if 'for(auto& transform:transforms)transform.translation.z+=root_delta' not in walker: raise SystemExit('coherent walker support missing')
linux=r/'build_linux.sh'
if not linux.exists() or not os.access(linux,os.X_OK): raise SystemExit('executable Linux build script missing')
if 'clean_build_directory_preserving_artifacts.sh' not in linux.read_text(): raise SystemExit('Linux build does not preserve artifacts')
with tempfile.TemporaryDirectory() as d:
    b=pathlib.Path(d)/'build'; (b/'nested').mkdir(parents=True); (b/'nested'/'game.EXE').write_bytes(b'exe'); (b/'nested'/'PATCH00.pCk').write_bytes(b'pck'); (b/'stale.o').write_bytes(b'old')
    subprocess.run([str(r/'tools/clean_build_directory_preserving_artifacts.sh'),str(b)],check=True)
    if (b/'nested'/'game.EXE').read_bytes()!=b'exe' or (b/'nested'/'PATCH00.pCk').read_bytes()!=b'pck': raise SystemExit('artifact preservation failed')
    if (b/'stale.o').exists(): raise SystemExit('stale non-artifact survived')
print('G395_EDITOR_REGRESSION_CONTRACT PASS')
