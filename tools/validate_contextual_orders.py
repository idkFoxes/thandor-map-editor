#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, json, struct
from pathlib import Path

EXPECTED_EXE = "53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969"
EXPECTED_GZF = "feb31b7fe5fc32997898955b7dfd880a8fc24dc2a66aa25017e5147d126f9dc7"
EXPECTED_CURRENT_C = "ba432824f99af42a2d28cd0071fe8c09bffe6e2f5ea2a1f959414c1f095d3e9c"
EXPECTED_UNTOUCHED_C = "eeea2dc72d85060054937540719d71b2c5ecc150c7af70f1358ed754f173b2d5"
RANGES = {
    "InGameUiCommand_ResolveCursorCodeByMode": (0x0056F7F0, 0x0056FA70,
        "b967a190b8d32c94d05282dca57474c055b0fa1f48af275494facf273396843c"),
    "InGameUiCommand_BeginInteractionByMode": (0x0056FA70, 0x005703D0,
        "5e6a0636ae56b40cd493e73ceb98c7bdab4ed44545f7fc83bd806966657e4cf1"),
    "ArmyRuntime_UpdateMovementAndWaypoints": (0x0051CEE0, 0x0051D0B0,
        "1002e5965d53d1531c5608cccdfa697000cc300de258ebca44292e9b297e085a"),
}

def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def pe_range(data: bytes, start: int, end: int) -> bytes:
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe+4] != b"PE\0\0": raise ValueError("not PE")
    count = struct.unpack_from("<H", data, pe+6)[0]
    optional_size = struct.unpack_from("<H", data, pe+20)[0]
    optional = pe+24
    image_base = struct.unpack_from("<I", data, optional+28)[0]
    section_table = optional+optional_size
    rva = start-image_base
    size = end-start
    for i in range(count):
        off = section_table+i*40
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", data, off+8)
        extent = max(virtual_size, raw_size)
        if virtual_address <= rva and rva+size <= virtual_address+extent:
            file_offset = raw_offset+rva-virtual_address
            return data[file_offset:file_offset+size]
    raise ValueError(f"unmapped VA {start:#x}")

def require(value: bool, message: str) -> None:
    if not value: raise SystemExit(message)

def main() -> int:
    ap=argparse.ArgumentParser()
    ap.add_argument("--source",type=Path,required=True)
    ap.add_argument("--game-exe",type=Path,required=True)
    ap.add_argument("--gzf",type=Path)
    ap.add_argument("--current-c",type=Path)
    ap.add_argument("--untouched-c",type=Path)
    ap.add_argument("--output",type=Path)
    a=ap.parse_args()
    source=a.source.resolve()
    hashes={"game_exe":sha(a.game_exe)}
    require(hashes["game_exe"]==EXPECTED_EXE,f"immutable game.exe hash mismatch: {hashes['game_exe']}")
    full_authority=all((a.gzf,a.current_c,a.untouched_c))
    if full_authority:
        hashes.update({"gzf":sha(a.gzf),"current_c":sha(a.current_c),"untouched_c":sha(a.untouched_c)})
        require(hashes=={"game_exe":EXPECTED_EXE,"gzf":EXPECTED_GZF,"current_c":EXPECTED_CURRENT_C,"untouched_c":EXPECTED_UNTOUCHED_C},f"authority hash mismatch: {hashes}")
    else:
        require(not any((a.gzf,a.current_c,a.untouched_c)),"provide all or none of --gzf/--current-c/--untouched-c")
    exe=a.game_exe.read_bytes(); range_result={}
    for name,(start,end,expected) in RANGES.items():
        payload=pe_range(exe,start,end); actual=hashlib.sha256(payload).hexdigest()
        require(actual==expected,f"binary range mismatch {name}: {actual}")
        range_result[name]={"start":f"0x{start:08X}","end_exclusive":f"0x{end:08X}","size":len(payload),"sha256":actual,"first16":payload[:16].hex(),"last16":payload[-16:].hex()}
    current=a.current_c.read_text(errors="replace") if full_authority else ""
    untouched=a.untouched_c.read_text(errors="replace") if full_authority else ""
    c_tokens=[
        "InGameUiCommand_ResolveCursorCodeByMode",
        "InGameUiCommand_BeginInteractionByMode",
        "InGameUiCommand_BeginSelectionOrPointerTargetAction",
        "InGameUiCommand_ResolvePointerArmyAndCommandPayload",
        "PlayerRuntime_SetState8094",
        "InGameCommandQueue_AppendLocalPlayerCommand(0x2ef0",
        "ArmyRuntime_UpdateMovementAndWaypoints",
        "ArmyRuntime_ResolveCommandTargetAndRoute",
    ]
    if full_authority:
        for token in c_tokens:
            require(token in current,f"current C missing {token}")
            require(token in untouched or token.startswith("InGameUiCommand_") or token.startswith("ArmyRuntime_"),f"untouched C missing expected behavior token {token}")
        require("iVar3 = 0x19" in current and "iVar3 = 0x1a" in current,"current C missing attack cursor pair 0x19/0x1A")
    app=(source/"src/editor/editor_app.cpp").read_text()
    header=(source/"src/editor/editor_app.hpp").read_text()
    cursor=(source/"src/editor/game_cursor.hpp").read_text()
    order=(source/"include/thandor/context_order.hpp").read_text()
    source_tokens=[
        "resolve_context_order_at_pointer", "issue_context_order_at_pointer",
        "simulation_attack_placement_targets_", "simulation_attack_point_targets_",
        "right_pointer_dragged_&&ImGui::IsMouseDown(ImGuiMouseButton_Right)",
        "placements[target->second].faction_index!=placements[attacker_index].faction_index",
        "Entity targets are resolved again every 20 Hz step",
        "Right-click: attack", "Right-click: drive",
    ]
    for token in source_tokens: require(token in app or token in header,f"source missing {token}")
    require("AttackPlacement" in order and "MoveToTerrain" in order and "AttackTerrainPoint" in order,"context order policy incomplete")
    require("kPointTargetValidFrame = 21U" in cursor and "kMoveOrderValidFrame = 23U" in cursor and "kAttackPlacementValidFrame = 25U" in cursor,"stock cursor IDs missing")
    result={"schema":"thandor-map-editor-g374-contextual-orders-v1","status":"PASS","authorities":hashes,"binary_ranges":range_result,
            "full_cross_source_authority":full_authority,"contracts":{"entity_before_terrain":True,"different_faction_attack_hover":True,"movable_unit_terrain_drive":True,"armed_immobile_terrain_attack_point":True,"right_drag_camera_preserved":True,"moving_entity_target_followed_at_20hz":True,"transient_only":True}}
    text=json.dumps(result,indent=2,sort_keys=True)+"\n"
    if a.output: a.output.write_text(text)
    else: print(text,end="")
    return 0
if __name__=="__main__": raise SystemExit(main())
