// Test-only native function bodies, unchanged; detached ABI stubs are below.
#pragma once
#include <cstdint>
#include <climits>
#include <functional>
#include <vector>
namespace native_v628 {
using Q12=int32_t;using PckArmyAssetIdCatalog=uint32_t;using FactionRuntimeIndex=uint32_t;
inline constexpr int Q12_ONE=4096;
struct WorldRuntimeContext{};
struct FieldGridCell{int32_t worldX{},worldY{};uint32_t triangle0NormalAngles{};};
struct ModelRuntimeNode{struct{struct{int32_t x{},y{},z{};}translation;}worldTransform;};
struct GameEntityRuntime{struct{int32_t commandState{};}common;};
struct ModelRuntimeSlot{struct{ModelRuntimeNode* modelNode{};}rootModelNodeOrSavedOffset;struct{GameEntityRuntime* entityRuntime{};}ownerArmyRuntimeOrSavedOffset;};
struct AiStructureWorkspaceEntry{ModelRuntimeSlot* runtimeSlotAddressOrZero{};uint32_t armyAssetId{};};
struct AiRuntimeWorkspaceEntry{ModelRuntimeSlot* modelRuntime{};uint32_t armyAssetId{};};
inline AiStructureWorkspaceEntry* g_AiWorkspace00Structures{};inline uint32_t g_AiWorkspace00Count{};
inline AiRuntimeWorkspaceEntry* g_AiWorkspace02VisibleHostiles{};inline uint32_t g_AiWorkspace02Count{};
inline AiRuntimeWorkspaceEntry* g_AiWorkspace03UnseenHostiles{};inline uint32_t g_AiWorkspace03Count{};
inline FieldGridCell** g_AiWorkspace09Cells{};inline uint32_t g_AiWorkspace09Count{};
struct FactionRecord{uint32_t primaryAnchorCooldown{};int32_t primaryAnchorXQ12{},primaryAnchorYQ12{};};
inline struct{FactionRecord records[8];}g_GameFactionRuntimeImage;
inline FieldGridCell* chosen{};inline std::vector<size_t> calls;
inline std::function<bool(size_t)> placement;
inline bool ArmyPlacement_CanPlaceAssetAtFieldPoint(uint32_t mode,int32_t padding,uint32_t heading,int32_t,int32_t,uint32_t,uint32_t,WorldRuntimeContext*,void*){
 if(mode!=1||padding!=0)throw "native placement mode/padding changed";
 calls.push_back(heading);return placement(heading);
}
inline void AiConstructionPlanner_CreatePlacedAsset(FieldGridCell* cell,uint32_t,uint32_t,WorldRuntimeContext*){chosen=cell;}
int AiPrimaryWorkspace_GetMinimumActiveManhattanDistanceToPoint(Q12 worldY,Q12 worldX)

{
  Q12 minimumActiveManhattanDistanceQ12;
  int workspaceEntriesRemaining;
  int deltaYAbsQ12;
  Q12 deltaXAbsQ12;
  AiStructureWorkspaceEntry *workspaceEntryCursor;
  ModelRuntimeSlot *slotModelRuntime;
  
  minimumActiveManhattanDistanceQ12 = INT32_MAX;
  workspaceEntryCursor = g_AiWorkspace00Structures;
  for (workspaceEntriesRemaining = g_AiWorkspace00Count; workspaceEntriesRemaining != 0;
      workspaceEntriesRemaining--) {
    slotModelRuntime = workspaceEntryCursor->runtimeSlotAddressOrZero;
    if ((slotModelRuntime != nullptr) &&
       ((slotModelRuntime->ownerArmyRuntimeOrSavedOffset.entityRuntime->common).commandState != 0)) {
      deltaXAbsQ12 = worldX - (slotModelRuntime->rootModelNodeOrSavedOffset.modelNode->worldTransform).translation.x;
      if (deltaXAbsQ12 < 0) {
        deltaXAbsQ12 = -deltaXAbsQ12;
      }
      deltaYAbsQ12 = worldY - (slotModelRuntime->rootModelNodeOrSavedOffset.modelNode->worldTransform).translation.y;
      if (deltaYAbsQ12 < 0) {
        deltaYAbsQ12 = -deltaYAbsQ12;
      }
      if (deltaXAbsQ12 + deltaYAbsQ12 < minimumActiveManhattanDistanceQ12) {
        minimumActiveManhattanDistanceQ12 = deltaXAbsQ12 + deltaYAbsQ12;
      }
    }
    workspaceEntryCursor++;
  }
  return minimumActiveManhattanDistanceQ12;
}
int AiHostileWorkspace_GetNearestVisibleHostileDistance(Q12 worldY,Q12 worldX)

{
  Q12 minimumManhattanDistanceQ12;
  int workspaceEntriesRemaining;
  int deltaYAbsQ12;
  Q12 deltaXAbsQ12;
  AiRuntimeWorkspaceEntry *workspaceEntryCursor;
  ModelRuntimeNode *modelNode;
  
  minimumManhattanDistanceQ12 = INT32_MAX;
  workspaceEntryCursor = g_AiWorkspace02VisibleHostiles;
  for (workspaceEntriesRemaining = g_AiWorkspace02Count; workspaceEntriesRemaining != 0;
      workspaceEntriesRemaining--) {
    if (workspaceEntryCursor->modelRuntime != nullptr) {
      modelNode = workspaceEntryCursor->modelRuntime->rootModelNodeOrSavedOffset.modelNode;
      deltaXAbsQ12 = worldX - (modelNode->worldTransform).translation.x;
      if (deltaXAbsQ12 < 0) {
        deltaXAbsQ12 = -deltaXAbsQ12;
      }
      deltaYAbsQ12 = worldY - (modelNode->worldTransform).translation.y;
      if (deltaYAbsQ12 < 0) {
        deltaYAbsQ12 = -deltaYAbsQ12;
      }
      if (deltaXAbsQ12 + deltaYAbsQ12 < minimumManhattanDistanceQ12) {
        minimumManhattanDistanceQ12 = deltaXAbsQ12 + deltaYAbsQ12;
      }
    }
    workspaceEntryCursor++;
  }
  return minimumManhattanDistanceQ12;
}
int AiHostileWorkspace_GetNearestUnseenHostileDistance(Q12 worldY,Q12 worldX)

{
  Q12 minimumManhattanDistanceQ12;
  int workspaceEntriesRemaining;
  int deltaYAbsQ12;
  Q12 deltaXAbsQ12;
  AiRuntimeWorkspaceEntry *workspaceEntryCursor;
  ModelRuntimeNode *modelNode;
  
  minimumManhattanDistanceQ12 = INT32_MAX;
  workspaceEntryCursor = g_AiWorkspace03UnseenHostiles;
  for (workspaceEntriesRemaining = g_AiWorkspace03Count; workspaceEntriesRemaining != 0;
      workspaceEntriesRemaining--) {
    if (workspaceEntryCursor->modelRuntime != nullptr) {
      modelNode = workspaceEntryCursor->modelRuntime->rootModelNodeOrSavedOffset.modelNode;
      deltaXAbsQ12 = worldX - (modelNode->worldTransform).translation.x;
      if (deltaXAbsQ12 < 0) {
        deltaXAbsQ12 = -deltaXAbsQ12;
      }
      deltaYAbsQ12 = worldY - (modelNode->worldTransform).translation.y;
      if (deltaYAbsQ12 < 0) {
        deltaYAbsQ12 = -deltaYAbsQ12;
      }
      if (deltaXAbsQ12 + deltaYAbsQ12 < minimumManhattanDistanceQ12) {
        minimumManhattanDistanceQ12 = deltaXAbsQ12 + deltaYAbsQ12;
      }
    }
    workspaceEntryCursor++;
  }
  return minimumManhattanDistanceQ12;
}
void AiConstructionPlanner_PlaceExtendedAssetNearFactionAnchor
          (PckArmyAssetIdCatalog armyAssetId,FactionRuntimeIndex factionIndex,
          WorldRuntimeContext *worldRuntime)

{
  int anchorDistanceY;
  int structureDistance;
  int siteDistance;
  int candidateScore;
  int remainingCells;
  int anchorDistanceX;
  FieldGridCell **gridCellCursor;
  FieldGridCell *bestCell;
  int bestScore;
  FieldGridCell *candidateCell;
  bool siteDistanceInRange;

  if ((g_GameFactionRuntimeImage.records[factionIndex].primaryAnchorCooldown == 0) ||
     (g_AiWorkspace09Count == 0)) {
    return;
  }
  bestScore = INT32_MAX;
  gridCellCursor = g_AiWorkspace09Cells;
  for (remainingCells = g_AiWorkspace09Count; remainingCells != 0; remainingCells--, gridCellCursor++) {
    candidateCell = *gridCellCursor;
    anchorDistanceY = g_GameFactionRuntimeImage.records[factionIndex].primaryAnchorYQ12 - candidateCell->worldX;
    if (anchorDistanceY < 0) {
      anchorDistanceY = -anchorDistanceY;
    }
    anchorDistanceX = g_GameFactionRuntimeImage.records[factionIndex].primaryAnchorXQ12 - candidateCell->worldY;
    if (anchorDistanceX < 0) {
      anchorDistanceX = -anchorDistanceX;
    }
    if (anchorDistanceX + anchorDistanceY >= bestScore) continue;
    structureDistance = AiPrimaryWorkspace_GetMinimumActiveManhattanDistanceToPoint
                          (candidateCell->worldY,candidateCell->worldX);
    if (structureDistance <= 2 * Q12_ONE - 1) continue;
    /* Distance to the nearest workspace-02 site (x4, at most 5.0), or without any such site to the
       nearest workspace-03 site (x2, at most 8.0). */
    siteDistance = AiHostileWorkspace_GetNearestVisibleHostileDistance(candidateCell->worldY,candidateCell->worldX);
    if (siteDistance < INT32_MAX) {
      siteDistanceInRange = siteDistance < 5 * Q12_ONE + 1;
      if (siteDistanceInRange) {
        siteDistance = siteDistance * 4;
      }
    }
    else {
      siteDistance = AiHostileWorkspace_GetNearestUnseenHostileDistance(candidateCell->worldY,candidateCell->worldX);
      siteDistanceInRange = siteDistance < 8 * Q12_ONE + 1;
      if (siteDistanceInRange) {
        siteDistance = siteDistance * 2;
      }
    }
    if (!siteDistanceInRange) continue;
    candidateScore = anchorDistanceX + anchorDistanceY + siteDistance;
    if ((candidateScore < bestScore) &&
       (ArmyPlacement_CanPlaceAssetAtFieldPoint
                          (1,0,(uint32_t)(uint16_t)candidateCell->triangle0NormalAngles,
                           candidateCell->worldY,candidateCell->worldX,armyAssetId,factionIndex,
                           worldRuntime,nullptr))) {
      bestCell = candidateCell;
      bestScore = candidateScore;
    }
  }
  if (bestScore < INT32_MAX) {
    AiConstructionPlanner_CreatePlacedAsset(bestCell,armyAssetId,factionIndex,worldRuntime);
  }
}

}
