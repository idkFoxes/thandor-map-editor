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
    "current_c": "ba432824f99af42a2d28cd0071fe8c09bffe6e2f5ea2a1f959414c1f095d3e9c",
    "untouched_c": "eeea2dc72d85060054937540719d71b2c5ecc150c7af70f1358ed754f173b2d5",
}

RANGES = {
    "ArmyRuntime_UpdateMovementAndWaypoints": (0x0051CEE0, 0x0051D0B0, "1002e5965d53d1531c5608cccdfa697000cc300de258ebca44292e9b297e085a"),
    "ArmyRuntimeWeapon_UpdateTargetAimAndFireAttachments": (0x00523410, 0x00523690, "335445fa610c6d1c6b1d55f7330138319de3031af57009f62e71fcb5ad2f8d2c"),
    "ModelNodeRuntime_SmoothYawTowardTarget": (0x0052AAC0, 0x0052AC00, "9c2917c70a6378d3ba831736cd273191816ed560becf82eb5a9c69c150d51c49"),
    "ModelNodeRuntime_SmoothPitchTowardTarget": (0x0052AC00, 0x0052AD50, "7f7e2ee50707142c7068985f0e9a38ae661776ea3ba30d2c0f626fe0215c08ad"),
    "ShotDefinition_ComputeLaunchAnglesRegs": (0x0052B8C0, 0x0052B9D0, "29723a81067cbfa9f0c72ea69404d5b0a4633516de6e19a9c1a5782f73678ad3"),
    "ArmyWeaponRuntime_TestTargetLineOfFireCf": (0x0052B9D0, 0x0052BCE0, "293a84ab5e79781745e3e01b28a134f6d164dc4e5619ef05f1623fb4200ea930"),
    "ShotRuntimePool_CreateProjectileFromDefinitionCf": (0x0052BDB0, 0x0052C1A0, "b3bfb90b496811c44b51a532cac4be6faff9fdf5fcc15164a2110dd807dbd56b"),
    "ShotModelRuntimeMaintenance_UpdateProjectileMotionCollisionAndEffects": (0x0052C230, 0x0052CEE0, "c12cf635c509003f12f00b6b46cbbdeb58e17dcdfd2acec58894166a4fc36d8d"),
}

def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)

def pe_range(data: bytes, start: int, end: int) -> bytes:
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    require(data[pe:pe + 4] == b"PE\0\0", "game.exe is not a PE image")
    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    image_base = struct.unpack_from("<I", data, optional + 28)[0]
    rva = start - image_base
    size = end - start
    table = optional + optional_size
    for index in range(section_count):
        off = table + index * 40
        virtual_size, virtual_address, raw_size, raw_offset = struct.unpack_from("<IIII", data, off + 8)
        if virtual_address <= rva and rva + size <= virtual_address + max(virtual_size, raw_size):
            file_offset = raw_offset + rva - virtual_address
            return data[file_offset:file_offset + size]
    raise SystemExit(f"unmapped PE range {start:#x}-{end:#x}")

def function_body(text: str, name: str) -> str:
    match = re.search(r"(?m)^(?!//)[^\n]*\b" + re.escape(name) + r"\s*(?:$|\()", text)
    require(match is not None, f"current semantic C missing function {name}")
    start = text.find("{", match.end())
    require(start >= 0, f"current semantic C missing body for {name}")
    depth = 0
    for pos in range(start, len(text)):
        if text[pos] == "{": depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return text[match.start():pos + 1]
    raise SystemExit(f"unterminated function {name}")

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--game-exe", type=Path, required=True)
    parser.add_argument("--gzf", type=Path)
    parser.add_argument("--current-c", type=Path)
    parser.add_argument("--untouched-c", type=Path)
    parser.add_argument("--game-root", type=Path)
    parser.add_argument("--stock-test", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    source = args.source.resolve()
    authorities = {"game_exe": sha(args.game_exe)}
    full_authority = all((args.gzf, args.current_c, args.untouched_c))
    require(full_authority or not any((args.gzf, args.current_c, args.untouched_c)),
            "provide all or none of --gzf/--current-c/--untouched-c")
    if full_authority:
        authorities.update({"gzf": sha(args.gzf), "current_c": sha(args.current_c),
                            "untouched_c": sha(args.untouched_c)})
        require(authorities == EXPECTED, f"authority hash mismatch: {authorities}")
    else:
        require(authorities["game_exe"] == EXPECTED["game_exe"],
                f"immutable game.exe hash mismatch: {authorities['game_exe']}")

    executable = args.game_exe.read_bytes()
    ranges = {}
    for name, (start, end, expected_hash) in RANGES.items():
        payload = pe_range(executable, start, end)
        actual = hashlib.sha256(payload).hexdigest()
        require(actual == expected_hash, f"binary range mismatch {name}: {actual}")
        ranges[name] = {
            "start": f"0x{start:08X}", "end_exclusive": f"0x{end:08X}",
            "size": len(payload), "sha256": actual,
            "first16": payload[:16].hex(), "last16": payload[-16:].hex(),
        }

    current = args.current_c.read_text(encoding="utf-8", errors="replace") if full_authority else ""
    untouched = args.untouched_c.read_text(encoding="utf-8", errors="replace") if full_authority else ""
    current_functions = [
        "ArmyRuntime_UpdateMovementAndWaypoints",
        "ArmyRuntime_UpdateAnimatedModelSubnodes",
        "ArmyRuntimeWeapon_UpdateTargetAimAndFireAttachments",
        "ModelRuntime_EmitProjectilesFromAttachmentPoints",
        "ArmyRuntime_ResolveShotLaunchFromModelAttachmentCf",
        "ShotRuntimePool_CreateProjectileFromDefinitionCf",
        "ShotModelRuntimeMaintenance_UpdateProjectileMotionCollisionAndEffects",
    ]
    body_hashes = {}
    if full_authority:
        for name in current_functions:
            body = function_body(current, name)
            body_hashes[name] = hashlib.sha256(body.encode()).hexdigest()
    untouched_tokens = [
        "ArmyRuntime_UpdateMovementAndWaypoints", "ArmyRuntime_UpdateAnimatedModelSubnodes",
        "ModelRuntime_EmitProjectilesFromAttachmentPoints",
        "ShotModelRuntimeMaintenance_UpdateProjectileMotionCollisionAndEffects",
    ]
    if full_authority:
        for token in untouched_tokens:
            require(token in untouched or token.replace("ArmyRuntime_", "FUN_") in untouched,
                    f"untouched C comparison lacks expected owner/token {token}")
        for token in ["(*localPointRecord & 0xf) == 2", "ModelNodeRuntime_TransformLocalPointRegs", "SmoothYaw", "SmoothPitch"]:
            require(token.lower() in current.lower(), f"current C missing recovered semantic token {token}")

    files = {
        "simulation_hpp": source / "include/thandor/simulation.hpp",
        "render_policy": source / "include/thandor/simulation_render_policy.hpp",
        "catalog_cpp": source / "src/core/catalog.cpp",
        "editor_cpp": source / "src/editor/editor_app.cpp",
        "editor_hpp": source / "src/editor/editor_app.hpp",
        "model_vert": source / "shaders/model.vert",
        "model_frag": source / "shaders/model.frag",
        "g375_test": source / "tests/animation_persistent_drive_weapon_hierarchy_effect_unit_animation_tests.cpp",
    }
    text = {name: path.read_text(encoding="utf-8") for name, path in files.items()}
    contracts = {
        "drive_updates_serialized_placement_continuously": all(token in text["editor_cpp"] for token in [
            "synchronize_simulated_actor_placement(index,actor)",
            "placement.set_render_position_q12(x_q12,y_q12)", "document_->mark_dirty()",
        ]),
        "drive_order_creates_single_undo_checkpoint": "checkpoint();\n            auto& actor=simulation_state_for(selected_index)" in text["editor_cpp"],
        "distinct_yaw_pitch_launch_nodes": all(token in text["simulation_hpp"] + text["catalog_cpp"] + text["editor_cpp"] for token in [
            "yaw_model_node_index", "pitch_model_node_index", "launch_model_node_index",
            "launch_model_node_indices", "weapon.yaw_model_node_index", "weapon.pitch_model_node_index",
        ]),
        "muzzle_uses_transformed_kind2_attachment": all(token in text["editor_cpp"] for token in [
            "transform_point(transforms[launch_node],attachment.local_translation)",
            "attachment.kind()!=2U", "recoil_launch_selector",
        ]),
        "sibling_and_class21_launch_policies": all(token in text["catalog_cpp"] + text["simulation_hpp"] for token in [
            "AlternateLaunchNodes", "SequentialKind2Attachments", "runtime_model_class_id == 21U",
        ]),
        "full_authored_effect_chain": all(token in text["editor_cpp"] for token in [
            "shot->launch_effect_id", "shot->secondary_effect_id", "shot->primary_effect_id",
            "shot->terrain_impact_effect_id", "simulation_projectile_position", "shading_color_argb",
        ]),
        "generic_unit_subnode_animation": all(token in text["editor_cpp"] + text["editor_hpp"] + text["render_policy"] for token in [
            "animated_unit_placements_", "kObjectVertexAuthoredAnimatedUnitBit", "current_presentation_tick_",
        ]),
        "shader_categories_retain_authored_and_simulated_units": "& 15u" in text["model_vert"] and "& 12u" in text["model_frag"],
        "focused_stock_census_is_release_blocking": all(token in text["g375_test"] for token in [
            "weapons!=160U", "muzzles!=335U", "animated_units!=54U", "visited_effects", "visited_shots",
        ]),
    }
    for name, passed in contracts.items():
        require(passed, f"G375 source contract failed: {name}")

    stock = None
    if args.stock_test:
        require(args.game_root is not None, "--stock-test requires --game-root")
        run = subprocess.run([str(args.stock_test), str(args.game_root)], text=True,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        require(run.returncode == 0 and " PASS" in run.stdout,
                "G375 stock/effect census failed:\n" + run.stdout)
        stock = run.stdout.strip()

    result = {
        "schema": "thandor-map-editor-g375-persistent-drive-weapon-hierarchy-effect-pipeline-v1",
        "status": "PASS", "authorities": authorities, "binary_ranges": ranges,
        "full_cross_source_authority": full_authority,
        "current_c_function_body_sha256": body_hashes,
        "contracts": contracts, "stock_test": stock,
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
