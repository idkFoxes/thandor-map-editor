#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

EXPECTED_SHA256 = "53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969"
EXPECTED_PRESETS = [
    [2, 1, 0, 1],  # ONE/ZERO, no blending, Z write
    [5, 6, 1, 1],  # SRCALPHA/INVSRCALPHA, blending, Z write
    [5, 6, 1, 0],  # SRCALPHA/INVSRCALPHA, blending, no Z write
    [5, 6, 1, 1],
    [2, 2, 1, 0],  # ONE/ONE, blending, no Z write
]
EXPECTED_DISPATCH = [
    0x57ADA0,0x57B410,0x57BBE0,0x57C6C0,0x57C890,0x57CAA0,0x57CCB0,0x57CF20,
    0x57D480,0,0x57CCB0,0,0x57D1D0,0,0x57CCB0,0x57CF20,
    0x57D480,0,0x57CCB0,0,0x57D1D0,0,0x57D730,0x57DD70,
    0x57E3B0,0,0x57DA50,0,0x57E090,0,0x57D730,0x57DD70,
    0x57E3B0,0,0x57DA50,0,0x57E090,0,0x57CF20,0x57CF20,
    0x57D480,0,0x57CF20,0,0x57CF20,0,0x57CF20,0x57CF20,
    0x57D480,0,0x57CF20,0,0x57CF20,0,0x57DD70,0x57DD70,
    0x57E3B0,0,0x57DD70,0,0x57DD70,0,0x57DD70,0x57DD70,
]


def parse_pe(data: bytes):
    if data[:2] != b"MZ":
        raise ValueError("not an MZ executable")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("missing PE signature")
    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    image_base = struct.unpack_from("<I", data, optional + 28)[0]
    sections = []
    cursor = optional + optional_size
    for _ in range(section_count):
        name = data[cursor:cursor + 8].split(b"\0", 1)[0].decode("ascii", "replace")
        virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from(
            "<IIII", data, cursor + 8)
        sections.append((name, virtual_address, virtual_size, raw_pointer, raw_size))
        cursor += 40
    return image_base, sections


def read_va(data: bytes, image_base: int, sections, va: int, size: int) -> bytes:
    rva = va - image_base
    for _name, section_va, virtual_size, raw_pointer, raw_size in sections:
        if section_va <= rva < section_va + max(virtual_size, raw_size):
            offset = raw_pointer + rva - section_va
            result = data[offset:offset + size]
            if len(result) != size:
                raise ValueError(f"short read at 0x{va:08X}")
            return result
    raise ValueError(f"VA 0x{va:08X} is outside file-backed sections")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("game_exe", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    data = args.game_exe.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_SHA256:
        raise SystemExit(f"unexpected game.exe SHA-256: {digest}")
    image_base, sections = parse_pe(data)

    presets_flat = struct.unpack("<20I", read_va(data, image_base, sections, 0x00577E50, 80))
    presets = [list(presets_flat[i:i + 4]) for i in range(0, 20, 4)]
    if presets != EXPECTED_PRESETS:
        raise SystemExit(f"render-state presets changed: {presets}")

    dispatch = list(struct.unpack("<64I", read_va(data, image_base, sections, 0x00577EA0, 256)))
    if dispatch != EXPECTED_DISPATCH:
        raise SystemExit("64-entry graphics dispatch table changed")

    anchors = {
        "ModelRender_SubmitTriangle_entry": (0x004BD9B0, "5053515257565589e58b7d248b752857"),
        "per_triangle_cull_flag_0x400": (0x004BD9C5, "f747340004000075093b4520"),
        "palette_index_mask_0x1ff": (0x004BDB3D, "25ff0100003b82b0000000"),
        "full_render_flags_queued": (0x004BDABF, "ffb734000000e856300100"),
        "primitive_color_selector_update": (0x004D0C80, "5053515257565589e58b5d2c8b4b04"),
        "texture_bit_material_binding": (0x004D0D00, "5053515257565589e58b5d288b5304"),
    }
    verified = {}
    for name, (va, expected_hex) in anchors.items():
        expected = bytes.fromhex(expected_hex)
        actual = read_va(data, image_base, sections, va, len(expected))
        if actual != expected:
            raise SystemExit(f"binary anchor changed: {name} at 0x{va:08X}")
        verified[name] = {"va": f"0x{va:08X}", "bytes": actual.hex()}

    preset_by_handler = {
        0x57CCB0: "opaque_depth_write",
        0x57CF20: "alpha_no_depth_write",
        0x57D1D0: "alpha_depth_write",
        0x57D480: "additive_no_depth_write",
        0x57D730: "opaque_depth_write",
        0x57DA50: "alpha_depth_write",
        0x57DD70: "alpha_no_depth_write",
        0x57E090: "alpha_depth_write",
        0x57E3B0: "additive_no_depth_write",
    }
    selector_classes = {
        str(index): preset_by_handler.get(pointer, "special_or_null")
        for index, pointer in enumerate(dispatch)
    }
    report = {
        "schema": "thandor-map-editor-g320-binary-resource-routing-v1",
        "status": "PASS",
        "game_exe_sha256": digest,
        "render_state_presets_va": "0x00577E50",
        "render_state_presets": presets,
        "dispatch_table_va": "0x00577EA0",
        "dispatch_table_entries": [f"0x{x:08X}" if x else "0x00000000" for x in dispatch],
        "selector_classes": selector_classes,
        "anchors": verified,
        "conclusions": [
            "Subresource number does not select the blend pipeline.",
            "The final six-bit selector comes from render flags plus the runtime texture-present bit.",
            "Render flag 0x400 bypasses per-triangle culling.",
            "Preset 4 is ONE/ONE additive with Z writes disabled.",
            "G319's radial terrain mesh has no binary support and is removed from the active path.",
        ],
    }
    text = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    print("G320_BINARY_RESOURCE_ROUTING_VALIDATION PASS presets=5 dispatch=64 anchors=6")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
