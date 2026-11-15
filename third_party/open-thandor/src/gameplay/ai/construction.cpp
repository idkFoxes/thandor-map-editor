/*
 * Open Thandor
 * Project: https://github.com/idkFoxes/open-thandor/tree/main
 * File: https://github.com/idkFoxes/open-thandor/blob/main/src/gameplay/ai/construction.cpp
 * Reverse engineering by idkFoxes 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <thandor/gameplay/ai/construction.h>
#include <thandor/thandor.h>
#include <thandor/gameplay/ai/improved.h>

/* Module data. */

ModelRuntimeSlot *g_AiWorkspaceOwnedAsset300Runtime = nullptr;

static uint32_t g_AiConstructionPendingAssetConsumedCount = 0;

static void AiConstructionPlanner_CreatePlacedAsset
          (FieldGridCell *cell,PckArmyAssetIdCatalog armyAssetId,FactionRuntimeIndex factionIndex,
          WorldRuntimeContext *worldRuntime);

/* Repairs: the model definition of an army asset's root node (as ArmyPlacement_CanPlaceAssetAtFieldPoint takes it). */
static ModelDefinition *AiConstruction_AssetDefinition(PckArmyAssetIdCatalog armyAssetId)
{
  ArmyAssetRecordPrefix *armyAsset;
  if (ArmyAssetRegistry_FindById(armyAssetId,&armyAsset) != 0) {
    return nullptr;
  }
  return reinterpret_cast<ModelDefinition *>(ModelDefinitionRegistry_FindById(
       Thandor_U32ToPointer<AiLinkedDefinitionListView>(armyAsset->rootNodeOffsetOrPointer)->definitionIds[0])); /* 32-bit format field */
}

/* Repairs: the ground parts of the building placement test at a field cell, without the support test: no building
   in the way and the terrain test of the definition's contact kind. */
static bool AiConstruction_GroundAccepts(ModelDefinition *definition,FieldGridCell *cell,WorldRuntimeContext *worldRuntime)
{
  Q12 height = 0;
  (*g_FieldGridInterpolationCallbacks5.callbacks[definition->placementContactKindIndex])
       (cell->worldY,cell->worldX,worldRuntime->fieldGrid,&height);
  if (ArmyPlacementCollision_TestPointAgainstRuntimeList(ARMY_PLACEMENT_MODE_STRUCTURES_ONLY,definition->footprintRadius,
                                                         cell->worldY,cell->worldX,worldRuntime)) {
    return false;
  }
  return !g_TerrainClassPlacementAndOverlayCallbacks10.placementTests[definition->placementContactKindIndex]
            (definition->footprintRadius,height,cell->worldY,cell->worldX,worldRuntime->fieldGrid);
}

bool AiConstructionPlanner_CanFoundBaseAt(PckArmyAssetIdCatalog extractorAssetId,Q12 siteX,Q12 siteY,
                                          WorldRuntimeContext *worldRuntime)
{
  ModelDefinition *extractor = AiConstruction_AssetDefinition(extractorAssetId);
  ModelDefinition *headquarters = AiConstruction_AssetDefinition(ARM_0300_BUILDING_MDL0301);
  FieldGridAsset *grid = worldRuntime->fieldGrid;
  if (extractor == nullptr || headquarters == nullptr || grid == nullptr) {
    return true; /* no verdict */
  }
  const int64_t depositReach = 3 * (int64_t)Q12_ONE;
  const int64_t headquartersReach = 5 * (int64_t)Q12_ONE;
  const int64_t headquartersGap = 2 * (int64_t)Q12_ONE + headquarters->footprintRadius;
  uint32_t resourceBit = (uint32_t)ToBits(FieldCell_ResourceSupportBit((uint8_t)extractor->classParameterC0 & 31));
  uint32_t cellCount = grid->gridWidth * grid->gridHeight;
  FieldGridCell *deposit = grid->cells;
  for (uint32_t d = 0; d < cellCount; d++, deposit++) {
    if (((uint32_t)ToBits(deposit->flagsAndMaterial) & resourceBit) == 0) continue;
    int64_t dx = (int64_t)deposit->worldX - siteX;
    int64_t dy = (int64_t)deposit->worldY - siteY;
    if (dx * dx + dy * dy > depositReach * depositReach) continue;
    if (!AiConstruction_GroundAccepts(extractor,deposit,worldRuntime)) continue;
    /* a place for the headquarters near this mine */
    FieldGridCell *cell = grid->cells;
    for (uint32_t c = 0; c < cellCount; c++, cell++) {
      int64_t hx = (int64_t)cell->worldX - deposit->worldX;
      int64_t hy = (int64_t)cell->worldY - deposit->worldY;
      if (hx > headquartersReach || hx < -headquartersReach || hy > headquartersReach || hy < -headquartersReach) continue;
      int64_t distance = hx * hx + hy * hy;
      if (distance > headquartersReach * headquartersReach || distance < headquartersGap * headquartersGap) continue;
      if (Any(cell->flagsAndMaterial & FIELD_CELL_XENITE_OR_TRITIUM_SUPPORT_MASK)) continue;
      if (AiConstruction_GroundAccepts(headquarters,cell,worldRuntime)) {
        return true;
      }
    }
  }
  return false;
}

/* Test aid (OPEN_THANDOR_AI_MAPDUMP=1, protocol on): writes kartendump.txt once per session start, one line per field
   cell: column, row, world x, y, height, deposit bits, whether the headquarters' ground test passes, presence of
   faction 1..3. */
void AiConstructionPlanner_DumpMapOnce(FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime)
{
  static bool s_done = false;
  if (s_done || getenv("OPEN_THANDOR_AI_MAPDUMP") == nullptr || worldRuntime->fieldGrid == nullptr) {
    return;
  }
  s_done = true;
  ModelDefinition *headquarters = AiConstruction_AssetDefinition(ARM_0300_BUILDING_MDL0301);
  FILE *out = fopen("kartendump.txt","w");
  if (out == nullptr || headquarters == nullptr) {
    if (out != nullptr) fclose(out);
    return;
  }
  FieldGridAsset *grid = worldRuntime->fieldGrid;
  for (uint32_t r = 0; r < grid->gridHeight; r++) {
    for (uint32_t c = 0; c < grid->gridWidth; c++) {
      FieldGridCell *cell = &grid->cells[r * grid->gridWidth + c];
      fprintf(out,"%u %u %d %d %d %u %d\n",c,r,cell->worldX,cell->worldY,cell->terrainHeight,
              (unsigned)(ToBits(cell->flagsAndMaterial) & ToBits(FIELD_CELL_XENITE_OR_TRITIUM_SUPPORT_MASK)),
              AiConstruction_GroundAccepts(headquarters,cell,worldRuntime) ? 1 : 0);
    }
  }
  for (WorldOwnerListNode *node = worldRuntime->ownerListHead; node != nullptr; node = node->nextNode) {
    if (node->ownerClassId != WORLD_OWNER_RUNTIME_MODEL) continue;
    ModelRuntimeSlot *model = WorldOwnerNode_ModelRuntime(node);
    ArmyRuntimeSlot *army = model->ownerArmyRuntimeOrSavedOffset.armyRuntime;
    if (army == nullptr || army->modelNodeRuntime == nullptr || army->modelRuntimeOrSavedOffset.modelRuntime != model) continue;
    fprintf(out,"OBJ %d %u %d %d\n",army->factionIndex,(unsigned)army->armyAssetId,
            (army->modelNodeRuntime->worldTransform).translation.x,(army->modelNodeRuntime->worldTransform).translation.y);
  }
  fclose(out);
  (void)factionIndex;
}

/* Improved AI / repairs: the AI must not wall itself in. The user's pictures showed the base entrance built shut
   and a freshly built tank wedged between storages in front of the factory. A new building therefore keeps
   - a lane of AI_CONSTRUCTION_LANE_Q12 world units between its own footprint and the footprint of every building
     the faction already has, so its vehicles can drive between them, and
   - AI_CONSTRUCTION_DOOR_Q12 of room around the door (the (1,5) anchor point) of its factories and aircraft pads,
     where new units come out; the game itself only keeps ARMY_PLACEMENT_ANCHOR_CLEARANCE_Q12 (0.75) free there.
   Defence buildings (ARM 340 and up) are exempt: they are small, they belong at the edge and in a row, and the
   original places them where an attack is happening.
   The rule is a preference, not a law: the placement handlers first look for a cell that keeps the lanes free and
   fall back to their usual choice when no cell does, so the AI never stops building for lack of room. */
static constexpr int64_t AI_CONSTRUCTION_LANE_Q12 = 5 * (int64_t)Q12_ONE / 4;  /* 1.25: a tank's width and room */
static constexpr int64_t AI_CONSTRUCTION_DOOR_Q12 = 3 * (int64_t)Q12_ONE;

static bool AiConstruction_AssetKeepsLanes(PckArmyAssetIdCatalog armyAssetId)
{
  /* only the big buildings below ARM 340 (headquarters, factories, power plants, storages, labs): defence
     buildings are small and belong at the edge, walls and tank traps are meant to stand in the way */
  return (AiImproved_RepairsActive() || AiImproved_IsActive()) && (armyAssetId < ARM_0340_BUILDING_MDL0314);
}

/* The door (the (1,5) anchor) of a factory, class-11 structure or aircraft pad in world coordinates. False when
   the model has none. Note the swap the game's own anchor test uses: the point's y is the world x. */
static bool AiConstruction_StructureDoor(ModelRuntimeSlot *structure,Q12 *outWorldX,Q12 *outWorldY)
{
  ModelRuntimeClassId classId = structure->definitionOrSavedId.runtimeDefinition->runtimeClassId;
  if (classId != MODEL_RUNTIME_CLASS_13 && classId != MODEL_RUNTIME_CLASS_11 && classId != MODEL_RUNTIME_CLASS_22) {
    return false;
  }
  ModelRuntimeNode *node = structure->rootModelNodeOrSavedOffset.modelNode;
  ModelPackedPointRecord *anchorRecord;
  if (!ModelLookupTable_FindPackedPoint(1,5,(node->modelPayload).modelResource,&anchorRecord)) {
    return false;
  }
  ModelWorldPoint anchor = ModelNodeRuntime_TransformLocalPoint(anchorRecord,node);
  *outWorldX = (Q12)anchor.xQ12;
  *outWorldY = (Q12)anchor.yQ12;
  return true;
}

/* True when a building of armyAssetId at this cell leaves the lanes of the faction's base free (see above). */
static bool AiConstruction_KeepsBaseOpen(PckArmyAssetIdCatalog armyAssetId,Q12 worldX,Q12 worldY,int lanePass)
{
  /* pass 0 wants the full lane, pass 1 half of it (a base that is already full would otherwise fall back to the
     old placement at once and the new building would again stand against its neighbour) */
  const int64_t lanePart = (lanePass == 0) ? AI_CONSTRUCTION_LANE_Q12 : AI_CONSTRUCTION_LANE_Q12 / 2;
  const int64_t doorPart = (lanePass == 0) ? AI_CONSTRUCTION_DOOR_Q12 : AI_CONSTRUCTION_DOOR_Q12 * 2 / 3;
  if (!AiImproved_RepairsActive() && !AiImproved_IsActive()) {
    return true;
  }
  /* the lane only between the big buildings, the room in front of a door for every building */
  const bool keepsLane = AiConstruction_AssetKeepsLanes(armyAssetId);
  ModelDefinition *definition = AiConstruction_AssetDefinition(armyAssetId);
  if (definition == nullptr) {
    return true;
  }
  int64_t ownRadius = definition->footprintRadius;
  AiStructureWorkspaceEntry *entry = g_AiWorkspace00Structures;
  for (uint32_t i = 0; i < g_AiWorkspace00Count; i++, entry++) {
    ModelRuntimeSlot *structure = entry->runtimeSlotAddressOrZero;
    if (structure == nullptr) {
      continue;
    }
    ModelRuntimeNode *node = structure->rootModelNodeOrSavedOffset.modelNode;
    Q12 structureX = (node->worldTransform).translation.x;
    Q12 structureY = (node->worldTransform).translation.y;
    ModelDefinition *structureDefinition = structure->definitionOrSavedId.runtimeDefinition;
    if (keepsLane && AiConstruction_AssetKeepsLanes(entry->armyAssetId)) {
      int64_t lane = ownRadius + (int64_t)structureDefinition->footprintRadius + lanePart;
      if (AiImproved_DistanceSquaredQ12(worldX,worldY,structureX,structureY) < lane * lane) {
        return false;
      }
    }
    Q12 doorX;
    Q12 doorY;
    if (AiConstruction_StructureDoor(structure,&doorX,&doorY)) {
      int64_t clearance = ownRadius + doorPart;
      if (AiImproved_DistanceSquaredQ12(worldX,worldY,doorX,doorY) < clearance * clearance) {
        return false;
      }
    }
  }
  return true;
}

/* Improved AI / repairs: the heading a building is placed with. The AI used the cell's terrain normal angles,
   which point wherever the ground happens to tilt, so the door of its factories faced any direction, often into
   the base (a player turns the building before placing it: g_InGamePlacementHeading16).
   For the producers (factory class 13, class 11, aircraft pad class 22) the AI now tries 16 headings and takes
   the one that passes the placement test and puts the door (the (1,5) anchor, rotated as
   ArmyPlacement_CanPlaceAnchoredModel rotates it) farthest from the middle of its own base, so new units drive
   out of the base instead of into it. Without a usable heading the cell's own angles are kept. */
static constexpr int AI_CONSTRUCTION_HEADING_STEPS = 16;

/* The world position of the door of a model definition placed at worldX/worldY with this heading. */
static void AiConstruction_DoorForHeading(ModelDefinition *definition,uint32_t heading,Q12 worldX,Q12 worldY,
                                          Q12 *outDoorX,Q12 *outDoorY)
{
  ModelPackedPointRecord *anchorRecord = nullptr;
  *outDoorX = worldX;
  *outDoorY = worldY;
  MdlSerializedNodeHeader *rootNode = Thandor_U32ToPointer<MdlSerializedNodeHeader>(definition->rootNodeOffsetOrPointer); /* 32-bit format field */
  if (rootNode == nullptr ||
      !ModelLookupTable_FindPackedPoint(1,5,rootNode->spriteAssetReference.modelResource,&anchorRecord) ||
      anchorRecord == nullptr) {
    return;
  }
  /* the same rotation as the placement test: the local point as angle and length, turned by the heading */
  FixedLengthAngle offset = FixedMath_Vector2AngleAndLength((anchorRecord->localPosition).y,
                                                            (anchorRecord->localPosition).x);
  FixedSinCos rotated = FixedMath_SinCosScaled(offset.angle + heading & FIXED_ANGLE16_MASK,offset.length);
  *outDoorX = worldX + rotated.cosValue;
  *outDoorY = worldY + rotated.sinValue;
}

/* The middle of the faction's base: the average position of its buildings (false without any). */
static bool AiConstruction_BaseCentre(Q12 *outWorldX,Q12 *outWorldY)
{
  int64_t sumX = 0;
  int64_t sumY = 0;
  uint32_t counted = 0;
  AiStructureWorkspaceEntry *entry = g_AiWorkspace00Structures;
  for (uint32_t i = 0; i < g_AiWorkspace00Count; i++, entry++) {
    ModelRuntimeSlot *structure = entry->runtimeSlotAddressOrZero;
    if (structure == nullptr) {
      continue;
    }
    ModelRuntimeNode *node = structure->rootModelNodeOrSavedOffset.modelNode;
    sumX += (node->worldTransform).translation.x;
    sumY += (node->worldTransform).translation.y;
    counted++;
  }
  if (counted == 0) {
    return false;
  }
  *outWorldX = (Q12)(sumX / (int64_t)counted);
  *outWorldY = (Q12)(sumY / (int64_t)counted);
  return true;
}

/* The heading a building is placed with (see above): the cell's own angles for everything but the producers. */
static uint32_t AiConstruction_PlacementHeading(FieldGridCell *cell,PckArmyAssetIdCatalog armyAssetId,
                                                FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime)
{
  uint32_t cellHeading = (uint32_t)(uint16_t)cell->triangle0NormalAngles;
  if (!AiImproved_RepairsActive() && !AiImproved_IsActive()) {
    return cellHeading;
  }
  ModelDefinition *definition = AiConstruction_AssetDefinition(armyAssetId);
  if (definition == nullptr) {
    return cellHeading;
  }
  ModelRuntimeClassId classId = definition->runtimeClassId;
  if (classId != MODEL_RUNTIME_CLASS_13 && classId != MODEL_RUNTIME_CLASS_11 && classId != MODEL_RUNTIME_CLASS_22) {
    return cellHeading;
  }
  Q12 centreX;
  Q12 centreY;
  if (!AiConstruction_BaseCentre(&centreX,&centreY)) {
    return cellHeading;
  }
  uint32_t bestHeading = cellHeading;
  int64_t bestDistance = -1;
  for (int step = 0; step < AI_CONSTRUCTION_HEADING_STEPS; step++) {
    uint32_t heading = (uint32_t)((step * FIXED_ANGLE16_FULL_TURN) / AI_CONSTRUCTION_HEADING_STEPS) &
                       FIXED_ANGLE16_MASK;
    Q12 doorX;
    Q12 doorY;
    AiConstruction_DoorForHeading(definition,heading,cell->worldX,cell->worldY,&doorX,&doorY);
    int64_t distance = AiImproved_DistanceSquaredQ12(doorX,doorY,centreX,centreY);
    if (distance <= bestDistance) {
      continue;
    }
    if (!ArmyPlacement_CanPlaceAssetAtFieldPoint(1,0,heading,cell->worldY,cell->worldX,armyAssetId,factionIndex,
                                                 worldRuntime,nullptr)) {
      continue;
    }
    bestDistance = distance;
    bestHeading = heading;
  }
  return bestHeading;
}

/* Repairs: the headquarters of a faction that has none yet/* Repairs: the headquarters of a faction that has none yet/* Repairs: the headquarters of a faction that has none yet (Paradis: placed after the pioneer founded the base).
   The original only tries the large base sites with the class-18 support left out, so the pioneer base that
   carries the new base did not count and the headquarters never came. Here every field cell within 6 world units
   of one of the faction's buildings counts, with the ordinary placement test (the pioneer base supports it), the
   nearest one to the faction's buildings first, as long as the region around it is reachable. */
static bool AiConstructionPlanner_PlaceFirstHeadquarters(PckArmyAssetIdCatalog armyAssetId,FactionRuntimeIndex factionIndex,
                                                         WorldRuntimeContext *worldRuntime,uint32_t radiusMetric)
{
  FieldGridAsset *grid = worldRuntime->fieldGrid;
  if (grid == nullptr) {
    return false;
  }
  const int64_t reach = 6 * (int64_t)Q12_ONE;
  FieldGridCell *bestCell = nullptr;
  int64_t bestDistance = INT64_MAX;
  uint32_t cellCount = grid->gridWidth * grid->gridHeight;
  FieldGridCell *cell = grid->cells;
  for (uint32_t c = 0; c < cellCount; c++, cell++) {
    int64_t nearest = INT64_MAX;
    AiStructureWorkspaceEntry *entry = g_AiWorkspace00Structures;
    for (uint32_t k = 0; k < g_AiWorkspace00Count; k++, entry++) {
      ModelRuntimeSlot *structure = entry->runtimeSlotAddressOrZero;
      if (structure == nullptr) continue;
      ModelRuntimeNode *node = structure->rootModelNodeOrSavedOffset.modelNode;
      int64_t dx = (int64_t)cell->worldX - (node->worldTransform).translation.x;
      int64_t dy = (int64_t)cell->worldY - (node->worldTransform).translation.y;
      if (dx > reach || dx < -reach || dy > reach || dy < -reach) continue;
      int64_t distance = dx * dx + dy * dy;
      if (distance < nearest) nearest = distance;
    }
    if (nearest > reach * reach || nearest >= bestDistance) continue;
    if (Any(cell->flagsAndMaterial & FIELD_CELL_XENITE_OR_TRITIUM_SUPPORT_MASK)) continue;
    if (!ArmyPlacement_CanPlaceAssetAtFieldPoint(0,0,(uint32_t)(uint16_t)cell->triangle0NormalAngles,cell->worldY,
                                                 cell->worldX,armyAssetId,factionIndex,worldRuntime,nullptr)) continue;
    if (GridReachability_RebuildConnectedRegionAroundWorldPoint(radiusMetric,cell->worldY,cell->worldX)) continue;
    bestDistance = nearest;
    bestCell = cell;
  }
  if (bestCell == nullptr) {
    return false;
  }
  AiImproved_Log("Partei %d: setzt das Hauptquartier bei %d/%d",factionIndex,bestCell->worldX / Q12_ONE,
                 bestCell->worldY / Q12_ONE);
  AiConstructionPlanner_CreatePlacedAsset(bestCell,armyAssetId,factionIndex,worldRuntime);
  return true;
}

/* Repairs: a pending resource extractor (ARM 330 / 332) for which no workspace-08 site passes the placement test
   goes onto a deposit cell next to one of the faction's pioneer vehicles that stands still: the cell nearest to the
   pioneer, within its support radius, where the ordinary placement test passes. The workspace sites are one cell
   per deposit, picked without the terrain test, so on a slope the pioneer waited beside a deposit whose site the
   mine could not take while the cells around it could (a player puts the mine there). This is what founds the base
   of a faction that starts with nothing but a pioneer (Paradis). */
static void AiConstructionPlanner_PlaceExtractorNextToPioneer
          (PckArmyAssetIdCatalog armyAssetId,FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime)
{
  if (!AiImproved_RepairsActive() || !AiImproved_IsAiFaction(factionIndex) || worldRuntime->fieldGrid == nullptr) {
    return;
  }
  FieldGridAsset *grid = worldRuntime->fieldGrid;
  AiRuntimeWorkspaceEntry *unitEntry = g_AiWorkspace01Units;
  for (uint32_t u = 0; u < g_AiWorkspace01Count; u++, unitEntry++) {
    ModelRuntimeSlot *unitModel = unitEntry->modelRuntime;
    if (unitModel == nullptr) continue;
    ModelDefinition *unitDefinition = unitModel->definitionOrSavedId.runtimeDefinition;
    if (unitDefinition->runtimeClassId != MODEL_RUNTIME_CLASS_18 || unitDefinition->supportRadius <= 0) continue;
    ArmyRuntimeSlot *pioneer = unitModel->ownerArmyRuntimeOrSavedOffset.armyRuntime;
    if (pioneer == nullptr || pioneer->modelNodeRuntime == nullptr ||
        Any(pioneer->movementStateFlags & ARMY_MOVEMENT_ACTIVE)) continue;
    Q12 pioneerX = (pioneer->modelNodeRuntime->worldTransform).translation.x;
    Q12 pioneerY = (pioneer->modelNodeRuntime->worldTransform).translation.y;
    int64_t reach = unitDefinition->supportRadius;
    FieldGridCell *bestCell = nullptr;
    int64_t bestDistance = INT64_MAX;
    FieldGridCell *cell = grid->cells;
    uint32_t cellCount = grid->gridWidth * grid->gridHeight;
    for (uint32_t c = 0; c < cellCount; c++, cell++) {
      if (!Any(cell->flagsAndMaterial & FIELD_CELL_XENITE_OR_TRITIUM_SUPPORT_MASK)) continue;
      int64_t dx = (int64_t)cell->worldX - pioneerX;
      int64_t dy = (int64_t)cell->worldY - pioneerY;
      if (dx > reach || dx < -reach || dy > reach || dy < -reach) continue;
      int64_t distance = dx * dx + dy * dy;
      if (distance > reach * reach || distance >= bestDistance) continue;
      if (!ArmyPlacement_CanPlaceAssetAtFieldPoint(0,0,(uint32_t)(uint16_t)cell->triangle0NormalAngles,cell->worldY,
                                                   cell->worldX,armyAssetId,factionIndex,worldRuntime,nullptr)) continue;
      bestDistance = distance;
      bestCell = cell;
    }
    if (bestCell != nullptr) {
      AiImproved_Log("Partei %d: setzt Bergwerk (ARM %u) neben den Pionier bei %d/%d",factionIndex,(unsigned)armyAssetId,
                     bestCell->worldX / Q12_ONE,bestCell->worldY / Q12_ONE);
      AiConstructionPlanner_CreatePlacedAsset(bestCell,armyAssetId,factionIndex,worldRuntime);
      return;
    }
  }
}

/* Builds a pending resource structure (ARM_0330/ARM_0332) of the AI faction: at the first workspace-08 site of
   this asset where the mode-0 placement test passes it creates the structure with the site's heading, rebuilds
   its model transforms, dispatches its class command, starts the removal effect of its model definition and
   removes the asset from the faction's pending list. Nothing happens when no site passes.
*/
void AiConstructionPlanner_PlaceSpecialAssetFromWorkspace
          (PckArmyAssetIdCatalog armyAssetId,FactionRuntimeIndex factionIndex,
          WorldRuntimeContext *worldRuntime)

{
  FieldGridCell *workspaceRecord;
  ModelRuntimeNode *modelNodeRuntime;
  ModelRuntimeSlot *createdModelRuntime;
  ModelDefinition *createdModelDefinition;
  ArmyRuntimeSlot *createdArmy;
  int recordsRemaining;
  AiTerrainFeatureWorkspaceEntry *terrainFeatureEntry;

  terrainFeatureEntry = g_AiWorkspace08TerrainFeatureSites;
  for (recordsRemaining = g_AiWorkspace08Count; recordsRemaining != 0; recordsRemaining--, terrainFeatureEntry++) {
    if (armyAssetId != terrainFeatureEntry->armyAssetId) {
      continue;
    }
    workspaceRecord = terrainFeatureEntry->cell;
    if (AiPlacement_TestWorkspaceRecordAtPoint(armyAssetId,workspaceRecord,factionIndex,worldRuntime)) {
      continue; /* placement rejected */
    }
    createdArmy = ArmyRuntime_CreateInstanceFromAsset
                      (ARMY_CREATE_UNLOCK_TECHNOLOGY,(uint32_t)(uint16_t)workspaceRecord->triangle0NormalAngles,
                       workspaceRecord->worldY,workspaceRecord->worldX,factionIndex,armyAssetId,
                       worldRuntime,nullptr);
    if (createdArmy == nullptr) {
      return;
    }
    modelNodeRuntime = createdArmy->modelNodeRuntime;
    createdModelRuntime = createdArmy->modelRuntimeOrSavedOffset.modelRuntime;
    modelNodeRuntime->tintArgb = 0;
    createdModelDefinition = createdModelRuntime->definitionOrSavedId.runtimeDefinition;
    ModelNodeRuntime_RebuildTransformsFromRoot(modelNodeRuntime);
    ArmyRuntime_DispatchClassCommand(createdArmy,worldRuntime);
    EffectRuntimePool_CreateInstanceFromDefinition
              (EFFECT_RUNTIME_COMPLETION_NONE,THANDOR_COMPOUND(EffectRuntimeOwnerReference){nullptr},
               modelNodeRuntime->modelPayload.worldRotationAngle2,
               modelNodeRuntime->modelPayload.worldRotationAngle1,
               modelNodeRuntime->modelPayload.worldRotationAngle0,
               modelNodeRuntime->worldTransform.translation.z,
               modelNodeRuntime->worldTransform.translation.y,
               modelNodeRuntime->worldTransform.translation.x,
               createdModelDefinition->removalEffectDefinitionReference.definition,
               worldRuntime);
    AiConstructionPlanner_ConsumeFactionPendingArmyAsset(armyAssetId,factionIndex);
    return;
  }
  /* Repairs: no workspace site took it */
  AiConstructionPlanner_PlaceExtractorNextToPioneer(armyAssetId,factionIndex,worldRuntime);
}

/* Works through the faction's pending asset requests (workspace 04) in order and hands each to its placement
   handler by ARM id: 300 only while no unassigned 330 exists, 330/332 at a workspace site, 333 derived from a
   330/332 site, other ids below 340 at a reachable candidate, ids from 340 on near the faction anchor.
   Returns true as soon as a handler has placed an asset (g_AiConstructionPendingAssetConsumedCount).
*/
bool AiConstructionPlanner_ProcessPendingAssetRequests
          (FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime)

{
  PckArmyAssetIdCatalog armyAssetId;
  int remainingRequests;
  AiRuntimeWorkspaceEntry *requestEntry;
  ArmyAssetRecordPrefix *armyAsset;
  
  g_AiConstructionPendingAssetConsumedCount = 0;
  requestEntry = g_AiWorkspace04RequestedAssets;
  for (remainingRequests = g_AiWorkspace04Count; remainingRequests != 0; remainingRequests--, requestEntry++) {
    armyAssetId = requestEntry->armyAssetId;
    if (armyAssetId == ARM_0300_BUILDING_MDL0301) {
      /* Repairs: a faction without a headquarters (Paradis) places it as soon as it can; only the headquarters
         builds, so waiting for a mine that has no deposit in reach left it without one for the whole game */
      if (!AiPrimaryWorkspace_HasUnassignedEntryById(ARM_0330_BUILDING_MDL0303) ||
          ((g_AiWorkspaceOwnedAsset300Runtime == nullptr) && AiImproved_RepairsActive() &&
           AiImproved_IsAiFaction(factionIndex))) {
        AiConstructionPlanner_PlaceArmyAssetAtReachableCandidate
                  (armyAssetId,factionIndex,worldRuntime);
      }
    }
    else if ((armyAssetId == ARM_0330_BUILDING_MDL0303) ||
            (armyAssetId == ARM_0332_BUILDING_MDL0302)) {
      AiConstructionPlanner_PlaceSpecialAssetFromWorkspace(armyAssetId,factionIndex,worldRuntime);
    }
    else if (armyAssetId == ARM_0331_BUILDING_MDL0308) {
      AiConstructionPlanner_PlaceArmyAssetAtReachableCandidate
                (armyAssetId,factionIndex,worldRuntime);
    }
    else if (armyAssetId == ARM_0333_BUILDING_MDL0307) {
      AiConstructionPlanner_PlaceTritiumStorageNearResourceSite
                (ARM_0333_BUILDING_MDL0307,factionIndex,worldRuntime);
    }
    else if (armyAssetId < ARM_0340_BUILDING_MDL0314) {
      if (ArmyAssetRegistry_FindById(armyAssetId,&armyAsset) == 0) {
        /* The original then compares the selected definition's placementContactKindIndex with 1 (ignoring the
           selector's status), but both outcomes call the same placement handler. */
        (void)ModelDefinition_SelectFactionUnlockedLinkedDefinition
                          (factionIndex,armyAsset->rootNodeOffsetOrPointer); /* 32-bit format field: ArmyAssetRecord.rootNodeOffsetOrPointer */
        AiConstructionPlanner_PlaceArmyAssetAtReachableCandidate
                  (armyAssetId,factionIndex,worldRuntime);
      }
    }
    else {
      AiConstructionPlanner_PlaceExtendedAssetNearFactionAnchor
                (armyAssetId,factionIndex,worldRuntime);
    }
    if (g_AiConstructionPendingAssetConsumedCount != 0) {
      return true;
    }
  }
  return false;
}

/* Tries to build armyAssetId next to the workspace-08 resource site siteEntry: when
   AiPlacement_ReserveAdditionalSpecialSite accepts the site and a placeable base cell is found near it, the asset
   is created there (technology unlocked), its model node's tint cleared, its transforms rebuilt, its
   class command dispatched, the removal effect of its model definition started at the node and the asset
   removed from the faction's pending list. Returns true when the attempt is over (asset created, or its creation
   failed), false when the site is not usable. */
static bool AiConstructionPlanner_TryPlaceStorageAtResourceSite
          (AiTerrainFeatureWorkspaceEntry *siteEntry,PckArmyAssetIdCatalog armyAssetId,
          FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime)
{
  FieldGridCell *sourceCell;
  ModelRuntimeNode *createdModelNode;
  ModelRuntimeSlot *createdModelRuntime;
  ModelDefinition *createdModelDefinition;
  ArmyRuntimeSlot *createdArmy;
  Q12 anchorXQ12;
  Q12 anchorYQ12;

  sourceCell = siteEntry->cell;
  if (AiPlacement_ReserveAdditionalSpecialSite(siteEntry->armyAssetId,sourceCell,factionIndex,worldRuntime)) {
    return false;
  }
  if (!AiPlacement_FindNearestPlaceableBaseSite
         (sourceCell->worldY,sourceCell->worldX,armyAssetId,factionIndex,worldRuntime,&anchorYQ12,&anchorXQ12)) {
    return false;
  }
  createdArmy = ArmyRuntime_CreateInstanceFromAsset
                    (ARMY_CREATE_UNLOCK_TECHNOLOGY,0,anchorYQ12,anchorXQ12,factionIndex,armyAssetId,worldRuntime,
                     nullptr);
  if (createdArmy == nullptr) {
    return true;
  }
  createdModelNode = createdArmy->modelNodeRuntime;
  createdModelRuntime = createdArmy->modelRuntimeOrSavedOffset.modelRuntime;
  createdModelNode->tintArgb = 0;
  createdModelDefinition = createdModelRuntime->definitionOrSavedId.runtimeDefinition;
  ModelNodeRuntime_RebuildTransformsFromRoot(createdModelNode);
  ArmyRuntime_DispatchClassCommand(createdArmy,worldRuntime);
  EffectRuntimePool_CreateInstanceFromDefinition
            (EFFECT_RUNTIME_COMPLETION_NONE,THANDOR_COMPOUND(EffectRuntimeOwnerReference){nullptr},
             createdModelNode->modelPayload.worldRotationAngle2,
             createdModelNode->modelPayload.worldRotationAngle1,
             createdModelNode->modelPayload.worldRotationAngle0,
             createdModelNode->worldTransform.translation.z,
             createdModelNode->worldTransform.translation.y,
             createdModelNode->worldTransform.translation.x,
             createdModelDefinition->removalEffectDefinitionReference.definition,
             worldRuntime);
  AiConstructionPlanner_ConsumeFactionPendingArmyAsset(armyAssetId,factionIndex);
  return true;
}

/* Runs AiConstructionPlanner_TryPlaceStorageAtResourceSite for every workspace-08 site of siteAssetId in order;
   returns true as soon as one attempt is over. */
static bool AiConstructionPlanner_TryPlaceStorageAtResourceSitesOf
          (PckArmyAssetIdCatalog siteAssetId,PckArmyAssetIdCatalog armyAssetId,FactionRuntimeIndex factionIndex,
          WorldRuntimeContext *worldRuntime)
{
  AiTerrainFeatureWorkspaceEntry *siteEntry;
  int remainingEntries;

  siteEntry = g_AiWorkspace08TerrainFeatureSites;
  for (remainingEntries = g_AiWorkspace08Count; remainingEntries != 0; remainingEntries--, siteEntry++) {
    if ((siteEntry->armyAssetId == siteAssetId) &&
        AiConstructionPlanner_TryPlaceStorageAtResourceSite(siteEntry,armyAssetId,factionIndex,worldRuntime)) {
      return true;
    }
  }
  return false;
}

static void AiConstructionPlanner_CreatePlacedAsset
          (FieldGridCell *cell,PckArmyAssetIdCatalog armyAssetId,FactionRuntimeIndex factionIndex,
          WorldRuntimeContext *worldRuntime);

/* Builds a pending ARM_0333 (the caller's only asset here, 0x14D) next to a resource site: first for each
   ARM_0330 site of workspace 08, then for each ARM_0332 site, where AiPlacement_ReserveAdditionalSpecialSite
   accepts the site it creates the structure at the nearest valid workspace-09 cell, initialises it like the other
   planners, starts its effect and removes it from the pending list (a failed creation ends the attempt). Without
   such a site it falls back to AiConstructionPlanner_PlaceArmyAssetAtReachableCandidate.
*/
void AiConstructionPlanner_PlaceTritiumStorageNearResourceSite(PckArmyAssetIdCatalog armyAssetId,FactionRuntimeIndex factionIndex,
          WorldRuntimeContext *worldRuntime)

{
  /* Repairs / improved AI: the bridge rule puts the storage building at the base site nearest to the deposit it
     grows toward (at least 3.0 from it, so the mine keeps its place, and at least 1.0 nearer than every building so
     far, so each step makes progress; without such a site the deposit is given up and the storage building goes
     where the original puts it) */
  Q12 bridgeX;
  Q12 bridgeY;
  if (AiImproved_BridgeTarget(factionIndex,&bridgeX,&bridgeY) && (g_AiWorkspace09Count != 0)) {
    FieldGridCell *bestCell = nullptr;
    int64_t frontier = AiImproved_NearestOwnStructureManhattanQ12(bridgeX,bridgeY);
    int64_t bestDistance = frontier == INT64_MAX ? INT64_MAX : frontier - Q12_ONE;
    FieldGridCell **gridCellCursor = g_AiWorkspace09Cells;
    for (uint32_t remainingCells = g_AiWorkspace09Count; remainingCells != 0; remainingCells--, gridCellCursor++) {
      FieldGridCell *candidateCell = *gridCellCursor;
      int64_t deltaX = (int64_t)candidateCell->worldX - bridgeX;
      int64_t deltaY = (int64_t)candidateCell->worldY - bridgeY;
      int64_t distance = (deltaX < 0 ? -deltaX : deltaX) + (deltaY < 0 ? -deltaY : deltaY);
      if ((distance >= bestDistance) || (distance < 3 * (int64_t)Q12_ONE)) continue;
      if (!ArmyPlacement_CanPlaceAssetAtFieldPoint
             (1,0,(uint32_t)(uint16_t)candidateCell->triangle0NormalAngles,candidateCell->worldY,candidateCell->worldX,
              armyAssetId,factionIndex,worldRuntime,nullptr)) continue;
      bestDistance = distance;
      bestCell = candidateCell;
    }
    if (bestCell != nullptr) {
      AiImproved_OnBridgeStoragePlaced(factionIndex,bestCell->worldX,bestCell->worldY);
      AiConstructionPlanner_CreatePlacedAsset(bestCell,armyAssetId,factionIndex,worldRuntime);
      return;
    }
    AiImproved_BridgeNoProgress(factionIndex);
  }
  if (AiConstructionPlanner_TryPlaceStorageAtResourceSitesOf
        (ARM_0330_BUILDING_MDL0303,armyAssetId,factionIndex,worldRuntime)) {
    return;
  }
  if (AiConstructionPlanner_TryPlaceStorageAtResourceSitesOf
        (ARM_0332_BUILDING_MDL0302,armyAssetId,factionIndex,worldRuntime)) {
    return;
  }
  AiConstructionPlanner_PlaceArmyAssetAtReachableCandidate(armyAssetId,factionIndex,worldRuntime);
}

/* Creates armyAssetId for the faction at the chosen field cell (technology unlocked), clears the new model
   node's tint, rebuilds its transforms, dispatches the army's class command, spawns the removal effect
   of its model definition at the node and removes the asset from the faction's pending list. Shared tail of the
   placement planners; nothing happens when the creation fails. */
static void AiConstructionPlanner_CreatePlacedAsset
          (FieldGridCell *cell,PckArmyAssetIdCatalog armyAssetId,FactionRuntimeIndex factionIndex,
          WorldRuntimeContext *worldRuntime)
{
  ArmyRuntimeSlot *createdArmy;
  ModelRuntimeNode *modelNodeRuntime;
  ModelRuntimeSlot *createdModelRuntime;
  ModelDefinition *createdModelDefinition;

  const uint32_t placementHeading = AiConstruction_PlacementHeading(cell,armyAssetId,factionIndex,worldRuntime);
  createdArmy = ArmyRuntime_CreateInstanceFromAsset
                    (ARMY_CREATE_UNLOCK_TECHNOLOGY,placementHeading,
                     cell->worldY,cell->worldX,factionIndex,armyAssetId,worldRuntime,nullptr);
  if (createdArmy == nullptr) {
    return;
  }
  modelNodeRuntime = createdArmy->modelNodeRuntime;
  createdModelRuntime = createdArmy->modelRuntimeOrSavedOffset.modelRuntime;
  modelNodeRuntime->tintArgb = 0;
  createdModelDefinition = createdModelRuntime->definitionOrSavedId.runtimeDefinition;
  ModelNodeRuntime_RebuildTransformsFromRoot(modelNodeRuntime);
  ArmyRuntime_DispatchClassCommand(createdArmy,worldRuntime);
  EffectRuntimePool_CreateInstanceFromDefinition
            (EFFECT_RUNTIME_COMPLETION_NONE,THANDOR_COMPOUND(EffectRuntimeOwnerReference){nullptr},
             modelNodeRuntime->modelPayload.worldRotationAngle2,
             modelNodeRuntime->modelPayload.worldRotationAngle1,
             modelNodeRuntime->modelPayload.worldRotationAngle0,
             modelNodeRuntime->worldTransform.translation.z,
             modelNodeRuntime->worldTransform.translation.y,
             modelNodeRuntime->worldTransform.translation.x,
             createdModelDefinition->removalEffectDefinitionReference.definition,
             worldRuntime);
  AiConstructionPlanner_ConsumeFactionPendingArmyAsset(armyAssetId,factionIndex);
  if (AiImproved_LogEnabled()) {
    /* protocol: where the building went and how much room is left to the nearest other building of the faction
       (the lane the vehicles drive through) */
    int64_t nearest = INT64_MAX;
    AiStructureWorkspaceEntry *entry = g_AiWorkspace00Structures;
    for (uint32_t i = 0; i < g_AiWorkspace00Count; i++, entry++) {
      ModelRuntimeSlot *structure = entry->runtimeSlotAddressOrZero;
      if (structure == nullptr || structure == createdModelRuntime) {
        continue;
      }
      ModelRuntimeNode *node = structure->rootModelNodeOrSavedOffset.modelNode;
      int64_t gap = AiImproved_DistanceSquaredQ12(cell->worldX,cell->worldY,(node->worldTransform).translation.x,
                                                  (node->worldTransform).translation.y);
      int64_t radii = (int64_t)createdModelDefinition->footprintRadius +
                      structure->definitionOrSavedId.runtimeDefinition->footprintRadius;
      gap = FixedMath_UInt64Sqrt((UInt64Half32)((uint64_t)gap >> 32),(UInt64Half32)gap) - radii;
      if (gap < nearest) nearest = gap;
    }
    Q12 doorX = 0;
    Q12 doorY = 0;
    bool hasDoor = AiConstruction_StructureDoor(createdModelRuntime,&doorX,&doorY);
    char door[48];
    door[0] = 0;
    if (hasDoor) {
      snprintf(door,sizeof door,", Tor bei %d/%d",doorX / Q12_ONE,doorY / Q12_ONE);
    }
    AiImproved_Log("Partei %d: baut ARM %u bei %d/%d, Luecke zum naechsten Gebaeude %d.%02d Felder%s",
                   factionIndex,(unsigned)armyAssetId,cell->worldX / Q12_ONE,cell->worldY / Q12_ONE,
                   nearest == INT64_MAX ? 0 : (int)(nearest / Q12_ONE),
                   nearest == INT64_MAX ? 0 : (int)((nearest % Q12_ONE) * 100 / Q12_ONE),door);
  }
}

/* Improved AI: places a pending defence building without an attack. Among the workspace-09 cells (inside the base)
   at least 2.0 from every active structure, at least 5.0 from every own defence building and at least 10.0 from
   the threat (AiImproved_NearestThreat from the base centre: the nearest hostile building the faction has seen, else
   the map centre) it takes the one nearest to the threat where the mode-1 placement test passes, i.e. the edge of
   the base facing the enemy. Distances are Manhattan. */
static void AiConstructionPlanner_PlaceDefenceTowardThreat
          (PckArmyAssetIdCatalog armyAssetId,FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime)
{
  Q12 baseX;
  Q12 baseY;
  Q12 threatX;
  Q12 threatY;
  if ((g_AiWorkspace09Count == 0) || !AiImproved_FactionBaseCentre(&baseX,&baseY) ||
      !AiImproved_NearestThreat(baseX,baseY,&threatX,&threatY)) {
    return;
  }
  int64_t bestScore = INT64_MAX;
  FieldGridCell *bestCell = nullptr;
  FieldGridCell **gridCellCursor = g_AiWorkspace09Cells;
  for (uint32_t remainingCells = g_AiWorkspace09Count; remainingCells != 0; remainingCells--, gridCellCursor++) {
    FieldGridCell *candidateCell = *gridCellCursor;
    int64_t deltaX = (int64_t)candidateCell->worldX - threatX;
    int64_t deltaY = (int64_t)candidateCell->worldY - threatY;
    int64_t threatDistance = (deltaX < 0 ? -deltaX : deltaX) + (deltaY < 0 ? -deltaY : deltaY);
    if ((threatDistance >= bestScore) || (threatDistance < 10 * (int64_t)Q12_ONE)) continue;
    if (AiPrimaryWorkspace_GetMinimumActiveManhattanDistanceToPoint(candidateCell->worldY,candidateCell->worldX) <
        2 * Q12_ONE) continue;
    if (AiImproved_DefenceBuildingNear(candidateCell->worldX,candidateCell->worldY,5 * (int64_t)Q12_ONE)) continue;
    if (!AiConstruction_KeepsBaseOpen(armyAssetId,candidateCell->worldX,candidateCell->worldY,0)) continue;
    if (!ArmyPlacement_CanPlaceAssetAtFieldPoint
           (1,0,(uint32_t)(uint16_t)candidateCell->triangle0NormalAngles,candidateCell->worldY,candidateCell->worldX,
            armyAssetId,factionIndex,worldRuntime,nullptr)) continue;
    bestScore = threatDistance;
    bestCell = candidateCell;
  }
  if (bestCell == nullptr) {
    return;
  }
  AiImproved_Log("Partei %d: baut Verteidigung (ARM %u) bei %d/%d, Richtung Gegner %d/%d",factionIndex,
                 (unsigned)armyAssetId,bestCell->worldX / Q12_ONE,bestCell->worldY / Q12_ONE,threatX / Q12_ONE,
                 threatY / Q12_ONE);
  AiConstructionPlanner_CreatePlacedAsset(bestCell,armyAssetId,factionIndex,worldRuntime);
}

/* Default placement of a pending building (every asset the request dispatcher does not handle specially), only
   while the faction's primary anchor cooldown is nonzero: among the workspace-09 cells at least 0x2000 from every
   active primary-workspace structure it takes the one with the lowest Manhattan distance to the primary anchor
   plus 4x the distance to the nearest workspace-02 site (at most 0x5000) or, without such a site, 2x the
   distance to the nearest workspace-03 site (at most 0x8000), where the mode-1 placement test passes. The
   building is created there, initialised like the other planners and removed from the pending list.
*/
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
    /* Improved AI: a few defence buildings also without an attack */
    if ((g_GameFactionRuntimeImage.records[factionIndex].primaryAnchorCooldown == 0) &&
        (armyAssetId >= ARM_0340_BUILDING_MDL0314) && (armyAssetId < ARM_0380_BUILDING_MDL0329) &&
        ((AiImproved_IsActive() && (AiImproved_CountPlacedDefenceBuildings() < AiImproved_ProactiveTowerLimit(factionIndex))) ||
         AiImproved_LaserCoilPlacementWanted(armyAssetId))) {
      AiConstructionPlanner_PlaceDefenceTowardThreat(armyAssetId,factionIndex,worldRuntime);
    }
    return;
  }
  bestScore = INT32_MAX;
  /* as in AiConstructionPlanner_PlaceArmyAssetAtReachableCandidate: first only cells that leave the doors of the
     own factories free, then, when none does, the original choice */
  for (int lanePass = (AiImproved_RepairsActive() || AiImproved_IsActive()) ? 0 : 2; lanePass < 3; lanePass++) {
  gridCellCursor = g_AiWorkspace09Cells;
  for (remainingCells = g_AiWorkspace09Count; remainingCells != 0; remainingCells--, gridCellCursor++) {
    candidateCell = *gridCellCursor;
    if ((lanePass < 2) &&
        !AiConstruction_KeepsBaseOpen(armyAssetId,candidateCell->worldX,candidateCell->worldY,lanePass)) {
      continue;
    }
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
    break;
  }
  }
  if (bestScore < INT32_MAX) {
    AiConstructionPlanner_CreatePlacedAsset(bestCell,armyAssetId,factionIndex,worldRuntime);
  }
}

/* Places a pending asset at a workspace-10 cell, only while the faction's primary anchor cooldown is 0. Each cell
   is scored by its Manhattan distance to the faction's ARM_0300 structure (g_AiWorkspaceOwnedAsset300Runtime;
   the raw cell coordinates without one) plus 16 random bits; the lowest-scoring cell where the mode-1
   placement test passes and GridReachability_RebuildConnectedRegionAroundWorldPoint (with the radius of the
   asset's model definition) finds a connected region wins. The asset is created there, initialised like the
   other planners and removed from the pending list.
*/
void AiConstructionPlanner_PlaceArmyAssetAtReachableCandidate
          (PckArmyAssetIdCatalog armyAssetId,FactionRuntimeIndex factionIndex,
          WorldRuntimeContext *worldRuntime)

{
  uint32_t radiusMetric;
  FieldGridCell *candidateCell;
  int distanceX;
  int candidateScore;
  uint32_t randomBits;
  int remainingCells;
  int distanceY;
  FieldGridCell **gridCellCursor;
  bool regionUnreachable;
  ArmyAssetRecordPrefix *armyAsset;
  ModelDefinitionRecordPrefix *modelDefinition;
  FieldGridCell *bestCell;
  int bestScore;

  /* Repairs: under an alarm of 90 s and more the building still goes up, away from the hits */
  const bool buildDuringAlarm = (g_GameFactionRuntimeImage.records[factionIndex].primaryAnchorCooldown != 0) &&
                                AiImproved_BuildDuringAlarm(factionIndex);
  if ((ArmyAssetRegistry_FindById(armyAssetId,&armyAsset) != 0) ||
     ((g_GameFactionRuntimeImage.records[factionIndex].primaryAnchorCooldown != 0) && !buildDuringAlarm)) {
    return;
  }
  modelDefinition = ModelDefinitionRegistry_FindById
                     (Thandor_U32ToPointer<AiLinkedDefinitionListView>(armyAsset->rootNodeOffsetOrPointer)->definitionIds[0]); /* 32-bit format field: ArmyAssetRecord.rootNodeOffsetOrPointer */
  if (modelDefinition == nullptr) {
    return;
  }
  radiusMetric = reinterpret_cast<ModelDefinition *>(modelDefinition)->footprintRadius; /* prefix view -> full definition */
  /* Repairs: a faction without a headquarters places it with the pioneer base counting as support */
  if ((armyAssetId == ARM_0300_BUILDING_MDL0301) && (g_AiWorkspaceOwnedAsset300Runtime == nullptr) &&
      AiImproved_RepairsActive() && AiImproved_IsAiFaction(factionIndex) &&
      AiConstructionPlanner_PlaceFirstHeadquarters(armyAssetId,factionIndex,worldRuntime,radiusMetric)) {
    return;
  }
  if (g_AiWorkspace10Count == 0) {
    return;
  }
  bestScore = INT32_MAX;
  /* Two passes: first only cells that keep the base's lanes free (AiConstruction_KeepsBaseOpen), then, when none
     does, the original choice, so a cramped base still gets its building. */
  for (int lanePass = (AiImproved_RepairsActive() || AiImproved_IsActive()) ? 0 : 2; lanePass < 3; lanePass++) {
  gridCellCursor = g_AiWorkspace10Cells;
  for (remainingCells = g_AiWorkspace10Count; remainingCells != 0; remainingCells--, gridCellCursor++) {
    candidateCell = *gridCellCursor;
    if ((lanePass < 2) &&
        !AiConstruction_KeepsBaseOpen(armyAssetId,candidateCell->worldX,candidateCell->worldY,lanePass)) {
      continue;
    }
    distanceX = candidateCell->worldX;
    distanceY = candidateCell->worldY;
    if (g_AiWorkspaceOwnedAsset300Runtime != nullptr) {
      distanceX = distanceX -
                  (g_AiWorkspaceOwnedAsset300Runtime->rootModelNodeOrSavedOffset.modelNode->worldTransform).translation.x;
      if (distanceX < 0) {
        distanceX = -distanceX;
      }
      distanceY = distanceY -
                  (g_AiWorkspaceOwnedAsset300Runtime->rootModelNodeOrSavedOffset.modelNode->worldTransform).translation.y;
      if (distanceY < 0) {
        distanceY = -distanceY;
      }
    }
    randomBits = g_RandomGeneratorState.next();
    candidateScore = distanceY + distanceX + (randomBits & 0xffff);
    /* Improved AI: the second headquarters goes far from the first and from the enemy */
    int reserveScore;
    if ((armyAssetId == ARM_0300_BUILDING_MDL0301) &&
        AiImproved_ReserveHeadquartersCellScore(candidateCell->worldX,candidateCell->worldY,randomBits,&reserveScore)) {
      candidateScore = reserveScore;
    }
    if (buildDuringAlarm && AiImproved_AlarmKeepsCellFree(factionIndex,candidateCell->worldX,candidateCell->worldY)) {
      continue;
    }
    if ((candidateScore < bestScore) &&
       (ArmyPlacement_CanPlaceAssetAtFieldPoint
                          (1,0,(uint32_t)(uint16_t)candidateCell->triangle0NormalAngles,candidateCell->worldY,
                           candidateCell->worldX,armyAssetId,factionIndex,worldRuntime,
                           nullptr))) {
      /* Original quirk: the reachability test edits g_GridScratchPrimary itself (no copy and swap as in
         EntityPathing_RebuildOverlappingGroupRoutes); its footprint walkers can clear GRID_SCRATCH_BLOCKED on
         the blocked map-edge ring until the next terrain rebuild (see
         GridFootprint_ClearTraversalFlagsDiagonalNegative). */
      regionUnreachable = GridReachability_RebuildConnectedRegionAroundWorldPoint
                            (radiusMetric,candidateCell->worldY,candidateCell->worldX);
      if (!regionUnreachable) {
        bestCell = candidateCell;
        bestScore = candidateScore;
      }
    }
  }
  if (bestScore < INT32_MAX) {
    break; /* a cell that keeps the lanes free */
  }
  }
  if (bestScore < INT32_MAX) {
    AiConstructionPlanner_CreatePlacedAsset(bestCell,armyAssetId,factionIndex,worldRuntime);
  }
}

/* Called after the AI has placed an army asset: counts the placement (global counter and the faction's
   relationCounterB) and removes the first entry for that asset's registry record from the faction's pending
   primary army asset list, shifting the rest down. The registry lookup's result is not checked; an unknown id simply
   matches no entry.
*/
void AiConstructionPlanner_ConsumeFactionPendingArmyAsset
          (PckArmyAssetIdCatalog armyAssetId,FactionRuntimeIndex factionIndex)

{
  FactionRelationCounter *relationCounter;
  FactionArmyAssetCount *pendingAssetCount;
  FactionArmyAssetCount remainingAssets;
  uint32_t *assetPointerCursor;
  ArmyAssetRecordPrefix *armyAsset;
  
  g_AiConstructionPendingAssetConsumedCount++;
  remainingAssets = g_GameFactionRuntimeImage.records[factionIndex].primaryArmyAssetCount;
  /* Original quirk: the lookup status is not checked (an unknown id leaves the error code in armyAsset,
     which then matches no list entry) */
  ArmyAssetRegistry_FindById(armyAssetId,&armyAsset);
  assetPointerCursor = g_GameFactionRuntimeImage.records[factionIndex].primaryArmyAssetPointersOrIds;
  relationCounter = &g_GameFactionRuntimeImage.records[factionIndex].relationCounterB;
  (*relationCounter)++;
  for (; remainingAssets != 0; remainingAssets--) {
    if (armyAsset == Thandor_U32ToPointer<ArmyAssetRecordPrefix>(*assetPointerCursor)) break; /* 32-bit format field: GameFactionRuntimeRecord.primaryArmyAssetPointersOrIds */
    assetPointerCursor++;
  }
  if (remainingAssets == 0) {
    return;
  }
  pendingAssetCount = &g_GameFactionRuntimeImage.records[factionIndex].primaryArmyAssetCount;
  (*pendingAssetCount)--;
  /* Shift the entries after the match down by one. */
  for (remainingAssets--; remainingAssets != 0; remainingAssets--) {
    *assetPointerCursor = assetPointerCursor[1];
    assetPointerCursor++;
  }
}
