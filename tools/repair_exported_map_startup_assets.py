#!/usr/bin/env python3
"""Repair the submitted custom map startup chain for the immutable game contract.

This utility preserves stored-method PCK records, restores the mandatory LEV environment-movie
base, crops the field to the stock-proven 131x131 envelope, shifts retained placements, and removes
all map-chain shadows from PATCH archives so the numbered level package is authoritative.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
from dataclasses import dataclass
from pathlib import Path

PCK_HEADER = 0x200
PCK_ENTRY_HEADER = 0x200
PCK_NAME_BYTES = 0x1EC
FIELD_HEADER = 0x200
FIELD_CELL = 0x80
FIELD_MAGIC = 0x00646C66
LEVEL_MAGIC = 0x0076656C
GAME_FIELD_VERSION = 0x00060006
LEGACY_EDITOR_FIELD_VERSION = 0x00070001
VALID_DIMENSIONS = tuple(range(59, 140, 8))
MAX_PROVEN_AREA = 131 * 131
LEVEL_PATH_FIELDS = tuple(0xB0 + i * 4 for i in range(10))
ENDING_MOVIE_PATH_INDEX = 7
DEFAULT_ENDING_MOVIE_BASE = r"flm\erde"


def u32(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def i32(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def put_u32(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", data, offset, value & 0xFFFFFFFF)


def put_i32(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<i", data, offset, value)


def read_utf16z(data: bytes, offset: int, capacity: int) -> str:
    chars: list[str] = []
    for cursor in range(offset, offset + capacity - 1, 2):
        value = struct.unpack_from("<H", data, cursor)[0]
        if value == 0:
            break
        chars.append(chr(value))
    return "".join(chars)


def write_utf16_fixed(data: bytearray, offset: int, capacity: int, value: str) -> None:
    encoded = value.encode("utf-16le")
    if len(encoded) + 2 > capacity:
        raise ValueError(f"PCK path exceeds fixed UTF-16 field: {value}")
    data[offset:offset + capacity] = b"\0" * capacity
    data[offset:offset + len(encoded)] = encoded


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


@dataclass
class Entry:
    name: str
    data: bytes
    type_tag: int
    method: int


def parse_stored_pck(path: Path) -> tuple[bytes, list[Entry]]:
    raw = path.read_bytes()
    if len(raw) < PCK_HEADER or u32(raw, 0) != 0x006B6370:
        raise ValueError(f"invalid PCK header: {path}")
    if u32(raw, 4) != len(raw):
        raise ValueError(f"PCK declared size mismatch: {path}")
    count = u32(raw, 0xB0)
    cursor = PCK_HEADER
    entries: list[Entry] = []
    for _ in range(count):
        if cursor + PCK_ENTRY_HEADER > len(raw):
            raise ValueError("PCK entry header exceeds archive")
        name = read_utf16z(raw, cursor, PCK_NAME_BYTES)
        unpacked = u32(raw, cursor + 0x1F0)
        type_tag = u32(raw, cursor + 0x1F4)
        packed = u32(raw, cursor + 0x1F8)
        method = u32(raw, cursor + 0x1FC)
        payload_start = cursor + PCK_ENTRY_HEADER
        payload_end = payload_start + packed
        if payload_end > len(raw):
            raise ValueError(f"PCK payload exceeds archive: {name}")
        if method != 1:
            raise ValueError(f"repair requires stored-method entries; {name} uses method {method}")
        if unpacked > packed:
            raise ValueError(f"stored PCK entry is truncated: {name}")
        entries.append(Entry(name, raw[payload_start:payload_start + unpacked], type_tag, method))
        cursor = payload_end
    if cursor != len(raw):
        raise ValueError("PCK entry walk did not end at archive boundary")
    return raw[:PCK_HEADER], entries


def build_stored_pck(header: bytes, entries: list[Entry]) -> bytes:
    if len(header) != PCK_HEADER:
        raise ValueError("PCK header template must be exactly 0x200 bytes")
    total = PCK_HEADER + sum(PCK_ENTRY_HEADER + len(entry.data) for entry in entries)
    if total > 0xFFFFFFFF:
        raise ValueError("PCK exceeds 32-bit size")
    output = bytearray(total)
    output[:PCK_HEADER] = header
    put_u32(output, 0, 0x006B6370)
    put_u32(output, 4, total)
    put_u32(output, 8, 1)
    put_u32(output, 0x0C, 0x00010000)
    put_u32(output, 0xB0, len(entries))
    cursor = PCK_HEADER
    seen: set[str] = set()
    for entry in entries:
        if len(entry.data) & 3:
            raise ValueError(f"stored PCK payload is not four-byte aligned: {entry.name}")
        if "/" in entry.name or entry.name.lower() != entry.name:
            raise ValueError(f"entry path is not exact lowercase backslash form: {entry.name}")
        key = entry.name.replace("\\", "/").lower()
        if key in seen:
            raise ValueError(f"duplicate PCK path: {entry.name}")
        seen.add(key)
        write_utf16_fixed(output, cursor, PCK_NAME_BYTES, entry.name)
        put_u32(output, cursor + 0x1EC, 0)
        put_u32(output, cursor + 0x1F0, len(entry.data))
        put_u32(output, cursor + 0x1F4, entry.type_tag)
        put_u32(output, cursor + 0x1F8, len(entry.data))
        put_u32(output, cursor + 0x1FC, 1)
        start = cursor + PCK_ENTRY_HEADER
        output[start:start + len(entry.data)] = entry.data
        cursor = start + len(entry.data)
    return bytes(output)


def repair_field(payload: bytes, target_width: int, target_height: int) -> tuple[bytes, dict[str, int]]:
    if len(payload) < FIELD_HEADER or u32(payload, 0) != FIELD_MAGIC:
        raise ValueError("selected FLD payload has invalid magic")
    old_width = u32(payload, 0xB8)
    old_height = u32(payload, 0xBC)
    version = u32(payload, 0x0C)
    expected = FIELD_HEADER + old_width * old_height * FIELD_CELL
    if len(payload) != expected:
        raise ValueError("selected FLD size disagrees with dimensions")
    if version not in (GAME_FIELD_VERSION, LEGACY_EDITOR_FIELD_VERSION):
        raise ValueError(f"unsupported source FLD converter version 0x{version:08X}")
    if target_width not in VALID_DIMENSIONS or target_height not in VALID_DIMENSIONS:
        raise ValueError("target FLD dimensions are outside the proven game-compatible sequence")
    if target_width * target_height > MAX_PROVEN_AREA:
        raise ValueError("target FLD area exceeds the stock-proven 131x131 compatibility envelope")
    if target_width > old_width or target_height > old_height:
        raise ValueError("repair only supports center-cropping, not padding")

    source_x = (old_width - target_width) // 2
    source_y = (old_height - target_height) // 2
    header = bytearray(payload[:FIELD_HEADER])
    output = bytearray(FIELD_HEADER + target_width * target_height * FIELD_CELL)
    output[:FIELD_HEADER] = header
    material_mask = 0
    for y in range(target_height):
        for x in range(target_width):
            old_index = (y + source_y) * old_width + (x + source_x)
            new_index = y * target_width + x
            old_offset = FIELD_HEADER + old_index * FIELD_CELL
            new_offset = FIELD_HEADER + new_index * FIELD_CELL
            output[new_offset:new_offset + FIELD_CELL] = payload[old_offset:old_offset + FIELD_CELL]
            put_i32(output, new_offset + 0x40, y * 0x480 + x * 0x901)
            put_i32(output, new_offset + 0x44, -y * 1999)
            material = u32(output, new_offset + 0x50) & 0xFF
            if material < 32:
                material_mask |= 1 << material
    put_u32(output, 4, len(output))
    put_u32(output, 0x0C, GAME_FIELD_VERSION)
    put_u32(output, 0xB0, material_mask)
    put_u32(output, 0xB8, target_width)
    put_u32(output, 0xBC, target_height)
    return bytes(output), {
        "old_width": old_width,
        "old_height": old_height,
        "target_width": target_width,
        "target_height": target_height,
        "source_x": source_x,
        "source_y": source_y,
        "source_version": version,
        "target_version": GAME_FIELD_VERSION,
        "material_mask": material_mask,
    }


def repair_level(payload: bytes, field_info: dict[str, int]) -> tuple[bytes, dict[str, object]]:
    if len(payload) < 0x200 or u32(payload, 0) != LEVEL_MAGIC:
        raise ValueError("selected LEV payload has invalid magic")
    count = u32(payload, 0xD8)
    placement_offset = u32(payload, 0xDC)
    if placement_offset + count * 0x20 != len(payload):
        raise ValueError("LEV placement count/offset mismatch")
    dx_cells = -field_info["source_x"]
    dy_cells = -field_info["source_y"]
    delta_render_x = dx_cells * 0x901 + dy_cells * 0x480
    delta_render_y = -dy_cells * 1999
    output = bytearray(payload)
    placement_report: list[dict[str, object]] = []
    for index in range(count):
        offset = placement_offset + index * 0x20
        old_render_x = i32(output, offset + 0x08)
        old_render_y = i32(output, offset + 0x0C)
        new_render_x = old_render_x + delta_render_x
        new_render_y = old_render_y + delta_render_y
        grid_y = -new_render_y / 1999.0
        grid_x = (new_render_x - 1152.0 * grid_y) / 2305.0
        if not (-0.01 <= grid_x <= field_info["target_width"] - 0.99 and
                -0.01 <= grid_y <= field_info["target_height"] - 0.99):
            raise ValueError(f"placement {index} leaves cropped field at ({grid_x:.3f},{grid_y:.3f})")
        put_i32(output, offset + 0x08, new_render_x)
        put_i32(output, offset + 0x0C, new_render_y)
        placement_report.append({
            "index": index,
            "asset_id": u32(output, offset),
            "faction": u32(output, offset + 4),
            "old_render_x_q12": old_render_x,
            "old_render_y_q12": old_render_y,
            "new_render_x_q12": new_render_x,
            "new_render_y_q12": new_render_y,
            "new_grid_x": grid_x,
            "new_grid_y": grid_y,
        })

    path_offsets = [u32(output, field) for field in LEVEL_PATH_FIELDS]
    movie_offset = path_offsets[ENDING_MOVIE_PATH_INDEX]
    later = [candidate for candidate in path_offsets if candidate > movie_offset]
    movie_end = min(later, default=placement_offset)
    old_movie = read_utf16z(bytes(output), movie_offset, movie_end - movie_offset)
    write_utf16_fixed(output, movie_offset, movie_end - movie_offset, DEFAULT_ENDING_MOVIE_BASE)
    return bytes(output), {
        "placement_count": count,
        "delta_render_x_q12": delta_render_x,
        "delta_render_y_q12": delta_render_y,
        "ending_movie_before": old_movie,
        "ending_movie_after": DEFAULT_ENDING_MOVIE_BASE,
        "placements": placement_report,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--level-package", type=Path, required=True)
    parser.add_argument("--patch00", type=Path, required=True)
    parser.add_argument("--output-level", type=Path, required=True)
    parser.add_argument("--output-patch", type=Path, required=True)
    parser.add_argument("--target-width", type=int, default=131)
    parser.add_argument("--target-height", type=int, default=131)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()

    header, entries = parse_stored_pck(args.level_package)
    fld_indices = [i for i, entry in enumerate(entries) if entry.name.endswith(".fld")]
    lev_indices = [i for i, entry in enumerate(entries) if entry.name.endswith(".lev")]
    if len(fld_indices) != 1 or len(lev_indices) != 1:
        raise ValueError("repair requires exactly one LEV and one FLD entry")

    repaired_fld, field_info = repair_field(
        entries[fld_indices[0]].data, args.target_width, args.target_height)
    repaired_lev, level_info = repair_level(entries[lev_indices[0]].data, field_info)
    entries[fld_indices[0]].data = repaired_fld
    entries[lev_indices[0]].data = repaired_lev

    repaired_package = build_stored_pck(header, entries)
    args.output_level.parent.mkdir(parents=True, exist_ok=True)
    args.output_level.write_bytes(repaired_package)
    patch_header, patch_entries = parse_stored_pck(args.patch00)
    owned = {entry.name for entry in entries if entry.name.startswith("level\\")}
    removed = [entry.name for entry in patch_entries if entry.name in owned]
    patch_entries = [entry for entry in patch_entries if entry.name not in owned]
    args.output_patch.parent.mkdir(parents=True, exist_ok=True)
    args.output_patch.write_bytes(build_stored_pck(patch_header, patch_entries))

    verify_header, verify_entries = parse_stored_pck(args.output_level)
    del verify_header
    verify_fld = next(entry.data for entry in verify_entries if entry.name.endswith(".fld"))
    verify_lev = next(entry.data for entry in verify_entries if entry.name.endswith(".lev"))
    if u32(verify_fld, 0x0C) != GAME_FIELD_VERSION:
        raise ValueError("repaired package FLD version verification failed")
    if (u32(verify_fld, 0xB8), u32(verify_fld, 0xBC)) != (args.target_width, args.target_height):
        raise ValueError("repaired package FLD dimensions verification failed")
    if len(verify_fld) != FIELD_HEADER + args.target_width * args.target_height * FIELD_CELL:
        raise ValueError("repaired package FLD size verification failed")
    if u32(verify_lev, 0x310) != 2 or u32(verify_lev, 0x314) != 2:
        raise ValueError("G330 player-count repair was not preserved")
    verify_offsets = [u32(verify_lev, field) for field in LEVEL_PATH_FIELDS]
    movie_offset = verify_offsets[ENDING_MOVIE_PATH_INDEX]
    movie_end = min([candidate for candidate in verify_offsets if candidate > movie_offset], default=u32(verify_lev, 0xDC))
    if read_utf16z(verify_lev, movie_offset, movie_end - movie_offset) != DEFAULT_ENDING_MOVIE_BASE:
        raise ValueError("mandatory ending-movie/environment base was not restored")
    _, verify_patch_entries = parse_stored_pck(args.output_patch)
    remaining_patch_names = {entry.name for entry in verify_patch_entries}
    if remaining_patch_names.intersection(owned):
        raise ValueError("repaired PATCH archive still shadows the numbered map chain")

    report = {
        "schema": "thandor-g332-required-level-movie-stock-shape-patch-shadow-repair-v1",
        "status": "PASS",
        "input": {
            "level_package": str(args.level_package.resolve()),
            "level_sha256": sha256(args.level_package),
            "patch00": str(args.patch00.resolve()),
            "patch00_sha256": sha256(args.patch00),
        },
        "output": {
            "level_package": str(args.output_level.resolve()),
            "level_sha256": sha256(args.output_level),
            "patch00": str(args.output_patch.resolve()),
            "patch00_sha256": sha256(args.output_patch),
            "patch00_byte_identical": args.patch00.read_bytes() == args.output_patch.read_bytes(),
            "patch_map_entries_removed": removed,
        },
        "field": field_info,
        "level": level_info,
        "preserved": {
            "entry_names": [entry.name for entry in entries],
            "scenario_alias": u32(next(entry.data for entry in entries if entry.name.endswith(".dat")), 0x70),
            "lev_alias": u32(verify_lev, 0x170),
            "player_counts": {
                "lev_embedded_min": u32(verify_lev, 0x140),
                "lev_embedded_max": u32(verify_lev, 0x148),
                "lev_assignable": u32(verify_lev, 0x310),
                "lev_active": u32(verify_lev, 0x314),
            },
        },
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
