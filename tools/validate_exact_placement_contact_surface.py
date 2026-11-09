#!/usr/bin/env python3
"""Validate G384 binary/C/source placement-contact correspondence."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


def require(value: bool, message: str) -> None:
    if not value:
        raise SystemExit(message)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def function_body(text: str, name: str) -> str:
    match = re.search(rf"(?m)^[^\n]*\b{re.escape(name)}\s*\n?\s*\(", text)
    if not match:
        match = re.search(rf"(?m)^[^\n]*\b{re.escape(name)}\s*\(", text)
    require(match is not None, f"missing function {name}")
    brace = text.find("{", match.start())
    require(brace >= 0, f"missing body for {name}")
    depth = 0
    for cursor in range(brace, len(text)):
        if text[cursor] == "{":
            depth += 1
        elif text[cursor] == "}":
            depth -= 1
            if depth == 0:
                return text[match.start():cursor + 1]
    raise SystemExit(f"unterminated body for {name}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--semantic-c", type=Path, required=True)
    parser.add_argument("--previous-semantic-c", type=Path, required=True)
    parser.add_argument("--untouched-c", type=Path, required=True)
    parser.add_argument("--stock-census", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    source = args.source.resolve()
    binary = args.binary.read_bytes()
    semantic = args.semantic_c.read_text(encoding="utf-8", errors="replace")
    previous = args.previous_semantic_c.read_text(encoding="utf-8", errors="replace")
    untouched = args.untouched_c.read_text(encoding="utf-8", errors="replace")
    census = json.loads(args.stock_census.read_text(encoding="utf-8"))

    expected_binary_sha = "53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969"
    require(sha256(args.binary) == expected_binary_sha, "immutable game.exe hash mismatch")

    callbacks = {
        "ArmyPlacementContact_ApplyTerrainHeight": {
            "address": 0x004BE7F0,
            "hash64": "b9e03e569fa7de62f666352f716e411dcfa426703ff2539681ad0838c61378a6",
            "tokens": ["FieldGrid_InterpolateTerrainHeight", "placementHeightOffsetQ12"],
        },
        "ArmyPlacementContact_ApplyWaterSurfaceHeight": {
            "address": 0x004BE860,
            "hash64": "7c8975a87bbe2e6352ba4b29a082d8a080920b1bae63a69818d2210fdde58593",
            "tokens": ["FieldGrid_InterpolateWaterSurfaceHeight"],
            "forbidden": ["placementHeightOffsetQ12"],
        },
        "ArmyPlacementContact_ApplyTerrainHeightAndNormal": {
            "address": 0x004BE8C0,
            "hash64": "2b04ca2317a4d140d8491657f9d8bff9b25670aff9e2088ff8bbf2fac624e2f5",
            "tokens": ["FieldGrid_InterpolateTerrainHeightAndNormal", "placementHeightOffsetQ12",
                       "worldRotationAngle0", "worldRotationAngle1"],
        },
        "ArmyPlacementContact_ApplyTopSurfaceHeight": {
            "address": 0x004BE930,
            "hash64": "5003c5dc31ac6912604480f995cf22d6a2748fdd421c145f77fd5499086c02bd",
            "tokens": ["FieldGrid_InterpolateTopSurfaceHeight"],
            "forbidden": ["placementHeightOffsetQ12"],
        },
        "ArmyPlacementContact_InitializeArticulatedSuspension": {
            "address": 0x004BE990,
            "hash64": "0918b293bf92accd340ad2148180ee734c06864d44104542dcf19ca248e8c27c",
            "tokens": ["ArmyArticulatedRuntime_InitializeTerrainContactGeometry",
                       "ArmyArticulatedRuntime_UpdateSuspensionHierarchy"],
        },
    }
    callback_results = {}
    for name, spec in callbacks.items():
        offset = spec["address"] - 0x00400000
        digest = hashlib.sha256(binary[offset:offset + 64]).hexdigest()
        require(digest == spec["hash64"], f"binary anchor mismatch for {name}")
        body = function_body(semantic, name)
        previous_body = function_body(previous, name)
        require(all(token in body for token in spec["tokens"]),
                f"semantic body contract mismatch for {name}")
        require(all(token not in body for token in spec.get("forbidden", [])),
                f"semantic body overstates placement offset for {name}")
        callback_results[name] = {
            "address": f"0x{spec['address']:08X}",
            "binary_sha256_64": digest,
            "semantic_sha256": hashlib.sha256(body.encode()).hexdigest(),
            "previous_semantic_sha256": hashlib.sha256(previous_body.encode()).hexdigest(),
        }

    table_offset = 0x000BD890
    table = list(struct.unpack_from("<5I", binary, table_offset))
    expected_table = [0x004BE7F0, 0x004BE860, 0x004BE8C0, 0x004BE990, 0x004BE930]
    require(table == expected_table, "placement-contact callback table order mismatch")

    struct_tokens = [
        "localBoundsX0Q12", "localBoundsX1Q12", "localBoundsY0Q12", "localBoundsY1Q12",
        "localBoundsZ0Q12", "localBoundsZ1Q12", "boundingRadiusQ12",
        "placementHeightOffsetQ12",
    ]
    require(all(token in semantic for token in struct_tokens),
            "latest semantic export lacks recovered SPR model-resource fields")
    require("placementHeightOffsetQ12" not in untouched or "+ 0xdc" in untouched.lower(),
            "untouched C does not preserve the raw placement-offset evidence")

    files = {
        "model_hpp": source / "include/thandor/model.hpp",
        "field_hpp": source / "include/thandor/field.hpp",
        "model_cpp": source / "src/core/model.cpp",
        "field_cpp": source / "src/core/field.cpp",
        "transform_hpp": source / "src/editor/model_transform.hpp",
        "editor_cpp": source / "src/editor/editor_app.cpp",
        "test": source / "tests/placement_exact_placement_contact_surface_bounds_tests.cpp",
    }
    texts = {key: path.read_text(encoding="utf-8") for key, path in files.items()}
    source_contracts = {
        "spr_bounds_and_offset_parsed": all(token in texts["model_cpp"] for token in
            ["0xC0U", "0xCCU", "0xD8U", "0xDCU", "placement_height_offset_"]),
        "surface_interpolation": all(token in texts["field_cpp"] for token in
            ["sample_water_surface_height", "sample_top_surface_height", "std::max(water_delta, 0.0F)"]),
        "callback_kind_resolution": all(token in texts["transform_hpp"] for token in
            ["resolve_placement_contact_pose", "case 0:", "case 1:", "case 2:", "case 3:", "case 4:"]),
        "normal_aligned_root": "terrain_contact_root_transform" in texts["transform_hpp"],
        "static_and_dynamic_use": texts["editor_cpp"].count("placement_contact_pose") >= 4 and
                                  texts["editor_cpp"].count("terrain_contact_root_transform") >= 3,
        "recursive_header_bounds": "has_valid_header_bounds" in texts["editor_cpp"] and
                                   "local_bounds_min" in texts["editor_cpp"],
        "focused_test": "G384_EXACT_PLACEMENT_CONTACT_SURFACE_BOUNDS PASS" in texts["test"],
    }
    require(all(source_contracts.values()), "G384 source correspondence contract failed")

    rows = census.get("rows", [])
    counts = census.get("contact_kind_counts", {})
    require(len(rows) == 213, "stock direct ARM-root census changed")
    require(counts == {"0": 110, "1": 3, "2": 100}, "stock contact-kind census changed")
    nonzero = sum(int(row.get("placement_height_q12", 0)) != 0 for row in rows)
    require(nonzero == 79, "stock nonzero SPR placement-offset census changed")
    kind1_assets = sorted({int(row["asset_id"]) for row in rows if row.get("contact_kind") == 1})
    require(kind1_assets == [305, 391, 853], "stock water-contact asset set changed")
    shot_assets = {int(row["asset_id"]): row for row in rows if int(row["asset_id"]) in (351, 352, 360)}
    require(set(shot_assets) == {351, 352, 360}, "stock shot assets missing from direct census")
    require(all(row["contact_kind"] == 0 and row["placement_height_q12"] == 0
                for row in shot_assets.values()),
            "new placement metadata unexpectedly changes assets 351/352/360")

    output = {
        "schema": "thandor-g384-exact-placement-contact-surface-bounds-v1",
        "status": "PASS",
        "inputs": {
            "binary_sha256": sha256(args.binary),
            "semantic_c_sha256": sha256(args.semantic_c),
            "previous_semantic_c_sha256": sha256(args.previous_semantic_c),
            "untouched_c_sha256": sha256(args.untouched_c),
            "gzf_note": "Supplied GZF is older/unpaired and was used only as historical naming evidence.",
        },
        "binary_callback_table": [f"0x{value:08X}" for value in table],
        "callbacks": callback_results,
        "semantic_struct_fields": struct_tokens,
        "source_contracts": source_contracts,
        "stock_direct_arm_root_census": {
            "resolved_rows": len(rows),
            "contact_kind_counts": counts,
            "nonzero_placement_height_offsets": nonzero,
            "water_contact_assets": kind1_assets,
            "shot_assets_351_352_360_unchanged_by_new_metadata": True,
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(output, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
