#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

EXPECTED = {
    "game": "53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969",
    "gzf": "41255a253d70a36d4e252ac236fdec0a42539dbe1d8edf1a0a5b1a35688064f7",
    "current_c": "236b2dd848bda14296972666983d8c9c6c024f0623c9dc33f2944d0a4a2bacbf",
    "original_c": "eeea2dc72d85060054937540719d71b2c5ecc150c7af70f1358ed754f173b2d5",
    "gzf_payload": "37ccfeee4524147ea4cb10394b55f3e501c5e42ceacbf92bbf6b62424320123e",
}
RANGES = {
    "terrain_vertex_packet": (0x004D0F20, 0x004D10A0,
        "aa9a6cc15f1bc2f49a8d11786e547a1c8f6a52e70040112fc747fd1edfbbb4a9"),
    "terrain_vertex_color_overlay": (0x005004A0, 0x0050069E,
        "432213e64762219358eea656774840ec589a3fd5591bf821890902b0afd1a280"),
    "terrain_cell_packet_builder": (0x00500820, 0x00500B00,
        "7fb7b46d90744fba70f6b2a9f016ddbf95f39ca0a60ef914af198bc9698a6d60"),
    "terrain_soil_selector_core": (0x00500A00, 0x00500AF9,
        "e7680468da6903c6edc093ccf16b4d83145f7556880daaa1494fab754ade0879"),
    "field_six_neighbour_normal": (0x005052E0, 0x0050561A,
        "8c1fa61616c43b8bf1fa977d098f31a0b9bcc77b0368da36c7349dccf2c2a4a1"),
}

def require(ok: bool, message: str) -> None:
    if not ok:
        raise SystemExit(message)

def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def image_slice(data: bytes, start: int, end: int) -> bytes:
    return data[start - 0x00400000:end - 0x00400000]

def dword(data: bytes, va: int) -> int:
    return struct.unpack_from("<I", data, va - 0x00400000)[0]

def owner(text: str, name: str, next_marker: str = "\n\n\n") -> str:
    pos = text.find(name)
    require(pos >= 0, f"missing C owner token: {name}")
    begin = text.rfind("\n", 0, pos)
    end = text.find(next_marker, pos)
    if end < 0: end = min(len(text), pos + 16000)
    return text[begin + 1:end]

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", type=Path, required=True)
    ap.add_argument("--binary", type=Path, required=True)
    ap.add_argument("--current-c", type=Path, required=True)
    ap.add_argument("--original-c", type=Path, required=True)
    ap.add_argument("--gzf", type=Path, required=True)
    ap.add_argument("--gzf-payload", type=Path, required=True)
    ap.add_argument("--stock-ground-census", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    a = ap.parse_args()
    paths = {
        "source": a.source.resolve(), "game": a.binary.resolve(),
        "current_c": a.current_c.resolve(), "original_c": a.original_c.resolve(),
        "gzf": a.gzf.resolve(), "gzf_payload": a.gzf_payload.resolve(),
        "census": a.stock_ground_census.resolve(),
    }
    require(paths["source"].is_dir(), "source tree missing")
    for key in ("game", "current_c", "original_c", "gzf", "gzf_payload", "census"):
        require(paths[key].is_file(), f"missing {key}: {paths[key]}")
    for key, expected in EXPECTED.items():
        actual = sha(paths[key])
        require(actual == expected, f"{key} identity drift: {actual}")

    game = paths["game"].read_bytes()
    range_checks = {}
    for name, (start, end, expected) in RANGES.items():
        payload = image_slice(game, start, end)
        actual = hashlib.sha256(payload).hexdigest()
        require(actual == expected, f"binary range drift {name}: {actual}")
        range_checks[name] = {"start": f"0x{start:08X}", "end_exclusive": f"0x{end:08X}",
                              "bytes": len(payload), "sha256": actual}
    state = {"filter": dword(game, 0x00577E10),
             "perspective": dword(game, 0x00577E14),
             "antialias": dword(game, 0x00577E18)}
    require(state == {"filter": 2, "perspective": 1, "antialias": 0},
            f"Direct3D texture state drift: {state}")

    current = paths["current_c"].read_text(errors="replace")
    original = paths["original_c"].read_text(errors="replace")
    c_checks = {
        "current_vertex_diffuse_per_vertex": all(x in current for x in
            ["vertex0DiffuseColor", "vertex1DiffuseColor", "vertex2DiffuseColor",
             "GraphicsPrimitiveQueue_AppendTexturedTriangleRegs"]),
        "original_vertex_diffuse_per_vertex": all(x in original for x in
            ["vertex0DiffuseColor", "vertex1DiffuseColor", "vertex2DiffuseColor",
             "GraphicsPrimitiveQueue_AppendTexturedTriangleRegs"]),
        "current_glide_no_mips_wrap_bilinear": all(x in current for x in
            ["(*_g_GrTexMipMapMode)(iVar9,0,0);", "(*_g_GrTexClampMode)(iVar9,0,0);",
             "(*_g_GrTexFilterMode)(iVar9,1,1);"]),
        "original_glide_no_mips_wrap_bilinear": all(x in original for x in
            ["(*_g_GrTexMipMapMode)(iVar9,0,0);", "(*_g_GrTexClampMode)(iVar9,0,0);",
             "(*_g_GrTexFilterMode)(iVar9,1,1);"]),
        "current_d3d_linear_perspective": all(x in current for x in
            ["D3DRENDERSTATE_TEXTUREMAG", "D3DRENDERSTATE_TEXTUREMIN",
             "g_Direct3DTextureFilterMode", "D3DRENDERSTATE_TEXTUREPERSPECTIVE",
             "g_Direct3DTexturePerspectiveEnabled"]),
        "normal_owner_current": "FieldGridCell_RecomputeTriangleNormalAngles" in current and
            current.count("cell->terrainHeight") > 1 and "0xc00000" in current,
        "normal_owner_original": "FieldGridCell_RecomputeTriangleNormalAngles" in original and
            original.count("cell->terrainHeight") > 1 and "0xc00000" in original,
    }
    require(all(c_checks.values()), "C correspondence failure: " +
            ", ".join(k for k,v in c_checks.items() if not v))

    gzf_text = paths["gzf_payload"].read_bytes().decode("latin1", errors="ignore")
    gzf_checks = {
        "normal_owner": "FUN_005052e0 to FieldGridCell_RecomputeTriangleNormalAngles" in gzf_text,
        "packet_owner": "FUN_004d0f20 to GraphicsPrimitiveQueue_AppendTexturedTriangleRegs" in gzf_text,
        "glide_owner": "GraphicsGlide3_ApplyDisplayModeAndInitializeResourcesCf" in gzf_text,
        "d3d_filter_global": "g_Direct3DTextureFilterMode" in gzf_text,
    }
    require(all(gzf_checks.values()), "GZF semantic owner failure: " +
            ", ".join(k for k,v in gzf_checks.items() if not v))

    lines = [line for line in paths["census"].read_text().splitlines() if line.strip()]
    require(len(lines) == 26, f"stock ground census count changed: {len(lines)}")
    stock_checks = {
        "all_256_square": all("logical=256x256 pixel=256x256" in x for x in lines),
        "all_zero_origin": all("origin=0,0" in x for x in lines),
        "all_direct_color": all("palbank=-1" in x for x in lines),
        "all_single_subresource": all("subs=1" in x for x in lines),
        "uv_extends_for_repeat": all("uv=-64..287,0..255" in x for x in lines),
    }
    require(all(stock_checks.values()), "stock GFX census failure: " +
            ", ".join(k for k,v in stock_checks.items() if not v))

    source = paths["source"]
    field = (source/"src/core/field.cpp").read_text()
    renderer = (source/"src/editor/terrain_renderer.cpp").read_text()
    editor = (source/"src/editor/editor_app.cpp").read_text()
    field_hpp = (source/"include/thandor/field.hpp").read_text()
    terrain_vertex_shader = (source/"shaders/terrain.vert").read_text()
    surface_vertex_shader = (source/"shaders/surface.vert").read_text()
    terrain_texture = (source/"src/core/terrain_texture.cpp").read_text()
    test = (source/"tests/visibility_retail_terrain_fog_surface_tests.cpp").read_text()
    source_checks = {
        "six_axial_neighbours": "{1, 0}, {0, 1}, {0, -1}, {-1, 0}, {-1, 1}, {1, -1}" in field,
        "fixed_normal_z": "0x00C00000" in field,
        "soil_uv_record_direct": "record->uv_q12[vertex * 2U]" in field and
                                 "kUploadedTerrainTextureExtent" in field,
        "soil_layout_20byte_records": "constexpr std::size_t kRecordSize = 0x20" in terrain_texture,
        "terrain_single_level_linear": "TextureSamplingMode::DirectDrawSingleLevelLinear" in renderer,
        "terrain_not_mipmapped_in_update_materials": "void TerrainRenderer::update_materials" in renderer and
            "MipmappedAnisotropic" not in renderer[renderer.find("void TerrainRenderer::update_materials"):
                renderer.find("void TerrainRenderer::update_surface_materials")],
        "fog_per_vertex_cell_state": "uint32_t fog_cell_index{}" in field_hpp and
            "static_cast<uint32_t>(cell_index)" in field and
            "runtimeVisibility = fogVisibility.cells[inFogCellIndex] & 255u" in terrain_vertex_shader,
        "fog_animated_surface_same_vertex_path":
            "vertexDiffuse.rgb *= float(runtimeVisibility) / 255.0" in surface_vertex_shader and
            "vec3 worldPosition = inPosition + displacement" in surface_vertex_shader,
        "fog_coplanar_geometry_absent": "append_fog_vertex" not in editor and
                                         "FogOverlayTerrainDepthTest" not in editor,
        "adversarial_regression": "six-neighbour diagonal height did not affect terrain normal" in test and
                                   "superseded coplanar fog geometry returned" in test,
    }
    require(all(source_checks.values()), "source correspondence failure: " +
            ", ".join(k for k,v in source_checks.items() if not v))

    result = {
        "schema": "thandor-g408-retail-terrain-fog-surface-validation-v1",
        "status": "PASS",
        "identities": {k: {"path": str(paths[k]), "sha256": sha(paths[k])}
                       for k in EXPECTED},
        "binary_ranges": range_checks,
        "direct3d_state": state,
        "c_export_checks": c_checks,
        "gzf_checks": gzf_checks,
        "stock_ground_gfx": {"count": len(lines), "checks": stock_checks},
        "source_checks": source_checks,
        "claims": {
            "fog": "Visibility state remains discrete per FLD vertex; G411 carries the temporally clamped cell byte on the terrain/surface vertex path instead of a second coplanar mesh, preserving triangle interpolation without depth intersection.",
            "ground_uv": "soil.dat supplies the three packet UV pairs directly; stock soil GFX origin is zero and UVs intentionally wrap beyond the image edge.",
            "ground_filter": "Retail Glide disables mipmapping, wraps, and uses bilinear min/mag; D3D state value 2 is used for min/mag and perspective correction is enabled.",
            "ground_normal": "The terrain normal builder accumulates all six axial FLD neighbours with fixed Z 0x00C00000.",
            "remaining_uncertainty": "Exact retail fog palette/color composition is not established by this closure.",
        },
    }
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    print("G408_RETAIL_TERRAIN_FOG_SURFACE_VALIDATION PASS")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
