#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path

EXPECTED = {
    "game_exe": "53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969",
    "gzf": "feb31b7fe5fc32997898955b7dfd880a8fc24dc2a66aa25017e5147d126f9dc7",
    "current_c": "bb03d532fb1b89b22cdca18ef091ca3979fd5102387da765bc9ea8c882dbdf9b",
    "untouched_c": "eeea2dc72d85060054937540719d71b2c5ecc150c7af70f1358ed754f173b2d5",
}

RANGES = {
    "ModelRender_PrepareProjectedVertex": (
        0x004BD4B0, 0x004BD620,
        "8e13aa737447ba1a1a15f7166cec2fa017b8f28c1c76ad72ad6c1f7ef65077a9"),
    "ArmyRuntimeWeapon_UpdateTargetAimAndFireAttachments": (
        0x00523980, 0x00523CA0,
        None),
    "ShotRuntimePool_CreateProjectileFromDefinitionCf": (
        0x0052BDB0, 0x0052C1A0,
        "b3bfb90b496811c44b51a532cac4be6faff9fdf5fcc15164a2110dd807dbd56b"),
    "ShotModelRuntimeMaintenance_UpdateProjectileMotionCollisionAndEffects": (
        0x0052C230, 0x0052CEE0,
        "c12cf635c509003f12f00b6b46cbbdeb58e17dcdfd2acec58894166a4fc36d8d"),
}


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def pe_slice(data: bytes, start_va: int, end_va: int) -> bytes:
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    require(data[pe:pe + 4] == b"PE\0\0", "game.exe is not a PE image")
    count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    image_base = struct.unpack_from("<I", data, optional + 28)[0]
    rva = start_va - image_base
    size = end_va - start_va
    table = optional + optional_size
    for index in range(count):
        off = table + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", data, off + 8)
        if virtual_address <= rva and rva + size <= virtual_address + max(virtual_size, raw_size):
            start = raw_offset + rva - virtual_address
            return data[start:start + size]
    raise SystemExit(f"unmapped PE range {start_va:#x}-{end_va:#x}")


def body(text: str, name: str) -> str:
    match_start = None
    match_end = None
    for match in re.finditer(re.escape(name), text):
        line_start = text.rfind("\n", 0, match.start()) + 1
        if line_start >= len(text) or text[line_start].isspace() or text.startswith("//", line_start):
            continue
        cursor = match.end()
        while cursor < len(text) and text[cursor].isspace():
            cursor += 1
        if cursor < len(text) and text[cursor] == "(":
            match_start = line_start
            match_end = cursor
            break
    require(match_start is not None and match_end is not None, f"C export missing {name}")
    opening = text.find("{", match_end)
    require(opening >= 0, f"C export missing body for {name}")
    depth = 0
    for pos in range(opening, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[match_start:pos + 1]
    raise SystemExit(f"unterminated C body for {name}")


def require_all(text: str, tokens: list[str], scope: str) -> dict[str, bool]:
    checks = {token: token in text for token in tokens}
    missing = [token for token, present in checks.items() if not present]
    require(not missing, f"{scope} missing: {missing}")
    return checks


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--game-exe", type=Path, required=True)
    parser.add_argument("--gzf", type=Path, required=True)
    parser.add_argument("--current-c", type=Path, required=True)
    parser.add_argument("--untouched-c", type=Path, required=True)
    parser.add_argument("--previous-source", type=Path, required=True)
    parser.add_argument("--game-root", type=Path)
    parser.add_argument("--stock-test", type=Path)
    parser.add_argument("--screenshot", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    source = args.source.resolve()
    previous = args.previous_source.resolve()
    authorities = {
        "game_exe": sha256(args.game_exe),
        "gzf": sha256(args.gzf),
        "current_c": sha256(args.current_c),
        "untouched_c": sha256(args.untouched_c),
    }
    require(authorities == EXPECTED, f"authority hash mismatch: {authorities}")

    executable = args.game_exe.read_bytes()
    ranges: dict[str, object] = {}
    for name, (start, end, expected) in RANGES.items():
        payload = pe_slice(executable, start, end)
        actual = hashlib.sha256(payload).hexdigest()
        if expected is not None:
            require(actual == expected, f"binary range mismatch for {name}: {actual}")
        ranges[name] = {
            "start_va": f"0x{start:08X}",
            "end_va_exclusive": f"0x{end:08X}",
            "bytes": len(payload),
            "sha256": actual,
        }

    renderer = pe_slice(executable, 0x004BD4B0, 0x004BD520)
    creation = pe_slice(executable, 0x0052BE70, 0x0052BEA4)
    class9 = pe_slice(executable, 0x00523980, 0x00523C80)
    opcode_contracts = {
        "renderer_loads_node_bc_and_halves": bytes.fromhex("8b97bc000000") in renderer and bytes.fromhex("c1ea01") in renderer,
        "renderer_negative_z_unchanged": bytes.fromhex("83be0800000000 7c0e".replace(" ", "")) in renderer,
        "renderer_zero_z_half_positive_z_full": all(pattern in renderer for pattern in [
            bytes.fromhex("7406"), bytes.fromhex("019608000000")]),
        "creation_stores_yaw_pitch_yaw": bytes.fromhex(
            "89870c000000899710000000898714000000") in creation,
        "class9_independent_restore_bits": all(pattern in class9 for pattern in [
            bytes.fromhex("838f4400000001"), bytes.fromhex("838f4400000002"),
            bytes.fromhex("838f4400000004")]),
        "class9_cumulative_prefix_clear": bytes.fromhex(
            "b8feffffff8bca".replace("8bca", "")) in class9 and
            bytes.fromhex("d3e0218744000000") in class9,
    }
    for name, passed in opcode_contracts.items():
        require(passed, f"binary opcode contract failed: {name}")

    current = args.current_c.read_text(encoding="utf-8", errors="replace")
    untouched = args.untouched_c.read_text(encoding="utf-8", errors="replace")
    c_bodies = {
        name: body(current, name) for name in [
            "ModelRender_PrepareProjectedVertex",
            "ArmyRuntimeWeapon_UpdateTargetAimAndFireAttachments",
            "ShotRuntimePool_CreateProjectileFromDefinitionCf",
            "ShotModelRuntimeMaintenance_UpdateProjectileMotionCollisionAndEffects",
        ]
    }
    c_contracts = {
        "renderer_half_full_local_z": all(token in c_bodies["ModelRender_PrepareProjectedVertex"] for token in [
            "*(uint *)(modelNode + 0xbc) >> 1", "if (-1 < vertex->z)",
            "if (vertex->z != 0)", "vertex->z = vertex->z + uVar3"]),
        "creation_yaw_pitch_yaw": all(token in c_bodies["ShotRuntimePool_CreateProjectileFromDefinitionCf"] for token in [
            "worldRotationAngle0 = (int)uVar26", "worldRotationAngle1 = (int)((ulonglong)uVar26 >> 0x20)",
            "worldRotationAngle2 = (dword)uVar26"]),
        "mode0_range_and_nearest_hit": all(token in c_bodies["ShotModelRuntimeMaintenance_UpdateProjectileMotionCollisionAndEffects"] for token in [
            "rangeScaleD0Q12 * pSVar19->launchSpeedQ12", "modelNode->renderDepthBiasOrState = (int)pSVar19",
            "ModelRuntime_RaycastCandidateListNearestCf", "FieldGrid_RaycastTerrainSurfaceDistanceCf",
            "FieldGrid_RaycastSecondarySurfaceDistanceCf"]),
        "class9_prefix_clear": "*pMVar2 = *pMVar2 & -2 <<" in c_bodies["ArmyRuntimeWeapon_UpdateTargetAimAndFireAttachments"],
        "class9_independent_timer_restore": all(token in c_bodies["ArmyRuntimeWeapon_UpdateTargetAimAndFireAttachments"] for token in [
            "*pMVar2 = *pMVar2 | 1", "*pMVar2 = *pMVar2 | 2", "*pMVar2 = *pMVar2 | 4"]),
        "untouched_parallel_evidence": all(token in untouched for token in [
            "ModelRender_PrepareProjectedVertex", "ArmyRuntimeWeapon_UpdateTargetAimAndFireAttachments",
            "ShotRuntimePool_CreateProjectileFromDefinitionCf"]),
    }
    for name, passed in c_contracts.items():
        require(passed, f"C correspondence contract failed: {name}")

    files = {
        "editor": source / "src/editor/editor_app.cpp",
        "transform": source / "src/editor/model_transform.hpp",
        "simulation": source / "src/core/simulation.cpp",
        "simulation_hpp": source / "include/thandor/simulation.hpp",
        "test": source / "tests/combat_shot_axis_beam_class9_reload_tests.cpp",
        "cmake": source / "CMakeLists.txt",
    }
    source_text = {name: path.read_text(encoding="utf-8") for name, path in files.items()}
    source_contracts = {
        "authored_local_z_basis": all(token in source_text["transform"] + source_text["editor"] for token in [
            "shot_model_transform", "forward axis is therefore local +Z", "projectile_model_pose"]),
        "mode0_launch_anchor_precedes_end_clamp": source_text["simulation"].find("if(shot.trajectory_mode==0)") <
            source_text["simulation"].find("if(age>=projectile.travel_ticks)return projectile.target_position"),
        "mode0_half_full_stretch": all(token in source_text["simulation"] + source_text["editor"] for token in [
            "simulation_mode0_beam_length_world", "simulation_mode0_stretch_local_position",
            "mode0_beam_length"]),
        "mode0_lifetime_overlaps_impact": "std::max(projectile.lifetime_ticks,impact_end)" in source_text["simulation"],
        "class9_binary_exact_prefix_clear": "0xFFFFFFFEU<<selector" in source_text["simulation"],
        "class9_independent_restore_retained": "state.launch_mesh_group_mask|=1U<<selector" in source_text["simulation"],
        "g379_test_registered": all(token in source_text["cmake"] for token in [
            "combat_shot_axis_beam_class9_reload_tests", "tests/combat_shot_axis_beam_class9_reload_tests.cpp"]),
        "stock_351_352_and_full_attack_census": all(token in source_text["test"] for token in [
            "catalog.army_preset(351U)", "catalog.army_preset(352U)", "attack_placements != 125U",
            "weapon_profiles != 160U", "shot_ids.size() != 26U",
            "const std::map<uint32_t, std::size_t> expected_racks{{1U,23U},{2U,23U},{4U,23U}}"]),
    }
    for name, passed in source_contracts.items():
        require(passed, f"source contract failed: {name}")

    rejected = {
        "local_x_projectile_direction": "if(shot->trajectory_mode==0)visual_direction=initial_direction",
        "selected_bit_only_class9_clear": "state.launch_mesh_group_mask&=~(1U<<selector)",
        "mode0_target_clamp_before_launch_anchor": (
            "if(age>=projectile.travel_ticks)return projectile.target_position;\n"
            "    if(shot.trajectory_mode==0)"),
    }
    active_implementation = source_text["editor"] + "\n" + source_text["simulation"]
    for name, token in rejected.items():
        require(token not in active_implementation, f"rejected predecessor behavior returned: {name}")

    predecessor_text = {
        "editor": (previous / "src/editor/editor_app.cpp").read_text(encoding="utf-8"),
        "simulation": (previous / "src/core/simulation.cpp").read_text(encoding="utf-8"),
    }
    predecessor_defects = {
        "used_direction_model_transform_for_projectiles": "direction_model_transform(position,visual_direction,yaw)" in predecessor_text["editor"],
        "cleared_only_selected_class9_bit": "state.launch_mesh_group_mask&=~(1U<<selector)" in predecessor_text["simulation"],
        "mode0_anchor_was_after_target_clamp": rejected["mode0_target_clamp_before_launch_anchor"] in predecessor_text["simulation"],
    }
    for name, present in predecessor_defects.items():
        require(present, f"predecessor defect evidence missing: {name}")

    stock_output = None
    if args.stock_test:
        require(args.game_root is not None, "--stock-test requires --game-root")
        run = subprocess.run([str(args.stock_test), str(args.game_root)], text=True,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        require(run.returncode == 0 and "G379_SHOT_AXIS_BEAM_CLASS9_RELOAD PASS" in run.stdout,
                "G379 stock test failed:\n" + run.stdout)
        stock_output = run.stdout.strip()

    screenshot = None
    if args.screenshot:
        screenshot = {
            "path": str(args.screenshot.resolve()),
            "sha256": sha256(args.screenshot),
            "diagnostic_only": True,
            "reported_symptoms": [
                "asset 351 laser has no visible beam",
                "asset 352 rocket model/effects point backwards",
                "asset 352 reload geometry appears in the wrong sequence",
            ],
        }

    result = {
        "schema": "thandor-vulkan-map-editor-g379-shot-local-z-mode0-beam-class9-mask-v1",
        "status": "PASS",
        "authorities": authorities,
        "gzf_status": {
            "supplied_hash": authorities["gzf"],
            "role": "comparison evidence; accepted project semantic GZF remains newer",
            "promoted": False,
        },
        "binary_ranges": ranges,
        "binary_opcode_contracts": opcode_contracts,
        "current_and_untouched_c_contracts": c_contracts,
        "current_c_body_sha256": {
            name: hashlib.sha256(text.encode()).hexdigest() for name, text in c_bodies.items()
        },
        "source_contracts": source_contracts,
        "predecessor_defects": predecessor_defects,
        "stock_test": stock_output,
        "screenshot": screenshot,
        "class9_reload_interpretation": {
            "binary_exact": "eight independent timers OR each selector bit back when it expires",
            "not_changed": "stock rockets therefore reappear independently; G379 fixes wrong group clearing, not serialized timing",
        },
    }
    output = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding="utf-8")
    else:
        print(output, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
