#!/usr/bin/env python3
"""Independent bounded census for Thandor ARM, MDL, and SPR resources."""
from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

HEADER = 0x200

def u32(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise ValueError(f"u32 out of range at 0x{offset:X}")
    return struct.unpack_from("<I", data, offset)[0]

def norm(path: str) -> str:
    return path.replace("\\", "/").lower()

def first_spr_path(record: bytes) -> str | None:
    for start in range(0, max(0, len(record) - 8), 2):
        chars: list[str] = []
        cursor = start
        while cursor + 2 <= len(record):
            value = struct.unpack_from("<H", record, cursor)[0]
            if value < 0x20 or value >= 0x7F:
                break
            chars.append(chr(value))
            cursor += 2
        text = norm("".join(chars))
        if text.startswith("spr/") and len(text) > 4:
            return text if text.endswith(".spr") else text + ".spr"
    return None

def parse_mdl(path: Path) -> list[tuple[int, str]]:
    data = path.read_bytes()
    if len(data) < HEADER or data[:3] != b"mdl":
        raise ValueError(f"bad MDL header: {path}")
    count = u32(data, 0xB0)
    offset = HEADER
    result: list[tuple[int, str]] = []
    for _ in range(count):
        size = u32(data, offset)
        if size < 0x0C or offset + size > len(data):
            raise ValueError(f"bad MDL record bounds: {path}")
        sprite = first_spr_path(data[offset:offset + size])
        if sprite is None:
            raise ValueError(f"MDL record lacks SPR path: {path}")
        result.append((u32(data, offset + 8), sprite))
        offset += size
    if offset > len(data):
        raise ValueError(f"MDL walk overrun: {path}")
    return result

def parse_spr(path: Path) -> tuple[int, int, int, int]:
    data = path.read_bytes()
    if len(data) < HEADER or data[:3] != b"spr":
        raise ValueError(f"bad SPR header: {path}")
    declared = u32(data, 4)
    if declared < HEADER or declared > len(data) or u32(data, 0x0C) != 0x00020007:
        raise ValueError(f"bad SPR size/version: {path}")
    groups = u32(data, 0xB0)
    group_offset = HEADER
    mesh_records = vertices = triangles = 0
    for _ in range(groups):
        group_size = u32(data, group_offset)
        record_count = u32(data, group_offset + 4)
        if group_size < 0x20 or group_offset + group_size > len(data):
            raise ValueError(f"bad SPR group bounds: {path}")
        record_offset = group_offset + 0x20
        for _ in range(record_count):
            record_size = u32(data, record_offset)
            vertex_count = u32(data, record_offset + 8)
            triangle_count = u32(data, record_offset + 0x0C)
            vertex_start = record_offset + 0x20
            triangle_start = vertex_start + vertex_count * 0x40
            if record_size < 0x20 or record_offset + record_size > group_offset + group_size:
                raise ValueError(f"bad SPR mesh bounds: {path}")
            if triangle_start + triangle_count * 0x40 > record_offset + record_size:
                raise ValueError(f"bad SPR vertex/triangle payload: {path}")
            for tri in range(triangle_count):
                tri_offset = triangle_start + tri * 0x40
                for field in (0, 0x0C, 0x18):
                    pointer = u32(data, tri_offset + field)
                    if pointer < vertex_start or pointer >= triangle_start or (pointer - vertex_start) % 0x40:
                        raise ValueError(f"bad SPR triangle vertex offset: {path}")
            mesh_records += 1
            vertices += vertex_count
            triangles += triangle_count
            record_offset += record_size
        group_offset += group_size
    return groups, mesh_records, vertices, triangles

def parse_arm(path: Path) -> list[tuple[int, int]]:
    data = path.read_bytes()
    if len(data) < HEADER or data[:3] != b"arm":
        raise ValueError(f"bad ARM header: {path}")
    count = u32(data, 0xB0)
    offset = HEADER
    result: list[tuple[int, int]] = []
    for _ in range(count):
        size = u32(data, offset)
        if size < 0x10 or offset + size > len(data):
            raise ValueError(f"bad ARM record bounds: {path}")
        registry_id = u32(data, offset + 8)
        root = u32(data, offset + 0x0C)
        model_id = u32(data, root + 0x20) if root + 0x24 <= len(data) else 0
        result.append((registry_id, model_id))
        offset += size
    return result

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path, help="Extracted stock PCK tree")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()

    sprite_paths = {norm(str(path.relative_to(root)).split("/", 1)[-1]): path for path in root.rglob("*.spr")}
    model_map: dict[int, str] = {}
    mdl_records = 0
    for path in sorted(root.rglob("*.mdl")):
        records = parse_mdl(path)
        mdl_records += len(records)
        for definition_id, sprite in records:
            model_map.setdefault(definition_id, sprite)

    spr_groups = spr_mesh_records = spr_vertices = spr_triangles = 0
    for path in sorted(root.rglob("*.spr")):
        groups, records, vertices, triangles = parse_spr(path)
        spr_groups += groups
        spr_mesh_records += records
        spr_vertices += vertices
        spr_triangles += triangles

    arm_records: list[tuple[int, int]] = []
    for path in sorted(root.rglob("*.arm")):
        arm_records.extend(parse_arm(path))
    resolved_records = 0
    unresolved_records: list[dict[str, int]] = []
    unique: dict[int, int] = {}
    for registry_id, model_id in arm_records:
        unique.setdefault(registry_id, model_id)
        sprite = model_map.get(model_id)
        if sprite and sprite in sprite_paths:
            resolved_records += 1
        else:
            unresolved_records.append({"registry_id": registry_id, "model_definition_id": model_id})
    unique_resolved = sum(1 for model_id in unique.values() if model_map.get(model_id) in sprite_paths)

    result = {
        "schema": "thandor-map-editor-stock-asset-census-v1",
        "root": str(root),
        "files": {
            "total": sum(1 for path in root.rglob("*") if path.is_file()),
            "lev": sum(1 for _ in root.rglob("*.lev")),
            "fld": sum(1 for _ in root.rglob("*.fld")),
            "gfx": sum(1 for _ in root.rglob("*.gfx")),
            "pal": sum(1 for _ in root.rglob("*.pal")),
            "arm": sum(1 for _ in root.rglob("*.arm")),
            "mdl": sum(1 for _ in root.rglob("*.mdl")),
            "spr": sum(1 for _ in root.rglob("*.spr")),
            "flm": sum(1 for _ in root.rglob("*.flm")),
        },
        "spr": {
            "files": len(sprite_paths),
            "groups": spr_groups,
            "mesh_records": spr_mesh_records,
            "vertices": spr_vertices,
            "triangles": spr_triangles,
        },
        "mdl": {
            "records": mdl_records,
            "unique_definition_ids": len(model_map),
        },
        "arm": {
            "records": len(arm_records),
            "resolved_record_visuals": resolved_records,
            "unresolved_record_visuals": len(unresolved_records),
            "unique_registry_ids": len(unique),
            "unique_resolved_visuals": unique_resolved,
            "unique_marker_fallbacks": len(unique) - unique_resolved,
            "unresolved_records": unresolved_records,
        },
    }
    text = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
