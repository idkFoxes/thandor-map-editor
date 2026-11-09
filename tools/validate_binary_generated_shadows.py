#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

GAME_SHA256 = "53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def pe_va_to_offset(data: bytes, va: int) -> int:
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    optional = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    rva = va - base
    table = pe + 24 + optional
    for index in range(count):
        entry = table + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", data, entry + 8)
        if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
            return raw_offset + rva - virtual_address
    raise ValueError(f"VA {va:08X} outside PE sections")


def bytes_at(data: bytes, va: int, expected: str) -> bool:
    raw = bytes.fromhex(expected)
    offset = pe_va_to_offset(data, va)
    return data[offset : offset + len(raw)] == raw


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--game-exe", type=Path, required=True)
    parser.add_argument("--current-c", type=Path, required=True)
    parser.add_argument("--untouched-c", type=Path, required=True)
    parser.add_argument("--gzf", type=Path, required=True)
    parser.add_argument("--editor-shot", type=Path, required=True)
    parser.add_argument("--game-shot", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    source = args.source.resolve()
    game = args.game_exe.read_bytes()
    shadow_hpp = (source / "include/thandor/terrain_shadow.hpp").read_text(encoding="utf-8")
    shadow_cpp = (source / "src/core/terrain_shadow.cpp").read_text(encoding="utf-8")
    editor_cpp = (source / "src/editor/editor_app.cpp").read_text(encoding="utf-8")
    model_cpp = (source / "src/core/model.cpp").read_text(encoding="utf-8")
    shader = (source / "shaders/shadow.frag").read_text(encoding="utf-8")
    gate = (source / "docs/G355_BINARY_GENERATED_CAMERA_PLANE_SHADOW_GATE.md").read_text(encoding="utf-8")
    test_cpp = (source / "tests/rendering_binary_generated_shadow_tests.cpp").read_text(encoding="utf-8")
    cmake = (source / "CMakeLists.txt").read_text(encoding="utf-8")

    checks = {
        "immutable_game_sha256": sha256(args.game_exe) == GAME_SHA256,
        "persistent_shadow_defaults_64_32_16": (
            bytes_at(game, 0x00564642, "68 14 00 00 00 6A 40")
            and bytes_at(game, 0x0056464F, "68 10 00 00 00 6A 20")
            and bytes_at(game, 0x0056465C, "68 18 00 00 00 6A 10")
        ),
        "generated_palette_white_alpha_ramp": bytes_at(game, 0x004CD097, "B8 FF FF FF 00"),
        "runtime_caster_and_suppression_flags": (
            bytes_at(game, 0x0050BFF0, "F7 47 4C 40 00 00 00")
            and bytes_at(game, 0x0050BFF9, "F7 47 4C 00 01 00 00")
        ),
        "blur_quantizes_to_three_bits": (
            bytes_at(game, 0x004CD41F, "0F 73 D0 05")
            and bytes_at(game, 0x004CD42F, "0F DB C4")
        ),
        "source_retains_generated_mask_pipeline": all(
            token in shadow_hpp + shadow_cpp + editor_cpp + model_cpp + shader
            for token in [
                "texture_dimension{64U}",
                "grid_half_size{32U}",
                "subresource_count{16U}",
                "blur_game_soft_group",
                "std::max(alpha[index], hard[index])",
                "sprite.generated_shadow_mesh()",
                "offset + 0x68U",
                "runtime_render_flags & 0x40U",
                "32x32 by default",
                "0x7F000000U",
            ]
        ),
        "direction_corrected_by_g357": (
            "shadow_projection_direction=document_->level().lighting().shadow_ray_direction()" in editor_cpp
            and "shadow_camera_forward" not in editor_cpp
            and "shadow_camera_dirty_" not in editor_cpp
        ),
        "regression_target": all(
            token in cmake + test_cpp
            for token in [
                "rendering_binary_generated_shadow_tests",
                "soft group was not quantized",
                "light-direction shadow",
                "G355/G357 binary-generated light-plane shadow tests passed",
            ]
        ),
        "task_gate_supersedes_direction": all(
            token in gate
            for token in [
                "Direction-source clause superseded by G357",
                "64×64",
                "MDL +0x68",
                "Research-only output fails",
            ]
        ),
        "screenshots_present": args.editor_shot.is_file() and args.game_shot.is_file(),
        "c_inputs_present": args.current_c.is_file() and args.untouched_c.is_file(),
        "gzf_comparison_present": args.gzf.is_file() and args.gzf.stat().st_size > 0,
    }
    for name, passed in checks.items():
        require(passed, f"G355 corrected validation failed: {name}")

    result = {
        "schema": "thandor-map-editor-g355-retained-shadow-subsystem-g357-direction-corrected-v2",
        "status": "PASS",
        "checks": checks,
        "hashes": {
            "game.exe": sha256(args.game_exe),
            "current_c": sha256(args.current_c),
            "untouched_c": sha256(args.untouched_c),
            "supplied_gzf": sha256(args.gzf),
            "editor_shot": sha256(args.editor_shot),
            "game_shot": sha256(args.game_shot),
        },
        "supersession": {
            "geometry_source": "G356 dedicated SPR +0xEC table",
            "direction_source": "G357 LEV +0x2F0 low azimuth/high elevation",
            "camera_direction_claim": "rejected",
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
