#!/usr/bin/env python3
"""Compare requested parity-domain symbols with an Open-Thandor reference tree."""
from __future__ import annotations
import argparse, json, re
from pathlib import Path

DOMAINS={
 "owner_scheduling":["g_RuntimeMaintenanceCallbackPhases","WorldRuntime_UnlinkOwnerListNode","ownerListHead"],
 "static_owner_identity":["factionIndex","terrainOccupancyMask0","footprintRadius"],
 "movement_writers":["SelectionPointerArray_ApplyArmyRuntimeTarget","SelectionPointerArray_ApplyTargetPositionCommand",
                     "ArmyRuntime_StartMoveCommandWithAuxiliaryValues","AiUnitBehavior_UpdatePioneerVehicle"],
 "group_pathing":["EntityPathing_RebuildOverlappingGroupRoutes","ENTITY_PATHING_PRIORITY_PAIR_CAPACITY",
                  "PriorityPairHeap_SiftUp","GridScratch_SwapPrimarySecondary"],
 "military_scoring":["AiCombatTarget_EvaluateCandidateScore","AiCombatTarget_SelectBestCandidate",
                     "g_AiCombatTargetClassBaseScores"],
 "workspace01":["AI_WORKSPACE01_CAPACITY","g_AiWorkspace01Units","AiUnitBehavior_UpdateOwnUnits"],
 "research":["AiTechnologyPlanning_AddCandidateRecord","AiTechnologyCandidate_AddBestResearch",
             "AiPurchasePlanner_ExecuteAffordableCandidates"],
 "rendering":["InGameWorldOverlay_RebuildOrReleaseTransientMarkers","ModelNodeRuntime_RebuildTransformsFromRoot"],
 "aircraft":["MODEL_RUNTIME_CLASS_21_AIRCRAFT","SelectionPointerArray_SetAircraftPadTargets"],
 "projectiles":["ArmyRuntime_ResolveShotAimPoint","targetClassImpactDamageQ12"],
 "destruction":["ModelRuntimeHierarchy_MarkDestroyedRecursive","ARMY_MODEL_STATE_DESTRUCTION_STARTED"],
 "effects":["EffectRuntimePool_CreateInstanceFromDefinition","ArmyRuntime_UpdateTimedShotAndEffectEmitters"],
}

def source_text(root:Path):
    index={}
    for p in root.rglob("*"):
        if p.is_file() and p.suffix.lower() in {".cpp",".cc",".c",".h",".hpp"}:
            try:index[p.relative_to(root).as_posix()]=p.read_text(errors="ignore")
            except OSError:pass
    return index

def locate(index, symbol):
    return [path for path,text in index.items() if symbol.lower() in text.lower()]

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--project",type=Path,required=True)
    ap.add_argument("--reference",type=Path)
    ap.add_argument("--output",type=Path,required=True)
    ns=ap.parse_args()
    project=source_text(ns.project)
    reference=source_text(ns.reference) if ns.reference and ns.reference.exists() else {}
    result={"project":str(ns.project),"reference":str(ns.reference) if ns.reference else None,"domains":{}}
    for domain,symbols in DOMAINS.items():
        rows=[]
        for sym in symbols:
            ph=locate(project,sym); rh=locate(reference,sym)
            rows.append({"symbol":sym,"project_present":bool(ph),"project_files":ph,
                         "reference_present":bool(rh),"reference_files":rh})
        result["domains"][domain]=rows
    result["summary"]={
      d:{"present":sum(r["project_present"] for r in rows),"total":len(rows)}
      for d,rows in result["domains"].items()
    }
    ns.output.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps(result["summary"],indent=2))
if __name__=="__main__":
    main()
