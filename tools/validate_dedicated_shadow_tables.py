#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path

GAME_SHA256 = "53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969"
EXPECTED_CENSUS = {
    "tree_assets": 54,
    "shadow_triangles": 867,
    "soft": 414,
    "hard": 453,
    "mixed": 31,
    "missing": 9,
}


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
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", data, entry + 8)
        if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
            return raw_offset + rva - virtual_address
    raise ValueError(f"VA {va:08X} is outside mapped sections")


def bytes_at(data: bytes, va: int, expected: bytes) -> bool:
    offset = pe_va_to_offset(data, va)
    return data[offset:offset + len(expected)] == expected


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"G356 validation failed: {message}")


def parse_census(text: str) -> dict[str, int]:
    match = re.search(
        r"G356_STOCK_SHADOW_TABLE_CENSUS PASS "
        r"tree_assets=(\d+) shadow_triangles=(\d+) soft=(\d+) hard=(\d+) mixed=(\d+) missing=(\d+)",
        text,
    )
    require(match is not None, "stock census output shape")
    values = [int(value) for value in match.groups()]
    return dict(zip(EXPECTED_CENSUS, values, strict=True))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--game-exe", type=Path, required=True)
    parser.add_argument("--current-c", type=Path, required=True)
    parser.add_argument("--untouched-c", type=Path, required=True)
    parser.add_argument("--gzf", type=Path, required=True)
    parser.add_argument("--game-root", type=Path, required=True)
    parser.add_argument("--stock-census-exe", type=Path, required=True)
    parser.add_argument("--editor-shot", type=Path, required=True)
    parser.add_argument("--game-shot", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    source = args.source.resolve()
    game = args.game_exe.read_bytes()
    current_c = args.current_c.read_text(encoding="utf-8", errors="replace")
    untouched_c = args.untouched_c.read_text(encoding="utf-8", errors="replace")
    model_hpp = (source / "include/thandor/model.hpp").read_text(encoding="utf-8")
    model_cpp = (source / "src/core/model.cpp").read_text(encoding="utf-8")
    shadow_hpp = (source / "include/thandor/terrain_shadow.hpp").read_text(encoding="utf-8")
    shadow_cpp = (source / "src/core/terrain_shadow.cpp").read_text(encoding="utf-8")
    editor_cpp = (source / "src/editor/editor_app.cpp").read_text(encoding="utf-8")
    tests = (source / "tests/rendering_dedicated_shadow_table_tests.cpp").read_text(encoding="utf-8")
    stock_test = (source / "tests/rendering_stock_shadow_table_census.cpp").read_text(encoding="utf-8")
    gate = (source / "docs/G356_DEDICATED_SPR_SHADOW_TABLE_AND_RECORD_GROUP_CORRESPONDENCE_GATE.md").read_text(encoding="utf-8")
    cmake = (source / "CMakeLists.txt").read_text(encoding="utf-8")

    collection = editor_cpp.find("generated_shadow_triangles.push_back")
    dedicated_before_collection = editor_cpp.rfind("sprite.generated_shadow_mesh()", 0, collection)
    visible_before_collection = editor_cpp.rfind("sprite.mesh()", 0, collection)

    checks = {
        "immutable_game_sha256": sha256(args.game_exe) == GAME_SHA256,
        "hard_recursive_walker_follows_spr_ec": all([
            bytes_at(game, 0x004CD98C, bytes.fromhex("8B 86 EC 00 00 00")),
            bytes_at(game, 0x004CD998, bytes.fromhex("8B 8E 04 00 00 00")),
            bytes_at(game, 0x004CD99E, bytes.fromhex("81 C6 20 00 00 00")),
            bytes_at(game, 0x004CD9B0, bytes.fromhex("56 E8 CA FE FF FF")),
            bytes_at(game, 0x004CD9B6, bytes.fromhex("03 B6 00 00 00 00")),
        ]),
        "soft_recursive_walker_follows_spr_ec": all([
            bytes_at(game, 0x004CDB0E, bytes.fromhex("8B 86 EC 00 00 00")),
            bytes_at(game, 0x004CDB1A, bytes.fromhex("8B 8E 04 00 00 00")),
            bytes_at(game, 0x004CDB20, bytes.fromhex("81 C6 20 00 00 00")),
            bytes_at(game, 0x004CDB30, bytes.fromhex("56 E8 BA FE FF FF")),
            bytes_at(game, 0x004CDB38, bytes.fromhex("03 B6 00 00 00 00")),
        ]),
        "hard_record_requires_clear_bit0": (
            bytes_at(game, 0x004CD897, bytes.fromhex("F7 46 10 01 00 00 00")) and
            bytes_at(game, 0x004CD89E, bytes.fromhex("0F 85 80 00 00 00"))
        ),
        "soft_record_requires_set_bit0": (
            bytes_at(game, 0x004CDA08, bytes.fromhex("F7 46 10 01 00 00 00")) and
            bytes_at(game, 0x004CDA0F, bytes.fromhex("0F 84 84 00 00 00"))
        ),
        "soft_quantization_precedes_blur": all([
            bytes_at(game, 0x004CD41F, bytes.fromhex("0F 73 D0 05")),
            bytes_at(game, 0x004CD42F, bytes.fromhex("0F DB C4")),
            bytes_at(game, 0x004CD48F, bytes.fromhex("0F 73 F0 02")),
        ]),
        "source_parses_dedicated_table": all(token in model_hpp + model_cpp for token in [
            "read_u32(data, 0xECU)", "shadow_table_offset", "shadow_record_count",
            "shadow_record_offset", "generated_shadow_mesh_", "mesh_record_flags",
        ]),
        "source_keeps_visible_and_shadow_meshes_distinct": all(token in model_hpp for token in [
            "const SpriteMesh& mesh() const", "generated_shadow_mesh()",
            "generated_shadow_record_count()", "has_generated_shadow_mesh()",
        ]),
        "source_uses_per_record_bit0": all(token in model_cpp + editor_cpp for token in [
            "record_offset + 0x10", "mesh_record_flags", "(a.mesh_record_flags&1U)!=0U",
        ]),
        "source_collects_only_dedicated_shadow_geometry": (
            collection >= 0 and dedicated_before_collection > visible_before_collection and
            "sprite.has_generated_shadow_mesh()" in editor_cpp and
            "sprite.lod_flags(0U)&1U" not in editor_cpp
        ),
        "source_has_no_visible_lod_shadow_fallback": (
            "if(projected_object_shadows_&&preset->casts_generated_shadow" in editor_cpp and
            "sprite.has_generated_shadow_mesh()" in editor_cpp and
            "generated_shadow_mesh().indices" not in shadow_cpp
        ),
        "source_preserves_g355_generated_mask_and_blur": all(token in shadow_hpp + shadow_cpp for token in [
            "texture_dimension{64U}", "grid_half_size{32U}", "blur_game_soft_group",
            "build_game_generated_object_shadow", "std::max(alpha[index], hard[index])",
        ]),
        "focused_regression_present": all(token in tests + cmake for token in [
            "rendering_dedicated_shadow_table_tests", "read_u32(data, 0xECU)",
            "visible LOD mesh still appears to be the active shadow source",
            "G356 dedicated SPR shadow-table tests passed",
        ]),
        "stock_regression_present": all(token in stock_test + cmake for token in [
            "rendering_stock_shadow_table_census", "tree_assets == 54U",
            "shadow_triangles == 867U", "soft_triangles == 414U", "hard_triangles == 453U",
        ]),
        "task_gate_present": all(token in gate for token in [
            "header `+0xEC`", "`+0x10` flags", "no `+0xEC` table",
            "867 triangles", "Research notes",
        ]),
        "c_exports_contain_generated_texture_context": all(
            token in current_c and token in untouched_c for token in [
                "GraphicsShadingRuntime_InitializeGeneratedTextureCf",
                "g_GraphicsShadingGeneratedAsset",
                "g_GraphicsShadingTextureSet",
            ]
        ),
        "screenshots_present": args.editor_shot.is_file() and args.game_shot.is_file(),
        "gzf_present": args.gzf.is_file() and args.gzf.stat().st_size > 0,
    }
    for name, passed in checks.items():
        require(passed, name)

    census_process = subprocess.run(
        [str(args.stock_census_exe.resolve()), str(args.game_root.resolve())],
        check=False, capture_output=True, text=True,
    )
    require(census_process.returncode == 0, "stock census executable returned failure")
    census = parse_census(census_process.stdout + census_process.stderr)
    require(census == EXPECTED_CENSUS, f"stock census mismatch: {census}")

    current_hash = sha256(args.current_c)
    untouched_hash = sha256(args.untouched_c)
    result = {
        "schema": "thandor-map-editor-g356-dedicated-spr-shadow-table-v1",
        "status": "PASS",
        "checks": checks,
        "stock_tree_shadow_census": census,
        "hashes": {
            "game.exe": sha256(args.game_exe),
            "current_c": current_hash,
            "untouched_c": untouched_hash,
            "supplied_gzf": sha256(args.gzf),
            "shadows_editor_current.png": sha256(args.editor_shot),
            "shadows_ingame.png": sha256(args.game_shot),
        },
        "binary_contract": {
            "shadow_table_pointer": "SPR +0xEC",
            "record_count": "table +0x04",
            "first_record": "table +0x20",
            "next_record": "record + record[+0x00]",
            "record_flags": "record +0x10",
            "soft_selector": "bit 0 set",
            "hard_selector": "bit 0 clear",
            "visible_lod_table": "not read by generated-shadow walkers",
        },
        "authority_notes": {
            "current_and_untouched_c_are_distinct": current_hash != untouched_hash,
            "c_export_limitation": (
                "The supplied current and untouched paths hash identically and do not recover the detached "
                "0x004CD880-0x004CDB71 shadow-table walkers as C functions. Binary opcodes and stock PCK "
                "records provide the source-geometry proof."
            ),
            "gzf_role": "comparison only; the supplied GZF predates the accepted semantic baseline",
            "screenshot_role": "visual mismatch input, not proof of the binary data layout",
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
