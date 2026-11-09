#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

EXPECTED_GAME_SHA256 = "53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969"
SUPPLIED_GZF_SHA256 = "feb31b7fe5fc32997898955b7dfd880a8fc24dc2a66aa25017e5147d126f9dc7"
ACCEPTED_V281_GZF_SHA256 = "8db4b13d5a87d0862aa3ddb06b84a4b1e54ceee70b0884652601cee0721f68a6"
EXPECTED_ASSET_HASHES = {
    "water_dat": "96308fc48189ba24b825ffdd32407352344e0c116fb170b8ec5880352ad03cba",
    "water_gfx": "4d733372dc440b2c0d34d767b27a0ba1ab717332a7f3c53b4c82d2d5297ea0c7",
    "water_pal": "0dfce649403fe5c2c092cc3f0bfd85565c5e2a36e3c90994c8ab15a8dbd47f0b",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def image_slice(data: bytes, start_va: int, end_va: int) -> bytes:
    # This immutable Thandor image is laid out with RVA == file offset for the sealed code/data
    # ranges used by the project validators.
    return data[start_va - 0x00400000:end_va - 0x00400000]


def parse_water_dat(path: Path) -> dict[str, object]:
    data = path.read_bytes()
    require(len(data) == 0xA0, f"water.dat size changed: {len(data)}")
    bit_count = struct.unpack_from("<I", data, 0)[0]
    require(bit_count == 2, f"water.dat selector bit count changed: {bit_count}")
    records = []
    for index in range(4):
        values = struct.unpack_from("<6i2I", data, 0x20 + index * 0x20)
        records.append({
            "index": index,
            "uv_q12": list(values[:6]),
            "uv_pixels": [value / 4096.0 for value in values[:6]],
            "texture_index": values[6],
            "palette_index": values[7],
        })
    expected = [
        [0, 0, 0, 0x100000, 0x0DE000, 0x080000, 0, 0],
        [0x040000, 0, 0x040000, 0x100000, 0x11E000, 0x080000, 0, 0],
        [0x080000, 0, 0x080000, 0x100000, 0x15E000, 0x080000, 0, 0],
        [0x0C0000, 0, 0x0C0000, 0x100000, 0x19E000, 0x080000, 0, 0],
    ]
    require([list(struct.unpack_from("<6i2I", data, 0x20 + i * 0x20)) for i in range(4)] == expected,
            "water.dat records no longer match the stock surface selector/UV table")
    return {"size": len(data), "selector_bit_count": bit_count, "records": records}


def parse_water_pal(path: Path) -> dict[str, object]:
    data = path.read_bytes()
    require(len(data) >= 0x208, "water.pal is too small")
    entry_count = struct.unpack_from("<I", data, 0xB0)[0]
    first = struct.unpack_from("<I", data, 0x200)[0]
    require(entry_count > 0, "water.pal has no entries")
    require(first == 0xFF001E43, f"water.pal entry zero changed: 0x{first:08X}")
    return {"entry_count": entry_count, "entry0_argb": f"0x{first:08X}"}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--c-export", type=Path, required=True)
    parser.add_argument("--gzf", type=Path, required=True)
    parser.add_argument("--water-dat", type=Path, required=True)
    parser.add_argument("--water-gfx", type=Path, required=True)
    parser.add_argument("--water-pal", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    source = args.source.resolve()
    paths = {
        "binary": args.binary.resolve(),
        "c_export": args.c_export.resolve(),
        "gzf": args.gzf.resolve(),
        "water_dat": args.water_dat.resolve(),
        "water_gfx": args.water_gfx.resolve(),
        "water_pal": args.water_pal.resolve(),
    }
    require(source.is_dir(), f"missing source tree: {source}")
    for name, path in paths.items():
        require(path.is_file(), f"missing {name}: {path}")

    binary_hash = sha256(paths["binary"])
    require(binary_hash == EXPECTED_GAME_SHA256, f"unexpected game.exe SHA-256: {binary_hash}")
    binary = paths["binary"].read_bytes()
    ranges = {
        "FieldGrid_InitializeRuntimeCellsAndBoundaryFlags":
            (0x00503C90, 0x00503DAA, "079ecd7ea825b1c8443b3acb47a83780f698ec5f43e0dcfce8394634561f010f"),
        "TerrainDirectionTable_InitializeWaveRecords":
            (0x005043A0, 0x0050441E, "dc843a9e907868179f12b1e0301feb3ae5a71932397aaaa18b8856ed6e3e0c7e"),
        "WaterVertex_ProjectDisplacedSurface":
            (0x005004A0, 0x0050069C, "ddbef376bcab61faa015afd904d3df103d95e71bd958feb4c4c6a2b4244379af"),
        "WaterPrimitive_AppendTexturedPacket":
            (0x004D0DA0, 0x004D0F1D, "56fcd42090fc4c517b551e21257dfd90b4592b42149cc8a8c852d29f7007c42b"),
        "FieldGrid_DrawTerrainAndWaterTriangles":
            (0x00500820, 0x00500D20, "fff1ad47c72dae011f068a4631cd88d4e1ab40e2363a50d6027ec32eb121ebcc"),
        "InGamePresentationTick_WaterDirectionAdvance":
            (0x00566200, 0x00566520, "aad14cc64ece7a25b4496ed3069c766a011fcedf411b8442abc9ff398ccf5b5b"),
    }
    binary_ranges: dict[str, object] = {}
    for name, (start, end, expected_hash) in ranges.items():
        payload = image_slice(binary, start, end)
        actual = sha256_bytes(payload)
        require(actual == expected_hash, f"{name} range hash mismatch: {actual}")
        binary_ranges[name] = {
            "start_va": f"0x{start:08X}",
            "end_va_exclusive": f"0x{end:08X}",
            "size": len(payload),
            "sha256": actual,
        }

    c_text = paths["c_export"].read_text(encoding="utf-8", errors="replace")
    c_tokens = [
        "FieldGrid_InitializeRuntimeCellsAndBoundaryFlags",
        "TerrainVisualResources_LoadPrimary",
        "TerrainDirectionTable_AdvanceAndRebuildVectors",
        "Direct3D_PrimitiveHandler_TexturedPreset3",
        "Copies vertex diffuse colors",
        "alpha blending enabled, Z-write enabled",
    ]
    c_checks = {token: token in c_text for token in c_tokens}
    require(all(c_checks.values()), "complete C export is missing water correspondence owners/comments")
    textured_start = c_text.find("void Direct3D_PrimitiveHandler_TexturedPreset3")
    textured_end = c_text.find("void Direct3D_PrimitiveHandler_TexturedPreset4", textured_start)
    require(textured_start >= 0 and textured_end > textured_start, "cannot isolate textured preset 3")
    textured_owner = c_text[textured_start:textured_end]
    require("modulationColor" not in textured_owner,
            "textured preset 3 unexpectedly reads packet modulationColor")

    asset_hashes = {name: sha256(paths[name]) for name in EXPECTED_ASSET_HASHES}
    for name, expected in EXPECTED_ASSET_HASHES.items():
        require(asset_hashes[name] == expected, f"{name} identity changed: {asset_hashes[name]}")
    dat_census = parse_water_dat(paths["water_dat"])
    pal_census = parse_water_pal(paths["water_pal"])

    source_text = {
        relative: (source / relative).read_text(encoding="utf-8")
        for relative in [
            "include/thandor/field.hpp", "include/thandor/terrain_texture.hpp",
            "src/core/field.cpp", "src/core/terrain_texture.cpp",
            "src/editor/editor_app.cpp", "src/editor/terrain_renderer.cpp",
            "src/editor/texture_sampling.hpp", "shaders/water.vert", "shaders/water.frag",
            "tests/rendering_directdraw_single_level_object_texture_sampling_tests.cpp",
            "tests/animation_directdraw_water_surface_animation_color_tests.cpp", "CMakeLists.txt",
        ]
    }
    joined = "\n".join(source_text.values())
    source_checks = {
        "resource_object_textures_single_level":
            "DirectDrawSingleLevelLinear" in source_text["src/editor/texture_sampling.hpp"] and
            "return {1U, false, false}" in source_text["src/editor/texture_sampling.hpp"],
        "surface_dat_parser": "TerrainSurfaceTextureLayout::parse" in joined,
        "surface_dat_gfx_pal_chain": all(token in source_text["src/editor/editor_app.cpp"] for token in [
            'surface_base+".dat"', 'surface_base+".gfx"', 'surface_base+".pal"']),
        "surface_single_level_upload":
            source_text["src/editor/terrain_renderer.cpp"].count(
                "TextureSamplingMode::DirectDrawSingleLevelLinear") >= 3,
        "binary_rng_and_wave_records": all(token in source_text["src/core/field.cpp"] for token in [
            "build_surface_record_indices", "build_water_wave_records", "water_wave_record_index",
            "0x1FU", "0x80U", "0x7FU", "0x200U"]),
        "wave_shader_xyz": all(token in source_text["shaders/water.vert"] for token in [
            "cos(angleXY) * amplitudeXY", "sin(angleXY) * amplitudeXY",
            "cos(angleZ) * amplitudeZ"]),
        "textured_path_uses_only_vertex_diffuse":
            "encodedTexture * clamp(vertexDiffuse.rgb" in source_text["shaders/water.frag"] and
            "paletteTint" not in source_text["shaders/water.frag"] and
            "encodedLighting" not in source_text["shaders/water.frag"],
        "preset3_depth_and_alpha":
            "VK_POLYGON_MODE_FILL, true, true, VK_CULL_MODE_BACK_BIT" in
                source_text["src/editor/terrain_renderer.cpp"],
        "integer_20hz_presentation_phase": all(token in joined for token in [
            "kSurfacePresentationTicksPerSecond=20.0",
            "renderer_.set_presentation_tick(current_presentation_tick_)",
            "uniform.material_scale[3] = static_cast<float>(presentation_tick_)"]) and
            "animation_epoch_" not in source_text["src/editor/terrain_renderer.cpp"],
        "active_regressions_registered": all(token in source_text["CMakeLists.txt"] for token in [
            "rendering_directdraw_single_level_object_texture_sampling_tests",
            "animation_directdraw_water_surface_animation_color_tests"]),
    }
    require(all(source_checks.values()), "G346 source correspondence failed: " +
            ", ".join(name for name, ok in source_checks.items() if not ok))

    gzf_hash = sha256(paths["gzf"])
    require(gzf_hash == SUPPLIED_GZF_SHA256, f"supplied GZF identity changed: {gzf_hash}")

    result = {
        "schema": "thandor-vulkan-map-editor-g346-directdraw-water-surface-v1",
        "status": "PASS",
        "binary": {"path": str(paths["binary"]), "sha256": binary_hash,
                   "ranges": binary_ranges},
        "complete_c_export": {"path": str(paths["c_export"]),
                              "sha256": sha256(paths["c_export"]),
                              "owner_checks": c_checks,
                              "textured_preset3_reads_modulation_color": False},
        "gzf": {"path": str(paths["gzf"]), "sha256": gzf_hash,
                "accepted_v281_sha256_recorded_by_project": ACCEPTED_V281_GZF_SHA256,
                "matches_accepted_v281": gzf_hash == ACCEPTED_V281_GZF_SHA256,
                "promoted": False,
                "note": "Supplied GZF is older comparison evidence; accepted V281 remains semantic authority."},
        "stock_surface_assets": {
            "hashes": asset_hashes,
            "water_dat": dat_census,
            "water_pal": pal_census,
            "water_gfx": {"sha256": asset_hashes["water_gfx"],
                          "proven_by_regression": "4 layers, each 128x128, opaque, blue 96..168"},
        },
        "source": {"path": str(source), "checks": source_checks},
        "correction": {
            "resource": "DirectDraw object GFX arrays now use one bilinear level with no mip interpolation or anisotropy.",
            "water": "Load the LEV primary surface DAT/GFX/PAL chain; animate the 256 binary-derived direction records; sample water.gfx; multiply only by queued vertex diffuse; use textured preset-3 alpha/depth state.",
            "rejected_models": [
                "flat hard-coded blue water color",
                "surface PAL multiplied into the textured water packet",
                "static water vertices",
                "water depth writes disabled",
            ],
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
