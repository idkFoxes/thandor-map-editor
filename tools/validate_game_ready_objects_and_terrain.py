#!/usr/bin/env python3
"""Validate G334 game-ready object chains and terrain compatibility contracts."""
from __future__ import annotations

import argparse
import json
import os
import subprocess
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def contains_all(text: str, tokens: list[str]) -> bool:
    return all(token in text for token in tokens)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--game-root", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    source = args.source.resolve()

    catalog_hpp = (source / "include/thandor/catalog.hpp").read_text(encoding="utf-8")
    field_hpp = (source / "include/thandor/field.hpp").read_text(encoding="utf-8")
    model_hpp = (source / "include/thandor/model.hpp").read_text(encoding="utf-8")
    catalog_cpp = (source / "src/core/catalog.cpp").read_text(encoding="utf-8")
    field_cpp = (source / "src/core/field.cpp").read_text(encoding="utf-8")
    model_cpp = (source / "src/core/model.cpp").read_text(encoding="utf-8")
    editor_cpp = (source / "src/editor/editor_app.cpp").read_text(encoding="utf-8")
    editor_hpp = (source / "src/editor/editor_app.hpp").read_text(encoding="utf-8")
    cmake = (source / "CMakeLists.txt").read_text(encoding="utf-8")
    tests = (source / "tests/terrain_game_ready_object_terrain_compatibility_tests.cpp").read_text(encoding="utf-8")

    checks = {
        "registry_variants_preserved": contains_all(catalog_hpp + catalog_cpp, [
            "compatible_arm_paths", "compatible_model_paths", "model_source_affinity",
            "merged_presets", "contains_any_base"
        ]),
        "current_lev_dependency_gate": contains_all(catalog_cpp + editor_cpp, [
            "placement_available_in_level", "placement_compatibility_diagnostic",
            "Cannot place ", "game export blocked:"
        ]),
        "empty_spr_helpers_hidden": contains_all(catalog_cpp, [
            "direct_visual_cache", "!sprite.mesh().vertices.empty()",
            "!sprite.mesh().indices.empty()", "preset.browser_visible && preset.game_placeable"
        ]),
        "stock_only_elements": contains_all(catalog_cpp, [
            "preset.stock_placement_count != 0U", "arm/kulisse.arm",
            "direct_placement_proven"
        ]),
        "model_runtime_fields_parsed": contains_all(model_hpp + model_cpp, [
            "placement_occupancy_class", "traversal_low_band_index",
            "traversal_high_band_index", "offset + 0xC0U", "offset + 0x260U",
            "offset + 0x264U", "visual.placement_occupancy_class < 21U"
        ]),
        "terrain_elevation_convention": contains_all(field_hpp + field_cpp, [
            "terrain_normal_elevation_turn16", "water_normal_elevation_turn16",
            "std::atan2(std::max(0.0F, normal.z), horizontal)", "0x01000000U << high_band_index"
        ]),
        "building_occupancy_bit_contract": contains_all(field_cpp, [
            "0x00000800U << occupancy_class", "0x88006000U | mask"
        ]),
        "terrain_overlay_present": contains_all(editor_hpp + editor_cpp, [
            "terrain_compatibility_overlay_", "terrain_overlay_profile()",
            "PrimitiveBlendPreset::AlphaNoDepthWrite", "terrain_overlay_safe_cell_count_",
            "terrain_overlay_caution_cell_count_", "terrain_overlay_blocked_cell_count_"
        ]),
        "damage_claim_is_bounded": "No independent terrain damage-over-time table was found" in editor_cpp,
        "g334_regression_target": contains_all(cmake + tests, [
            "terrain_game_ready_object_terrain_compatibility_tests",
            "campaign building01 / mdl/01 alternative chain was not recognized",
            "building04 ARM with mdl/05 model family was not resolved by registry ID",
            "expected complete 56-map stock catalog", "stock_placements="
        ]),
    }
    for name, passed in checks.items():
        require(passed, f"G334 static contract failed: {name}")

    runtime = {"executed": False}
    if args.build_dir is not None:
        executable = args.build_dir.resolve() / "terrain_game_ready_object_terrain_compatibility_tests"
        require(executable.is_file(), f"missing G334 test executable: {executable}")
        command = [str(executable)]
        if args.game_root is not None:
            command.append(str(args.game_root.resolve()))
        environment = dict(os.environ)
        environment["PYTHONDONTWRITEBYTECODE"] = "1"
        completed = subprocess.run(command, text=True, capture_output=True, env=environment, check=False)
        require(completed.returncode == 0,
                f"G334 runtime regression failed\n{completed.stdout}{completed.stderr}")
        runtime = {
            "executed": True,
            "command": command,
            "stdout": completed.stdout.strip(),
        }

    report = {
        "schema": "thandor-g334-game-ready-object-terrain-compatibility-validation-v1",
        "status": "PASS",
        "source": str(source),
        "checks": checks,
        "runtime": runtime,
    }
    text = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
