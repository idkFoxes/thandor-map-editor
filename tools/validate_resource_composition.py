#!/usr/bin/env python3
"""Validate G321's backend split and shared terrain/model resource-shading composition."""
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
        raise SystemExit(f"G321 validation failed: {message}")


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
    raise SystemExit(f"G321 validation failed: VA 0x{va:08X} is not image-backed")


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
    with tempfile.TemporaryDirectory(prefix="thandor-g321-") as temporary:
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
        "explicit_dual_backend_decoder": all(token in primitive for token in (
            "enum class LegacyGraphicsBackend", "classify_direct3d_sprite_primitive",
            "classify_glide3_sprite_primitive", "render_flags & 0x7000U",
            "render_flags & 0x20000U")),
        "resource_only_glide_routing": all(token in editor for token in (
            "resource_kind!=thandor::ResourceEnergyKind::None",
            "LegacyGraphicsBackend::Glide3", "LegacyGraphicsBackend::Direct3D",
            "primitive_backend", "backend globally would alter thousands of non-resource triangles")),
        "exact_glide_additive_vulkan_factors": all(token in renderer for token in (
            "grAlphaBlendFunction(ONE, ONE, ONE, ZERO)",
            "VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE",
            "VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ZERO")),
        "shared_shading_buffer": all(token in renderer + terrain_frag + model_frag for token in (
            "VK_DESCRIPTOR_TYPE_STORAGE_BUFFER", "dstBinding = 3",
            "ResourceGlowBuffer", "resourceShadingAddition", "resource_shading_addition")),
        "terrain_world_position_and_pretexture_composition": all(token in terrain_vert + terrain_frag for token in (
            "layout(location=5) out vec3 worldPosition", "layout(location=5) in vec3 worldPosition",
            "light.rgb+resourceShadingAddition(worldPosition)", "tex.rgb*lighting")),
        "no_active_speculative_glow_geometry": all(token not in renderer + editor for token in (
            "resource_glow_pipeline_", "resource_glow_vertex_buffer_", "append_resource_glow")),
        "stock_flags_and_backend_divergence_regression": all(token in primitive_test + stock_test for token in (
            "0x221AU", "0x201AU", "0x221BU", "0x2020U", "0x2221U",
            "G321_DUAL_BACKEND_PRIMITIVE_ROUTING PASS",
            "G321_STOCK_RESOURCE_DUAL_BACKEND_ROUTING PASS",
            "Direct3D selector-18 divergence")),
        "black_key_additive_regression": all(token in stock_test for token in (
            "power_black_key_pixels", "building_black_key_pixels",
            "additive_channel(91U, 0U) == 91U")),
        "fresh_source_path_test_contract": "THANDOR_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"" in cmake,
        "task_gate_present": (source / "docs/G321_DUAL_BACKEND_RESOURCE_BLEND_AND_GENERATED_SHADING_TERRAIN_COMPOSITION_GATE.md").is_file(),
    }
    failed = [name for name, passed in checks.items() if not passed]
    require(not failed, "source checks failed: " + ", ".join(failed))

    primitive_output = compile_and_run_primitive_test(source)
    require("G321_DUAL_BACKEND_PRIMITIVE_ROUTING PASS" in primitive_output,
            "primitive regression did not emit its G321 PASS marker")

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
        }

    result = {
        "schema": "thandor-vulkan-map-editor-g321-resource-composition-validation-v1",
        "checks": checks,
        "primitive_test_output": primitive_output,
        "binary": binary,
        "status": "PASS",
    }
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("G321_RESOURCE_COMPOSITION_VALIDATION PASS"
          f" source_checks={len(checks)}/{len(checks)}"
          f" binary={'PASS' if binary else 'NOT_REQUESTED'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
