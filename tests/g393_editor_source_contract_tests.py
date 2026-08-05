#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import subprocess
import tempfile
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    source = args.source.resolve()

    build = (source / "build_windows_from_linux.sh").read_text(encoding="utf-8")
    helper = source / "tools/clean_build_directory_preserving_artifacts.sh"
    helper_text = helper.read_text(encoding="utf-8")
    app = (source / "src/editor/editor_app.cpp").read_text(encoding="utf-8")
    app_header = (source / "src/editor/editor_app.hpp").read_text(encoding="utf-8")
    catalog_header = (source / "include/thandor/catalog.hpp").read_text(encoding="utf-8")
    catalog = (source / "src/core/catalog.cpp").read_text(encoding="utf-8")
    manifest = (source / "src/core/map_document.cpp").read_text(encoding="utf-8")
    vertex = (source / "shaders/model.vert").read_text(encoding="utf-8")
    fragment = (source / "shaders/model.frag").read_text(encoding="utf-8")

    require("clean_build_directory_preserving_artifacts.sh" in build,
            "Windows build does not use the artifact-preserving cleaner")
    require("-iname '*.exe' -o -iname '*.pck'" in helper_text,
            "artifact cleaner does not preserve case-insensitive EXE/PCK files")
    require('rm -rf -- "${build_dir}"' in helper_text,
            "artifact cleaner no longer removes stale CMake/object state")

    with tempfile.TemporaryDirectory(prefix="thandor-g393-clean-") as temporary:
        build_dir = Path(temporary) / "build"
        (build_dir / "nested").mkdir(parents=True)
        (build_dir / "CMakeFiles").mkdir()
        (build_dir / "nested" / "keep.EXE").write_bytes(b"exe-authority")
        (build_dir / "nested" / "keep.pck").write_bytes(b"pck-authority")
        (build_dir / "CMakeFiles" / "stale.o").write_bytes(b"stale")
        (build_dir / "stale.txt").write_text("stale", encoding="utf-8")
        result = subprocess.run([str(helper), str(build_dir)], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        require(result.returncode == 0, "artifact cleaner failed:\n" + result.stdout)
        require(result.stdout.strip() == "2", "artifact cleaner reported wrong preserve count")
        require((build_dir / "nested" / "keep.EXE").read_bytes() == b"exe-authority",
                "nested EXE was not preserved byte-for-byte")
        require((build_dir / "nested" / "keep.pck").read_bytes() == b"pck-authority",
                "nested PCK was not preserved byte-for-byte")
        require(not (build_dir / "CMakeFiles").exists() and
                not (build_dir / "stale.txt").exists(),
                "stale non-game build content survived the clean")

    require("technology_level_previews.size() > 1U" in app and
            "Select technology level##placement" in app and
            "Level \"+std::to_string(preview.level_index+1U)" in app and
            " · STR: " in app,
            "drag/drop technology-level context menu is incomplete")
    popup_start = app.index("void EditorApp::draw_technology_placement_popup()")
    popup_end = app.index("void EditorApp::add_asset_placement", popup_start)
    popup_source = app[popup_start:popup_end]
    require("technology_effect_title" in popup_source and
            "BeginPopup(kPopup" in popup_source and "BeginPopupModal" not in popup_source,
            "technology selection is not a context popup with STR effect text")
    require("placement_technology_level." in manifest and
            "placement_technology_effect." in manifest and
            "retail LEV stores no technology level" in app,
            "editor-only technology selection persistence/export boundary is missing")
    require("topology_matches_base" in catalog,
            "technology-level choices are not gated by hierarchy topology")

    require("normalize_model_texture_offset_u" in app,
            "tank renderer does not consume the fixed-coordinate track offset helper")
    track_section = app[app.index("normalize_model_texture_offset_u") - 1000:
                        app.index("normalize_model_texture_offset_u") + 1500]
    require("parent.root" not in track_section,
            "tank texture scrolling is still incorrectly restricted to the root runtime node")

    require("has_continuous_radar_subnode_animation" in catalog_header and
            "!continuous_radar_placements_.empty()" in app and
            "runtime_class_uses_continuous_radar_subnode_animation" in app and
            "model_runtime_child_yaw_step_turn16" in app,
            "linked class-10 radar animation route is incomplete")
    require("simulation_model_node_is_weapon_aim_axis" in app,
            "ordinary weapon axes are not protected from ambient authored rotation")
    require("& 31u" in vertex and "in_static_animation_category & 16u" in fragment,
            "continuous-radar static replacement category is not wired through the shaders")

    require("Selected faction" in app and "All active factions" in app and
            "build_radial_fog_preview_visibility" in app,
            "faction-aware fog preview UI or visibility mask is missing")
    require("LEV/FLD do not serialize explored fog history" in app_header and
            "current radial visibility" in app,
            "fog preview overstates persistent explored-map support")

    print("G393 editor source/build contracts PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
