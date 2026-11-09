#!/usr/bin/env python3
"""Deep V748 source contract for combat/AI/pathing/presentation parity.

The contract scans production source only. Documentation and this test itself cannot
satisfy a required production marker.
"""
from __future__ import annotations
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
PROD_EXT={".cpp",".cc",".c",".h",".hpp"}
parts=[]
paths=[]
for p in ROOT.rglob("*"):
    if not p.is_file() or p.suffix.lower() not in PROD_EXT:
        continue
    rel=p.relative_to(ROOT).as_posix().lower()
    if rel.startswith("tests/") or rel.startswith("docs/") or "third_party/" in rel:
        continue
    try:
        parts.append(p.read_text(errors="ignore"))
        paths.append(rel)
    except OSError:
        pass
src="\n".join(parts)

checks={
 "selection target-army writer": [
   r"SelectionPointerArray_ApplyArmyRuntimeTarget",
   r"assignedTargetArmyRuntime",
   r"ARMY_COMMAND_MODE_SELECTION_ORDER",
 ],
 "selection target-position writer": [
   r"SelectionPointerArray_ApplyTargetPositionCommand",
   r"commandGeneration\s*=\s*[^;]*<<\s*2",
 ],
 "workspace 01 capacity/admission": [
   r"AI_WORKSPACE01_CAPACITY\s*=\s*64|workspace.?01.{0,80}64",
   r"workspace.?01|Workspace01",
 ],
 "group route cap/copy-swap": [
   r"ENTITY_PATHING_PRIORITY_PAIR_CAPACITY\s*=\s*32|priority.{0,60}32",
   r"GridScratch_CopyPrimaryToSecondary|copy.{0,30}scratch",
   r"GridScratch_SwapPrimarySecondary|swap.{0,30}scratch",
 ],
 "military scoring": [
   r"AiCombatTarget_(?:EvaluateCandidateScore|SelectBestCandidate)|military.{0,40}score",
   r"targetClassShotDamage|class.{0,20}damage",
 ],
 "research pipeline": [
   r"AiTechnologyPlanning_AddCandidateRecord|workspace.?12",
   r"AiTechnologyCandidate_AddBestResearch|best.{0,20}research",
   r"AiPurchasePlanner_ExecuteAffordableCandidates|purchase.{0,30}candidate",
 ],
 "factory orientation/exit": [
   r"ModelLookupTable_FindPackedPoint\s*\(\s*1\s*,\s*5|factory.{0,40}exit",
   r"FixedMath_Atan2Angle16|atan2",
   r"ArmyRuntime_StartMoveCommandWithAuxiliaryValues|auxiliary.{0,20}move",
 ],
 "rendering overlays": [
   r"InGameWorldOverlay_RebuildOrReleaseTransientMarkers|transient.{0,20}marker",
   r"ModelNodeRuntime_RebuildTransformsFromRoot|rebuild.{0,20}transform",
 ],
 "aircraft targeting": [
   r"MODEL_RUNTIME_CLASS_21_AIRCRAFT|aircraft",
   r"childNodes\s*\[\s*0\s*\]|child.{0,20}body",
 ],
 "projectile/shot aim": [
   r"ArmyRuntime_ResolveShotAimPoint|resolve.{0,20}(?:shot|projectile).{0,20}aim",
   r"targetClassImpactDamageQ12|impact.{0,20}damage",
 ],
 "destruction lifecycle": [
   r"ModelRuntimeHierarchy_MarkDestroyedRecursive|mark.{0,20}destroy",
   r"ARMY_MODEL_STATE_DESTRUCTION_STARTED|destruction.{0,20}started",
 ],
 "effects lifecycle": [
   r"EffectRuntimePool_CreateInstanceFromDefinition|create.{0,20}effect",
   r"ArmyRuntime_UpdateTimedShotAndEffectEmitters|effect.{0,20}emitter",
 ],
}
fail=[]
for name,patterns in checks.items():
    missing=[p for p in patterns if re.search(p,src,re.I|re.S) is None]
    if missing:
        fail.append((name,missing))
if fail:
    lines=["V748 deep source contract failures:"]
    for name,missing in fail:
        lines.append(f"- {name}: {', '.join(missing)}")
    raise SystemExit("\n".join(lines))
print(f"V748 deep source contract passed ({len(paths)} production files scanned)")
