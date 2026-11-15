/*
 * Open Thandor
 * Project: https://github.com/idkFoxes/open-thandor/tree/main
 * File: https://github.com/idkFoxes/open-thandor/blob/main/include/thandor/gameplay/ai/construction.h
 * Reverse engineering by idkFoxes 2026
 */

#ifndef THANDOR_GAMEPLAY_AI_CONSTRUCTION_H
#define THANDOR_GAMEPLAY_AI_CONSTRUCTION_H

#include <thandor/core/types.h>
#include <thandor/gameplay/army/types.h>
#include <thandor/ui/ingame/types.h>
#include <thandor/core/contracts.h>

void AiConstructionPlanner_PlaceSpecialAssetFromWorkspace
          (PckArmyAssetIdCatalog armyAssetId,FactionRuntimeIndex factionIndex,
          WorldRuntimeContext *worldRuntime);

bool AiConstructionPlanner_ProcessPendingAssetRequests
          (FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime);

void AiConstructionPlanner_PlaceTritiumStorageNearResourceSite(PckArmyAssetIdCatalog armyAssetId,FactionRuntimeIndex factionIndex,
          WorldRuntimeContext *worldRuntime);

void AiConstructionPlanner_PlaceExtendedAssetNearFactionAnchor
          (PckArmyAssetIdCatalog armyAssetId,FactionRuntimeIndex factionIndex,
          WorldRuntimeContext *worldRuntime);

void AiConstructionPlanner_PlaceArmyAssetAtReachableCandidate
          (PckArmyAssetIdCatalog armyAssetId,FactionRuntimeIndex factionIndex,
          WorldRuntimeContext *worldRuntime);

void AiConstructionPlanner_ConsumeFactionPendingArmyAsset
          (PckArmyAssetIdCatalog armyAssetId,FactionRuntimeIndex factionIndex);

/* Repairs: whether a pioneer at the deposit site siteX/siteY can found a base: a deposit cell within 3 world units
   takes the extractor (ground and buildings only, no support test) and a cell within 5 units of it takes the
   headquarters. */
bool AiConstructionPlanner_CanFoundBaseAt(PckArmyAssetIdCatalog extractorAssetId,Q12 siteX,Q12 siteY,
                                          WorldRuntimeContext *worldRuntime);

void AiConstructionPlanner_DumpMapOnce(FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime);

extern ModelRuntimeSlot *g_AiWorkspaceOwnedAsset300Runtime;

#endif /* THANDOR_GAMEPLAY_AI_CONSTRUCTION_H */
