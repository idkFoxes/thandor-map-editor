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
    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    image_base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    rva = va - image_base
    table = pe + 24 + optional_size
    for index in range(section_count):
        entry = table + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
            "<IIII", data, entry + 8
        )
        if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
            return raw_offset + rva - virtual_address
    raise ValueError(f"VA {va:08X} is outside mapped sections")


def bytes_at(data: bytes, va: int, expected_hex: str) -> bool:
    expected = bytes.fromhex(expected_hex)
    offset = pe_va_to_offset(data, va)
    return data[offset : offset + len(expected)] == expected


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--game-exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    source = args.source.resolve()
    game = args.game_exe.read_bytes()
    field_hpp = (source / "include/thandor/field.hpp").read_text(encoding="utf-8")
    field_cpp = (source / "src/core/field.cpp").read_text(encoding="utf-8")
    level_cpp = (source / "src/core/level.cpp").read_text(encoding="utf-8")
    shadow_hpp = (source / "include/thandor/terrain_shadow.hpp").read_text(encoding="utf-8")
    shadow_cpp = (source / "src/core/terrain_shadow.cpp").read_text(encoding="utf-8")
    editor_cpp = (source / "src/editor/editor_app.cpp").read_text(encoding="utf-8")
    editor_hpp = (source / "src/editor/editor_app.hpp").read_text(encoding="utf-8")

    checks = {
        "immutable_game_sha256": sha256(args.game_exe) == GAME_SHA256,
        "lev_2f0_split_low_then_high": bytes_at(
            game,
            0x0053192B,
            "8B 86 F0 02 00 00 8B D0 25 FF FF 00 00 C1 FA 10 50 52 E8 7E BC FD FF",
        ),
        "world_runtime_stores_low_azimuth_high_elevation": bytes_at(
            game,
            0x0050D5ED,
            "8B 4D 18 8B 55 14 89 8B B8 00 00 00 89 93 BC 00 00 00",
        ),
        "shadow_terrain_raycast_uses_b8_bc": bytes_at(
            game,
            0x004CE31F,
            "03 C0 50 FF B3 B8 00 00 00 FF B3 BC 00 00 00 E8 2D 6B 03 00",
        ),
        "shadow_direction_vector_uses_b8_bc": bytes_at(
            game,
            0x004CE33A,
            "50 FF B3 B8 00 00 00 FF B3 BC 00 00 00 E8 24 64 FB FF",
        ),
        "source_low_azimuth_high_elevation": all(
            token in field_hpp + level_cpp
            for token in [
                "uint16_t azimuth{};",
                "uint16_t elevation{};",
                "lighting_.azimuth = static_cast<uint16_t>(packed & 0xFFFFU)",
                "lighting_.elevation = static_cast<uint16_t>(packed >> 16U)",
                "static_cast<uint32_t>(lighting_.elevation) << 16U",
            ]
        ),
        "source_fixed_math_vector": all(
            token in field_cpp
            for token in [
                "FixedMath_DirectionFromAnglesScaledRegs(elevation, azimuth, scale)",
                "std::cos(elevation_radians)",
                "std::sin(azimuth_radians)",
                "std::sin(elevation_radians)",
            ]
        ),
        "editor_uses_level_light_direction": all(
            token in editor_cpp + shadow_hpp + shadow_cpp
            for token in [
                "shadow_projection_direction=document_->level().lighting().shadow_ray_direction()",
                "Vec3f shadow_projection_direction",
                "projection_direction",
            ]
        ),
        "camera_direction_path_absent": all(
            token not in editor_cpp + editor_hpp
            for token in [
                "shadow_camera_forward",
                "shadow_camera_dirty_",
                "camera_.target.x-shadow_eye.x",
            ]
        ),
    }
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        raise SystemExit("G357 install validation failed: " + ", ".join(failed))

    result = {
        "schema": "thandor-map-editor-g357-install-contract-v1",
        "status": "PASS",
        "checks": checks,
        "game_exe_sha256": sha256(args.game_exe),
        "scope": (
            "Target-install verification of immutable binary bytes and active source. "
            "Release-time current-C, untouched-C, and GZF comparison remains sealed in "
            "evidence/g357/G357_REVERSE_ENGINEERING_VALIDATION.json."
        ),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
