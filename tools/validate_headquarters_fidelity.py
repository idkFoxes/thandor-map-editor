#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--game-exe", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    source = args.source.resolve()

    paths = {
        "effect_hpp": source / "include/thandor/effect.hpp",
        "effect_cpp": source / "src/core/effect.cpp",
        "editor_hpp": source / "src/editor/editor_app.hpp",
        "editor_cpp": source / "src/editor/editor_app.cpp",
        "renderer_hpp": source / "src/editor/terrain_renderer.hpp",
        "cmake": source / "CMakeLists.txt",
        "effect_test": source / "tests/rendering_effect_transition2_smoke_tests.cpp",
        "hq_test": source / "tests/rendering_headquarters_fidelity_tests.cpp",
        "gate": source / "docs/G369_HEADQUARTERS_TRANSITION2_SMOKE_RADAR_AND_ATOMIC_BILLBOARD_GATE.md",
        "progress": source / "G369_PROGRESS_REPORT.md",
        "defects": source / "G369_VALIDATOR_AND_REGRESSION_DEFECTS.md",
        "evidence": source / "evidence/g369/G369_REVERSE_ENGINEERING_AND_VALIDATION.json",
    }
    for name, path in paths.items():
        require(path.is_file(), f"missing G369 file {name}: {path}")
    text = {name: path.read_text(encoding="utf-8") for name, path in paths.items()}
    combined = "\n".join(text.values())

    checks = {
        "effect_offsets": all(token in text["effect_cpp"] for token in [
            "offset + 0x20", "offset + 0x50", "offset + 0x54"]),
        "effect_fields": all(token in text["effect_hpp"] for token in [
            "movement_speed_q12", "alpha_fade_in_ticks", "alpha_fade_out_ticks"]),
        "transition2_motion": all(token in text["effect_cpp"] + text["editor_cpp"] for token in [
            "effect_transition2_displacement_at_age", "orientation_random >> 20U",
            "elevation) * 63U + 0x4000U", "pose.translation=add(base_position"]),
        "runtime_alpha": all(token in text["effect_cpp"] + text["editor_cpp"] for token in [
            "effect_alpha_at_age", "alpha_fade_in_ticks", "alpha_fade_out_ticks",
            "combined_alpha"]),
        "atomic_billboard_packet": all(token in text["renderer_hpp"] + text["editor_cpp"] for token in [
            "uint32_t sort_group{}", "effect_sort_group", "run.sort_group!=0U",
            "One authored sprite billboard is one queue packet"]),
        "radar_preview": all(token in text["editor_hpp"] + text["editor_cpp"] for token in [
            "animation_preview_multiplier_{1U}",
            "Exact game rate (1x)", "16x",
            "model_runtime_child_yaw_step_turn16",
            "authored_parent_node.simulation",
            "child0_rotation_step_turn16)*authored_animation_tick"]),
        "building_default_off": "bool animate_buildings_{}" in text["editor_hpp"],
        "regression_targets": all(token in text["cmake"] + text["effect_test"] + text["hq_test"] for token in [
            "rendering_effect_transition2_smoke_tests",
            "rendering_headquarters_fidelity_tests",
            "G369 transition-2 smoke motion", "G369 Headquarters radar preview"]),
        "no_invented_volume": "volumetric_cross_plane" not in text["editor_cpp"]
            and "invented_smoke_plane" not in text["editor_cpp"],
        "task_gate": "Headquarters transition-2 smoke" in text["gate"]
            and "atomic" in text["gate"].lower(),
    }
    for name, passed in checks.items():
        require(passed, f"G369 validation failed: {name}")

    binary = None
    if args.game_exe is not None:
        game_exe = args.game_exe.resolve()
        require(game_exe.is_file(), f"missing game.exe: {game_exe}")
        require(sha256(game_exe) == "53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969",
                "immutable game.exe hash mismatch")
        data = game_exe.read_bytes()
        def range_hash(start: int, end: int) -> str:
            return hashlib.sha256(data[start - 0x400000:end - 0x400000]).hexdigest()
        require(range_hash(0x484770, 0x4847DD) ==
                "bb1cf1634c51511250fe2e3c58931fb06e14902a5f55dbb758a0b7831e52cdd0",
                "effect direction-vector binary range mismatch")
        require(range_hash(0x51E850, 0x51EEE5) ==
                "eb4600b14beda451fc03b16038ea751285a6750ace03f20bcb3c16f73a5ebe94",
                "effect lifecycle binary range mismatch")
        binary = {"game_exe_sha256": sha256(game_exe), "ranges": 2, "status": "PASS"}

    result = {
        "schema": "thandor-map-editor-g369-headquarters-fidelity-validation-v1",
        "source": str(source),
        "checks": checks,
        "binary": binary,
        "status": "PASS",
    }
    output = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding="utf-8")
    print(output, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
