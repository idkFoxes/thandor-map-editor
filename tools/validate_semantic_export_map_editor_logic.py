#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path

EXPECTED = {
    "game_exe": "53d097963e0b99c92b3b635255373d83f8b2ae5f65451ec76a11ff41588bd969",
    "supplied_gzf": "feb31b7fe5fc32997898955b7dfd880a8fc24dc2a66aa25017e5147d126f9dc7",
    "untouched_c": "eeea2dc72d85060054937540719d71b2c5ecc150c7af70f1358ed754f173b2d5",
    "previous_c_export": "bb03d532fb1b89b22cdca18ef091ca3979fd5102387da765bc9ea8c882dbdf9b",
    "previous_semantic_c": "458cb07803f937cafb76ce86e536675588e3c5a0fd200c97be79473fb00068de",
    "new_semantic_c_archive": "b240e981078159c1c245cbc93c5e098bb64cd6095e7f0bb1179b5ab02f122cf6",
    "new_semantic_c": "97e18448ecf0aafea4e87fb9ea7868832f6f3037110cb291d2db74a7e37a2371",
}

MAP_TERMS = (
    "Army", "Shot", "Model", "Effect", "Terrain", "Field", "Level", "Resource",
    "Sprite", "Graphics", "DirectDraw", "Camera", "Cursor", "Input", "Map", "Pck",
    "Rom", "Scenario", "Selection", "Object", "Water", "Shadow", "Light", "Animation",
    "Runtime", "World", "Texture", "Palette", "Sound", "Movement", "Collision", "Path", "Route",
)

SEMANTIC_CONTRACTS = {
    "ArmyRuntimeClass_UpdateSpecialBehaviorAndGroundMovement": [
        "movementVariantSelector",
        "definitionOrAsset + 0x278",
        "movementVariantSelector == 1",
        "ArmyRuntimeClass_UpdateGroundMovementVariantB",
        "ArmyRuntimeClass_UpdateGroundMovementVariantA",
    ],
    "ArmyRuntimeAudio_DispatchPositionedSoundVariant": [
        "definitionOrAsset + 0x278",
        "ArmyRuntimeClass_UpdatePositionedSoundsVariantB",
        "ArmyRuntimeClass_UpdatePositionedSoundsVariantA",
    ],
    "ArmyRuntimeClass_UpdateGroundMovementVariantA": [
        "g_ArmyPlacementContactKindDispatchTable.callbacks",
        "definitionOrAsset + 0x278",
    ],
    "ArmyRuntimeClass_UpdateGroundMovementVariantB": [
        "g_ArmyPlacementContactKindDispatchTable.callbacks",
        "definitionOrAsset + 0x278",
    ],
    "ArmyRuntime_ResetMovementStateFromCurrentPosition": [
        "currentWorldXQ12", "currentWorldYQ12", "fallbackPosition0Q12",
        "movementPosition0Q12", "movementPosition1Q12",
    ],
    "ArmyRuntime_ResetMovementStatePreserveQueuedTarget": [
        "currentWorldXQ12", "currentWorldYQ12", "movementTarget0Q12",
        "movementTarget1Q12", "fallbackPosition0Q12",
    ],
    "SelectionPointerArray_ApplyPositionCommandVariantB": [
        "selectedEntryCount", "singleClass13Entry", "+ 0x4c) == 0xd",
        "+ 0x78", "+ 0x7c", "| 0x800", "SelectionPointerArray_Clear32",
    ],
    "WorldRuntime_ApplyEdgeScrollAndGetCursorFrame": [
        "edgeScrollStep", "PersistentSettings_ReadDword(0x20,0x48)",
        "WorldRuntime_TranslateCameraByScreenDelta",
    ],
    "EffectDefinitions_ResolveCrossReferences": [
        "registrySlotsRemaining", "registryCursor", "currentDefinition",
        "linkedEffectDefinition", "linkedShotDefinition",
    ],
    "GraphicsTexture_ReleaseObjects": [
        "currentTexture2", "stagingTexture2", "currentSurface3", "stagingSurface3",
        "releasedTextureHandle",
    ],
}

CRITICAL_UNCHANGED = [
    "ArmyRuntimeWeapon_UpdateTargetAimAndFireAttachments",
    "ArmyRuntime_ResolveShotLaunchFromModelAttachmentCf",
    "ShotDefinition_ComputeLaunchAnglesRegs",
    "ShotRuntimePool_CreateProjectileFromDefinitionCf",
    "ShotModelRuntimeMaintenance_UpdateProjectileMotionCollisionAndEffects",
]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def code_mask(text: str) -> str:
    chars = list(text)
    index = 0
    while index < len(chars):
        if chars[index:index + 2] == ["/", "/"]:
            chars[index] = chars[index + 1] = " "
            index += 2
            while index < len(chars) and chars[index] != "\n":
                chars[index] = " "
                index += 1
            continue
        if chars[index:index + 2] == ["/", "*"]:
            chars[index] = chars[index + 1] = " "
            index += 2
            while index + 1 < len(chars) and chars[index:index + 2] != ["*", "/"]:
                if chars[index] != "\n":
                    chars[index] = " "
                index += 1
            if index + 1 < len(chars):
                chars[index] = chars[index + 1] = " "
                index += 2
            continue
        if chars[index] in {'"', "'"}:
            quote = chars[index]
            chars[index] = " "
            index += 1
            while index < len(chars):
                if chars[index] == "\\":
                    chars[index] = " "
                    if index + 1 < len(chars):
                        chars[index + 1] = " "
                    index += 2
                    continue
                if chars[index] == quote:
                    chars[index] = " "
                    index += 1
                    break
                if chars[index] != "\n":
                    chars[index] = " "
                index += 1
            continue
        index += 1
    return "".join(chars)


def function_bodies(text: str) -> tuple[dict[str, str], int]:
    """Extract top-level function definitions in one pass and return first definition offset."""
    masked = code_mask(text)
    result: dict[str, str] = {}
    excluded = {"if", "for", "while", "switch", "sizeof"}
    index = 0
    first_definition = len(text)
    while index < len(masked):
        if masked[index] != "{":
            index += 1
            continue
        cursor = index - 1
        while cursor >= 0 and masked[cursor].isspace():
            cursor -= 1
        if cursor < 0 or masked[cursor] != ")":
            # A top-level non-function aggregate. Skip its complete body.
            depth = 1
            end = index + 1
            while end < len(masked) and depth:
                if masked[end] == "{": depth += 1
                elif masked[end] == "}": depth -= 1
                end += 1
            index = end
            continue
        parens = 1
        signature_cursor = cursor - 1
        while signature_cursor >= 0 and parens:
            if masked[signature_cursor] == ")": parens += 1
            elif masked[signature_cursor] == "(": parens -= 1
            signature_cursor -= 1
        if parens != 0:
            raise SystemExit("unbalanced function parameter list")
        name_cursor = signature_cursor
        while name_cursor >= 0 and masked[name_cursor].isspace():
            name_cursor -= 1
        name_end = name_cursor + 1
        while name_cursor >= 0 and (masked[name_cursor].isalnum() or masked[name_cursor] == "_"):
            name_cursor -= 1
        name = masked[name_cursor + 1:name_end]
        if not name or name in excluded:
            index += 1
            continue
        line_start = text.rfind("\n", 0, name_cursor + 1) + 1
        depth = 1
        end = index + 1
        while end < len(masked) and depth:
            if masked[end] == "{": depth += 1
            elif masked[end] == "}": depth -= 1
            end += 1
        require(depth == 0, f"unterminated function {name}")
        require(name not in result, f"duplicate callable {name}")
        result[name] = text[line_start:end]
        first_definition = min(first_definition, line_start)
        index = end
    return result, first_definition

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--game-exe", type=Path, required=True)
    parser.add_argument("--supplied-gzf", type=Path, required=True)
    parser.add_argument("--untouched-c", type=Path, required=True)
    parser.add_argument("--previous-c-export", type=Path, required=True)
    parser.add_argument("--previous-semantic-c", type=Path, required=True)
    parser.add_argument("--new-semantic-c-archive", type=Path, required=True)
    parser.add_argument("--new-semantic-c", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    hashes = {
        "game_exe": sha256(args.game_exe),
        "supplied_gzf": sha256(args.supplied_gzf),
        "untouched_c": sha256(args.untouched_c),
        "previous_c_export": sha256(args.previous_c_export),
        "previous_semantic_c": sha256(args.previous_semantic_c),
        "new_semantic_c_archive": sha256(args.new_semantic_c_archive),
        "new_semantic_c": sha256(args.new_semantic_c),
    }
    require(hashes == EXPECTED, f"authority hash mismatch: {hashes}")

    untouched_text = args.untouched_c.read_text(encoding="utf-8", errors="replace")
    previous_c_text = args.previous_c_export.read_text(encoding="utf-8", errors="replace")
    previous_text = args.previous_semantic_c.read_text(encoding="utf-8", errors="replace")
    new_text = args.new_semantic_c.read_text(encoding="utf-8", errors="replace")
    untouched_functions, _ = function_bodies(untouched_text)
    previous_c_functions, _ = function_bodies(previous_c_text)
    previous_functions, previous_first = function_bodies(previous_text)
    new_functions, new_first = function_bodies(new_text)
    require(len(untouched_functions) == 1880, f"untouched callable inventory changed: {len(untouched_functions)}")
    require(len(previous_c_functions) == 1908, f"previous C callable inventory changed: {len(previous_c_functions)}")
    require(len(previous_functions) == 1908 and len(new_functions) == 1908,
            f"callable inventory changed: {len(previous_functions)} / {len(new_functions)}")
    require(set(previous_functions) == set(new_functions),
            "new export changed callable identities")

    changed = sorted(name for name in previous_functions if previous_functions[name] != new_functions[name])
    require(len(changed) == 112, f"expected 112 changed renderings, found {len(changed)}")
    relevant = [name for name in changed if any(term in name for term in MAP_TERMS)]
    require(len(relevant) == 63, f"expected 63 map-editor-relevant renderings, found {len(relevant)}")

    previous_preamble = previous_text[:previous_first]
    new_preamble = new_text[:new_first]
    require(previous_preamble == new_preamble,
            "typedef/global preamble changed; this pass is not local-rendering-only")

    unchanged_hashes = {}
    for name in CRITICAL_UNCHANGED:
        require(previous_functions[name] == new_functions[name],
                f"critical shot owner changed unexpectedly: {name}")
        unchanged_hashes[name] = hashlib.sha256(new_functions[name].encode()).hexdigest()

    contract_evidence = {}
    for name, tokens in SEMANTIC_CONTRACTS.items():
        function = new_functions[name]
        missing = [token for token in tokens if token not in function]
        require(not missing, f"{name} missing semantic tokens {missing}")
        contract_evidence[name] = {"tokens": tokens, "sha256": hashlib.sha256(function.encode()).hexdigest()}

    untouched_contracts = {
        "ArmyRuntimeClass_UpdateSpecialBehaviorAndGroundMovement": [
            "+ 0x278", "ArmyRuntimeClass_UpdateGroundMovementVariantB",
            "ArmyRuntimeClass_UpdateGroundMovementVariantA",
        ],
        "ArmyRuntimeAudio_DispatchPositionedSoundVariant": [
            "+ 0x278", "ArmyRuntimeClass_UpdatePositionedSoundsVariantB",
            "ArmyRuntimeClass_UpdatePositionedSoundsVariantA",
        ],
        "ArmyRuntimeClass_UpdateGroundMovementVariantA": [
            "g_ArmyPlacementContactKindDispatchTable", "+ 0x278",
        ],
        "ArmyRuntimeClass_UpdateGroundMovementVariantB": [
            "g_ArmyPlacementContactKindDispatchTable", "+ 0x278",
        ],
        "WorldRuntime_ApplyEdgeScrollAndGetCursorFrame": [
            "PersistentSettings_ReadDword(0x20,0x48)",
            "WorldRuntime_TranslateCameraByScreenDelta",
        ],
    }
    untouched_evidence = {}
    for name, tokens in untouched_contracts.items():
        require(name in untouched_functions, f"untouched C missing {name}")
        body = untouched_functions[name]
        missing = [token for token in tokens if token not in body]
        require(not missing, f"untouched {name} missing binary-shape tokens {missing}")
        untouched_evidence[name] = {
            "tokens": tokens,
            "sha256": hashlib.sha256(body.encode()).hexdigest(),
        }
    for name in SEMANTIC_CONTRACTS:
        require(name in previous_c_functions, f"previous C export missing {name}")

    source_files = {
        "model_header": args.source / "include/thandor/model.hpp",
        "simulation_header": args.source / "include/thandor/simulation.hpp",
        "model_source": args.source / "src/core/model.cpp",
        "catalog_source": args.source / "src/core/catalog.cpp",
        "simulation_source": args.source / "src/core/simulation.cpp",
        "level_source": args.source / "src/core/level.cpp",
        "field_source": args.source / "src/core/field.cpp",
        "editor_source": args.source / "src/editor/editor_app.cpp",
        "test_source": args.source / "tests/editor_semantic_export_map_editor_logic_audit_tests.cpp",
    }
    source_text = {name: path.read_text(encoding="utf-8", errors="replace") for name, path in source_files.items()}
    require("class_step_q12" in source_text["model_header"] and
            "class_speed_q12" not in source_text["model_header"],
            "MDL +0x0C still has an over-specific source name")
    require("placement_contact_kind" in source_text["model_header"] and
            "placement-contact callback table" in source_text["model_header"] and
            "offset + 0x278U" in source_text["model_source"] and
            "placement_contact_kind" in source_text["catalog_source"],
            "MDL +0x278 is not named generically, parsed, and propagated")
    require("ShotDefinition_ComputeLaunchAnglesRegs" in source_text["simulation_source"] and
            "ShotDefinition_ComputeLaunchAngles returns" not in source_text["simulation_source"],
            "shot comment uses a shortened function name")
    require("InGameLevelRuntime_LoadResourcesAfterDefaultResetCf" in source_text["level_source"] and
            "InGameLevelRuntime_LoadResourcesAfterExternalTablesCf" in source_text["level_source"],
            "LEV comment does not cite both loaders")
    require("immutable field-grid draw owner at 0x00500CE0" in source_text["field_source"] and
            "FieldGrid_DrawCellTriangles at 0x00500CE0" not in source_text["field_source"],
            "field comment invents a callable absent from the semantic export")
    require("contact %d: %s · default movement callback %s" in source_text["editor_source"],
            "editor does not expose the recovered movement selector")

    report = {
        "schema": "thandor-g383-semantic-export-map-editor-logic-audit-v1",
        "authority_hashes": hashes,
        "callable_counts": {
            "untouched": len(untouched_functions),
            "previous_c_export": len(previous_c_functions),
            "previous_semantic": len(previous_functions),
            "new_semantic": len(new_functions),
        },
        "callable_identity_changes": 0,
        "changed_renderings": len(changed),
        "map_editor_relevant_changed_renderings": len(relevant),
        "changed_functions": changed,
        "map_editor_relevant_functions": relevant,
        "typedef_global_preamble_unchanged": True,
        "critical_shot_owner_hashes": unchanged_hashes,
        "semantic_contracts": contract_evidence,
        "untouched_c_contracts": untouched_evidence,
        "gzf_role": "supplied historical database; hash-pinned but not paired with the latest semantic export",
        "source_files": {name: str(path) for name, path in source_files.items()},
        "source_corrections": [
            "MDL +0x0C renamed from class_speed_q12 to class_step_q12",
            "MDL +0x278 renamed and propagated as placement_contact_kind rather than a movement-only selector",
            "shot helper comment cites ShotDefinition_ComputeLaunchAnglesRegs",
            "LEV lighting comment cites both paired level loaders",
            "field triangle comment no longer invents FieldGrid_DrawCellTriangles",
            "movement UI reports callback variant A/B without claiming the variants are behavior-identical",
        ],
        "new_understanding": [
            "MDL +0x278 indexes placement-contact callbacks and also selects movement and positioned-sound variants",
            "MDL +0x0C is runtime-class-specific, not universally planar speed",
            "movement reset paths copy live model X/Y into fallback and movement state",
            "single selected class-13 position command writes definition state +0x78/+0x7C, sets +0xEC bit 0x800, then clears selection",
            "persistent setting domain 0x20 offset 0x48 supplies edge-scroll step",
        ],
        "behavior_change_scope": "metadata, naming, diagnostics, and source correspondence only",
        "pass": True,
    }
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "callables": len(new_functions),
        "untouched_callables": len(untouched_functions),
        "changed_renderings": len(changed),
        "map_editor_relevant": len(relevant),
        "source_contracts": "PASS",
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
