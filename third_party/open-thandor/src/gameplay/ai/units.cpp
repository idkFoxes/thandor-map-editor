/*
 * Open Thandor
 * Project: https://github.com/idkFoxes/open-thandor/tree/main
 * File: https://github.com/idkFoxes/open-thandor/blob/main/src/gameplay/ai/units.cpp
 * Reverse engineering by idkFoxes 2026
 */

#include <thandor/gameplay/ai/units.h>
#include <thandor/thandor.h>
#include <thandor/gameplay/ai/improved.h>

/* Module data. */

uint32_t g_AiCollectedEntityCount = 0;

/* The movement orders take the army slot through their ArmyMovementRuntime view (the same record: entity/model
   references, faction and movement state at the slot's offsets). */
static ArmyMovementRuntime *AiUnit_MovementView(ArmyRuntimeSlot *armyRuntime)
{
  return reinterpret_cast<ArmyMovementRuntime *>(armyRuntime);
}

/* Per AI tick for the faction's own units (workspace 01): clears the collected-army list, lets busy units
   (ARMY_MOVEMENT_ACTIVE / _ROUTE_POINT_REACHED in the movement state, the entity's common.commandFlags) wait
   for their behaviour cooldown (common.aiCommandCooldownTicks), skips units with ARMY_MOVEMENT_LOCKED, and
   dispatches the rest by model class: class 18 to UpdateSpecialClass12Entity, the ground, tracked, walker, water
   and glider classes (1/2/3/19/17) to SelectBestAnchorAction. The class is the runtimeClassId of the model's
   definition.
*/
void AiUnitBehavior_UpdateOwnUnits(FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime)

{
  int workspaceEntriesRemaining;
  AiRuntimeWorkspaceEntry *workspaceEntryCursor;
  MdlDefinitionSemanticPrefix *modelDefinition;
  int *behaviorCooldownTicks;
  ModelRuntimeSlot *unitModelRuntime;
  GameEntityRuntime *slotEntityRuntime;

  g_AiCollectedEntityCount = 0;
  workspaceEntriesRemaining = g_AiWorkspace01Count;
  workspaceEntryCursor = g_AiWorkspace01Units;
  for (; workspaceEntriesRemaining != 0; workspaceEntriesRemaining--, workspaceEntryCursor++) {
    unitModelRuntime = workspaceEntryCursor->modelRuntime;
    if (unitModelRuntime == nullptr) continue;
    slotEntityRuntime = unitModelRuntime->ownerArmyRuntimeOrSavedOffset.entityRuntime;
    if ((AiImproved_IsActive() || AiImproved_RepairsActive()) &&
        ((slotEntityRuntime->common.commandFlags & ToBits(ARMY_MOVEMENT_LOCKED)) == 0)) {
      const ModelRuntimeClassId unitClass = unitModelRuntime->definitionOrSavedId.runtimeDefinition->runtimeClassId;
      if (unitClass == MODEL_RUNTIME_CLASS_01_GROUND || unitClass == MODEL_RUNTIME_CLASS_02_TRACKED ||
          unitClass == MODEL_RUNTIME_CLASS_03_ARTICULATED_WALKER || unitClass == MODEL_RUNTIME_CLASS_19_WATER_SURFACE ||
          unitClass == MODEL_RUNTIME_CLASS_17_DEPLOYING_GLIDER) {
        ArmyRuntimeSlot *unitArmyRuntime = unitModelRuntime->ownerArmyRuntimeOrSavedOffset.armyRuntime;
        /* Repairs: a unit that does no damage to its target (out of reach for good, line of fire blocked,
           target in lava) gives it up instead of standing there for ever. */
        if (AiImproved_TrackUnit(factionIndex,unitArmyRuntime)) {
          continue;
        }
        /* Improved AI: badly damaged military units retreat to the base (even while busy) and wait there for
           their self repair. */
        if (AiImproved_IsActive() && AiImproved_HandleDamagedUnit(factionIndex,unitArmyRuntime)) {
          continue;
        }
        /* Improved AI from level 3: a radar Goliath holds a post toward the enemy for reconnaissance */
        if (AiImproved_HandleRadarUnit(factionIndex,unitArmyRuntime)) {
          continue;
        }
      }
    }
    if ((slotEntityRuntime->common.commandFlags &
         ToBits(ARMY_MOVEMENT_ACTIVE | ARMY_MOVEMENT_LOCKED | ARMY_MOVEMENT_ROUTE_POINT_REACHED)) != 0) {
      if ((slotEntityRuntime->common.commandFlags & ToBits(ARMY_MOVEMENT_LOCKED)) != 0) continue;
      /* Busy entities only get a behavior update when their cooldown runs out (or was already negative,
         which the increment below undoes). */
      behaviorCooldownTicks = &slotEntityRuntime->common.aiCommandCooldownTicks;
      (*behaviorCooldownTicks)--;
      if (*behaviorCooldownTicks != 0) {
        if (*behaviorCooldownTicks > 0) continue;
        (*behaviorCooldownTicks)++;
      }
    }
    modelDefinition = reinterpret_cast<MdlDefinitionSemanticPrefix *>( /* field view of the ModelDefinition */
         unitModelRuntime->definitionOrSavedId.runtimeDefinition.get());
    if (modelDefinition->runtimeClassId == MODEL_RUNTIME_CLASS_18) {
      AiUnitBehavior_UpdatePioneerVehicle
                (modelDefinition,unitModelRuntime->ownerArmyRuntimeOrSavedOffset.armyRuntime,factionIndex,
                 worldRuntime);
    }
    else if (modelDefinition->runtimeClassId == MODEL_RUNTIME_CLASS_01_GROUND ||
             modelDefinition->runtimeClassId == MODEL_RUNTIME_CLASS_02_TRACKED ||
             modelDefinition->runtimeClassId == MODEL_RUNTIME_CLASS_03_ARTICULATED_WALKER ||
             modelDefinition->runtimeClassId == MODEL_RUNTIME_CLASS_19_WATER_SURFACE ||
             modelDefinition->runtimeClassId == MODEL_RUNTIME_CLASS_17_DEPLOYING_GLIDER) {
      AiUnitBehavior_SelectBestAnchorAction
                (modelDefinition,unitModelRuntime->ownerArmyRuntimeOrSavedOffset.armyRuntime,factionIndex,
                 worldRuntime);
    }
  }
}


/* Decides what an idle military unit (ground, tracked, walker,
                                       glider or water class) does next. Three scorers run in turn, each
   given the best score so far and returning it unchanged unless it found better: a general site (workspace 05)
   -> move there (kind 1), the faction anchor -> move there (kind 2), a secondary-workspace target (kind 3).
   With kind 3 or no winner the unit is collected for the group assignment of
   AiUnitGroup_AssignCollectedEntitiesToBestTarget.
*/
void AiUnitBehavior_SelectBestAnchorAction
          (MdlDefinitionSemanticPrefix *modelDefinition,ArmyRuntimeSlot *armyRuntimeSlot,
          FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime)

{
  int workspaceScore;
  AiCandidateScore32 factionAnchorScore;
  uint32_t selectedAnchorActionKind;
  int currentBestScore;
  AiScoredSiteWorkspaceEntry *selectedWorkspaceEntry = nullptr;
  AiGeneralSiteDistanceSelection workspaceSelection;
  AiSecondaryWorkspaceDistanceSelection secondarySelection;
  
  workspaceSelection = AiUnitBehavior_ComputeGeneralSiteDistanceScore(0,modelDefinition,armyRuntimeSlot);
  workspaceScore = workspaceSelection.score;
  currentBestScore = 0;
  if (workspaceScore != 0) {
    currentBestScore = workspaceScore;
    selectedWorkspaceEntry = workspaceSelection.selectedEntry;
  }
  selectedAnchorActionKind = (uint32_t)(workspaceScore != 0);
  factionAnchorScore = AiUnitBehavior_ComputeFactionAnchorDistanceScore
                    (factionIndex,currentBestScore,modelDefinition,armyRuntimeSlot);
  if (factionAnchorScore != currentBestScore) {
    selectedAnchorActionKind = 2;
    currentBestScore = factionAnchorScore;
  }
  secondarySelection = AiUnitBehavior_ComputeSecondaryWorkspaceDistanceScore
                    (currentBestScore,modelDefinition,armyRuntimeSlot);
  if (secondarySelection.score != currentBestScore) {
    /* kind 3 does not use the secondary entry (the unit is collected for the group assignment) */
    selectedAnchorActionKind = 3;
  }
  if (selectedAnchorActionKind != 0) {
    if (selectedAnchorActionKind == 1) {
      AiUnitCommand_AssignWorkspacePoint
                (selectedWorkspaceEntry,armyRuntimeSlot,worldRuntime);
      return;
    }
    if (selectedAnchorActionKind < 3) {
      AiUnitCommand_AssignFactionAnchorPoint(factionIndex,armyRuntimeSlot,worldRuntime);
      return;
    }
  }
  AiUnitBehavior_CollectUnassignedEntity(armyRuntimeSlot,worldRuntime);
}


/* Scores the general sites (workspace 05) for a unit and returns the best one if it beats currentBestScore:
   score = ((max(0, bias - Manhattan distance) * scale + site score) >> 12) * the unit's aiSiteScoreWeight,
   so nearer and richer sites score higher. Returns currentBestScore and a NULL entry when none beats it.
   modelDefinition is not used.
*/
AiGeneralSiteDistanceSelection AiUnitBehavior_ComputeGeneralSiteDistanceScore
          (AiCandidateScore32 currentBestScore,MdlDefinitionSemanticPrefix *modelDefinition,
          ArmyRuntimeSlot *armyRuntimeSlot)

{
  int recordsRemaining;
  Q12 negAbsDeltaXQ12;
  Q12 negAbsDeltaYQ12;
  int distanceTerm;
  int siteScore;
  AiScoredSiteWorkspaceEntry *bestEntry;
  AiScoredSiteWorkspaceEntry *workspaceRecordCursor;
  AiGeneralSiteDistanceSelection selection;

  bestEntry = nullptr;
  workspaceRecordCursor = g_AiWorkspace05GeneralSites;
  for (recordsRemaining = g_AiWorkspace05Count; recordsRemaining != 0; recordsRemaining--) {
    negAbsDeltaXQ12 = (armyRuntimeSlot->articulatedContact).fallbackPosition0Q12 -
            workspaceRecordCursor->cellWorldXQ12;
    if (negAbsDeltaXQ12 >= 0) {
      negAbsDeltaXQ12 = -negAbsDeltaXQ12;
    }
    negAbsDeltaYQ12 = (armyRuntimeSlot->articulatedContact).fallbackPosition1Q12 -
                   workspaceRecordCursor->cellWorldYQ12;
    if (negAbsDeltaYQ12 >= 0) {
      negAbsDeltaYQ12 = -negAbsDeltaYQ12;
    }
    /* bias - Manhattan distance, clamped at 0 */
    distanceTerm = negAbsDeltaXQ12 + negAbsDeltaYQ12 + (g_AiKnowledgeData->parameters).workspace05DistanceBiasQ12;
    if (distanceTerm < 0) {
      distanceTerm = 0;
    }
    siteScore = ((distanceTerm * (g_AiKnowledgeData->parameters).workspace05DistanceScaleQ12 +
             workspaceRecordCursor->score) >> Q12_SHIFT) * armyRuntimeSlot->aiSiteScoreWeight;
    if (currentBestScore < siteScore) {
      bestEntry = workspaceRecordCursor;
      currentBestScore = siteScore;
    }
    workspaceRecordCursor++;
  }
  selection.selectedEntry = bestEntry;
  selection.score = currentBestScore;
  return selection;
}


/* Scores one faction anchor point (anchorYQ12/anchorXQ12 as named in the faction record) for a unit and returns
   the higher of that score and currentBestScore. */
static AiCandidateScore32 AiUnitBehavior_ScoreFactionAnchorPoint
          (const ModelRuntimeNode *modelNode,GraphicsWorldCoordinateQ12 anchorYQ12,
          GraphicsWorldCoordinateQ12 anchorXQ12,const ArmyRuntimeSlot *armyRuntimeSlot,
          AiCandidateScore32 currentBestScore)

{
  int negAbsDeltaFirstQ12;
  Q12 negAbsDeltaSecondQ12;
  int distanceTerm;
  int anchorScore;

  /* Both anchor coordinates are compared with translation.x: the original reads that field twice, so this
     "distance" ignores the unit's Y. (anchorYQ12 holds the anchor's X, anchorXQ12 its Y, see
     AiUnitCommand_AssignFactionAnchorPoint.) Repairs: the second term uses the unit's Y, so the units near an
     attacked building really come to its defence; with the original's distance only units whose X lies close to
     both anchor coordinates did. */
  negAbsDeltaFirstQ12 = (modelNode->worldTransform).translation.x - anchorYQ12;
  if (negAbsDeltaFirstQ12 >= 0) {
    negAbsDeltaFirstQ12 = -negAbsDeltaFirstQ12;
  }
  negAbsDeltaSecondQ12 = (AiImproved_RepairsActive() ? (modelNode->worldTransform).translation.y :
                          (modelNode->worldTransform).translation.x) - anchorXQ12;
  if (negAbsDeltaSecondQ12 >= 0) {
    negAbsDeltaSecondQ12 = -negAbsDeltaSecondQ12;
  }
  /* bias - distance, clamped at 0 (improved AI: a wider radius by level) */
  distanceTerm = negAbsDeltaFirstQ12 + negAbsDeltaSecondQ12 +
          (int)AiImproved_AnchorResponseBiasQ12((g_AiKnowledgeData->parameters).factionAnchorDistanceBiasQ12);
  if (distanceTerm < 0) {
    distanceTerm = 0;
  }
  anchorScore = (distanceTerm * (g_AiKnowledgeData->parameters).factionAnchorDistanceScaleQ12 >> Q12_SHIFT) *
          armyRuntimeSlot->aiFactionAnchorScoreWeight;
  if (currentBestScore < anchorScore) {
    currentBestScore = anchorScore;
  }
  return currentBestScore;
}


/* Scores the faction's primary and secondary anchor points (each only while its cooldown runs) for a unit like
   the general sites: ((max(0, bias - distance) * scale) >> 12) * aiFactionAnchorScoreWeight, and returns the
   highest of these and currentBestScore. modelDefinition is not used.
*/
AiCandidateScore32 AiUnitBehavior_ComputeFactionAnchorDistanceScore
          (FactionRuntimeIndex factionIndex,AiCandidateScore32 currentBestScore,
          MdlDefinitionSemanticPrefix *modelDefinition,ArmyRuntimeSlot *armyRuntimeSlot)

{
  ModelRuntimeNode *modelNode;

  modelNode = armyRuntimeSlot->modelNodeRuntime;
  if (g_GameFactionRuntimeImage.records[factionIndex].primaryAnchorCooldown != 0) {
    currentBestScore = AiUnitBehavior_ScoreFactionAnchorPoint
              (modelNode,g_GameFactionRuntimeImage.records[factionIndex].primaryAnchorYQ12,
               g_GameFactionRuntimeImage.records[factionIndex].primaryAnchorXQ12,armyRuntimeSlot,
               currentBestScore);
  }
  if (g_GameFactionRuntimeImage.records[factionIndex].anchorCooldown0 != 0) {
    currentBestScore = AiUnitBehavior_ScoreFactionAnchorPoint
              (modelNode,g_GameFactionRuntimeImage.records[factionIndex].secondaryAnchorYQ12,
               g_GameFactionRuntimeImage.records[factionIndex].secondaryAnchorXQ12,armyRuntimeSlot,
               currentBestScore);
  }
  return currentBestScore;
}


/* Scores the target entries of workspace 07 for a unit, or those of workspace 03 at three quarters of the score
   when workspace 07 is empty: ((max(0, bias - Manhattan distance) * scale) >> 12) * aiSecondaryWorkspaceScoreWeight.
   Returns the best entry if it beats currentBestScore, else currentBestScore and NULL. modelDefinition is not
   used.
*/
AiSecondaryWorkspaceDistanceSelection AiUnitBehavior_ComputeSecondaryWorkspaceDistanceScore
          (AiCandidateScore32 currentBestScore,MdlDefinitionSemanticPrefix *modelDefinition,
          ArmyRuntimeSlot *armyRuntimeSlot)

{
  int recordsRemaining;
  int negAbsDeltaXQ12;
  Q12 negAbsDeltaYQ12;
  int distanceTerm;
  int targetScore;
  AiTargetWorkspaceEntry *bestEntry;
  AiTargetWorkspaceEntry *workspaceRecordCursor;
  AiSecondaryWorkspaceDistanceSelection selection;

  bestEntry = nullptr;
  if ((AiImproved_IsActive() || AiImproved_RepairsActive()) && g_AiWorkspace07Count == 0) {
    /* Improved AI and repairs: the unseen hostiles of workspace 03 with their real 8-byte stride and positions. */
    AiRuntimeWorkspaceEntry *unseenEntry = g_AiWorkspace03UnseenHostiles;
    for (uint32_t i = 0; i < g_AiWorkspace03Count; i++, unseenEntry++) {
      if (unseenEntry->modelRuntime == nullptr) continue;
      ArmyRuntimeSlot *unseenArmy = unseenEntry->modelRuntime->ownerArmyRuntimeOrSavedOffset.armyRuntime;
      if (unseenArmy == nullptr || unseenArmy->modelNodeRuntime == nullptr) continue;
      negAbsDeltaXQ12 = (armyRuntimeSlot->modelNodeRuntime->worldTransform).translation.x -
                        (unseenArmy->modelNodeRuntime->worldTransform).translation.x;
      if (negAbsDeltaXQ12 >= 0) negAbsDeltaXQ12 = -negAbsDeltaXQ12;
      negAbsDeltaYQ12 = (armyRuntimeSlot->modelNodeRuntime->worldTransform).translation.y -
                        (unseenArmy->modelNodeRuntime->worldTransform).translation.y;
      if (negAbsDeltaYQ12 >= 0) negAbsDeltaYQ12 = -negAbsDeltaYQ12;
      distanceTerm = negAbsDeltaXQ12 + negAbsDeltaYQ12 +
           (g_AiKnowledgeData->parameters).secondaryWorkspaceDistanceBiasQ12;
      if (distanceTerm < 0) distanceTerm = 0;
      targetScore = (distanceTerm * (g_AiKnowledgeData->parameters).secondaryWorkspaceDistanceScaleQ12 >> Q12_SHIFT) *
              armyRuntimeSlot->aiSecondaryWorkspaceScoreWeight;
      targetScore = targetScore * 3 >> 2;
      if (currentBestScore < targetScore) {
        currentBestScore = targetScore;
      }
    }
    selection.selectedEntry = nullptr; /* kind 3 does not use the entry */
    selection.score = currentBestScore;
    return selection;
  }
  recordsRemaining = g_AiWorkspace07Count;
  workspaceRecordCursor = g_AiWorkspace07Targets;
  if (g_AiWorkspace07Count == 0) {
    /* Original quirk: workspace 03 holds 8-byte AiRuntimeWorkspaceEntry records but the original walks it with
       the 16-byte workspace-07 stride (0x0053B3C0 `add edi,0x10`) and reads its (+0, +4) as world X/Y, i.e. the
       (modelRuntime, armyAssetId) of ws03[2i]: the second half of the walk reads stale entries of an earlier
       rebuild, for count > 256 past the 0x1000-byte buffer. Kept: it feeds the AI decisions and the AI hash. */
    recordsRemaining = g_AiWorkspace03Count;
    workspaceRecordCursor = reinterpret_cast<AiTargetWorkspaceEntry *>(g_AiWorkspace03UnseenHostiles); /* the quirk's stride */
  }
  for (; recordsRemaining != 0; recordsRemaining--) {
    negAbsDeltaXQ12 = (armyRuntimeSlot->modelNodeRuntime->worldTransform).translation.x -
            (int)workspaceRecordCursor->worldXQ12;
    if (negAbsDeltaXQ12 >= 0) {
      negAbsDeltaXQ12 = -negAbsDeltaXQ12;
    }
    negAbsDeltaYQ12 = (armyRuntimeSlot->modelNodeRuntime->worldTransform).translation.y -
                   workspaceRecordCursor->worldYQ12;
    if (negAbsDeltaYQ12 >= 0) {
      negAbsDeltaYQ12 = -negAbsDeltaYQ12;
    }
    /* bias - Manhattan distance, clamped at 0 */
    distanceTerm = negAbsDeltaXQ12 + negAbsDeltaYQ12 +
         (g_AiKnowledgeData->parameters).secondaryWorkspaceDistanceBiasQ12;
    if (distanceTerm < 0) {
      distanceTerm = 0;
    }
    targetScore = (distanceTerm * (g_AiKnowledgeData->parameters).secondaryWorkspaceDistanceScaleQ12 >> Q12_SHIFT) *
            armyRuntimeSlot->aiSecondaryWorkspaceScoreWeight;
    if (g_AiWorkspace07Count == 0) {
      targetScore = targetScore * 3 >> 2;
    }
    if (currentBestScore < targetScore) {
      bestEntry = workspaceRecordCursor;
      currentBestScore = targetScore;
    }
    workspaceRecordCursor++;
  }
  selection.selectedEntry = bestEntry;
  selection.score = currentBestScore;
  return selection;
}


/* Sends the unit to a general site (a workspace-05 AiScoredSiteWorkspaceEntry: X, Y, score): marks it as
   AI-commanded and no longer group-assigned, zeroes the site's score (the bonus
   AiUnitBehavior_ComputeGeneralSiteDistanceScore adds for it, so the next unit is less drawn there) and queues
   the move. worldRuntimeContext is not used.
*/
void AiUnitCommand_AssignWorkspacePoint(AiScoredSiteWorkspaceEntry *workspacePoint,ArmyRuntimeSlot *armyRuntime,
          WorldRuntimeContext *worldRuntimeContext)

{
  armyRuntime->aiUnitState = AI_UNIT_COMMANDED_STATE;
  armyRuntime->aiUnitFlags = armyRuntime->aiUnitFlags & ~AI_UNIT_STATE94_GROUP_ASSIGNED;
  workspacePoint->score = 0;
  ArmyRuntime_StartRoutedMoveCommand
            (workspacePoint->cellWorldYQ12,workspacePoint->cellWorldXQ12,AiUnit_MovementView(armyRuntime));
}


/* Sends the unit to the faction's anchor point: the primary anchor while its cooldown runs, otherwise the
   secondary one. Marks the unit as AI-commanded and no longer group-assigned, then queues the move.
   worldRuntimeContext is not used.
*/
void AiUnitCommand_AssignFactionAnchorPoint(FactionRuntimeIndex factionIndex,ArmyRuntimeSlot *armyRuntime,
          WorldRuntimeContext *worldRuntimeContext)

{
  GraphicsWorldCoordinateQ12 targetWorldX;
  GraphicsWorldCoordinateQ12 targetWorldY;
  
  /* The field ...AnchorYQ12 goes to the targetWorldX parameter and ...AnchorXQ12 to targetWorldY,
     so the faction record's anchor X/Y field names are probably swapped. */
  targetWorldX = g_GameFactionRuntimeImage.records[factionIndex].primaryAnchorYQ12;
  targetWorldY = g_GameFactionRuntimeImage.records[factionIndex].primaryAnchorXQ12;
  if (g_GameFactionRuntimeImage.records[factionIndex].primaryAnchorCooldown == 0) {
    targetWorldX = g_GameFactionRuntimeImage.records[factionIndex].secondaryAnchorYQ12;
    targetWorldY = g_GameFactionRuntimeImage.records[factionIndex].secondaryAnchorXQ12;
  }
  /* Improved AI: the defenders drive at a shooter that fires from afar (artillery, out of sight) instead of
     waiting at the building it hits. */
  if (AiImproved_IsActive()) {
    Q12 responseX;
    Q12 responseY;
    if (AiImproved_ResponsePoint(factionIndex,&responseX,&responseY)) {
      targetWorldX = responseX;
      targetWorldY = responseY;
    }
  }
  armyRuntime->aiUnitState = AI_UNIT_COMMANDED_STATE;
  armyRuntime->aiUnitFlags = armyRuntime->aiUnitFlags & ~AI_UNIT_STATE94_GROUP_ASSIGNED;
  ArmyRuntime_StartRoutedMoveCommand
            (targetWorldY,targetWorldX,AiUnit_MovementView(armyRuntime));
}


/* Collects an idle unit that is not yet group-assigned into workspace 14 (at most 64 units), from which
   AiUnitGroup_AssignCollectedEntitiesToBestTarget later sends them together to one target.
   worldRuntimeContext is not used.
*/
void AiUnitBehavior_CollectUnassignedEntity(ArmyRuntimeSlot *armyRuntimeSlot,WorldRuntimeContext *worldRuntimeContext)

{
  /* Improved AI: damaged or retreating units stay out of the attack groups. */
  if (AiImproved_IsActive() && !AiImproved_UnitReadyForAttack(armyRuntimeSlot)) {
    return;
  }
  /* Repairs: the original never clears the group mark once the group's target is gone, so a unit that took part
     in one attack is never collected again (the AI stops attacking once its units have all been in a group). */
  AiImproved_ReleaseStaleGroupAssignment(armyRuntimeSlot);
  if ((g_AiCollectedEntityCount < AI_WORKSPACE14_CAPACITY) &&
      !Any(armyRuntimeSlot->aiUnitFlags & AI_UNIT_STATE94_GROUP_ASSIGNED)) {
    g_AiWorkspace14CollectedArmies[g_AiCollectedEntityCount] = armyRuntimeSlot;
    g_AiCollectedEntityCount++;
  }
}


/* Raw score of a workspace-08 resource site for the pioneer vehicle, before the division by the asset's assigned
   structures: priority, nearness to visible hostiles (workspace 02), shortfall against the secondary workspace,
   nearness to the unit, x2 for ARM_0330. */
static int AiUnitBehavior_ScorePioneerSite
          (const AiTerrainFeatureWorkspaceEntry *terrainFeatureEntry,const ArmyRuntimeSlot *armyRuntime,
          const AiKnowledgeDataImage *knowledgeData)

{
  FieldGridCell *cell;
  int siteScore;
  int hostileDistance;
  int secondaryDistanceTerm;
  int unitDistanceTerm;
  int negAbsDeltaY;

  cell = terrainFeatureEntry->cell;
  siteScore = terrainFeatureEntry->priority *
          (knowledgeData->parameters).specialClass12Workspace08Field0cCoefficient;
  if ((knowledgeData->parameters).specialClass12Workspace02NearDistanceCoefficient != 0) {
    hostileDistance = AiHostileWorkspace_GetNearestVisibleHostileDistance(cell->worldY,cell->worldX);
    if ((int)(hostileDistance - (knowledgeData->parameters).specialClass12Workspace02NearDistanceThresholdQ12)
        < 0) {
      siteScore = siteScore + hostileDistance *
              (knowledgeData->parameters).specialClass12Workspace02NearDistanceCoefficient;
    }
  }
  if ((knowledgeData->parameters).specialClass12SecondaryWorkspaceShortfallCoefficient != 0) {
    secondaryDistanceTerm = AiSecondaryWorkspace_GetMinimumManhattanDistanceToPoint(cell->worldY,cell->worldX);
    secondaryDistanceTerm = secondaryDistanceTerm -
            (knowledgeData->parameters).specialClass12SecondaryWorkspaceDistanceThresholdQ12;
    if (secondaryDistanceTerm < 0) {
      siteScore = siteScore - secondaryDistanceTerm *
              (knowledgeData->parameters).specialClass12SecondaryWorkspaceShortfallCoefficient;
    }
  }
  if ((knowledgeData->parameters).specialClass12EntityDistanceCoefficient != 0) {
    /* bias - Manhattan distance to the unit: only sites closer than the bias add to the score */
    unitDistanceTerm = (armyRuntime->modelNodeRuntime->worldTransform).translation.x - cell->worldX;
    if (unitDistanceTerm >= 0) {
      unitDistanceTerm = -unitDistanceTerm;
    }
    negAbsDeltaY = (armyRuntime->modelNodeRuntime->worldTransform).translation.y - cell->worldY;
    if (negAbsDeltaY >= 0) {
      negAbsDeltaY = -negAbsDeltaY;
    }
    unitDistanceTerm = unitDistanceTerm + negAbsDeltaY +
            (knowledgeData->parameters).specialClass12EntityDistanceBiasQ12;
    if (unitDistanceTerm >= 0) {
      siteScore = siteScore + unitDistanceTerm *
              (knowledgeData->parameters).specialClass12EntityDistanceCoefficient;
    }
  }
  if (terrainFeatureEntry->armyAssetId == ARM_0330_BUILDING_MDL0303) {
    siteScore = siteScore * 2;
  }
  return siteScore;
}


/* Repairs: an own resource building (ARM 330/332) already stands at the site. */
static bool AiUnitBehavior_PioneerSiteTaken(Q12 siteX,Q12 siteY)
{
  AiStructureWorkspaceEntry *structureEntry = g_AiWorkspace00Structures;
  for (uint32_t i = 0; i < g_AiWorkspace00Count; i++, structureEntry++) {
    ModelRuntimeSlot *structure = structureEntry->runtimeSlotAddressOrZero;
    if ((structure == nullptr) || ((structureEntry->armyAssetId != ARM_0330_BUILDING_MDL0303) &&
                                   (structureEntry->armyAssetId != ARM_0332_BUILDING_MDL0302))) {
      continue;
    }
    ModelRuntimeNode *structureNode = structure->rootModelNodeOrSavedOffset.modelNode;
    int deltaX = (structureNode->worldTransform).translation.x - siteX;
    int deltaY = (structureNode->worldTransform).translation.y - siteY;
    if (deltaX < 0) deltaX = -deltaX;
    if (deltaY < 0) deltaY = -deltaY;
    if (deltaX + deltaY < 3 * Q12_ONE) {
      return true;
    }
  }
  return false;
}

/* Repairs: sends the pioneer to a point AI_IMPROVED_PIONEER_PARK_DISTANCE_Q12 from the site on its own side, close
   enough for its support radius to cover the site and far enough to leave the extractor's footprint free. */
static void AiUnitBehavior_ParkPioneerAtSite(ArmyRuntimeSlot *armyRuntime,Q12 siteX,Q12 siteY)
{
  ModelRuntimeNode *modelNode = armyRuntime->modelNodeRuntime;
  int deltaX = (modelNode->worldTransform).translation.x - siteX;
  int deltaY = (modelNode->worldTransform).translation.y - siteY;
  uint32_t distance = FixedMath_Length2(deltaX,deltaY);
  Q12 parkX;
  Q12 parkY;
  if (distance < (uint32_t)Q12_ONE / 4) {
    /* on top of the site: back off along the heading */
    FixedSinCos headingOffset =
         FixedMath_SinCosScaled((modelNode->modelPayload).worldRotationAngle2,AI_IMPROVED_PIONEER_PARK_DISTANCE_Q12);
    parkX = siteX - headingOffset.cosValue;
    parkY = siteY - headingOffset.sinValue;
  }
  else {
    parkX = siteX + (Q12)(((int64_t)deltaX * AI_IMPROVED_PIONEER_PARK_DISTANCE_Q12) / (int64_t)distance);
    parkY = siteY + (Q12)(((int64_t)deltaY * AI_IMPROVED_PIONEER_PARK_DISTANCE_Q12) / (int64_t)distance);
  }
  armyRuntime->aiUnitState = AI_UNIT_COMMANDED_STATE;
  ArmyRuntime_StartRoutedMoveCommand(parkY,parkX,AiUnit_MovementView(armyRuntime));
  AiImproved_RestartMovementClock(armyRuntime);
}

/* Repairs: a faction without any building has seen a deposit the pioneer has not given up on (workspace 08 holds
   the deposits the faction has in sight or has seen). */
static bool AiUnitBehavior_PioneerHasSeenDeposit(const AiImprovedPioneerMemory *memory)
{
  /* while a Xenite mine is pending and the pioneer can still explore, only a Xenite deposit will do: it pays for
     everything after, and a Tritium pump alone left the faction with no income to build the rest */
  bool xeniteOnly = AiPrimaryWorkspace_HasUnassignedEntryById(ARM_0330_BUILDING_MDL0303) && (memory->exploreExhausted == 0);
  AiTerrainFeatureWorkspaceEntry *siteEntry = g_AiWorkspace08TerrainFeatureSites;
  for (uint32_t i = 0; i < g_AiWorkspace08Count; i++, siteEntry++) {
    if (siteEntry->cell == nullptr || (xeniteOnly && siteEntry->armyAssetId != ARM_0330_BUILDING_MDL0303)) continue;
    if (!AiImproved_PioneerSiteFailed(memory,siteEntry->cell->worldX,siteEntry->cell->worldY)) {
      return true;
    }
  }
  return false;
}

/* Repairs: a pioneer of a faction without any building that has seen no usable deposit explores: it drives to the
   nearest field cell its faction has not explored yet (6 to 40 world units away), never to one within 18 units of a
   hostile building it has seen (a faction without a base has nothing to defend it, and the original drove on into
   the enemy base), and gives a target up when it got stuck on the way for 20 s (a slope, water). As the pioneer
   sees what it passes, it works outward from its start until a deposit with room for a base comes into sight. */
static void AiUnitBehavior_ExplorePioneer(ArmyRuntimeSlot *armyRuntime,AiImprovedPioneerMemory *memory,
                                          FactionRuntimeIndex factionIndex,int32_t standingStillTicks)
{
  static constexpr int32_t EXPLORE_STUCK_TICKS = 20 * 20;
  static constexpr int64_t EXPLORE_MIN_Q12 = 6 * (int64_t)Q12_ONE;
  static constexpr int64_t EXPLORE_MAX_Q12 = 40 * (int64_t)Q12_ONE;
  static constexpr int64_t EXPLORE_HOSTILE_CLEARANCE_Q12 = 18 * (int64_t)Q12_ONE;
  ModelRuntimeNode *modelNode = armyRuntime->modelNodeRuntime;
  Q12 unitX = (modelNode->worldTransform).translation.x;
  Q12 unitY = (modelNode->worldTransform).translation.y;
  bool moving = Any(armyRuntime->movementStateFlags & ARMY_MOVEMENT_ACTIVE);
  if (moving && standingStillTicks < EXPLORE_STUCK_TICKS) {
    return;
  }
  if (moving) {
    /* stuck on the way: the target is given up for a while */
    ArmyRuntime_ResetMovementStateFromModel(armyRuntime);
    if (memory->exploreDirectionPlusOne != 0) {
      AiImproved_PioneerRememberFailedSite(memory,memory->exploreX,memory->exploreY);
    }
  }
  memory->exploreDirectionPlusOne = 0;
  FieldGridAsset *grid = (g_InGameRuntimeRoot != nullptr) ? g_InGameRuntimeRoot->worldRuntime.fieldGrid : nullptr;
  if (grid == nullptr) {
    return;
  }
  FieldGridCell *bestCell = nullptr;
  int64_t bestDistance = INT64_MAX;
  uint32_t cellCount = grid->gridWidth * grid->gridHeight;
  FieldGridCell *cell = grid->cells;
  for (uint32_t c = 0; c < cellCount; c++, cell++) {
    if ((FieldGridCell_OccupancyByte(cell,factionIndex) & FIELD_CELL_OCCUPANCY_EXPLORED_BITS) != 0) continue;
    int64_t dx = (int64_t)cell->worldX - unitX;
    int64_t dy = (int64_t)cell->worldY - unitY;
    if (dx > EXPLORE_MAX_Q12 || dx < -EXPLORE_MAX_Q12 || dy > EXPLORE_MAX_Q12 || dy < -EXPLORE_MAX_Q12) continue;
    int64_t distance = dx * dx + dy * dy;
    if (distance < EXPLORE_MIN_Q12 * EXPLORE_MIN_Q12 || distance > EXPLORE_MAX_Q12 * EXPLORE_MAX_Q12 ||
        distance >= bestDistance) continue;
    if (AiImproved_PioneerSiteFailed(memory,cell->worldX,cell->worldY)) continue;
    bool nearHostile = false;
    AiRuntimeWorkspaceEntry *seen = g_AiWorkspace02VisibleHostiles;
    for (uint32_t k = 0; k < g_AiWorkspace02Count && !nearHostile; k++, seen++) {
      ModelRuntimeSlot *hostile = seen->modelRuntime;
      if (hostile == nullptr || hostile->definitionOrSavedId.runtimeDefinition->accelerationPerTick != 0) continue;
      ModelRuntimeNode *node = hostile->rootModelNodeOrSavedOffset.modelNode;
      int64_t hx = (int64_t)(node->worldTransform).translation.x - cell->worldX;
      int64_t hy = (int64_t)(node->worldTransform).translation.y - cell->worldY;
      nearHostile = hx * hx + hy * hy < EXPLORE_HOSTILE_CLEARANCE_Q12 * EXPLORE_HOSTILE_CLEARANCE_Q12;
    }
    if (nearHostile) continue;
    bestDistance = distance;
    bestCell = cell;
  }
  if (bestCell == nullptr) {
    /* everything near is explored or hostile: any deposit will do now */
    if (memory->exploreExhausted == 0) {
      memory->exploreExhausted = 1;
      AiImproved_Log("Partei %d: Pionier hat die Umgebung erkundet, nimmt jede Quelle",factionIndex);
    }
    return;
  }
  memory->exploreDirectionPlusOne = 1;
  memory->exploreX = bestCell->worldX;
  memory->exploreY = bestCell->worldY;
  armyRuntime->aiUnitState = AI_UNIT_COMMANDED_STATE;
  ArmyRuntime_StartRoutedMoveCommand(bestCell->worldY,bestCell->worldX,AiUnit_MovementView(armyRuntime));
  AiImproved_RestartMovementClock(armyRuntime);
  AiImproved_Log("Partei %d: Pionier ohne Basis erkundet bei %d/%d (von %d/%d)",factionIndex,bestCell->worldX / Q12_ONE,
                 bestCell->worldY / Q12_ONE,unitX / Q12_ONE,unitY / Q12_ONE);
}

/* Repairs: the pioneer behaviour of an AI faction without the original's dead ends. The original sets the
   per-tick special behaviour flag on its first trip and never clears it, so the pioneer never picks a second site;
   it also waits (returns) at the first site that the base itself could reach directly or over a storage bridge,
   even when the AI never builds there. Here the pioneer
   - leaves sites the base reaches without a pioneer to the construction planner,
   - leaves sites a storage bridge reaches to the planner as long as the faction already has a Xenite mine,
   - drives to the best remaining site (scored as in the original) and parks next to it,
   - waits there AI_IMPROVED_PIONEER_WAIT_TICKS for the extractor that turns it into a pioneer base, then gives the
     site up for AI_IMPROVED_PIONEER_FAILED_SITE_TICKS and looks for the next one. */
static void AiUnitBehavior_UpdatePioneerVehicleRepaired
          (MdlDefinitionSemanticPrefix *modelDefinition,ArmyRuntimeSlot *armyRuntime,FactionRuntimeIndex factionIndex,
          WorldRuntimeContext *worldRuntime)
{
  (void)modelDefinition;
  AiImprovedPioneerMemory *memory = AiImproved_PioneerMemory(armyRuntime);
  armyRuntime->movementStateFlags = armyRuntime->movementStateFlags & ~ARMY_MOVEMENT_SPECIAL_BEHAVIOR;
  if (memory == nullptr || armyRuntime->modelNodeRuntime == nullptr) {
    return;
  }
  int32_t standingStillTicks = AiImproved_UnitTicksStandingStill(armyRuntime);
  if (Any(armyRuntime->movementStateFlags & ARMY_MOVEMENT_ACTIVE)) {
    if (standingStillTicks < (int32_t)AI_IMPROVED_PIONEER_BLOCKED_TICKS) {
      return; /* still driving */
    }
    /* the move does not get on (blocked way, unreachable park point): stop and give the site up */
    ArmyRuntime_ResetMovementStateFromModel(armyRuntime);
    if (memory->hasSite) {
      AiImproved_Log("Partei %d: Pionier kommt nicht weiter, gibt die Quelle auf",factionIndex);
      AiImproved_PioneerRememberFailedSite(memory,memory->siteX,memory->siteY);
      memory->hasSite = 0;
      memory->waitSinceTick = 0;
    }
  }
  ModelRuntimeNode *modelNode = armyRuntime->modelNodeRuntime;
  Q12 unitX = (modelNode->worldTransform).translation.x;
  Q12 unitY = (modelNode->worldTransform).translation.y;
  uint32_t now = AiImproved_GameTicks();

  if (memory->hasSite) {
    int deltaX = unitX - memory->siteX;
    int deltaY = unitY - memory->siteY;
    uint32_t distance = FixedMath_Length2(deltaX,deltaY);
    if (AiUnitBehavior_PioneerSiteTaken(memory->siteX,memory->siteY)) {
      /* the extractor stands, but another building's support kept the pioneer from turning into a base */
      AiImproved_Log("Partei %d: Pionier, Quelle bebaut, sucht die naechste",factionIndex);
      memory->hasSite = 0;
      memory->waitSinceTick = 0;
    }
    else if (distance <= (uint32_t)AI_IMPROVED_PIONEER_AT_SITE_DISTANCE_Q12) {
      if ((distance < (uint32_t)AI_IMPROVED_PIONEER_TOO_CLOSE_Q12) && (memory->reparked == 0)) {
        /* standing in the extractor's footprint: back off once */
        memory->reparked = 1;
        AiUnitBehavior_ParkPioneerAtSite(armyRuntime,memory->siteX,memory->siteY);
        return;
      }
      if (memory->waitSinceTick == 0) {
        memory->waitSinceTick = now | 1;
        AiImproved_Log("Partei %d: Pionier wartet an der Quelle auf das Bergwerk",factionIndex);
      }
      if ((int32_t)(now - memory->waitSinceTick) < (int32_t)AI_IMPROVED_PIONEER_WAIT_TICKS) {
        return;
      }
      AiImproved_Log("Partei %d: Pionier wartet vergeblich, gibt die Quelle auf",factionIndex);
      AiImproved_PioneerRememberFailedSite(memory,memory->siteX,memory->siteY);
      memory->hasSite = 0;
      memory->waitSinceTick = 0;
    }
    else {
      AiImproved_Log("Partei %d: Pionier erreicht die Quelle nicht, gibt sie auf",factionIndex);
      AiImproved_PioneerRememberFailedSite(memory,memory->siteX,memory->siteY);
      memory->hasSite = 0;
      memory->waitSinceTick = 0;
    }
  }

  /* The faction has no building at all, only pending ones (Paradis: each faction starts with a pioneer vehicle and
     its headquarters still to place). The original drove on along its heading for ever and only founded a base by
     chance; a player drives the pioneer to a deposit, puts the mine next to it (the pioneer's own support allows
     that) and then the headquarters. The repaired pioneer does the same: it drives to the best deposit it has
     seen, and while it has seen none it explores. */
  const bool noBuilding = g_AiWorkspace00Count == g_AiWorkspace04Count;
  if (noBuilding && !AiUnitBehavior_PioneerHasSeenDeposit(memory)) {
    AiUnitBehavior_ExplorePioneer(armyRuntime,memory,factionIndex,standingStillTicks);
    return;
  }
  if (g_AiWorkspace08Count == 0) {
    return;
  }
  bool hasXeniteMine = AiPrimaryWorkspace_CountAssignedEntriesById(ARM_0330_BUILDING_MDL0303) != 0;
  AiKnowledgeDataImage *knowledgeData = g_AiKnowledgeData;
  uint32_t bestScore = 0;
  AiTerrainFeatureWorkspaceEntry *bestEntry = nullptr;
  AiTerrainFeatureWorkspaceEntry *siteEntry = g_AiWorkspace08TerrainFeatureSites;
  for (uint32_t i = 0; i < g_AiWorkspace08Count; i++, siteEntry++) {
    FieldGridCell *siteCell = siteEntry->cell;
    if (AiImproved_PioneerSiteFailed(memory,siteCell->worldX,siteCell->worldY)) continue;
    if ((int)(knowledgeData->parameters).specialSiteMinimumWorkspaceDistanceQ12 >
        AiHostileWorkspace_GetNearestUnseenHostileDistance(siteCell->worldY,siteCell->worldX)) continue;
    if ((int)(knowledgeData->parameters).specialSiteMinimumWorkspaceDistanceQ12 >
        AiHostileWorkspace_GetNearestVisibleHostileDistance(siteCell->worldY,siteCell->worldX)) continue;
    if (noBuilding) {
      /* no base yet: a deposit where the mine and the headquarters both find ground founds one (the reach tests
         below need a base) */
      if (!AiConstructionPlanner_CanFoundBaseAt(siteEntry->armyAssetId,siteCell->worldX,siteCell->worldY,worldRuntime)) {
        AiImproved_PioneerRememberFailedSite(memory,siteCell->worldX,siteCell->worldY);
        AiImproved_Log("Partei %d: Quelle bei %d/%d: kein Platz fuer Bergwerk und Hauptquartier",factionIndex,
                       siteCell->worldX / Q12_ONE,siteCell->worldY / Q12_ONE);
        continue;
      }
      /* the extractor that founds the base must be one the faction has pending (a Xenite mine above all: it pays
         for everything after) */
      if (!AiPrimaryWorkspace_HasUnassignedEntryById(siteEntry->armyAssetId)) continue;
      if ((siteEntry->armyAssetId != ARM_0330_BUILDING_MDL0303) && (memory->exploreExhausted == 0) &&
          AiPrimaryWorkspace_HasUnassignedEntryById(ARM_0330_BUILDING_MDL0303)) continue;
      int siteScore = AiUnitBehavior_ScorePioneerSite(siteEntry,armyRuntime,knowledgeData);
      if (siteScore <= 0) siteScore = 1;
      if (siteEntry->armyAssetId == ARM_0330_BUILDING_MDL0303) siteScore = siteScore * 8;
      if (siteScore <= 0) siteScore = 1;
      if ((int)bestScore < siteScore) {
        bestScore = (uint32_t)siteScore;
        bestEntry = siteEntry;
      }
      continue;
    }
    uint32_t heading = (uint32_t)(uint16_t)siteCell->triangle0NormalAngles;
    /* the base reaches it without a pioneer (pioneers do not count, units do not block): the construction
       planner builds there */
    if (ArmyPlacement_CanPlaceAssetAtFieldPoint
          (ARMY_PLACEMENT_MODE_SKIP_CLASS18_SUPPORT | ARMY_PLACEMENT_MODE_STRUCTURES_ONLY,0,heading,siteCell->worldY,
           siteCell->worldX,siteEntry->armyAssetId,factionIndex,worldRuntime,nullptr)) continue;
    uint32_t bucketCount;
    if (!AiPlacement_QueryReachableSiteBucketCount
           (siteEntry->armyAssetId,siteCell,factionIndex,worldRuntime,&bucketCount)) continue;
    int deltaX = unitX - siteCell->worldX;
    int deltaY = unitY - siteCell->worldY;
    uint32_t distance = FixedMath_Length2(deltaX,deltaY);
    if (bucketCount == 0) {
      /* reachable now only through a pioneer's support: wait here if it is this pioneer's, else leave it to
         the pioneer that stands there */
      if (distance <= (uint32_t)AI_IMPROVED_PIONEER_AT_SITE_DISTANCE_Q12) {
        bestEntry = siteEntry;
        bestScore = UINT32_MAX;
        break;
      }
      continue;
    }
    if ((bucketCount < 5) && hasXeniteMine &&
        !AiPlacement_ReserveSeparatedSpecialSiteChain(siteEntry->armyAssetId,siteCell,factionIndex,worldRuntime)) {
      continue; /* a storage bridge reaches it */
    }
    int siteScore = AiUnitBehavior_ScorePioneerSite(siteEntry,armyRuntime,knowledgeData);
    int assignedEntryCount = AiPrimaryWorkspace_CountAssignedEntriesById(siteEntry->armyAssetId);
    uint32_t weightedScore = (uint32_t)(siteScore * 3) / (assignedEntryCount + 3U);
    if ((int)bestScore < (int)weightedScore) {
      bestScore = weightedScore;
      bestEntry = siteEntry;
    }
  }
  if (bestEntry == nullptr || bestScore == 0) {
    return;
  }
  memory->hasSite = 1;
  memory->siteX = bestEntry->cell->worldX;
  memory->siteY = bestEntry->cell->worldY;
  memory->waitSinceTick = 0;
  memory->reparked = 0;
  AiImproved_Log("Partei %d: Pionier faehrt zur Quelle (ARM %u) bei %d/%d",factionIndex,
                 (unsigned)bestEntry->armyAssetId,memory->siteX / Q12_ONE,memory->siteY / Q12_ONE);
  AiUnitBehavior_ParkPioneerAtSite(armyRuntime,memory->siteX,memory->siteY);
}

/* AI behaviour of a runtime-class-18 unit (the pioneer vehicle, which turns into a building at a resource
   site): drives it to the best free resource site of workspace 08. Called from AiUnitBehavior_UpdateOwnUnits and, while
   movement flag 0x100 is set, from its movement update. Once it has arrived (flag clear): with as many
   workspace-00 as workspace-04 entries it moves on 0x2D05 along its heading; otherwise it drives to the best
   resource site of workspace 08 that is far enough from workspaces 03/02, scored by priority, distances to
   workspaces 02/01 and to the unit, x2 for ARM_0330, then x3 / (assigned structures of that asset + 3). A site
   whose bucket query (AiPlacement_QueryReachableSiteBucketCount) fails is skipped; one with 0 buckets, or 1..4
   buckets and a successful (false) AiPlacement_ReserveSeparatedSpecialSiteChain, ends the update without a move.
   Not arrived or flag set: sets the flag and resets the movement once within 0x1B03 of its fallback position
   on both axes.
*/
void AiUnitBehavior_UpdatePioneerVehicle
          (MdlDefinitionSemanticPrefix *modelDefinition,ArmyRuntimeSlot *armyRuntime,
          FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime)

{
  AiKnowledgeDataImage *knowledgeData;
  uint32_t workspace00Count;
  uint32_t workspace04Count;
  uint32_t sitesRemaining;
  int unseenHostileDistance;
  int visibleHostileDistance;
  int siteScore;
  int assignedEntryCount;
  uint32_t weightedScore;
  uint32_t bestScore;
  int unitWorldX;
  int unitWorldY;
  int absDeltaX;
  int absDeltaY;
  FieldGridCell *siteCell;
  AiTerrainFeatureWorkspaceEntry *terrainFeatureEntry;
  bool chainFailed;
  FixedSinCos headingOffset;
  uint32_t bucketCount;
  Q12 steerWorldXQ12; /* unused here */
  Q12 steerWorldYQ12; /* unused here */
  FieldGridCell *bestCell;
  ModelRuntimeNode *modelNode;

  /* Repairs: the pioneer of an AI faction without the original's dead ends. */
  if (AiImproved_RepairsActive() && AiImproved_IsAiFaction(factionIndex)) {
    AiUnitBehavior_UpdatePioneerVehicleRepaired(modelDefinition,armyRuntime,factionIndex,worldRuntime);
    return;
  }
  workspace04Count = g_AiWorkspace04Count;
  workspace00Count = g_AiWorkspace00Count;
  if (!Any(armyRuntime->movementStateFlags & ARMY_MOVEMENT_SPECIAL_BEHAVIOR) &&
     (ArmyRuntime_UpdateMovementAndWaypoints
        (worldRuntime,AiUnit_MovementView(armyRuntime),&steerWorldXQ12,&steerWorldYQ12))) {
    if (workspace00Count == workspace04Count) {
      modelNode = armyRuntime->modelNodeRuntime;
      headingOffset = FixedMath_SinCosScaled((modelNode->modelPayload).worldRotationAngle2,5 * FIELD_GRID_WORLD_COLUMN_STEP_X);
      unitWorldX = (modelNode->worldTransform).translation.x;
      unitWorldY = (modelNode->worldTransform).translation.y;
      armyRuntime->aiUnitState = 8;
      ArmyRuntime_StartRoutedMoveCommand
                (headingOffset.sinValue + unitWorldY,headingOffset.cosValue + unitWorldX,
                 AiUnit_MovementView(armyRuntime));
    }
    else if (!Any(armyRuntime->movementStateFlags & ARMY_MOVEMENT_SPECIAL_BEHAVIOR) && (g_AiWorkspace08Count != 0)) {
      bestScore = 0;
      terrainFeatureEntry = g_AiWorkspace08TerrainFeatureSites;
      for (sitesRemaining = g_AiWorkspace08Count; sitesRemaining != 0; sitesRemaining--, terrainFeatureEntry++) {
        knowledgeData = g_AiKnowledgeData;
        siteCell = terrainFeatureEntry->cell;
        unseenHostileDistance = AiHostileWorkspace_GetNearestUnseenHostileDistance
                          (siteCell->worldY,siteCell->worldX);
        if ((int)(knowledgeData->parameters).specialSiteMinimumWorkspaceDistanceQ12 > unseenHostileDistance) {
          continue;
        }
        visibleHostileDistance = AiHostileWorkspace_GetNearestVisibleHostileDistance
                          (siteCell->worldY,siteCell->worldX);
        if ((int)(knowledgeData->parameters).specialSiteMinimumWorkspaceDistanceQ12 > visibleHostileDistance) {
          continue;
        }
        if (!AiPlacement_QueryReachableSiteBucketCount
              (terrainFeatureEntry->armyAssetId,siteCell,factionIndex,worldRuntime,&bucketCount)) {
          continue;
        }
        if (bucketCount == 0) {
          return;
        }
        if (bucketCount < 5) {
          chainFailed = AiPlacement_ReserveSeparatedSpecialSiteChain
                            (terrainFeatureEntry->armyAssetId,siteCell,factionIndex,worldRuntime);
          if (!chainFailed) {
            return;
          }
        }
        siteScore = AiUnitBehavior_ScorePioneerSite(terrainFeatureEntry,armyRuntime,knowledgeData);
        assignedEntryCount = AiPrimaryWorkspace_CountAssignedEntriesById(terrainFeatureEntry->armyAssetId);
        weightedScore = (uint32_t)(siteScore * 3) / (assignedEntryCount + 3U);
        if ((int)bestScore < (int)weightedScore) {
          bestScore = weightedScore;
          bestCell = siteCell;
        }
      }
      if (bestScore != 0) {
        armyRuntime->aiUnitState = 8;
        ArmyRuntime_StartRoutedMoveCommand
                  (bestCell->worldY,bestCell->worldX,AiUnit_MovementView(armyRuntime));
      }
    }
  }
  else {
    absDeltaX = (armyRuntime->articulatedContact).fallbackPosition0Q12 -
            (armyRuntime->modelNodeRuntime->worldTransform).translation.x;
    if (absDeltaX < 0) {
      absDeltaX = -absDeltaX;
    }
    absDeltaY = (armyRuntime->articulatedContact).fallbackPosition1Q12 -
            (armyRuntime->modelNodeRuntime->worldTransform).translation.y;
    if (absDeltaY < 0) {
      absDeltaY = -absDeltaY;
    }
    armyRuntime->movementStateFlags = armyRuntime->movementStateFlags | ARMY_MOVEMENT_SPECIAL_BEHAVIOR;
    if ((absDeltaX < 3 * FIELD_GRID_WORLD_COLUMN_STEP_X) && (absDeltaY < 3 * FIELD_GRID_WORLD_COLUMN_STEP_X)) {
      ArmyRuntime_ResetMovementStateFromModel(armyRuntime);
    }
  }
}

