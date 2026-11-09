#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

EXPECTED_GAME_SHA256 = "53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969"
ACCEPTED_V281_GZF_SHA256 = "8db4b13d5a87d0862aa3ddb06b84a4b1e54ceee70b0884652601cee0721f68a6"
SUPPLIED_GZF_SHA256 = "feb31b7fe5fc32997898955b7dfd880a8fc24dc2a66aa25017e5147d126f9dc7"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def image_slice(data: bytes, start_va: int, end_va: int) -> bytes:
    image_base = 0x00400000
    return data[start_va - image_base:end_va - image_base]


def png_dimensions(path: Path) -> tuple[int, int]:
    data = path.read_bytes()
    require(data.startswith(b"\x89PNG\r\n\x1a\n"), f"not a PNG: {path}")
    require(len(data) >= 24 and data[12:16] == b"IHDR", f"missing PNG IHDR: {path}")
    return struct.unpack(">II", data[16:24])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--c-export", type=Path, required=True)
    parser.add_argument("--gzf", type=Path, required=True)
    parser.add_argument("--editor-screenshot", type=Path)
    parser.add_argument("--game-screenshot", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    source = args.source.resolve()
    binary = args.binary.resolve()
    c_export = args.c_export.resolve()
    gzf = args.gzf.resolve()
    require(source.is_dir(), f"missing source tree: {source}")
    require(binary.is_file(), f"missing game.exe: {binary}")
    require(c_export.is_file(), f"missing C export: {c_export}")
    require(gzf.is_file(), f"missing GZF: {gzf}")

    game_hash = sha256(binary)
    require(game_hash == EXPECTED_GAME_SHA256, f"unexpected game.exe SHA-256: {game_hash}")
    data = binary.read_bytes()
    sealed_ranges = {
        "ModelRender_PrepareProjectedVertex": (
            0x004BD4B0, 0x004BD620,
            "8e13aa737447ba1a1a15f7166cec2fa017b8f28c1c76ad72ad6c1f7ef65077a9"),
        "direct_tint_or_default_lighting_branch": (
            0x004BD5A2, 0x004BD620,
            "31ff371c94bc0e579b83fd2bb3cb40159eb6c5e5bf551294e5071554ff089916"),
        "ModelRender_ComputeVertexIntensityDefaultPath_head": (
            0x004CC710, 0x004CC820,
            "1b20bf8d3905963c481dc1a0741ee5313980103cb790bdc73e701b998014d5ea"),
    }
    range_result: dict[str, object] = {}
    for name, (start, end, expected) in sealed_ranges.items():
        payload = image_slice(data, start, end)
        actual = hashlib.sha256(payload).hexdigest()
        require(actual == expected, f"{name} binary range hash mismatch: {actual}")
        range_result[name] = {
            "start_va": f"0x{start:08X}",
            "end_va_exclusive": f"0x{end:08X}",
            "size": len(payload),
            "sha256": actual,
        }

    branch = image_slice(data, 0x004BD5A2, 0x004BD620)
    opcodes = {
        "load_model_tint_from_node_58": bytes.fromhex("8b 87 58 00 00 00"),
        "test_direct_tint_bit_0200": bytes.fromhex("f7 c1 00 02 00 00"),
        "branch_to_direct_tint_return": bytes.fromhex("75 5a"),
        "call_default_intensity_helper": bytes.fromhex("e8 15 f1 00 00"),
        "store_default_lighting_result": bytes.fromhex("89 86 2c 00 00 00"),
        "store_direct_model_tint": bytes.fromhex("89 86 2c 00 00 00"),
    }
    for name, token in opcodes.items():
        require(token in branch, f"missing binary opcode proof: {name}")
    require(branch.index(opcodes["test_direct_tint_bit_0200"]) <
            branch.index(opcodes["call_default_intensity_helper"]),
            "default lighting call no longer follows the 0x0200 branch test")

    c_text = c_export.read_text(encoding="utf-8", errors="replace")
    c_checks = {
        "named_owner": "ModelRender_PrepareProjectedVertex" in c_text,
        "loads_model_node_tint":
            "materialPackedColor = *(uint *)(modelNode + 0x58);" in c_text,
        "direct_branch_copies_tint":
            "if ((uVar3 & 0x200) != 0)" in c_text and
            "vertex[3].z = materialPackedColor;" in c_text,
        "non_bypass_calls_default_lighting":
            "ModelRender_ComputeVertexIntensityDefaultPath" in c_text and
            "materialPackedColor,(GraphicsFixedVec3 *)surfaceNormalQ12" in c_text,
    }
    require(all(c_checks.values()), "C-export correspondence failed: " +
            ", ".join(name for name, ok in c_checks.items() if not ok))

    editor_app = (source / "src/editor/editor_app.cpp").read_text(encoding="utf-8")
    primitive = (source / "src/editor/primitive_render.hpp").read_text(encoding="utf-8")
    test_source = (source / "tests/rendering_directdraw_resource_non_bypass_lighting_tests.cpp").read_text(
        encoding="utf-8")
    cmake = (source / "CMakeLists.txt").read_text(encoding="utf-8")
    source_checks = {
        "packed_colour_selector_helper":
            "directdraw_select_model_vertex_packed_color" in primitive and
            "? direct_model_tint : directionally_lit_model_tint" in primitive,
        "builder_computes_direct_model_tint":
            "const uint32_t direct_model_tint=modulation | 0xFF000000U;" in editor_app,
        "builder_computes_directional_model_tint":
            "const uint32_t directionally_lit_model_tint=" in editor_app and
            "thandor::multiply_rgb_opaque(ramp[light_index],modulation);" in editor_app,
        "builder_selects_per_serialized_flag":
            "directdraw_select_model_vertex_packed_color(" in editor_app and
            "triangle_render_flags,direct_model_tint,directionally_lit_model_tint" in editor_app,
        "removed_resource_wide_white_override":
            "resource_kind!=thandor::ResourceEnergyKind::None\n                        ?0xFFFFFFFFU" not in editor_app,
        "regression_exercises_lit_resource_flags":
            "0x201AU" in test_source and "0x2020U" in test_source and
            "non-bypass resource body triangle lost directional facet lighting" in test_source,
        "regression_exercises_direct_resource_flags":
            "0x221AU" in test_source and "0x221BU" in test_source,
        "stock_split_is_sealed":
            "body_direct == 272U && body_lit == 192U" in test_source and
            "particle_direct == 159U && particle_lit == 0U" in test_source,
        "regression_target_registered":
            "rendering_directdraw_resource_non_bypass_lighting_tests" in cmake,
    }
    require(all(source_checks.values()), "G344 source checks failed: " +
            ", ".join(name for name, ok in source_checks.items() if not ok))

    gzf_hash = sha256(gzf)
    require(gzf_hash == SUPPLIED_GZF_SHA256,
            f"supplied GZF identity changed unexpectedly: {gzf_hash}")

    screenshots: dict[str, object] = {}
    for key, path in (("editor", args.editor_screenshot), ("game", args.game_screenshot)):
        if path is None:
            continue
        resolved = path.resolve()
        require(resolved.is_file(), f"missing {key} screenshot: {resolved}")
        width, height = png_dimensions(resolved)
        screenshots[key] = {
            "path": str(resolved),
            "sha256": sha256(resolved),
            "width": width,
            "height": height,
        }

    result = {
        "schema": "thandor-vulkan-map-editor-g344-directdraw-resource-non-bypass-lighting-v1",
        "status": "PASS",
        "binary": {
            "path": str(binary),
            "sha256": game_hash,
            "sealed_ranges": range_result,
            "contract": {
                "direct_tint_flag": "0x0200",
                "direct_path": "copy modelNode+0x58 packed tint to the prepared vertex",
                "non_bypass_path": "call ModelRender_ComputeVertexIntensityDefaultPath with the same model tint",
            },
        },
        "c_export": {
            "path": str(c_export),
            "sha256": sha256(c_export),
            "checks": c_checks,
        },
        "gzf": {
            "path": str(gzf),
            "sha256": gzf_hash,
            "accepted_v281_sha256_recorded_by_project": ACCEPTED_V281_GZF_SHA256,
            "matches_accepted_v281": gzf_hash == ACCEPTED_V281_GZF_SHA256,
            "promoted": False,
            "note": "The supplied GZF is recorded as comparison evidence only; it is older than the accepted V281 semantic authority.",
        },
        "source": {
            "checks": source_checks,
            "fix": "Select direct model tint only for 0x0200 triangles; retain the directional model-light result for every non-bypass triangle.",
        },
        "stock_resource_contract": {
            "body_direct_tint_triangles": 272,
            "body_directionally_lit_triangles": 192,
            "particle_direct_tint_triangles": 159,
            "particle_directionally_lit_triangles": 0,
        },
        "screenshots": {
            "files": screenshots,
            "interpretation": "The captures are not camera/frame matched, so they are diagnostic rather than pixel-correspondence evidence. The editor's flatter white crystal facets are consistent with the source-wide white override rejected by this gate.",
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("G344_DIRECTDRAW_RESOURCE_NON_BYPASS_LIGHTING_VALIDATION PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
