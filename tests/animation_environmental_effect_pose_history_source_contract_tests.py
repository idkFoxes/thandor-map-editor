#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
editor = (root / "src/editor/editor_app.cpp").read_text()
hpp = (root / "src/editor/editor_app.hpp").read_text()
effect_h = (root / "include/thandor/effect.hpp").read_text()
effect_cpp = (root / "src/core/effect.cpp").read_text()

spawn = editor[editor.index("void EditorApp::spawn_environmental_linked_shots"):
               editor.index("void EditorApp::update_simulation(uint32_t presentation_tick)", editor.index("void EditorApp::spawn_environmental_linked_shots"))]
reset = editor[editor.index("void EditorApp::reset_simulation"):
               editor.index("void EditorApp::ensure_simulation_economy_initialized")]

checks = {
    "historical pose snapshot type": "EnvironmentalEffectRuntimePoseSnapshot" in hpp,
    "historical pose map": "environmental_effect_runtime_pose_history_" in hpp,
    "reset clears pose history": "environmental_effect_runtime_pose_history_.clear();" in reset,
    "absolute parent tick helper declared": "linked_shot_parent_effect_spawn_tick" in effect_h,
    "absolute parent tick helper implemented": "parent_spawn_tick = current_tick - parent_age_tick" in effect_cpp,
    "captures only pulse creation phase": "pulse.spawn_tick!=cycle_tick" in spawn,
    "snapshot freezes selected definition": "definition->definition_id,simulation_tick" in spawn,
    "snapshot freezes effect pose": "lifetime_ticks{}" in hpp and "effect_pose{}" in hpp,
    "history key includes absolute tick": "static_cast<uint64_t>(parent_spawn_tick)<<32U" in spawn,
    "emission resolves historical parent": "linked_shot_parent_effect_spawn_tick(" in spawn,
    "emission requires stored parent": "environmental_effect_runtime_pose_history_.find(" in spawn,
    "historical land-water definition reused": "history->second.effect_id!=candidate_effect_id" in spawn,
    "historical root drives reflection": "const Vec3f origin=snapshot.effect_pose.translation;" in spawn,
    "historical pose transforms type2": "transform_point(snapshot.effect_pose" in spawn,
    "current owner pose not used for shot geometry": "simulation_linked_effect_shot_geometry(origin,emitter)" in spawn,
    "expired parent runtimes pruned": "snapshot.lifetime_ticks" in spawn and "erase(it)" in spawn,
}

failed = [name for name, ok in checks.items() if not ok]
if failed:
    print("V560 environmental effect pose history source contract FAILED:", ", ".join(failed), file=sys.stderr)
    sys.exit(1)
print("V560_ENVIRONMENTAL_EFFECT_POSE_HISTORY_SOURCE_CONTRACT " +
      " ".join(f"{name}=PASS" for name in checks))
