#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

EXPECTED_GAME_SHA256 = "53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969"
ACCEPTED_V281_GZF_SHA256 = "8db4b13d5a87d0862aa3ddb06b84a4b1e54ceee70b0884652601cee0721f68a6"
IMAGE_BASE = 0x00400000


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def image_slice(data: bytes, start: int, end: int) -> bytes:
    return data[start - IMAGE_BASE:end - IMAGE_BASE]


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
        "directdraw_device_initialization": (
            0x00578920, 0x005794DE,
            "5eaf2b56fe2f8762e1bacf91b44067cf5d4ab380b4fd6e2dba698988b5437b89"),
        "directdraw_render_state_presets": (
            0x00577E50, 0x00577EA0,
            "4d79a15e2a84eacb46f3215578e210433c0a5bcdfd85d54987e32ad2fc99f622"),
        "directdraw_primitive_selector_table": (
            0x00577EB8, 0x00577FB8,
            "70d1d00ba829da0d76e242a0904f1ec0b4bc1e0a2e41291f03e59cf81dd9e978"),
        "directdraw_textured_preset4_handler": (
            0x0057E3B0, 0x0057E680,
            "184481e9d49441c589fa962f4728bbaf44df93255452b30c8fed882667036113"),
    }
    binary_ranges = {}
    for name, (start, end, expected) in ranges.items():
        payload = image_slice(data, start, end)
        actual = hashlib.sha256(payload).hexdigest()
        require(actual == expected, f"{name} hash mismatch: {actual}")
        binary_ranges[name] = {
            "start_va": f"0x{start:08X}",
            "end_va_exclusive": f"0x{end:08X}",
            "size": len(payload),
            "sha256": actual,
        }

    # SetRenderState(TEXTUREMAPBLEND, 4) in the Direct3D 2 initialization body.
    initialization = image_slice(data, 0x00578920, 0x005794DE)
    require(bytes.fromhex("6a 04 6a 15") in initialization,
            "DirectDraw initialization no longer sets TEXTUREMAPBLEND=4")

    # Preset 4 is four dwords: source ONE=2, destination ONE=2, alpha enable 1, Z-write 0.
    preset4 = image_slice(data, 0x00577E50 + 4 * 16, 0x00577E50 + 5 * 16)
    require(preset4 == bytes.fromhex(
        "02 00 00 00 02 00 00 00 01 00 00 00 00 00 00 00"),
        f"unexpected DirectDraw preset-4 bytes: {preset4.hex()}")

    handler = image_slice(data, 0x0057E3B0, 0x0057E680)
    for token in (
        bytes.fromhex("6a 0e"),  # ZWRITEENABLE
        bytes.fromhex("6a 13"),  # SRCBLEND
        bytes.fromhex("6a 14"),  # DESTBLEND
        bytes.fromhex("8b 47 1c 8b 5f 3c 8b 4f 5c"),  # three diffuse colors
    ):
        require(token in handler, f"missing preset-4 opcode evidence: {token.hex()}")

    context = (source / "src/editor/vulkan_context.cpp").read_text(encoding="utf-8")
    terrain = (source / "shaders/terrain.frag").read_text(encoding="utf-8")
    model = (source / "shaders/model.frag").read_text(encoding="utf-8")
    primitive = (source / "src/editor/primitive_render.hpp").read_text(encoding="utf-8")
    cmake = (source / "CMakeLists.txt").read_text(encoding="utf-8")
    test = (source / "tests/rendering_directdraw_encoded_framebuffer_blend_tests.cpp").read_text(encoding="utf-8")

    unorm_position = context.find("VK_FORMAT_B8G8R8A8_UNORM")
    srgb_position = context.find("VK_FORMAT_B8G8R8A8_SRGB")
    checks = {
        "unorm_nonlinear_surface_preferred":
            unorm_position >= 0 and srgb_position >= 0 and unorm_position < srgb_position and
            "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR" in context,
        "terrain_emits_encoded_product":
            "return linearToSrgb(sampledLinear) *" in terrain and
            "return srgbToLinear(linearToSrgb(sampledLinear)" not in terrain,
        "model_emits_encoded_product":
            "return linear_to_srgb(sampled_linear) *" in model and
            "return srgb_to_linear(linear_to_srgb(sampled_linear)" not in model,
        "encoded_saturating_add_reference":
            "directdraw_add_encoded_byte" in primitive and "sum > 255U ? 255U : sum" in primitive,
        "g343_regression_registered":
            "rendering_directdraw_encoded_framebuffer_blend_tests" in cmake,
        "representative_stock_pixel_gate": all(token in test for token in (
            "dominant_nonblack", "power_direct == 255U",
            "building_direct > building_linear + 25U")),
    }
    failed = [name for name, passed in checks.items() if not passed]
    require(not failed, "G343 source checks failed: " + ", ".join(failed))

    c_checks = {}
    current_hash = original_hash = None
    identical = None
    if current_c and original_c:
        require(current_c.is_file() and original_c.is_file(), "missing C correspondence input")
        current_text = current_c.read_text(encoding="utf-8", errors="replace")
        original_text = original_c.read_text(encoding="utf-8", errors="replace")
        required_tokens = (
            "GraphicsDirectDraw_ApplyDisplayModeAndCreateResourcesCf",
            "D3DRENDERSTATE_TEXTUREMAPBLEND,4",
            "Direct3D_PrimitiveHandler_TexturedPreset4",
            "ONE/ONE additive",
            "g_ImmediateTLVertices[0].color = packet->vertices[0].diffuseColor",
            "D3DRENDERSTATE_ZWRITEENABLE",
            "D3DRENDERSTATE_SRCBLEND",
            "D3DRENDERSTATE_DESTBLEND",
        )
        c_checks = {
            "current_directdraw_contract": all(token in current_text for token in required_tokens),
            "untouched_directdraw_contract": all(token in original_text for token in required_tokens),
        }
        failed_c = [name for name, passed in c_checks.items() if not passed]
        require(not failed_c, "G343 C checks failed: " + ", ".join(failed_c))
        current_hash = sha256(current_c)
        original_hash = sha256(original_c)
        identical = current_c.read_bytes() == original_c.read_bytes()

    gzf_hash = sha256(gzf) if gzf and gzf.is_file() else None
    result = {
        "schema": "thandor-vulkan-map-editor-g343-directdraw-encoded-framebuffer-blend-v1",
        "status": "PASS",
        "binary": {
            "path": str(binary),
            "sha256": game_hash,
            "ranges": binary_ranges,
            "contract": {
                "api": "DirectDraw plus Direct3D 2 fixed-function immediate triangles",
                "texture_map_blend": 4,
                "resource_selector": 18,
                "resource_preset": 4,
                "resource_blend": "ONE/ONE",
                "z_write": False,
                "diffuse_color_source": "packet vertex packed diffuseColor",
                "srgb_render_target_state": "not present in Direct3D 2; packed framebuffer bytes are blended directly",
            },
        },
        "implementation": checks,
        "representative_stock_pixels": {
            "power_particle_dominant_nonblack_rgb": [35, 62, 107],
            "building_particle_dominant_nonblack_rgb": [39, 21, 0],
            "destination_reference_byte": 200,
            "power_directdraw_encoded_result": 255,
            "power_modern_linear_result": 221,
            "purpose": "representative source colors expose the intensity lost by linear-light ONE/ONE blending",
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
            "note": "G343 records the supplied GZF identity but does not modify or promote it.",
        },
        "rendering_contract": {
            "swapchain": "prefer B8G8R8A8_UNORM in SRGB_NONLINEAR presentation space",
            "texture_modulation": "sample SRGB texture to linear, reconstruct encoded bytes, multiply packed lighting in encoded domain",
            "additive_resources": "Vulkan ONE/ONE blend on UNORM target reproduces DirectDraw packed-byte addition",
            "g342_tint_bypass": "retained",
            "g341_vertex_aura": "retained",
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("G343_DIRECTDRAW_ENCODED_FRAMEBUFFER_BLEND_VALIDATION PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
