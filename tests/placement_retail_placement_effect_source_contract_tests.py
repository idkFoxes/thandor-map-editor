#!/usr/bin/env python3
import argparse
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"FAIL: {message}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    args = parser.parse_args()
    root = Path(args.source)
    header = (root / "include/thandor/simulation_lab.hpp").read_text(encoding="utf-8")
    sim_header = (root / "include/thandor/simulation.hpp").read_text(encoding="utf-8")
    sim = (root / "src/core/simulation_lab.cpp").read_text(encoding="utf-8")
    placement = (root / "src/core/retail_placement.cpp").read_text(encoding="utf-8")
    editor = (root / "src/editor/editor_app.cpp").read_text(encoding="utf-8")

    require("kProxyBuildDurationTicks" not in sim,
            "invented generic construction-duration proxy returned")
    require("placement_effect_ids_prepared" in header and
            "placement_effect_ids_by_faction" in header,
            "production snapshot lost technology-selected placement effects")
    require("fixed_atan2_angle16_compat" in sim_header,
            "exact fixed atan2 helper is no longer exposed for FLD angle reconstruction")
    require("retail_triangle0_normal_azimuth_turn16" in sim and
            "retail_placement_normal_angles(field,x,y,false)&0xFFFFU" in sim and
            all(neighbor in placement for neighbor in
                ("&f.at(x+1,y)", "&f.at(x,y+1)", "&f.at(x,y-1)",
                 "&f.at(x-1,y)", "&f.at(x-1,y+1)", "&f.at(x+1,y-1)")) and
            "fixed_atan2_angle16_compat(ny,nx)" in placement,
            "AI placement no longer reconstructs the retail six-neighbour triangle0 angle")
    require("struct ConstructionPlacement" in header and
            "std::uint32_t heading_turn16" in header and
            "entity.actor.body_yaw_turn16 = placement->heading_turn16" in sim,
            "AI placement no longer carries the validated creation heading into the runtime")
    require("best.heading_turn16=0U" in sim and
            "AiConstructionPlanner_TryPlaceStorageAtResourceSite" in sim,
            "ARM333 special-site creation lost its native zero-heading exception")
    require("product->placement_effect_ids_prepared" in sim and
            "product->placement_effect_ids_by_faction[faction]" in sim,
            "worker no longer selects faction technology root +0x190 effect")

    start = editor[editor.index("void EditorApp::start_simulation_lab()"):
                   editor.index("void EditorApp::clear_simulation_lab()")]
    require("product.placement_effect_ids_prepared=true" in start and
            "resolve_faction_technology_hierarchy(preset,mask)" in start and
            "root->simulation.construction_destruction_effect_id" in start,
            "editor snapshot no longer resolves per-faction root MDL +0x190")

    sync = editor[editor.index("void EditorApp::sync_selected_simulation_lab_run()"):
                  editor.index("void EditorApp::update_simulation_lab(")]
    effect = sync[sync.index("ConstructionPlacementEffect"):
                  sync.index("TerminalAttachmentEffect")]
    require("resolve_faction_technology_hierarchy(*preset,mask)" in effect,
            "placement effect playback lost technology-selected hierarchy context")
    require("resolve_placement_contact_pose" in effect and
            "terrain_contact_root_transform" in effect,
            "placement effect playback lost retail contact height/normal orientation")
    require("cached_sprite(root->sprite_path).placement_height_offset()" in effect,
            "placement effect playback lost root SPR placement-height offset")

    print("PASS: retail placement carries planner heading, preserves ARM333 zero heading, and uses the technology-selected +0x190 effect/contact pose")


if __name__ == "__main__":
    main()
