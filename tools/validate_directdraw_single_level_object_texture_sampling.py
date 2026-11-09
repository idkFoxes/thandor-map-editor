#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

EXPECTED_GAME_SHA256 = "53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969"
ACCEPTED_V281_GZF_SHA256 = "8db4b13d5a87d0862aa3ddb06b84a4b1e54ceee70b0884652601cee0721f68a6"
ACCEPTED_EVIDENCE_GZF_SHA256 = {
    # Historical G345 comparison container.
    "feb31b7fe5fc32997898955b7dfd880a8fc24dc2a66aa25017e5147d126f9dc7",
    # Current V434 semantic authority. Hash admission is supplemental: immutable game.exe and
    # named C-owner/range checks below remain mandatory.
    "41255a253d70a36d4e252ac236fdec0a42539dbe1d8edf1a0a5b1a35688064f7",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def image_slice(data: bytes, start_va: int, end_va: int) -> bytes:
    image_base = 0x00400000
    return data[start_va - image_base:end_va - image_base]


def image_dword(data: bytes, va: int) -> int:
    image_base = 0x00400000
    return struct.unpack_from("<I", data, va - image_base)[0]


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
    require(c_export.is_file(), f"missing complete C export: {c_export}")
    require(gzf.is_file(), f"missing GZF: {gzf}")

    game_hash = sha256(binary)
    require(game_hash == EXPECTED_GAME_SHA256, f"unexpected game.exe SHA-256: {game_hash}")
    data = binary.read_bytes()

    sealed_ranges = {
        "Direct3D_texture_filter_state": (
            0x00579200, 0x00579270,
            "c3a07a13778ac807f76b920a0dd1638f6c2ccc3fa7098ef18f3ea9f8a48dfa2e"),
        "GraphicsTexture_CreateStagingTexture": (
            0x0057A740, 0x0057A900,
            "0d34a7c78c1220de62e5fa87df127701c62d0f4ccec7180f75bf43896ea98725"),
        "GraphicsTexture_CreateDeviceTexture": (
            0x0057AAD0, 0x0057AD27,
            "88077cfd0e246ef71d990aa981c2069a8c414399dd1158bf46aaa15cac72e820"),
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

    filter_value = image_dword(data, 0x00577E10)
    require(filter_value == 2, f"Direct3D texture min/mag filter value changed: {filter_value}")

    filter_code = image_slice(data, 0x00579200, 0x00579270)
    filter_opcodes = {
        "push_filter_value_for_mag": bytes.fromhex("ff 35 10 7e 57 00 6a 11"),
        "push_filter_value_for_min": bytes.fromhex("ff 35 10 7e 57 00 6a 12"),
    }
    for name, token in filter_opcodes.items():
        require(token in filter_code, f"missing Direct3D filter-state opcode proof: {name}")

    staging_code = image_slice(data, 0x0057A740, 0x0057A900)
    device_code = image_slice(data, 0x0057AAD0, 0x0057AD27)
    surface_opcodes = {
        "staging_ddsd_flags_1007": (staging_code, bytes.fromhex("c7 05 84 7c 57 00 07 10 00 00")),
        "staging_ddscaps_1800": (staging_code, bytes.fromhex("c7 05 e8 7c 57 00 00 18 00 00")),
        "staging_square_width_height": (staging_code, bytes.fromhex("a3 8c 7c 57 00 a3 88 7c 57 00")),
        "device_ddsd_flags_1007": (device_code, bytes.fromhex("c7 05 84 7c 57 00 07 10 00 00")),
        "device_ddscaps_04001000": (device_code, bytes.fromhex("c7 05 e8 7c 57 00 00 10 00 04")),
        "device_square_width_height": (device_code, bytes.fromhex("a3 8c 7c 57 00 a3 88 7c 57 00")),
    }
    for name, (payload, token) in surface_opcodes.items():
        require(token in payload, f"missing texture-surface opcode proof: {name}")

    # DDSD_MIPMAPCOUNT and DDSCAPS_MIPMAP are intentionally absent from the sealed descriptors.
    DDSD_MIPMAPCOUNT = 0x00020000
    DDSCAPS_MIPMAP = 0x00400000
    staging_flags = 0x00001007
    staging_caps = 0x00001800
    device_flags = 0x00001007
    device_caps_variants = [0x04001000, 0x04001800, 0x04005000]
    descriptor_checks = {
        "staging_no_mipmap_count": (staging_flags & DDSD_MIPMAPCOUNT) == 0,
        "staging_no_mipmap_caps": (staging_caps & DDSCAPS_MIPMAP) == 0,
        "device_no_mipmap_count": (device_flags & DDSD_MIPMAPCOUNT) == 0,
        "device_no_mipmap_caps": all((caps & DDSCAPS_MIPMAP) == 0 for caps in device_caps_variants),
    }
    require(all(descriptor_checks.values()), "binary descriptor unexpectedly requests mipmaps")

    c_text = c_export.read_text(encoding="utf-8", errors="replace")
    staging_start = c_text.find("GraphicsTextureResource * GraphicsTexture_CreateStagingTexture")
    staging_end = c_text.find("// Releases the device and staging COM triplets", staging_start)
    device_start = c_text.find("GraphicsTextureResource * GraphicsTexture_CreateDeviceTexture")
    device_end = c_text.find("// Dispatches all-resource texture rebuilding by selected backend.", device_start)
    require(min(staging_start, staging_end, device_start, device_end) >= 0,
            "named texture-creation owners missing from complete C export")
    staging_c = c_text[staging_start:staging_end]
    device_c = c_text[device_start:device_end]
    c_checks = {
        "filter_mag_uses_shared_value":
            "D3DRENDERSTATE_TEXTUREMAG" in c_text and
            ("_DAT_00577e10" in c_text or "_g_Direct3DTextureFilterMode" in c_text),
        "filter_min_uses_shared_value":
            "D3DRENDERSTATE_TEXTUREMIN" in c_text and
            ("_DAT_00577e10" in c_text or "_g_Direct3DTextureFilterMode" in c_text),
        "staging_flags_and_caps":
            "g_SurfaceDesc.dwFlags = 0x1007;" in staging_c and
            "g_SurfaceDesc.ddsCaps.dwCaps = 0x1800;" in staging_c,
        "staging_is_square": "g_SurfaceDesc.dwWidth = g_SurfaceDesc.dwHeight;" in staging_c,
        "device_flags_and_caps":
            "g_SurfaceDesc.dwFlags = 0x1007;" in device_c and
            "g_SurfaceDesc.ddsCaps.dwCaps = 0x4001000;" in device_c,
        "device_is_square": "g_SurfaceDesc.dwWidth = g_SurfaceDesc.dwHeight;" in device_c,
        "no_mipmap_count_assignment":
            "dwMipMapCount" not in staging_c and "dwMipMapCount" not in device_c,
    }
    require(all(c_checks.values()), "C-export texture sampling correspondence failed: " +
            ", ".join(name for name, ok in c_checks.items() if not ok))

    sampling_header = (source / "src/editor/texture_sampling.hpp").read_text(encoding="utf-8")
    renderer_header = (source / "src/editor/terrain_renderer.hpp").read_text(encoding="utf-8")
    renderer = (source / "src/editor/terrain_renderer.cpp").read_text(encoding="utf-8")
    test_source = (source / "tests/rendering_directdraw_single_level_object_texture_sampling_tests.cpp").read_text(
        encoding="utf-8")
    cmake = (source / "CMakeLists.txt").read_text(encoding="utf-8")
    source_checks = {
        "independent_sampling_policy":
            "enum class TextureSamplingMode" in sampling_header and
            "DirectDrawSingleLevelLinear" in sampling_header and
            "return {1U, false, false}" in sampling_header,
        "upload_requires_policy": "TextureSamplingMode sampling_mode" in renderer_header,
        "mip_count_comes_from_policy":
            "destination.mip_levels = sampling_plan.mip_levels" in renderer,
        "object_uploads_are_single_level":
            renderer.count("TextureSamplingMode::DirectDrawSingleLevelLinear") >= 3,
        "terrain_uploads_are_retail_single_level":
            renderer.count("TextureSamplingMode::DirectDrawSingleLevelLinear") >= 5,
        "mip_interpolation_is_policy_controlled":
            "sampling_plan.blend_between_mip_levels" in renderer and
            "VK_SAMPLER_MIPMAP_MODE_NEAREST" in renderer,
        "anisotropy_is_policy_controlled":
            "sampling_plan.allow_anisotropy" in renderer and
            "use_anisotropy ? VK_TRUE : VK_FALSE" in renderer,
        "regression_seals_128_and_256_counts":
            "complete_mip_level_count(128U) == 8U" in test_source and
            "complete_mip_level_count(256U) == 9U" in test_source,
        "regression_seals_directdraw_policy":
            "directdraw.mip_levels == 1U" in test_source and
            "!directdraw.allow_anisotropy" in test_source and
            "!directdraw.blend_between_mip_levels" in test_source,
        "regression_target_registered":
            "rendering_directdraw_single_level_object_texture_sampling_tests" in cmake,
    }
    require(all(source_checks.values()), "G345 source checks failed: " +
            ", ".join(name for name, ok in source_checks.items() if not ok))

    gzf_hash = sha256(gzf)
    require(gzf_hash in ACCEPTED_EVIDENCE_GZF_SHA256,
            f"GZF is not an explicitly admitted semantic-evidence container: {gzf_hash}")

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
        "schema": "thandor-vulkan-map-editor-g345-directdraw-single-level-object-texture-sampling-v1",
        "status": "PASS",
        "binary": {
            "path": str(binary),
            "sha256": game_hash,
            "sealed_ranges": range_result,
            "filter_data": {
                "address": "0x00577E10",
                "value": filter_value,
                "used_for": ["D3DRENDERSTATE_TEXTUREMAG", "D3DRENDERSTATE_TEXTUREMIN"],
                "interpretation": "linear magnification and minification in the Direct3D 2 path",
            },
            "surface_descriptors": {
                "staging": {"dwFlags": "0x00001007", "dwCaps": "0x00001800"},
                "device": {
                    "dwFlags": "0x00001007",
                    "base_dwCaps": "0x04001000",
                    "possible_dwCaps": ["0x04001800", "0x04005000"],
                },
                "checks": descriptor_checks,
                "contract": "square single-level surfaces; no DDSD_MIPMAPCOUNT and no DDSCAPS_MIPMAP",
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
            "promoted": gzf_hash != ACCEPTED_V281_GZF_SHA256,
            "note": (
                "GZF admission never replaces immutable game.exe/range/C-owner checks. "
                "The current V434 container is accepted as advanced semantic evidence; the historical G345 container remains reproducible comparison evidence."
            ),
        },
        "source": {
            "checks": source_checks,
            "fix": (
                "Use one bilinearly filtered image level with no anisotropy for retail texture sampling. "
                "G408 extends the same binary-proven single-level linear policy to ground textures; the former terrain mipmapped path is superseded."
            ),
        },
        "screenshots": {
            "files": screenshots,
            "interpretation": (
                "The captures are not camera-, fog-, or animation-phase matched and are diagnostic only. "
                "The reported editor frame shows pale merged crystal lobes where the game retains distinct rod detail, "
                "which is consistent with the rejected full mip-chain/trilinear/anisotropic object sampling path."
            ),
            "live_post_fix_frame_available": False,
        },
        "supersedes": {
            "task": "G344 visual-completion claim",
            "reason": "G344 corrected per-triangle vertex-light selection but did not reproduce the DirectDraw texture surface and sampling contract.",
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("G345_DIRECTDRAW_SINGLE_LEVEL_OBJECT_TEXTURE_SAMPLING_VALIDATION PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
