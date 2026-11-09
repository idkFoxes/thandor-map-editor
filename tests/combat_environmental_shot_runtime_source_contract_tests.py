#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
editor = (root / "src/editor/editor_app.cpp").read_text()
hpp = (root / "src/editor/editor_app.hpp").read_text()
sim = (root / "include/thandor/simulation.hpp").read_text()

checks = {
    "environmental owner enum": "Environmental," in sim,
    "spawn method declared": "void spawn_environmental_linked_shots(uint32_t simulation_tick);" in hpp,
    "spawn method implemented": "void EditorApp::spawn_environmental_linked_shots(uint32_t simulation_tick)" in editor,
    "shared timed schedule": "build_resource_emitter_schedule(timings" in editor,
    "linked shot cadence": ("build_linked_shot_emissions(*definition)" in editor or "build_linked_shot_emissions(*candidate)" in editor),
    "exact linked geometry": "simulation_linked_effect_shot_geometry(origin,emitter)" in editor,
    "ownerless volley": "SimulationProjectileOwnerKind::Environmental,0U" in editor,
    "spawn before maintenance": editor.find("spawn_environmental_linked_shots(tick);") < editor.find("for(auto& projectile:simulation_projectiles_)", editor.find("spawn_environmental_linked_shots(tick);")),
    "mode0 no fabricated exclusion": editor.count("case thandor::SimulationProjectileOwnerKind::Environmental:") >= 2,
    "persistent render avoids duplicate reconstruction": "if(!simulation_mode_&&!simulation_lab_active()&&recursion_depth<8U&&definition.transition_kind==1U" in editor,
    "Lab snapshot projectiles rendered": "visible_projectiles=projectile_lab_run->projectiles;" in editor,
    "Lab projectile textures retained": "for(const auto& projectile:selected.projectiles)preload_shot_textures(projectile.shot_definition_id);" in editor,
    "shared model collision remains active": "simulation_refresh_non_direct_projectile_model_collision(projectile,*shot,tick);" in editor,
    "shared damage transition remains active": "projectile.impact_damage_applied=true;" in editor and "target_impact_damage_q12" in editor,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    print("V559 environmental shot runtime source contract FAILED:", ", ".join(failed), file=sys.stderr)
    sys.exit(1)
print("V559_ENVIRONMENTAL_SHOT_RUNTIME_SOURCE_CONTRACT " + " ".join(f"{k}=PASS" for k in checks))
