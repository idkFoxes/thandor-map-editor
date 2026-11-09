#!/usr/bin/env python3
"""Check the editor source contracts for serialized FLD, LEV and PCK metadata."""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.source.resolve()

    field_hpp = (root / "include/thandor/field.hpp").read_text(encoding="utf-8")
    field_cpp = (root / "src/core/field.cpp").read_text(encoding="utf-8")
    level_hpp = (root / "include/thandor/level.hpp").read_text(encoding="utf-8")
    level_cpp = (root / "src/core/level.cpp").read_text(encoding="utf-8")
    pck_hpp = (root / "include/thandor/pck.hpp").read_text(encoding="utf-8")
    pck_cpp = (root / "src/core/pck.cpp").read_text(encoding="utf-8")
    editor_cpp = (root / "src/editor/editor_app.cpp").read_text(encoding="utf-8")
    tests = (root / "tests/core_tests.cpp").read_text(encoding="utf-8")

    checks = {
        "field_runtime_aux54_stripped_by_retail_save": "serialized_aux54" not in field_hpp + field_cpp
            and "write_u32(data, offset + 0x54, 0U);" in field_cpp
            and "kRetailPersistedFlagsAndMaterialMask = 0xE80078FFU" in field_hpp
            and "kRetailSerializedTriangle0NormalSeed = 0x40000000U" in field_hpp,
        "field_runtime_pointer_not_misrepresented": "persisted_aux" not in field_hpp + field_cpp
            and "runtime terrain-direction lookup pointer" in field_cpp,
        "terrain_direction_a_b_semantics": all(token in field_hpp + field_cpp for token in [
            "struct TerrainDirectionRecordSeed", "angle_a", "angle_b", "rate_a", "rate_b", "scale_a", "scale_b"
        ]) and "xy_phase" not in field_hpp and "z_phase" not in field_hpp,
        "placement_tail_preservation": "preserved_tail_dwords" in level_hpp
            and "offset + 0x14 + j * 4" in level_cpp,
        "player_slot_complete_record": all(token in level_hpp + level_cpp for token in [
            "struct LevelPlayerSlotRecord", "start_camera_x_q12", "start_xenite_q4",
            "start_tritium_q4", "ai_class_or_mode", "faction_player_slot",
            "set_faction_player_slot", "faction_graphics_family_offset_raw",
            "set_faction_graphics_family_offset_raw"
        ]),
        "editor_exposes_raw_ai_class": "Graphics family offset (raw LEV)" in editor_cpp,
        "pck_method_is_typed": all(token in pck_hpp + pck_cpp for token in [
            "enum class PckCompressionMethod", "HuffmanRle = 0", "Stored = 1", "FieldGrid = 2",
            "PckCompressionMethod compression_method", "switch (entry.compression_method)"
        ]) and ".method" not in pck_cpp,
        "pck_runtime_directory_field_named": "directory_runtime_payload_offset" in pck_hpp + pck_cpp,
        "new_roundtrip_tests_present": all(token in tests for token in [
            "retail FLD save did not clear runtime cell dword +0x54", "typed compression enum",
            "LEV 0x20-byte player-slot record", "LEV placement tail dwords",
            "terrain direction A/B scales"
        ]),
    }

    for name, passed in checks.items():
        require(passed, f"Serialized asset metadata check failed: {name}")

    result = {"source": str(root), "checks": checks, "status": "PASS"}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
