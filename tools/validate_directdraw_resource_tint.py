#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

EXPECTED_GAME_SHA256 = "53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969"
ACCEPTED_V281_GZF_SHA256 = "8db4b13d5a87d0862aa3ddb06b84a4b1e54ceee70b0884652601cee0721f68a6"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def file_slice(data: bytes, va_start: int, va_end: int) -> bytes:
    return data[va_start - 0x00400000:va_end - 0x00400000]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--current-c", type=Path)
    parser.add_argument("--original-c", type=Path)
    parser.add_argument("--gzf", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    source = args.source.resolve()
    binary = args.binary.resolve()
    current_c = args.current_c.resolve() if args.current_c else None
    original_c = args.original_c.resolve() if args.original_c else None
    gzf = args.gzf.resolve() if args.gzf else None
    require(source.is_dir(), f"missing source tree: {source}")
    require(binary.is_file(), f"missing binary: {binary}")
    require((current_c is None) == (original_c is None),
            "--current-c and --original-c must be supplied together")

    game_hash = sha256(binary)
    require(game_hash == EXPECTED_GAME_SHA256, f"unexpected game.exe SHA-256: {game_hash}")
    data = binary.read_bytes()
    ranges = {
        "model_prepare_projected_vertex_primary": (
            0x004BD4B0, 0x004BD6A4,
            "cc77290b639476aeb27032d8e9b067ca7409107ce7be636d846d2eafc1bbd03e"),
        "primary_direct_tint_branch": (
            0x004BD59C, 0x004BD620,
            "b2ac3121f16d297019d8faaee7393316b5fa7840786c43f2f9818bf145e75d32"),
        "model_prepare_projected_vertex_clipped": (
            0x004BD6B0, 0x004BD784,
            "245d09e6c56f73e86577a3d216ec29ebd4afe907cea620f902a3b16eb308e34e"),
        "clipped_direct_tint_branch": (
            0x004BD71A, 0x004BD780,
            "66b29b85515d890af0594dfc7f0987754c6b59b140107dd98d43775a3777cad3"),
    }
    range_result = {}
    for name, (start, end, expected) in ranges.items():
        payload = file_slice(data, start, end)
        actual = hashlib.sha256(payload).hexdigest()
        require(actual == expected, f"{name} hash mismatch: {actual}")
        range_result[name] = {
            "start_va": f"0x{start:08X}",
            "end_va_exclusive": f"0x{end:08X}",
            "size": len(payload),
            "sha256": actual,
        }

    primary = file_slice(data, 0x004BD59C, 0x004BD620)
    for token in (
        bytes.fromhex("8b 87 58 00 00 00"),  # model tint
        bytes.fromhex("f7 c1 00 02 00 00"),  # test 0x0200
        bytes.fromhex("75 5a"),              # direct-tint branch
        bytes.fromhex("89 86 2c 00 00 00"),  # store prepared color
    ):
        require(token in primary, f"missing primary direct-tint opcode: {token.hex()}")
    clipped = file_slice(data, 0x004BD71A, 0x004BD780)
    for token in (
        bytes.fromhex("8b 87 58 00 00 00"),
        bytes.fromhex("f7 c1 00 02 00 00"),
        bytes.fromhex("75 42"),
        bytes.fromhex("0d ff ff ff 00"),
    ):
        require(token in clipped, f"missing clipped direct-tint opcode: {token.hex()}")

    model_vert = (source / "shaders/model.vert").read_text(encoding="utf-8")
    renderer_hpp = (source / "src/editor/terrain_renderer.hpp").read_text(encoding="utf-8")
    renderer_cpp = (source / "src/editor/terrain_renderer.cpp").read_text(encoding="utf-8")
    editor_app = (source / "src/editor/editor_app.cpp").read_text(encoding="utf-8")
    primitive = (source / "src/editor/primitive_render.hpp").read_text(encoding="utf-8")
    cmake = (source / "CMakeLists.txt").read_text(encoding="utf-8")

    checks = {
        "helper_masks_flag_0200": "(render_flags & 0x0200U) != 0U" in primitive,
        "object_vertex_carries_bypass": "uint32_t direct_tint_bypass" in renderer_hpp,
        "vulkan_location5_bound": all(x in renderer_cpp for x in (
            "std::array<VkVertexInputAttributeDescription, 6>",
            "offsetof(ObjectVertex, direct_tint_bypass)")),
        "editor_preserves_serialized_flag":
            "directdraw_model_vertex_uses_direct_tint(triangle_render_flags)" in editor_app,
        "cpu_selects_direct_and_directionally_lit_colors": all(x in editor_app + primitive for x in (
            "directdraw_select_model_vertex_packed_color",
            "const uint32_t direct_model_tint=modulation | 0xFF000000U",
            "const uint32_t directionally_lit_model_tint=",
            "triangle_render_flags,direct_model_tint,directionally_lit_model_tint")),
        "cpu_has_no_resource_wide_white_override":
            "resource_kind!=thandor::ResourceEnergyKind::None\n"
            "                        ?0xFFFFFFFFU" not in editor_app,
        "shader_has_bypass_input":
            "layout(location=5) in uint in_direct_tint_bypass" in model_vert,
        "shader_direct_tint_returns_base": all(x in model_vert for x in (
            "if (in_direct_tint_bypass != 0u)", "out_color = base")),
        "shader_lit_path_still_uses_resource_records": all(x in model_vert for x in (
            "direct_draw_resource_shading_words(in_position)",
            "direct_draw_finalize_vertex_lighting")),
        "regression_target_registered":
            "rendering_directdraw_resource_tint_bypass_tests" in cmake,
    }
    failed = [name for name, passed in checks.items() if not passed]
    require(not failed, "G342 source checks failed: " + ", ".join(failed))

    c_checks = {}
    current_hash = original_hash = None
    identical = None
    if current_c and original_c:
        require(current_c.is_file() and original_c.is_file(), "missing C correspondence input")
        current_text = current_c.read_text(encoding="utf-8", errors="replace")
        original_text = original_c.read_text(encoding="utf-8", errors="replace")
        c_checks = {
            "current_prepare_projected_vertex": "ModelRender_PrepareProjectedVertex" in current_text,
            "current_flag_0200_bypass": "if ((uVar3 & 0x200) != 0)" in current_text and
                                         "vertex[3].z = materialPackedColor" in current_text,
            "current_model_tint_field": "materialPackedColor = *(uint *)(modelNode + 0x58)" in current_text,
            "original_flag_0200_bypass": "if ((uVar3 & 0x200) != 0)" in original_text and
                                          "vertex[3].z = materialPackedColor" in original_text,
        }
        failed_c = [name for name, passed in c_checks.items() if not passed]
        require(not failed_c, "G342 C checks failed: " + ", ".join(failed_c))
        current_hash = sha256(current_c)
        original_hash = sha256(original_c)
        identical = current_c.read_bytes() == original_c.read_bytes()

    gzf_hash = sha256(gzf) if gzf and gzf.is_file() else None
    result = {
        "schema": "thandor-vulkan-map-editor-g342-directdraw-resource-tint-bypass-v1",
        "status": "PASS",
        "binary": {
            "path": str(binary),
            "sha256": game_hash,
            "ranges": range_result,
            "contract": {
                "mask": "renderFlags & 0x8E00",
                "direct_tint_flag": "0x0200",
                "primary_behavior": "copy model tint at +0x58 directly to vertex color and return before lighting",
                "clipped_behavior": "preserve tint alpha, force RGB white, and return before lighting",
            },
        },
        "implementation": checks,
        "stock_resource_contract": {
            "body_direct_tint_triangles": 272,
            "body_lit_triangles": 192,
            "particle_direct_tint_triangles": 159,
            "particle_lit_triangles": 0,
        },
        "c_correspondence": {
            "provided": current_c is not None,
            "checks": c_checks,
            "current_c_sha256": current_hash,
            "original_c_sha256": original_hash,
            "files_byte_identical": identical,
        },
        "gzf": {
            "provided": gzf_hash is not None,
            "supplied_sha256": gzf_hash,
            "accepted_v281_sha256_recorded_by_project": ACCEPTED_V281_GZF_SHA256,
            "matches_accepted_v281": (gzf_hash == ACCEPTED_V281_GZF_SHA256) if gzf_hash else None,
            "note": "G342 records the supplied GZF identity but does not modify or promote it.",
        },
        "rendering_contract": {
            "terrain_aura": "G341 DirectDraw per-vertex resource-light accumulation retained",
            "resource_crystal_direct_tint": "0x0200 triangles bypass dynamic aura and directional lighting",
            "resource_crystal_lit_subset": "triangles without 0x0200 retain the ordinary model-light path",
            "blend": "selector 18 ONE/ONE additive with depth writes disabled retained",
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("G342_DIRECTDRAW_RESOURCE_TINT_BYPASS_VALIDATION PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
