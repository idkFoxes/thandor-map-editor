#!/usr/bin/env python3
import argparse
import re
from pathlib import Path


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise SystemExit(f"FAIL {label}: missing {needle!r}")


def require_panel_guard(text: str, flag: str, method: str) -> None:
    # A scoped performance timer inside the guarded panel is not a behavior change.
    # Accept either the old direct call or a block containing that direct call.
    guard = rf"if\s*\(\s*{re.escape(flag)}\s*\)"
    call = rf"{re.escape(method)}\s*\(\s*\)\s*;"
    direct = rf"{guard}\s*{call}"
    block = rf"{guard}\s*\{{[^{{}}]*\b{call}[^{{}}]*\}}"
    if not re.search(rf"(?:{direct}|{block})", text):
        raise SystemExit(f"FAIL {flag} render guard: {method}() must be called directly inside its guard")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True)
    args = parser.parse_args()
    root = Path(args.source)
    cpp = ((root / "src/editor/editor_app.cpp").read_text(encoding="utf-8") +
           (root / "src/editor/editor_workspace_ui.cpp").read_text(encoding="utf-8"))
    hpp = (root / "src/editor/editor_app.hpp").read_text(encoding="utf-8")

    # The normal UI deliberately omits the old debug menu. Retained developer
    # rendering must remain off by default and guarded; logging is user-controlled.
    require(hpp, "bool show_retail_debug_overlay_{};", "diagnostics default off")
    require(cpp, "if(!show_retail_debug_overlay_)return;", "diagnostic render guard")
    require(cpp, 'ImGui::BeginMenu("Settings")', "settings menu")
    require(cpp, '"Create diagnostic logs"', "logging toggle")
    require(cpp, "set_logging_enabled(logging)", "live logging control")
    for label in (
        "Render counters", "World vectors", "Cursor / selection coordinates",
        "Memory / resource state", "Simulation clock",
    ):
        require(cpp, f'"{label}"', f"retained diagnostic category {label}")
    require(cpp, "FrontendDebugOverlay_RefreshCountersAndWorldCoordinates @ 0x00546190",
            "retail function provenance")
    require(cpp, "fourteen live UTF-16 slots", "14-slot retail overlay contract")
    require(cpp, "retail free-arena bytes: unavailable in Vulkan rewrite",
            "no fabricated retail allocator value")
    require(cpp, "retail texture-bind state changes: unavailable in Vulkan rewrite",
            "no fabricated retail texture-bind counter")
    require(cpp, "retail texture-device reloads: unavailable in Vulkan rewrite",
            "no fabricated retail texture-reload counter")
    require(cpp, "retail_debug_frame_accumulator_>=20U", "20-call retail counter cadence")

    require(hpp, "bool show_left_panel_{true};", "left panel default")
    require(hpp, "bool show_right_panel_{true};", "right panel default")
    require_panel_guard(cpp, "show_left_panel_", "draw_left_panel")
    require_panel_guard(cpp, "show_right_panel_", "draw_right_panel")
    require(cpp, 'ImGui::BeginMenu("Panels")', "panel view menu")
    require(cpp, '"Collapse assets"', "left inline collapse")
    require(cpp, '"Collapse inspector"', "right inline collapse")
    require(cpp, "float EditorApp::left_panel_width() const noexcept", "left dynamic width")
    require(cpp, "float EditorApp::right_panel_width() const noexcept", "right dynamic width")

    require(cpp, "return workspace_layout().assets.width", "left responsive width")
    require(cpp, "return workspace_layout().inspector.width", "right responsive width")
    require(cpp, "workspace_layout().canvas.contains(mouse_x,mouse_y)", "map-only input rectangle")
    require(cpp, "canvas.x+canvas.width*0.5F", "pointer capture uses map center")
    workspace = (root / "src/editor/editor_workspace_ui.cpp").read_text(encoding="utf-8")
    require(workspace, '"Assets >"', "left reopen control")
    require(workspace, '"< Inspector"', "right reopen control")
    require(workspace, "workspace_view_ray(camera_,x,y,workspace_layout().canvas)", "shared picking rectangle")

    print("PASS optional diagnostics, logging and collapsible panel source contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
