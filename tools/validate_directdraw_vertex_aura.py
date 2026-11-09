#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

EXPECTED_GAME_SHA256 = "53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969"
ACCEPTED_V281_GZF_SHA256 = "8db4b13d5a87d0862aa3ddb06b84a4b1e54ceee70b0884652601cee0721f68a6"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def file_slice(data: bytes, va_start: int, va_end: int) -> bytes:
    # game.exe .text/.rdata use image base 0x00400000 and matching RVA/raw offsets here.
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
    for path in (source, binary):
        require(path.exists(), f"missing required input: {path}")
    for path in (current_c, original_c, gzf):
        if path is not None:
            require(path.exists(), f"missing optional cross-source input: {path}")
    require((current_c is None) == (original_c is None),
            "--current-c and --original-c must be supplied together")

    game_hash = sha256(binary)
    require(game_hash == EXPECTED_GAME_SHA256, f"unexpected game.exe SHA-256: {game_hash}")
    data = binary.read_bytes()

    ranges = {
        "directdraw_vertex_resource_lighting": (0x004CCA90, 0x004CCB3E,
            "e6a06069fcb40ed24de0a18d6d3992ddc411efe5c60a7f49823516976c30bd37"),
        "terrain_packet_resource_lighting_callers": (0x005004A0, 0x0050081C,
            "7cdd22ebec1bbc53c98ee60e7aa29cc7ef8bf5443f00cd33c005016530baae90"),
        "packed_lighting_lookup_table": (0x0041DE80, 0x0041E680,
            "f15256055fd258913870ecc8c5c72217b5368e4bcb4c97051458030a1ca75b1b"),
        "shading_record_transition_and_compaction": (0x004CCB40, 0x004CCD70,
            "00e434120e685389cb09c668a6202c26c79b09606dbbb0831546941d87ba1040"),
    }
    range_result = {}
    for name, (start, end, expected) in ranges.items():
        payload = file_slice(data, start, end)
        actual = hashlib.sha256(payload).hexdigest()
        require(actual == expected, f"{name} range hash mismatch: {actual}")
        range_result[name] = {
            "start_va": f"0x{start:08X}", "end_va_exclusive": f"0x{end:08X}",
            "size": len(payload), "sha256": actual,
        }

    body = file_slice(data, 0x004CCA90, 0x004CCB3E)
    for token in (
        bytes.fromhex("8b 0d 50 6d 4c 00"),  # compact record count
        bytes.fromhex("bf 50 2d 4c 00"),     # compact record base
        bytes.fromhex("0f a4 d9 1b"),        # remaining squared >> 5
        bytes.fromhex("0f a4 d3 14"),        # squared radius >> 12
        bytes.fromhex("0f e5 24 c5 80 de 41 00"),
        bytes.fromhex("0f dd c4"),           # paddusw
    ):
        require(token in body, f"missing immutable DirectDraw aura opcode sequence: {token.hex()}")

    callers = file_slice(data, 0x005004A0, 0x0050081C)
    expected_calls = [0x00500577, 0x0050067B, 0x005007BE, 0x005007FB]
    for call_va in expected_calls:
        offset = call_va - 0x005004A0
        require(callers[offset:offset + 5][0] == 0xE8, f"missing call opcode at 0x{call_va:08X}")
        rel = struct.unpack_from("<i", callers, offset + 1)[0]
        require(call_va + 5 + rel == 0x004CCA90,
                f"call at 0x{call_va:08X} does not target 0x004CCA90")

    table = file_slice(data, 0x0041DE80, 0x0041E680)
    table_entries = []
    for index in range(256):
        qword = struct.unpack_from("<Q", table, index * 8)[0]
        lanes = [(qword >> (lane * 16)) & 0xFFFF for lane in range(4)]
        expected_rgb = (index * 257) >> 4
        require(lanes[:3] == [expected_rgb] * 3 and lanes[3] == 0x1000,
                f"packed lighting lookup mismatch at index {index}: {lanes}")
        if index in (0, 1, 16, 64, 96, 128, 255):
            table_entries.append({"index": index, "lanes": lanes})

    terrain_vert = (source / "shaders/terrain.vert").read_text(encoding="utf-8")
    terrain_frag = (source / "shaders/terrain.frag").read_text(encoding="utf-8")
    model_vert = (source / "shaders/model.vert").read_text(encoding="utf-8")
    model_frag = (source / "shaders/model.frag").read_text(encoding="utf-8")
    effect_cpp = (source / "src/core/effect.cpp").read_text(encoding="utf-8")
    renderer_cpp = (source / "src/editor/terrain_renderer.cpp").read_text(encoding="utf-8")
    c_text = current_c.read_text(encoding="utf-8", errors="replace") if current_c else ""
    original_text = original_c.read_text(encoding="utf-8", errors="replace") if original_c else ""

    source_checks = {
        "terrain_vertex_record_buffer": "layout(std430,set=0,binding=3)" in terrain_vert,
        "terrain_vertex_lookup_quotient": "remainingSquared * 128.0 / radiusSquared" in terrain_vert,
        "terrain_vertex_lookup_word": "lookupIndex * 257.0 / 16.0" in terrain_vert,
        "terrain_vertex_paddusw_emulation": "min(vec3(65535.0), words +" in terrain_vert,
        "terrain_vertex_final_pmulhw": "litWords * modulationWords / 65536.0" in terrain_vert,
        "terrain_smooth_interpolation": "layout(location=3) out vec3 lightingBytes" in terrain_vert,
        "terrain_fragment_no_shading_buffer": "ResourceShadingBuffer" not in terrain_frag,
        "terrain_fragment_no_alpha_overlay": all(x not in terrain_frag for x in
            ("generatedResourceShadingOverlay", "mix(result", "maskAlpha")),
        "model_vertex_record_buffer": "layout(std430,set=0,binding=3)" in model_vert,
        "model_vertex_exact_path": all(x in model_vert for x in
            ("direct_draw_resource_shading_words", "direct_draw_finalize_vertex_lighting")),
        "model_fragment_no_radial_tint": all(x not in model_frag for x in
            ("ResourceShadingBuffer", "resource_shading_addition")),
        "cpu_lookup_quotient": "remaining_squared * 128.0 / radius_squared" in effect_cpp,
        "cpu_raw_word": "graphics_shading_directdraw_raw_word" in effect_cpp,
        "cpu_final_modulation": "graphics_shading_directdraw_modulated_channel" in effect_cpp,
        "vertex_stage_descriptor_visibility":
            "VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT" in renderer_cpp,
    }
    failed = [name for name, passed in source_checks.items() if not passed]
    require(not failed, "G341 source checks failed: " + ", ".join(failed))

    c_checks = {}
    if current_c is not None and original_c is not None:
        c_checks = {
            "current_c_runtime_record": "struct GraphicsShadingRuntimeRecord" in c_text,
            "current_c_vertex_intensity": "ModelRender_ComputeVertexIntensityDefaultPath" in c_text,
            "current_c_lookup_table": "g_PackedLightingLookupTable" in c_text,
            "current_c_compact_records": "g_GraphicsShadingCompactRecords" in c_text,
            "original_c_runtime_record": "GraphicsShadingRuntimeRecord" in original_text,
            "original_c_lookup_table": "g_PackedLightingLookupTable" in original_text,
        }
        failed_c = [name for name, passed in c_checks.items() if not passed]
        require(not failed_c, "C correspondence checks failed: " + ", ".join(failed_c))

    gzf_hash = sha256(gzf) if gzf is not None else None
    result = {
        "schema": "thandor-vulkan-map-editor-g341-directdraw-vertex-resource-aura-validation-v1",
        "status": "PASS",
        "binary": {"path": str(binary), "sha256": game_hash, "ranges": range_result,
                   "terrain_call_sites": [f"0x{x:08X}" for x in expected_calls],
                   "lookup_samples": table_entries},
        "implementation": source_checks,
        "c_correspondence": {
            "provided": current_c is not None,
            "checks": c_checks,
            "current_c_sha256": sha256(current_c) if current_c else None,
            "original_c_sha256": sha256(original_c) if original_c else None,
            "files_byte_identical": (current_c.read_bytes() == original_c.read_bytes())
                if current_c and original_c else None,
            "note": "When supplied, byte-identical C files are disclosed and immutable binary ranges remain primary proof.",
        },
        "gzf": {
            "provided": gzf is not None,
            "supplied_sha256": gzf_hash,
            "accepted_v281_sha256_recorded_by_project": ACCEPTED_V281_GZF_SHA256,
            "matches_accepted_v281": (gzf_hash == ACCEPTED_V281_GZF_SHA256) if gzf_hash else None,
            "note": "The supplied GZF is recorded for cross-source identity; this update does not modify or promote it.",
        },
        "rendering_contract": {
            "composition": "resource RGB is quantized and saturating-added into terrain/model vertex lighting before ordinary texture modulation",
            "attenuation_index": "floor(128 * remainingSquared / radiusSquared), clamped 0..128",
            "fragment_alpha_overlay": False,
            "projected_generated_texture_used_for_resource_aura": False,
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("G341_DIRECTDRAW_VERTEX_RESOURCE_AURA_VALIDATION PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
