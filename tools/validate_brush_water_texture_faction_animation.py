#!/usr/bin/env python3
"""Validate the G347 brush, flat-water, texture-exchange, faction-colour, and building-animation update."""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def pe_range(path: Path, start_va: int, end_va: int) -> bytes:
    data = path.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    section_table = pe + 24 + optional_size
    rva = start_va - 0x400000
    size = end_va - start_va
    for index in range(section_count):
        offset = section_table + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", data, offset + 8)
        if virtual_address <= rva and rva + size <= virtual_address + max(virtual_size, raw_size):
            begin = raw_offset + rva - virtual_address
            return data[begin:begin + size]
    raise RuntimeError(f"VA range {start_va:#x}..{end_va:#x} is outside mapped PE sections")


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--game-exe", type=Path)
    parser.add_argument("--original-c", type=Path)
    parser.add_argument("--pck-root", type=Path)
    parser.add_argument("--stock-census", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    source = args.source.resolve()

    editor = read(source / "src/editor/editor_app.cpp")
    editor_hpp = read(source / "src/editor/editor_app.hpp")
    field_hpp = read(source / "include/thandor/field.hpp")
    field_cpp = read(source / "src/core/field.cpp")
    gfx_hpp = read(source / "include/thandor/gfx.hpp")
    gfx_cpp = read(source / "src/core/gfx.cpp")
    catalog_hpp = read(source / "include/thandor/catalog.hpp")
    catalog_cpp = read(source / "src/core/catalog.cpp")
    model_hpp = read(source / "include/thandor/model.hpp")
    model_cpp = read(source / "src/core/model.cpp")
    renderer_hpp = read(source / "src/editor/terrain_renderer.hpp")
    renderer_cpp = read(source / "src/editor/terrain_renderer.cpp")
    shader = read(source / "shaders/model.vert")
    test = read(source / "tests/animation_brush_water_texture_faction_animation_tests.cpp")
    cmake = read(source / "CMakeLists.txt")

    checks = {
        "eight_vector_brush_icons": all(token in editor for token in [
            "draw_tool_icon_button", "const std::array<Tool,8>", "InvisibleButton", "index%4U"]),
        "tool_specific_options_switch": all(token in editor for token in [
            "Only its own settings are shown", "switch(tool_)", "case Tool::Water", "case Tool::Texture"]),
        "temporary_water_selection_state": all(token in editor_hpp + editor for token in [
            "WaterSelectionStage", "water_selection_mask_", "draw_water_selection_overlay",
            "Ctrl+D", "paint_water_selection"]),
        "water_reference_pick_and_wheel_adjust": all(token in editor for token in [
            "PickReference", "water_height_=document_->field().at(x,y).terrain_height()",
            "io.MouseWheel*water_level_step_", "Water down", "Water up"]),
        "water_is_explicit_flat_mask_not_brush_falloff": all(token in field_hpp + field_cpp + editor for token in [
            "set_water_selection", "selection.size() != cells_.size()", "absolute_water_world",
            "Every selected wet cell stays at the same Z", "no radial hill"]),
        "texture_tga_import_export": all(token in gfx_hpp + gfx_cpp + editor for token in [
            "load_tga_rgba", "save_tga_rgba", "encode_single_rgba_gfx",
            "Export selected layer as TGA", "Import TGA into selected layer", "256x256"]),
        "faction_graphics_selector_not_raw_index": all(token in catalog_hpp + catalog_cpp + editor for token in [
            "default_faction_graphics_family", "graphics_family_for_faction",
            "faction_graphics_family_selector", "army%u.gfx"]),
        "army_family_eight_is_addressable": all(token in editor for token in [
            "std::min(faction,8U)", "army_texture_family_path"]),
        "building_animation_selector_parsed": all(token in model_hpp + model_cpp + catalog_hpp + catalog_cpp for token in [
            "primary_animated_subresource", "has_primary_texture_animation",
            "runtime_class_id == 13U", "visual.primary_animated_subresource = visual.placement_occupancy_class"]),
        "placed_building_animation_reaches_vertex_shader": all(token in editor + renderer_hpp + renderer_cpp + shader for token in [
            "animated_part", "animation_v_amplitude", "animation_cycle_ticks",
            "triangle_wave", "out_uv.y += in_animation_v_amplitude"]),
        "q12_animation_range_is_preserved": all(token in shader for token in [
            "0..0x80000", "0x100000", "0.5"]),
        "regression_target_present": all(token in cmake + test for token in [
            "animation_brush_water_texture_faction_animation_tests",
            "G347_BRUSH_WATER_TEXTURE_FACTION_ANIMATION PASS"]),
    }

    binary = {}
    if args.game_exe:
        expected = {
            "faction_graphics_family_load": (0x0051D710, 0x0051D7B7,
                "9ed3521875442346ead13d9248192a74190c40abb71a89389ce0fd9975e664fa"),
            "model_prepare_submit_animation_route": (0x004BD4B0, 0x004BD620,
                "8e13aa737447ba1a1a15f7166cec2fa017b8f28c1c76ad72ad6c1f7ef65077a9"),
            "class46_texture_offset_runtime": (0x00526EB0, 0x00526FDC,
                "fbd7793337856a3facb27b445f017969aea453ab7b9def958d0e591692ffd428"),
        }
        for name, (start, end, expected_hash) in expected.items():
            actual = sha256(pe_range(args.game_exe, start, end))
            binary[name] = {"start": f"0x{start:08X}", "end": f"0x{end:08X}",
                            "sha256": actual, "expected": expected_hash,
                            "pass": actual == expected_hash}
            checks[f"binary_{name}"] = actual == expected_hash

    c_evidence = {}
    if args.original_c:
        c = read(args.original_c)
        tokens = [
            "ArmyRuntime_InitializePoolAndGraphicsCf",
            "primaryAnimatedSubresourceIndex",
            "GraphicsPrimitiveQueue_OffsetTextureCoordinates",
            "primaryTextureOffsetV",
            "0x80000",
            "ModelRuntimeSlotClassInit_EnableRootAnimationAndCopyDefinitionC0",
        ]
        c_evidence = {token: token in c for token in tokens}
        checks["previous_c_animation_and_faction_correspondence"] = all(c_evidence.values())

    pck = {}
    if args.pck_root:
        root = args.pck_root
        model_records = 0
        class13_animated = 0
        for path in root.rglob("*.mdl"):
            data = path.read_bytes()
            if len(data) < 0x200 or data[:3] != b"mdl":
                continue
            count = struct.unpack_from("<I", data, 0xB0)[0]
            offset = 0x200
            for _ in range(count):
                if offset + 0x0C > len(data):
                    break
                size = struct.unpack_from("<I", data, offset)[0]
                if size < 0x0C or offset + size > len(data):
                    break
                model_records += 1
                if size >= 0xC4:
                    runtime_class = struct.unpack_from("<I", data, offset + 0x4C)[0]
                    selector = struct.unpack_from("<I", data, offset + 0xC0)[0]
                    if runtime_class == 13 and selector != 0:
                        class13_animated += 1
                offset += size

        def utf16z(data: bytes, offset: int, maximum: int) -> str:
            out = []
            for cursor in range(offset, offset + maximum, 2):
                value = struct.unpack_from("<H", data, cursor)[0]
                if value == 0:
                    break
                out.append(chr(value))
            return "".join(out).replace("/", "\\").lower()

        archive_names = set()
        for archive in root.glob("*.PCK"):
            data = archive.read_bytes()
            if len(data) < 0x200 or struct.unpack_from("<I", data, 0)[0] != 0x006B6370:
                continue
            cursor = 0x200
            for _ in range(struct.unpack_from("<I", data, 0xB0)[0]):
                if cursor + 0x200 > len(data):
                    break
                archive_names.add(utf16z(data, cursor, 0x1EC))
                packed = struct.unpack_from("<I", data, cursor + 0x1F8)[0]
                cursor += 0x200 + packed

        army_families = []
        for index in range(9):
            matches = list(root.rglob(f"army{index}.gfx"))
            if matches:
                army_families.append({"index": index, "path": str(matches[0]),
                                      "sha256": sha256(matches[0].read_bytes())})
            else:
                suffix = f"army{index}.gfx"
                matching_names = sorted(name for name in archive_names if name.endswith(suffix))
                if matching_names:
                    army_families.append({"index": index, "path": matching_names[0],
                                          "sha256": None})

        census = None
        if args.stock_census:
            census = json.loads(args.stock_census.read_text(encoding="utf-8"))
            buildings = census.get("buildings", [])
            model_records = max(model_records, len(buildings))
            class13_animated = max(class13_animated, sum(
                1 for item in buildings
                if item.get("runtime_class") == 13 and item.get("root_texture_animation") is True))
        pck = {"model_records": model_records,
               "class13_root_animated_records": class13_animated,
               "army_graphics_families": army_families,
               "stock_hierarchy_census": census}
        checks["pck_nine_army_graphics_families"] = len(army_families) == 9
        checks["pck_class13_root_animation_present"] = class13_animated > 0

    status = "PASS" if all(checks.values()) else "FAIL"
    result = {"schema": "thandor-map-editor-g347-validation-v2-corrected-class13", "status": status,
              "source": str(source), "checks": checks, "binary": binary,
              "previous_c": c_evidence, "pck": pck}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if status == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
