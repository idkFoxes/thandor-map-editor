#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

GAME_SHA256 = "53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def pe_va_to_offset(data: bytes, va: int) -> int:
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    sections = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    image_base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    rva = va - image_base
    table = pe + 24 + optional_size
    for index in range(sections):
        offset = table + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", data, offset + 8)
        if virtual_address <= rva < virtual_address + max(virtual_size, raw_size):
            return raw_offset + rva - virtual_address
    raise ValueError(f"VA {va:08X} is outside mapped sections")


def bytes_at(data: bytes, va: int, expected: bytes) -> bool:
    offset = pe_va_to_offset(data, va)
    return data[offset:offset + len(expected)] == expected


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--game-exe", type=Path, required=True)
    parser.add_argument("--current-c", type=Path, required=True)
    parser.add_argument("--untouched-c", type=Path, required=True)
    parser.add_argument("--gzf", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    source = args.source.resolve()
    editor_cpp = (source / "src/editor/editor_app.cpp").read_text(encoding="utf-8")
    editor_hpp = (source / "src/editor/editor_app.hpp").read_text(encoding="utf-8")
    cursor_hpp = (source / "src/editor/game_cursor.hpp").read_text(encoding="utf-8")
    test_cpp = (source / "tests/editor_game_cursor_mouse_camera_tests.cpp").read_text(encoding="utf-8")
    cmake = (source / "CMakeLists.txt").read_text(encoding="utf-8")
    gate = (source / "docs/G354_GAME_CURSOR_AND_BINARY_MOUSE_CAMERA_CONTROL_GATE.md").read_text(encoding="utf-8")
    current_c = args.current_c.read_text(encoding="utf-8", errors="replace")
    untouched_c = args.untouched_c.read_text(encoding="utf-8", errors="replace")
    game = args.game_exe.read_bytes()

    checks = {
        "immutable_game_sha256": sha256(args.game_exe) == GAME_SHA256,
        "camera_scale_constant_0x20": struct.unpack_from("<I", game, pe_va_to_offset(game, 0x0050BAE6))[0] == 0x20,
        "camera_scale_load": bytes_at(game, 0x0050C77B, bytes.fromhex("8B 15 E6 BA 50 00")),
        "camera_distance_multiply_and_q16_shift": (
            bytes_at(game, 0x0050C787, bytes.fromhex("0F AF 93 8C 00 00 00")) and
            bytes_at(game, 0x0050C78E, bytes.fromhex("C1 FA 10"))
        ),
        "pointer_delta_shift_0x40": bytes_at(game, 0x00570AEA, bytes.fromhex("C1 E0 06")),
        "cursor_record_stride_0x20": bytes_at(game, 0x00576C4D, bytes.fromhex("C1 E3 05")),
        "cursor_20ms_timer_divided_by_two": (
            bytes_at(game, 0x00576C50, bytes.fromhex("FF 0D FC 67 41 00")) and
            bytes_at(game, 0x00576C5E, bytes.fromhex("C7 05 FC 67 41 00 02 00 00 00"))
        ),
        "current_c_cursor_assets_and_timer": all(token in current_c for token in [
            "u_engine_mouse_gfx", "u_engine_mouse_dat", "extraout_ECX_00 >> 5",
            "TimerSystem_RegisterPeriodic(0x14,GraphicsCursor_AdvanceAnimationAndRefreshPrimaryTimer)",
            "g_CursorFrameRecords[g_CursorFrameIndex].idleSubresourceIndex + 1"
        ]),
        "untouched_c_cursor_assets_and_timer": all(token in untouched_c for token in [
            "u_engine_mouse_gfx", "u_engine_mouse_dat", "extraout_ECX_00 >> 5",
            "TimerSystem_RegisterPeriodic(0x14,GraphicsCursor_AdvanceAnimationAndRefreshPrimaryTimer)"
        ]),
        "current_c_button_offsets_and_masks": all(token in current_c for token in [
            "dwOfs == 0xc", "dwOfs != 0xd", "dwOfs == 0xe", "g_MouseButtonMask & 0xfffffffb",
            "g_CursorButtonState & 7"
        ]),
        "current_and_untouched_camera_correspondence": all(token in current_c and token in untouched_c for token in [
            "WorldRuntime_TranslateCameraByScreenDelta", "_DAT_0050bae6 * (worldRuntime->motion).targetDistanceQ12",
            "_g_InGameCommandPreviewHeading16 + iVar2 * 0x40",
            "_g_InGamePlacementHeading16 + iVar2 * 0x40"
        ]),
        "cursor_parser_preserves_alpha": all(token in cursor_hpp for token in [
            "kRecordSize = 0x20U", "kAnimationStepSeconds = 0.040",
            "gfx.decode_logical(index, false)", "first <= last", "last < result.images_.size()"
        ]) and "gfx.decode_logical(index, true)" not in cursor_hpp,
        "stock_cursor_frames": all(token in cursor_hpp + test_cpp for token in [
            "kDefaultFrame = 0U", "kCameraMoveFrame = 1U", "kCameraTurnFrame = 37U",
            "frame_count() == 55U", "subresource_count() == 303U",
            "turn.hotspot_x == 8", "turn.idle_first == 42U"
        ]),
        "right_drag_moves_camera": all(token in editor_cpp for token in [
            "right_pointer_dragged_&&ImGui::IsMouseDown(ImGuiMouseButton_Right)",
            "game_cursor_frame_=GameCursorAsset::kCameraMoveFrame",
            "game_camera_pan_world_units_per_pixel(camera_.distance)",
            "camera_.move_planar(-io.MouseDelta.x*scale,io.MouseDelta.y*scale)"
        ]),
        "left_select_drag_turns_camera": all(token in editor_cpp for token in [
            "tool_==Tool::Select&&ImGui::IsMouseDragging(ImGuiMouseButton_Left,4.0F)",
            "game_cursor_frame_=GameCursorAsset::kCameraTurnFrame",
            "game_camera_turn_radians_per_pixel()",
            "camera_.orbit(-io.MouseDelta.x*turn_scale,io.MouseDelta.y*turn_scale)"
        ]),
        "legacy_right_orbit_absent": "camera_.orbit(-io.MouseDelta.x*0.008F" not in editor_cpp,
        "game_cursor_drawn_last_with_hotspot": all(token in editor_cpp for token in [
            "draw_drag_drop_preview();draw_game_cursor();ImGui::Render()",
            "ImGui::SetMouseCursor(ImGuiMouseCursor_None)",
            "frame.hotspot_x", "frame.hotspot_y", "GetForegroundDrawList", "PushClipRect"
        ]),
        "brush_and_gizmo_priority_preserved": all(token in editor_cpp for token in [
            "if(gizmo_drag_.handle!=GizmoHandle::None)",
            "if(gizmo_hovered_!=GizmoHandle::None&&ImGui::IsMouseClicked(ImGuiMouseButton_Left))",
            "if(ImGui::IsMouseDown(ImGuiMouseButton_Left)&&stroke_active_)"
        ]),
        "regression_target": all(token in cmake + test_cpp for token in [
            "editor_game_cursor_mouse_camera_tests",
            "G354 game cursor and mouse camera controls PASS",
            "transparent cursor pixels must remain transparent"
        ]),
        "task_gate_present": all(token in gate for token in [
            "zero alpha is opaque", "Right-drag", "Left-drag", "0x40", "cameraDistance / 2048",
            "Research-only output fails this gate"
        ]),
        "gzf_comparison_present": args.gzf.is_file() and args.gzf.stat().st_size > 0,
    }
    for name, passed in checks.items():
        require(passed, f"G354 validation failed: {name}")

    result = {
        "schema": "thandor-map-editor-g354-game-cursor-mouse-camera-v1",
        "status": "PASS",
        "checks": checks,
        "hashes": {
            "game.exe": sha256(args.game_exe),
            "current_c": sha256(args.current_c),
            "untouched_c": sha256(args.untouched_c),
            "supplied_gzf": sha256(args.gzf),
        },
        "notes": {
            "gzf_role": "comparison evidence only; supplied database predates the accepted V281 authority",
            "camera_pan_scale": "distance / 2048 world units per pointer pixel",
            "camera_turn_scale_radians_per_pixel": 64.0 * 2.0 * 3.141592653589793 / 65536.0,
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
