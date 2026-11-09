#!/usr/bin/env python3
"""Validate G322's independent resource body, luminous-particle, terrain-overlay, and model-lighting paths."""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import subprocess
import tempfile
from pathlib import Path

EXPECTED_GAME_SHA256 = "53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969"
IMAGE_BASE = 0x00400000


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"G322 validation failed: {message}")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def pe_sections(data: bytes) -> list[tuple[int, int, int, int]]:
    require(data[:2] == b"MZ", "game.exe is not an MZ image")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    require(data[pe_offset:pe_offset + 4] == b"PE\0\0", "game.exe has no PE signature")
    section_count = struct.unpack_from("<H", data, pe_offset + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe_offset + 20)[0]
    table = pe_offset + 24 + optional_size
    result: list[tuple[int, int, int, int]] = []
    for index in range(section_count):
        offset = table + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", data, offset + 8)
        result.append((virtual_address, max(virtual_size, raw_size), raw_offset, raw_size))
    return result


def va_slice(data: bytes, sections: list[tuple[int, int, int, int]], va: int, size: int) -> bytes:
    rva = va - IMAGE_BASE
    for section_va, section_size, raw_offset, raw_size in sections:
        if section_va <= rva and rva + size <= section_va + section_size:
            delta = rva - section_va
            require(delta + size <= raw_size, f"VA 0x{va:08X} extends beyond raw section data")
            return data[raw_offset + delta:raw_offset + delta + size]
    raise SystemExit(f"G322 validation failed: VA 0x{va:08X} is not image-backed")


def exact_bytes(data: bytes, sections: list[tuple[int, int, int, int]], va: int, expected_hex: str) -> None:
    expected = bytes.fromhex(expected_hex)
    actual = va_slice(data, sections, va, len(expected))
    require(actual == expected,
            f"binary bytes changed at 0x{va:08X}: expected {expected.hex()} got {actual.hex()}")


def compile_and_run_primitive_test(source: Path) -> str:
    compiler = None
    for candidate in ("c++", "g++", "clang++"):
        completed = subprocess.run(["bash", "-lc", f"command -v {candidate}"],
                                   text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        if completed.returncode == 0:
            compiler = completed.stdout.strip()
            break
    require(bool(compiler), "no C++20 compiler available")
    with tempfile.TemporaryDirectory(prefix="thandor-g322-") as temporary:
        output = Path(temporary) / "primitive_test"
        command = [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                   "-I", str(source / "src/editor"),
                   str(source / "tests/rendering_primitive_render_tests.cpp"), "-o", str(output)]
        subprocess.run(command, check=True)
        completed = subprocess.run([str(output)], check=True, text=True,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        return completed.stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--game-exe", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    source = args.source.resolve()
    primitive = (source / "src/editor/primitive_render.hpp").read_text(encoding="utf-8")
    editor = (source / "src/editor/editor_app.cpp").read_text(encoding="utf-8")
    renderer = (source / "src/editor/terrain_renderer.cpp").read_text(encoding="utf-8")
    terrain_vert = (source / "shaders/terrain.vert").read_text(encoding="utf-8")
    terrain_frag = (source / "shaders/terrain.frag").read_text(encoding="utf-8")
    model_frag = (source / "shaders/model.frag").read_text(encoding="utf-8")
    primitive_test = (source / "tests/rendering_primitive_render_tests.cpp").read_text(encoding="utf-8")
    stock_test = (source / "tests/rendering_resource_glow_water_tests.cpp").read_text(encoding="utf-8")
    cmake = (source / "CMakeLists.txt").read_text(encoding="utf-8")

    checks = {
        "dual_backend_binary_decoders_retained": all(token in primitive for token in (
            "enum class LegacyGraphicsBackend", "classify_direct3d_sprite_primitive",
            "classify_glide3_sprite_primitive", "render_flags & 0x7000U",
            "render_flags & 0x20000U")),
        "resource_layers_are_not_one_generic_backend": all(token in primitive + editor for token in (
            "enum class ResourcePrimitiveRole", "DenseBody", "LuminousParticle",
            "classify_resource_preview_primitive", "resource_body_triangle",
            "resource_particle_triangle")) and "primitive_backend" not in editor,
        "dense_body_preserves_alpha_and_depth": all(token in primitive + primitive_test for token in (
            "result.blend = PrimitiveBlendPreset::AlphaDepthWrite",
            "DenseBody", "body=alpha-depth-write")),
        "black_backed_particles_are_additive_without_depth_write": all(token in primitive + renderer + primitive_test for token in (
            "LuminousParticle", "classify_glide3_sprite_primitive(render_flags)",
            "PrimitiveBlendPreset::AdditiveNoDepthWrite",
            "VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE",
            "luminous-particle=one-one-additive-no-zwrite")),
        "resource_source_rgb_is_not_palette_multiplied_twice": all(token in editor for token in (
            "const bool use_palette_modulation=!neutral_source_color",
            "already contain their recovered blue/gold source RGB",
            "ARM0 palette entries 26/27/32/33")),
        "shared_shading_buffer_feeds_two_distinct_compositions": all(token in renderer + terrain_frag + model_frag for token in (
            "VK_DESCRIPTOR_TYPE_STORAGE_BUFFER", "dstBinding = 3",
            "ResourceShadingBuffer", "generatedResourceShadingOverlay",
            "resource_shading_addition")),
        "terrain_generated_alpha_overlay_is_after_base_material": all(token in terrain_vert + terrain_frag for token in (
            "layout(location=5) out vec3 worldPosition", "layout(location=5) in vec3 worldPosition",
            "vec3 baseTerrain=gameByteModulate(tex.rgb,light.rgb)",
            "generatedResourceShadingOverlay(baseTerrain,worldPosition)",
            "result = mix(result, tint, alpha)", "maskAlpha * colorStrength")) and
            "light.rgb+resourceShadingAddition" not in terrain_frag,
        "model_lighting_remains_saturating_addition": all(token in model_frag for token in (
            "resource_shading_addition", "in_color.rgb + resource_shading_addition",
            "return clamp(addition", "clamp(in_color.rgb")),
        "pulse_clock_and_overlapping_release_are_live": all(token in renderer + terrain_frag + model_frag for token in (
            "elapsed * 20.0F", "mod(scene.materialScale.w", "age + period",
            "mod(scene.material_scale.w")),
        "no_active_speculative_glow_geometry": all(token not in renderer + editor for token in (
            "resource_glow_pipeline_", "resource_glow_vertex_buffer_", "append_resource_glow")),
        "stock_texture_alpha_and_black_canvas_census": all(token in stock_test for token in (
            "power_body.alpha_full == 4096U", "building_body.alpha_zero > 8000U",
            "building_body.alpha_intermediate > 1900U",
            "power_particle.black > 2500U", "building_particle.black > 2500U")),
        "stock_layer_routing_regression": all(token in primitive_test + stock_test for token in (
            "G322_RESOURCE_LAYER_COMPOSITION PASS",
            "G322_STOCK_RESOURCE_LAYER_ROUTING PASS",
            "preview_body_alpha_depth", "preview_particle_additive",
            "Direct3D selector-18 divergence")),
        "bright_terrain_overlay_regression": all(token in stock_test for token in (
            "graphics_shading_generated_terrain_overlay",
            "full pulse is invisible on bright terrain",
            "generated overlay escaped its recovered radius")),
        "fresh_source_path_test_contract": "THANDOR_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"" in cmake,
        "task_gate_present": (source / "docs/G322_RESOURCE_BODY_PARTICLE_GENERATED_TERRAIN_OVERLAY_AND_MODEL_LIGHTING_GATE.md").is_file(),
    }
    failed = [name for name, passed in checks.items() if not passed]
    require(not failed, "source checks failed: " + ", ".join(failed))

    primitive_output = compile_and_run_primitive_test(source)
    require("G322_RESOURCE_LAYER_COMPOSITION PASS" in primitive_output,
            "primitive regression did not emit its G322 PASS marker")

    binary = None
    if args.game_exe:
        game_exe = args.game_exe.resolve()
        require(game_exe.is_file(), f"missing game executable: {game_exe}")
        game_hash = sha256(game_exe)
        require(game_hash == EXPECTED_GAME_SHA256, f"unexpected game.exe SHA-256 {game_hash}")
        data = game_exe.read_bytes()
        sections = pe_sections(data)
        for va, expected in (
            (0x0057FC13, "f7 c7 00 00 02 00"),
            (0x0057FC1B, "bf 00 10 00 00"),
            (0x0057FC22, "81 e7 00 70 00 00"),
            (0x0057FC3F, "6a 00 6a 04 6a 04 6a 04 ff 15 e0 3f 57 00"),
            (0x0057FC66, "6a 00 6a 04 6a 00 6a 04 ff 15 e0 3f 57 00"),
            (0x0057FC89, "6a 00 6a 04 6a 05 6a 01 ff 15 e0 3f 57 00"),
            (0x0057FCBD, "6a 00 ff 15 70 40 57 00"),
            (0x0057FCDA, "6a 01 ff 15 70 40 57 00"),
        ):
            exact_bytes(data, sections, va, expected)
        # Generated terrain shading texture: one white-RGB palette whose high byte ramps 0..255.
        exact_bytes(data, sections, 0x004CD097,
                    "b8 ff ff ff 00 8d 9f 00 02 00 00 ba 00 01 00 00")
        exact_bytes(data, sections, 0x004CD0B0,
                    "89 03 05 00 00 00 01 83 c3 08 4a 75 f3")
        # Terrain packet binds the current generated shading subresource from g_GeneratedTextureSet.
        exact_bytes(data, sections, 0x004D03BB,
                    "8b 0d 14 ce 4c 00 a1 38 ce 4c 00 c1 e1 05 8d 84 08 08 00 00 00 89 86 64 00 00 00")
        dispatch = va_slice(data, sections, 0x00577EA0, 64 * 4)
        pointers = struct.unpack("<64I", dispatch)
        require(pointers[18] == 0x0057CCB0, "Direct3D selector 18 no longer points to 0x0057CCB0")
        require(pointers[24] == 0x0057E3B0, "Direct3D selector 24 no longer points to 0x0057E3B0")
        binary = {
            "game_sha256": game_hash,
            "glide_blend_decode_anchor": "0x0057FC13-0x0057FCE9",
            "glide_field_2_blend": "ONE/ONE color, ONE/ZERO alpha, depth writes disabled",
            "direct3d_dispatch_table": "0x00577EA0",
            "direct3d_selector_18_handler": "0x0057CCB0",
            "direct3d_selector_24_handler": "0x0057E3B0",
            "generated_alpha_palette_initial_value": "0x00FFFFFF",
            "generated_alpha_palette_increment": "0x01000000",
            "generated_texture_bind_anchor": "0x004D03BB",
        }

    result = {
        "schema": "thandor-vulkan-map-editor-g322-resource-path-validation-v1",
        "checks": checks,
        "primitive_test_output": primitive_output,
        "binary": binary,
        "status": "PASS",
    }
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("G322_RESOURCE_PATH_VALIDATION PASS"
          f" source_checks={len(checks)}/{len(checks)}"
          f" binary={'PASS' if binary else 'NOT_REQUESTED'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
