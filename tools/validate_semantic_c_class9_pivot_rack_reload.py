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
    "semantic_c": "03ce4dcbdabb290b861bf0f0420dd5b30bbb82f05cd081974e2e2e0b421d2d0f",
    "untouched_c": "eeea2dc72d85060054937540719d71b2c5ecc150c7af70f1358ed754f173b2d5",
    "predecessor_source_zip": "92c527c485beaafcdf0e6f1cf5579b41da1e036e216c540834ad54e3b84a4b55",
}

RANGES = {
    "ArmyRuntimeWeapon_UpdateTargetAimAndFireAttachments": (
        0x00523980, 0x00523CA0,
        "2507690f719488d6b7fb1c145367e0bd977ea8e623d3f345038ec465dea39c4c",
    ),
    "ArmyRuntime_ResolveShotLaunchFromModelAttachmentCf": (
        0x00529720, 0x005297D0,
        "d2717e7541544af9cff9f5b70eb1ded0bde8b039c4688cf32cd5ce711973624b",
    ),
    "ShotRuntimePool_CreateProjectileFromDefinitionCf": (
        0x0052BDB0, 0x0052C1A0,
        "b3bfb90b496811c44b51a532cac4be6faff9fdf5fcc15164a2110dd807dbd56b",
    ),
    "ShotModelRuntimeMaintenance_UpdateProjectileMotionCollisionAndEffects": (
        0x0052C230, 0x0052CEE0,
        "c12cf635c509003f12f00b6b46cbbdeb58e17dcdfd2acec58894166a4fc36d8d",
    ),
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def pe_slice(data: bytes, start_va: int, end_va: int) -> bytes:
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    require(data[pe:pe + 4] == b"PE\0\0", "game.exe is not a PE image")
    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    image_base = struct.unpack_from("<I", data, optional + 28)[0]
    rva = start_va - image_base
    size = end_va - start_va
    table = optional + optional_size
    for index in range(section_count):
        offset = table + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from(
            "<IIII", data, offset + 8
        )
        if virtual_address <= rva and rva + size <= virtual_address + max(virtual_size, raw_size):
            start = raw_offset + rva - virtual_address
            return data[start:start + size]
    raise SystemExit(f"unmapped PE range {start_va:#x}-{end_va:#x}")


def body(text: str, name: str) -> str:
    for match in re.finditer(re.escape(name), text):
        line = text.rfind("\n", 0, match.start()) + 1
        if line < len(text) and (text[line].isspace() or text.startswith("//", line)):
            continue
        cursor = match.end()
        while cursor < len(text) and text[cursor].isspace():
            cursor += 1
        if cursor >= len(text) or text[cursor] != "(":
            continue
        brace = text.find("{", cursor)
        if brace < 0:
            continue
        depth = 0
        for end in range(brace, len(text)):
            if text[end] == "{":
                depth += 1
            elif text[end] == "}":
                depth -= 1
                if depth == 0:
                    return text[line:end + 1]
    raise SystemExit(f"cannot extract C body {name}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--previous-source", type=Path, required=True)
    parser.add_argument("--game-exe", type=Path, required=True)
    parser.add_argument("--gzf", type=Path, required=True)
    parser.add_argument("--semantic-c", type=Path, required=True)
    parser.add_argument("--untouched-c", type=Path, required=True)
    parser.add_argument("--predecessor-source-zip", type=Path, required=True)
    parser.add_argument("--game-root", type=Path)
    parser.add_argument("--stock-test", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    source = args.source.resolve()
    previous = args.previous_source.resolve()
    authorities = {
        "game_exe": sha256(args.game_exe),
        "gzf": sha256(args.gzf),
        "semantic_c": sha256(args.semantic_c),
        "untouched_c": sha256(args.untouched_c),
        "predecessor_source_zip": sha256(args.predecessor_source_zip),
    }
    require(authorities == EXPECTED, f"authority hash mismatch: {authorities}")

    executable = args.game_exe.read_bytes()
    binary_ranges: dict[str, object] = {}
    for name, (start, end, expected) in RANGES.items():
        payload = pe_slice(executable, start, end)
        actual = hashlib.sha256(payload).hexdigest()
        require(actual == expected, f"binary range mismatch for {name}: {actual}")
        binary_ranges[name] = {
            "start_va": f"0x{start:08X}",
            "end_va_exclusive": f"0x{end:08X}",
            "bytes": len(payload),
            "sha256": actual,
        }

    semantic_text = args.semantic_c.read_text(encoding="utf-8", errors="replace")
    untouched_text = args.untouched_c.read_text(encoding="utf-8", errors="replace")
    names = list(RANGES)
    semantic_bodies = {name: body(semantic_text, name) for name in names}
    untouched_bodies = {name: body(untouched_text, name) for name in names}
    weapon = semantic_bodies["ArmyRuntimeWeapon_UpdateTargetAimAndFireAttachments"]
    resolver = semantic_bodies["ArmyRuntime_ResolveShotLaunchFromModelAttachmentCf"]
    creation = semantic_bodies["ShotRuntimePool_CreateProjectileFromDefinitionCf"]
    maintenance = semantic_bodies[
        "ShotModelRuntimeMaintenance_UpdateProjectileMotionCollisionAndEffects"
    ]
    launch_angles = body(semantic_text, "ShotDefinition_ComputeLaunchAnglesRegs")

    semantic_c_contracts = {
        "class9_callback_root_child0_is_aim_pivot": all(token in weapon for token in [
            "pMVar9 = pMVar6->childNodes[0]",
            "ShotDefinition_ComputeLaunchAnglesRegs",
            "(pMVar9->worldTransform).translation.z",
            "(pMVar9->worldTransform).translation.y",
            "(pMVar9->worldTransform).translation.x",
        ]),
        "class9_callback_root_child0_child0_owns_rack_mask": all(token in weapon for token in [
            "armyRuntime->modelNodeRuntime->childNodes[0]->childNodes[0]",
            "modelPayload).meshGroupMask",
            "*pMVar2 = *pMVar2 & -2 <<",
        ]),
        "class9_eight_reload_slots_restore_only_after_signed_negative": all(
            token in weapon for token in [
                "*pdVar1 = *pdVar1 - g_InGameSimulationStepTicks",
                "if ((int)*pdVar1 < 0)",
                "*pMVar2 = *pMVar2 | 1",
                "*pMVar2 = *pMVar2 | 2",
                "*pMVar2 = *pMVar2 | 4",
                "*pMVar2 = *pMVar2 | 0x80",
            ]
        ),
        "launch_resolver_rebuilds_live_pose_and_matches_exact_kind2_key": all(
            token in resolver for token in [
                "ModelNodeRuntime_RebuildTransformsFromRoot",
                "*localPointRecord == (launchMode << 4 | 2)",
                "ModelNodeRuntime_TransformLocalPointRegs",
                "ShotRuntimePool_CreateProjectileFromDefinitionCf",
            ]
        ),
        "launch_angles_separate_direct_mode_from_visual_elevation": all(
            token in launch_angles for token in [
                "FixedMath_VectorToAngles3Regs",
                "definition->elevationOffsetAngle16",
                "if (0x4000 < (int)dVar3)",
            ]
        ),
        "projectile_creation_starts_at_transformed_launch_point": all(
            token in creation for token in [
                "translation.x = launchWorldXQ12",
                "translation.y = launchWorldYQ12",
                "translation.z = launchWorldZQ12",
                "worldRotationAngle0 = (int)uVar26",
                "worldRotationAngle1 = (int)((ulonglong)uVar26 >> 0x20)",
                "worldRotationAngle2 = (dword)uVar26",
            ]
        ),
        "mode3_maintenance_subtracts_visual_offset_from_movement_pitch": all(
            token in maintenance for token in [
                "worldRotationAngle1 -",
                "shotRuntime->movementStateFlags",
                "rangeScale270Q12",
                "launchSpeedQ12",
            ]
        ),
        "untouched_export_contains_all_binary_corresponding_owners": all(
            len(untouched_bodies[name]) > 100 for name in names
        ),
    }
    for name, passed in semantic_c_contracts.items():
        require(passed, f"semantic C contract failed: {name}")

    semantic_identifiers = [
        "targetYawAngle16",
        "targetPitchAngle16",
        "targetWorldYQ12",
        "attachmentSelector",
        "modelDefinitionOrLookup",
        "launchMode",
        "launchWorldZQ12",
        "trajectoryMode",
        "elevationOffsetAngle16",
        "meshGroupMask",
    ]
    semantic_identifier_presence = {
        name: name in semantic_text for name in semantic_identifiers
    }
    require(all(semantic_identifier_presence.values()), "semantic local/field naming set incomplete")

    source_files = {
        "simulation_hpp": source / "include/thandor/simulation.hpp",
        "simulation_cpp": source / "src/core/simulation.cpp",
        "catalog_cpp": source / "src/core/catalog.cpp",
        "editor_cpp": source / "src/editor/editor_app.cpp",
        "test": source / "tests/combat_class9_pivot_rack_reload_tests.cpp",
        "cmake": source / "CMakeLists.txt",
    }
    source_text = {name: path.read_text(encoding="utf-8") for name, path in source_files.items()}
    source_contracts = {
        "profile_separates_aim_pivot_rack_and_launch_roles": all(token in source_text["simulation_hpp"] for token in [
            "aim_model_node_index",
            "rack_mesh_model_node_index",
            "launch_model_node_index",
        ]),
        "catalog_resolves_class9_roles_from_callback_owner_root": all(token in source_text["catalog_cpp"] for token in [
            "direct_child_index(yaw_node_index, 0U)",
            "direct_child_index(profile.aim_model_node_index, 0U)",
        ]),
        "editor_aims_from_pivot_but_tests_line_of_fire_from_muzzle": all(token in source_text["editor_cpp"] for token in [
            "aim_origin=transforms[weapon.aim_model_node_index].translation",
            "simulation_line_of_fire_clear(launch_origin,*target)",
            "*shot,aim_origin,target,clear",
        ]),
        "renderer_applies_class9_mask_to_resolved_rack_node": all(token in source_text["editor_cpp"] for token in [
            "profile.rack_mesh_model_node_index",
            "instance_index==profile.rack_mesh_model_node_index",
            "simulation_class9_render_mesh_mask",
        ]),
        "reload_visibility_is_callback_owned_not_render_synthesized": all(token in source_text["simulation_cpp"] for token in [
            "if(timer>0)",
            "state.launch_mesh_group_mask|=1U<<selector",
            "return state.launch_mesh_group_mask",
        ]) and "attachment_reload_ticks[selector]==0U" not in source_text["simulation_cpp"],
        "focused_test_registered": all(token in source_text["cmake"] + source_text["test"] for token in [
            "combat_class9_pivot_rack_reload_tests",
            "G381_SEMANTIC_C_CLASS9_PIVOT_RACK_RELOAD PASS",
        ]),
    }
    for name, passed in source_contracts.items():
        require(passed, f"source contract failed: {name}")

    previous_simulation = (previous / "src/core/simulation.cpp").read_text(encoding="utf-8")
    previous_simulation_hpp = (previous / "include/thandor/simulation.hpp").read_text(encoding="utf-8")
    previous_editor = (previous / "src/editor/editor_app.cpp").read_text(encoding="utf-8")
    predecessor_defects = {
        "aim_pivot_not_represented": "aim_model_node_index" not in previous_simulation_hpp,
        "rack_owner_not_represented": "rack_mesh_model_node_index" not in previous_simulation_hpp,
        "turret_aim_used_first_muzzle_point": "*shot,launch_origin,target,clear" in previous_editor,
        "renderer_synthesized_reload_bits_from_timer_zero":
            "attachment_reload_ticks[selector]==0U" in previous_simulation,
    }
    for name, present in predecessor_defects.items():
        require(present, f"predecessor defect evidence missing: {name}")

    stock_output = None
    if args.stock_test:
        require(args.game_root is not None, "--stock-test requires --game-root")
        completed = subprocess.run(
            [str(args.stock_test.resolve()), str(args.game_root.resolve())],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        require(
            completed.returncode == 0 and
            "G381_SEMANTIC_C_CLASS9_PIVOT_RACK_RELOAD PASS" in completed.stdout,
            "G381 stock test failed:\n" + completed.stdout,
        )
        stock_output = completed.stdout.strip()

    result = {
        "schema": "thandor-vulkan-map-editor-semantic-c-class9-pivot-rack-reload-v1",
        "status": "PASS",
        "authorities": authorities,
        "authority_roles": {
            "game_exe": "immutable execution authority",
            "semantic_c": "new semantic/decompiler evidence with improved locals and datatypes",
            "untouched_c": "untouched decompiler comparison oracle",
            "gzf": "older serialized comparison database; not promoted because it is not paired with the new C export",
            "predecessor_source_zip": "G380 source baseline",
        },
        "binary_ranges": binary_ranges,
        "semantic_c_contracts": semantic_c_contracts,
        "semantic_identifier_presence": semantic_identifier_presence,
        "source_contracts": source_contracts,
        "predecessor_defects": predecessor_defects,
        "stock_test": stock_output,
        "stock_asset_352_roles": {
            "callback_root_node": 1,
            "aim_and_pitch_node": 2,
            "rack_and_launch_node": 3,
            "selector_mask": "0x07",
            "inter_shot_ticks": 53,
            "reload_ticks": 400,
        },
        "semantic_c_archive_sha256": "9dd3c48e591fc06a2ec18e72e27fe59c383d8d9af9c7b101cafdf611463fec0c",
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
