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
    source = Path(args.source)
    editor = (source / "src/editor/editor_app.cpp").read_text(encoding="utf-8")

    renderer_start = editor.index("const auto append_simulation_lab_synthetic_model")
    renderer_end = editor.index("constexpr std::array<uint32_t,8> kRunGhostRgb", renderer_start)
    renderer = editor[renderer_start:renderer_end]

    require("catalog_.army_preset(entity.army_asset_id)" in renderer,
            "selected synthetic construction no longer resolves the real ARM catalog entry")
    require("resolve_faction_technology_hierarchy" in renderer,
            "selected synthetic construction no longer uses the faction technology MDL hierarchy")
    require("placement_contact_pose" in renderer and "terrain_contact_root_transform" in renderer,
            "selected synthetic construction lost normal terrain-contact placement")
    require("texture_family(texture_graphics_family)" in renderer,
            "selected synthetic construction lost faction graphics-family rendering")
    require("object_layer(texture_runtime_faction,texture_graphics_family,subresource)" in renderer,
            "selected synthetic construction lost runtime-faction-aware texture binding")
    require("constexpr float build_scale=1.0F" in renderer,
            "selected construction renderer no longer preserves immediate full retail scale")
    require("0.18F+0.82F" not in renderer and "grow the stock model vertically" not in renderer,
            "invented vertical construction-growth visualization returned to selected rendering")
    require("append_ghost_body" not in renderer,
            "real selected-run construction renderer unexpectedly calls the diagnostic ghost path")

    loop_start = editor.index("for(std::size_t run_index=0;run_index<runs.size();++run_index)", renderer_end)
    loop_end = editor.index("// Move each simulation replacement", loop_start)
    loop = editor[loop_start:loop_end]
    selected_start = loop.index("if(selected){")
    selected_end = loop.index("if(!simulation_lab_show_all_runs_)continue;", selected_start)
    selected = loop[selected_start:selected_end]
    require("append_simulation_lab_synthetic_model" in selected,
            "selected run does not dispatch synthetic construction to the real model renderer")
    require("bool rendered=append_simulation_lab_synthetic_model" in selected and
            "rendered=append_runtime_transient_model" in selected and
            "if(!rendered)" in selected and "append_ghost_body" in selected,
            "selected construction lost its real-model-first / diagnostic-last visibility chain")
    nonselected = loop[selected_end:]
    require("append_ghost_body" in nonselected,
            "non-selected opt-in comparison runs lost their lightweight ghost renderer")

    preload_start = editor.index("const auto preload_simulation_lab_preset_textures")
    preload_end = editor.index("struct ShadowAtlasTile", preload_start)
    preload = editor[preload_start:preload_end]
    require("simulation_lab_active()" in preload,
            "construction model texture preload is no longer gated by an active Lab")
    require("for_each_selected_lab_synthetic_asset(runs,selected" in preload and
            "catalog_.army_preset(entity.army_asset_id)" in preload,
            "construction texture preload no longer follows only selected Lab synthetic assets")
    require("run.faction_technology_masks" in preload,
            "construction texture preload lost the selected run's live research variants")

    sync_start = editor.index("void EditorApp::sync_selected_simulation_lab_run()")
    sync_end = editor.index("void EditorApp::update_simulation_lab(", sync_start)
    sync = editor[sync_start:sync_end]
    require("simulation_lab_render_asset_signature_" in sync and
            "simulation_static_replacement_dirty_=true" in sync and "entity.synthetic" in sync,
            "new synthetic construction no longer triggers one static texture/object residency rebuild")

    start = editor[editor.index("void EditorApp::start_simulation_lab()"):
                   editor.index("void EditorApp::clear_simulation_lab()")]
    require("simulation_static_replacement_dirty_=true" in start,
            "starting the Lab no longer forces the static texture/object rebuild needed for synthetic models")

    print("PASS: V541 selected construction uses real ARM/MDL rendering and keeps ghosts non-selected only")


if __name__ == "__main__":
    main()
