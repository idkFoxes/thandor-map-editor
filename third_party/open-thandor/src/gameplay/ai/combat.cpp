/*
 * Open Thandor
 * Project: https://github.com/idkFoxes/open-thandor/tree/main
 * File: https://github.com/idkFoxes/open-thandor/blob/main/src/gameplay/ai/combat.cpp
 * Reverse engineering by idkFoxes 2026
 */

#include <thandor/gameplay/ai/combat.h>
#include <thandor/thandor.h>
#include <thandor/gameplay/ai/improved.h>

/* Module data. */

static const ArmyCommandGeneration g_AiCommandGenerationCandidateBase = 32;

/* ArmyCommandGeneration (uint32_t) 0x200 assigned to an army's commandGeneration by the AI combat code */
static const ArmyCommandGeneration g_AiCommandGenerationRetainedTarget = 512;

/* uint32_t[24] AI combat target base score per target runtime class */
static const uint32_t g_AiCombatTargetClassBaseScores[24] = {
    /*  0 */ 0, 2048, 4096, 4096, 512, 0, 0, 0, 0, 0, 384, 256, 0, 128, 1024, 896,
    /* 16 */ 768, 4096, 6144, 4096, 256, 4096, 192, 64};

/* int32_t weight 0x600 multiplying the radial clearance term of the AI combat target score */
static const int32_t g_AiCombatTargetRadialClearanceWeight = 1536;

/* int32_t weight 0x12000 for the candidate class counter term of the AI combat target score, divided by the Q12 hierarchy scale unity */
static const int32_t g_AiCombatTargetCandidateCounterCountWeight = 73728;

/* int32_t weight 0x20000 for the source class counter term of the AI combat target score */
static const int32_t g_AiCombatTargetSourceCounterCountWeight = 131072;

/* int32_t weight 0x1100 for the hierarchy scale deficit (1.0 - condition ratio) term of the AI combat target score */
static const int32_t g_AiCombatTargetScaleDeficitWeight = 4352;

/* int32_t multiplier 0x800 applied to g_AiCombatTargetClassBaseScores[runtimeClassId] in the AI combat target score */
static const int32_t g_AiCombatTargetClassBaseScoreMultiplier = 2048;

/* Shift of the best candidate so far (copied from g_AiCombatTargetCurrentCommandGenerationRightShiftBits when
   AiCombatTarget_SelectBestCandidate takes a new best). Original quirk: it is only written when a candidate is
   taken, so a call that selects nothing keeps the value of an earlier call (possibly another army or faction);
   it is neither saved nor reset, so a loaded game sees what the previous session left (0 after program start). */
static AiCommandGenerationRightShiftBits g_AiCombatTargetSelectedCommandGenerationRightShiftBits = 0;

static AiCommandGenerationRightShiftBits g_AiCombatTargetCurrentCommandGenerationRightShiftBits = 0;

/* Source class count AiCombatTarget_SelectBestCandidate reports for a zero class counter sum, in place of the
   original's positive stack leftover (see the quirk there). */
static constexpr int AI_SOURCE_CLASS_COUNT_ZERO_SUM_LEFTOVER = 1;

/* Per-step AI target choice of a non-neutral army, called by ArmyRuntimeMaintenance_UpdateHierarchyAiAndTimers
   (the army entry of the primaryUpdate phase of g_RuntimeMaintenanceCallbackPhases). Skipped while an
   interrupted command is pending or a target command is still running (commandGeneration > 0). Keeps the
   current target (re-stamping commandGeneration), falls back to the stored group-attack target
   (assignedTargetArmyRuntime) when nothing was found although the army has hostile class counters, or else
   commands the selected target.
*/
void AiCombatDecision_UpdateTargetAssignment(WorldRuntimeContext *worldRuntime,ArmyRuntimeSlot *armyRuntime)

{
  ArmyRuntimeSlot *selectedTargetArmyRuntime;
  AiSourceClassCount sourceClassCount;
  ArmyCommandGeneration candidateCommandGenerationBase;
  AiCommandGenerationRightShiftBits selectedCommandGenerationRightShiftBits;

  /* Improved AI: a retreating unit does not look for a new fight. */
  if (AiImproved_IsActive() && AiImproved_UnitRetreating(armyRuntime)) {
    return;
  }
  if (!Any(armyRuntime->commandModeFlags & ARMY_COMMAND_MODE_INTERRUPTED) &&
     (((int)armyRuntime->commandGeneration < 1 ||
      (!Any(armyRuntime->commandModeFlags &
       (ARMY_COMMAND_MODE_TARGET_ARMY | ARMY_COMMAND_MODE_TARGET_POSITION)))))) {
    selectedTargetArmyRuntime =
         AiCombatTarget_SelectBestCandidate(worldRuntime,armyRuntime,&sourceClassCount);
    /* Original quirk: without a selected target this is still the shift of an earlier call (see the static). */
    selectedCommandGenerationRightShiftBits =
         g_AiCombatTargetSelectedCommandGenerationRightShiftBits;
    candidateCommandGenerationBase = g_AiCommandGenerationCandidateBase;
    if (selectedTargetArmyRuntime == armyRuntime->commandTargetArmyRuntime) {
      armyRuntime->commandGeneration = g_AiCommandGenerationRetainedTarget;
    }
    /* signed test of the returned sum: count > 0 */
    else if ((selectedTargetArmyRuntime == nullptr) && (0 < sourceClassCount)) {
      ArmyRuntime_ResolveCommandTarget(Thandor_U32ToPointer<ArmyRuntimeSlot>(armyRuntime->assignedTargetArmyRuntime),armyRuntime); /* 32-bit format field: ArmyRuntimeSlot.assignedTargetArmyRuntime */
      if (!Any(armyRuntime->commandModeFlags & ARMY_COMMAND_MODE_TARGET_ARMY)) {
        armyRuntime->commandModeFlags = armyRuntime->commandModeFlags | ARMY_COMMAND_MODE_INTERRUPTED;
      }
    }
    else {
      ArmyRuntime_ResolveCommandTarget(selectedTargetArmyRuntime,armyRuntime);
      armyRuntime->commandModeFlags = armyRuntime->commandModeFlags | ARMY_COMMAND_MODE_AI_COMBAT_TARGET;
      /* shift 2 (a quarter of the time) when the target's class counter for this army's class is zero,
         see AiCombatTarget_EvaluateCandidateScore */
      armyRuntime->commandGeneration =
           candidateCommandGenerationBase >> ((uint8_t)selectedCommandGenerationRightShiftBits & 31);
    }
  }
}


/* Group attack: when at least two armies were collected, sums their hierarchy scale ratios (x256 each) until
   the group is strong enough (sum >= 0x200), then picks the target with the highest class base score from
   workspace 07 (or workspace 03 when 07 is empty) and sends every collected army to attack it. The target
   class is the runtimeClassId of the target model's definition, not its type field.
*/
void AiUnitGroup_AssignCollectedEntitiesToBestTarget()

{
  uint32_t collectedCount;
  uint32_t collectedIndex;
  ArmyRuntimeSlot **collectedArmies;
  uint32_t accumulatedScaleRatio;
  Q12 collectedConditionRatioQ12;
  ArmyCommandGeneration assignedCommandGeneration;
  uint32_t targetCandidateCount;
  uint32_t targetCandidateIndex;
  AiTargetWorkspaceEntry *targetCandidateRecords;
  ModelRuntimeSlot *candidateTargetModelRuntime;
  uint32_t candidateClassScore;
  uint32_t bestScore;
  ModelRuntimeSlot *bestTargetModelRuntime;
  GameEntityRuntime *targetRuntime;
  ArmyRuntimeSlot *collectedArmy;

  if (g_AiCollectedEntityCount < 2) {
    /* Improved AI: a single idle unit takes position at the edge of the base */
    AiImproved_PositionIdleDefenders(g_AiWorkspace14CollectedArmies,g_AiCollectedEntityCount);
    return;
  }
  collectedCount = g_AiCollectedEntityCount;
  collectedArmies = g_AiWorkspace14CollectedArmies;
  accumulatedScaleRatio = 0;
  const uint32_t attackStrength = AiImproved_IsActive() ?
       AiImproved_GroupAttackStrength(collectedArmies[0]->factionIndex,g_AiWorkspace01Count) :
       (uint32_t)AI_UNIT_GROUP_ATTACK_STRENGTH;
  for (collectedIndex = 0; collectedIndex < collectedCount; collectedIndex++) {
    collectedConditionRatioQ12 =
         ModelRuntime_QueryHierarchyConditionRatioQ12(reinterpret_cast<RuntimeModelFactionPrefix *>(collectedArmies[collectedIndex])); /* the army slot header as the hierarchy metric view */
    accumulatedScaleRatio =
         accumulatedScaleRatio + (uint32_t)(collectedConditionRatioQ12 << 8) / (uint32_t)Q12_ONE;
    if (accumulatedScaleRatio >= attackStrength) {
      break;
    }
  }
  if (collectedIndex == collectedCount) {
    /* Improved AI: too weak for an attack: the idle units take position at the edge of the base */
    AiImproved_PositionIdleDefenders(collectedArmies,collectedCount);
    return;
  }
  assignedCommandGeneration = g_AiCommandGenerationCandidateBase;

  /* the group is strong enough: pick the target */
  const FactionRuntimeIndex groupFactionIndex = collectedArmies[0]->factionIndex;
  if (AiImproved_IsActive()) {
    /* Improved AI: defence mode. A scouted hostile army about to overrun the base keeps the units at home. */
    if (AiImproved_DefenceMode(groupFactionIndex)) {
      AiImproved_PositionIdleDefenders(collectedArmies,collectedCount);
      return;
    }
    /* Improved AI: near, weakly defended targets first; workspace 03 read with its real stride. A target the group
       has no way to (path check) marks its area unreachable, and the next best one is tried, up to three. */
    for (int attempt = 0;; attempt++) {
      bestTargetModelRuntime = AiImproved_SelectGroupTarget(groupFactionIndex,collectedArmies,collectedCount,
                                                            g_AiCombatTargetClassBaseScores);
      if ((bestTargetModelRuntime == nullptr) ||
          AiImproved_GroupCanReachTarget(groupFactionIndex,collectedArmies,collectedCount,bestTargetModelRuntime)) {
        break;
      }
      if (attempt == 2) {
        bestTargetModelRuntime = nullptr;
        break;
      }
    }
    if (bestTargetModelRuntime == nullptr) {
      /* no target allowed (grace period) or none to damage: the idle units take position */
      AiImproved_PositionIdleDefenders(collectedArmies,collectedCount);
      return;
    }
    goto send_group;
  }
  /* Repairs / grace period (original AI): the same class choice without the targets the faction gave up or may
     not attack yet, and (repairs) without targets none of the group's units can damage and with workspace 03 read
     with its real 8-byte stride. */
  if (AiImproved_RepairsActive() || AiImproved_GraceActive(groupFactionIndex)) {
    const bool repairGroupChoice = AiImproved_RepairsActive();
    /* repairs: a target without a way to it (path check) is marked unreachable and the choice repeated, up to
       three times */
    for (int attempt = 0; attempt < 3; attempt++) {
      bestScore = 0;
      bestTargetModelRuntime = nullptr;
      if (g_AiWorkspace07Count != 0) {
        for (targetCandidateIndex = 0; targetCandidateIndex < g_AiWorkspace07Count; targetCandidateIndex++) {
          candidateTargetModelRuntime = g_AiWorkspace07Targets[targetCandidateIndex].modelRuntime;
          if ((candidateTargetModelRuntime == nullptr) ||
              !AiImproved_GroupTargetAllowed(groupFactionIndex,candidateTargetModelRuntime) ||
              (repairGroupChoice &&
               AiImproved_CountGroupUnitsDamaging(collectedArmies,collectedCount,candidateTargetModelRuntime) == 0)) {
            continue;
          }
          candidateClassScore = g_AiCombatTargetClassBaseScores
                                  [candidateTargetModelRuntime->definitionOrSavedId.runtimeDefinition->runtimeClassId];
          /* ties go to the later candidate, as in the original */
          if (bestScore <= candidateClassScore) {
            bestScore = candidateClassScore;
            bestTargetModelRuntime = candidateTargetModelRuntime;
          }
        }
      }
      else {
        const bool readRealStride = repairGroupChoice;
        for (targetCandidateIndex = 0; targetCandidateIndex < g_AiWorkspace03Count; targetCandidateIndex++) {
          candidateTargetModelRuntime = readRealStride ?
              g_AiWorkspace03UnseenHostiles[targetCandidateIndex].modelRuntime.get() :
              reinterpret_cast<AiTargetWorkspaceEntry *>(g_AiWorkspace03UnseenHostiles)[targetCandidateIndex].modelRuntime.get(); /* the quirk's stride */
          if ((candidateTargetModelRuntime == nullptr) ||
              !AiImproved_GroupTargetAllowed(groupFactionIndex,candidateTargetModelRuntime) ||
              (repairGroupChoice &&
               AiImproved_CountGroupUnitsDamaging(collectedArmies,collectedCount,candidateTargetModelRuntime) == 0)) {
            continue;
          }
          candidateClassScore = g_AiCombatTargetClassBaseScores
                                  [candidateTargetModelRuntime->definitionOrSavedId.runtimeDefinition->runtimeClassId];
          if (bestScore <= candidateClassScore) {
            bestScore = candidateClassScore;
            bestTargetModelRuntime = candidateTargetModelRuntime;
          }
        }
      }
      if ((bestScore == 0) || (bestTargetModelRuntime == nullptr)) {
        return;
      }
      if (!repairGroupChoice ||
          AiImproved_GroupCanReachTarget(groupFactionIndex,collectedArmies,collectedCount,bestTargetModelRuntime)) {
        goto send_group;
      }
    }
    return;
  }
  targetCandidateCount = g_AiWorkspace07Count;
  targetCandidateRecords = g_AiWorkspace07Targets;
  if (g_AiWorkspace07Count == 0) {
    /* Original quirk: workspace 03 holds 8-byte AiRuntimeWorkspaceEntry records (written at ws03+count*8 in
       0x005383BE) but the original walks it with the 16-byte workspace-07 stride (0x0053B960 `add edi,0x10`,
       modelRuntime read at +8), so candidate i is ws03[2i+1].modelRuntime: for i >= count/2 a stale entry of an
       earlier rebuild, for count > 256 past the 0x1000-byte buffer. Kept: it feeds the AI decisions and the AI
       hash; a loaded game can see a different stale tail than an uninterrupted one. */
    targetCandidateCount = g_AiWorkspace03Count;
    targetCandidateRecords = reinterpret_cast<AiTargetWorkspaceEntry *>(g_AiWorkspace03UnseenHostiles); /* the quirk's stride */
    if (g_AiWorkspace03Count == 0) {
      return;
    }
  }
  bestScore = 0;
  bestTargetModelRuntime = nullptr;
  for (targetCandidateIndex = 0; targetCandidateIndex < targetCandidateCount; targetCandidateIndex++) {
    candidateTargetModelRuntime = targetCandidateRecords[targetCandidateIndex].modelRuntime;
    if (candidateTargetModelRuntime == nullptr) {
      continue;
    }
    candidateClassScore = g_AiCombatTargetClassBaseScores
                            [candidateTargetModelRuntime->definitionOrSavedId.runtimeDefinition->runtimeClassId];
    /* ties go to the later candidate */
    if (bestScore <= candidateClassScore) {
      bestScore = candidateClassScore;
      bestTargetModelRuntime = candidateTargetModelRuntime;
    }
  }
  if (bestScore == 0) {
    return;
  }

  /* send every collected army against the target model's owning army */
send_group:
  targetRuntime = bestTargetModelRuntime->ownerArmyRuntimeOrSavedOffset.entityRuntime;
  collectedCount = g_AiCollectedEntityCount;
  collectedArmies = g_AiWorkspace14CollectedArmies;
  if (AiImproved_IsActive()) {
    /* Improved AI: the group leaves in waves, light units first (AiImproved_SortGroupLightFirst) */
    AiImproved_SortGroupLightFirst(collectedArmies,collectedCount);
  }
  for (collectedIndex = 0; collectedIndex < collectedCount; collectedIndex++) {
    collectedArmy = collectedArmies[collectedIndex];
    const uint32_t waveDelay = AiImproved_IsActive() ? AiImproved_AttackWaveDelay(collectedIndex,collectedCount) : 0;
    if (waveDelay != 0 && AiImproved_QueueGroupOrder(collectedArmy,targetRuntime,bestTargetModelRuntime,waveDelay)) {
      /* a later wave: joins the group now, its route is planned when its wave leaves */
      collectedArmy->assignedTargetArmyRuntime = Thandor_PointerToU32(targetRuntime);
      collectedArmy->commandModeFlags = collectedArmy->commandModeFlags | ARMY_COMMAND_MODE_INTERRUPTED;
      collectedArmy->aiUnitFlags = collectedArmy->aiUnitFlags | AI_UNIT_STATE94_GROUP_ASSIGNED;
      collectedArmy->aiUnitState = 8;
      collectedArmy->commandGeneration = assignedCommandGeneration;
      continue;
    }
    if (AiImproved_RepairsActive()) {
      /* Repairs: the route to the target is only planned without ARMY_MOVEMENT_ROUTED and with the retry countdown
         run out (ArmyRuntime_StartMoveCommandWithFallbackWaypoints). A unit that last made a routed move (rally
         point, guard post) still has the flag, which the original clears only after this call, so such a unit
         got the attack order without a route and stood still. Cleared first, as the player's orders do
         (the selection commands). */
      collectedArmy->movementStateFlags = collectedArmy->movementStateFlags & ~ARMY_MOVEMENT_ROUTED;
      reinterpret_cast<ArmyMovementRuntime *>(collectedArmy)->retryCountdown = 0; /* the army slot as its movement view, as in units.cpp */
    }
    ArmyRuntime_ResolveCommandTargetAndRoute(targetRuntime,collectedArmy);
    collectedArmy->assignedTargetArmyRuntime = Thandor_PointerToU32(targetRuntime); /* 32-bit format field: ArmyRuntimeSlot.assignedTargetArmyRuntime */
    collectedArmy->commandModeFlags = collectedArmy->commandModeFlags | ARMY_COMMAND_MODE_INTERRUPTED;
    collectedArmy->aiUnitFlags = collectedArmy->aiUnitFlags | AI_UNIT_STATE94_GROUP_ASSIGNED;
    collectedArmy->movementStateFlags = collectedArmy->movementStateFlags & ~ARMY_MOVEMENT_ROUTED;
    collectedArmy->aiUnitState = 8;
    collectedArmy->commandGeneration = assignedCommandGeneration;
  }
  if (AiImproved_AnyActive()) {
    AiImproved_OnGroupAttack(groupFactionIndex,collectedArmies,collectedCount,bestTargetModelRuntime);
  }
}


/* Picks the best target for sourceArmyRuntime among the armies of the world owner list: sums the source's
   eight class counters (targetClassShotDamage); a positive sum searches armies of other factions, a negative one
   other armies of the own faction (skipping entities flagged 0x400), zero searches nothing. Candidates must carry
   the source faction's bit (2 << 2 * faction) in terrainOccupancyMask0 and are scored by
   AiCombatTarget_EvaluateCandidateScore within depth-bin masks around the source (radius weaponRangeQ12 plus
   4.0). Returns the best army (or NULL) and stores the sum in *outSourceClassCount.
   Only called by AiCombatDecision_UpdateTargetAssignment.
   Original quirk: a zero sum stores a stack leftover instead of the sum (see the body); this code stores the
   positive constant AI_SOURCE_CLASS_COUNT_ZERO_SUM_LEFTOVER, since every traced original path leaves a positive
   value there and the caller only tests the sign.
*/
ArmyRuntimeSlot *AiCombatTarget_SelectBestCandidate
          (WorldRuntimeContext *worldRuntime,ArmyRuntimeSlot *sourceArmyRuntime,
          AiSourceClassCount *outSourceClassCount)

{
  ArmyRuntimeSlot *candidateArmyRuntime;
  DepthBinMask32 sourceDepthMask1;
  DepthBinMask32 sourceDepthMask0;
  AiCandidateScore32 candidateScore;
  int classIndex;
  FactionRuntimeIndex candidateFactionIndex;
  bool relationFitsSearch;
  Q12 searchRadiusQ12;
  AiCandidateScore32 currentBestScore;
  AiSourceClassCount sourceClassCount;
  AiSourceClassCount returnedSourceClassCount;
  ArmyRuntimeSlot *bestCandidateArmyRuntime;
  WorldOwnerListNode *ownerNodeCursor;
  ModelRuntimeNode *sourceModelNode;
  GameEntityRuntime *candidateEntityRuntime;
  FactionRuntimeIndex sourceFactionIndex;

  sourceClassCount = 0;
  ownerNodeCursor = worldRuntime->ownerListHead;
  bestCandidateArmyRuntime = nullptr;
  /* the eight class counters targetClassShotDamage, summed from the last one down */
  for (classIndex = 7; classIndex >= 0; classIndex--) {
    sourceClassCount = sourceClassCount + sourceArmyRuntime->targetClassShotDamage[classIndex];
  }
  /* Original quirk: with a zero sum the original never stores the count and returns, as the count, whatever
     an earlier call left in that stack slot, 36 bytes below AiCombatDecision_UpdateTargetAssignment's return
     address. The call just before at the same depth is
     ArmyRuntimeHierarchy_UpdateProgressAndClassCallbacksRecursive: for an army with child entities each child's
     frame leaves worldRuntime there, otherwise the class handler's frame leaves e.g. the movement length (a
     local) or a pushed pointer / return address. The value is class and state dependent, but every traced path
     leaves a positive one (the movement length is 0 only when the unit stands exactly on its movement point),
     and the caller only tests the count > 0. An earlier version of this code returned the most common of them, the world
     runtime pointer (positive: /LARGEADDRESSAWARE:NO); since the only use of the value is that sign test (the
     zero-sum path finds no candidate, and AiCombatDecision_UpdateTargetAssignment reads sourceClassCount nowhere
     else), any positive constant gives the same decisions and stays positive on 64-bit. */
  returnedSourceClassCount = AI_SOURCE_CLASS_COUNT_ZERO_SUM_LEFTOVER;
  if (sourceClassCount != 0) {
    /* Repairs / grace period: the AI factions' units skip given-up targets, leave walls for last and, during the
       grace period, the human players' buildings outside their own base. */
    const bool adjustScores = AiImproved_AdjustsTargetsOf(sourceArmyRuntime);
    sourceModelNode = sourceArmyRuntime->modelNodeRuntime;
    searchRadiusQ12 = sourceArmyRuntime->weaponRangeQ12 + 4 * Q12_ONE;
    sourceDepthMask1 =
         DepthInterval_BuildBinMask(searchRadiusQ12,(sourceModelNode->worldTransform).translation.x)
    ;
    sourceDepthMask0 =
         DepthInterval_BuildBinMask(searchRadiusQ12,(sourceModelNode->worldTransform).translation.y)
    ;
    sourceFactionIndex = sourceArmyRuntime->factionIndex;
    currentBestScore = 0;
    returnedSourceClassCount = sourceClassCount;
    for (; ownerNodeCursor != nullptr; ownerNodeCursor = ownerNodeCursor->nextNode) {
      if (ownerNodeCursor->ownerClassId != WORLD_OWNER_RUNTIME_MODEL) {
        continue;
      }
      candidateEntityRuntime = WorldOwnerNode_EntityRuntime(ownerNodeCursor);
      candidateArmyRuntime = candidateEntityRuntime->common.ownership.linkedArmyRuntime();
      /* a negative sum skips entities flagged 0x400 */
      if ((sourceClassCount < 0) &&
          (Any((candidateEntityRuntime->common).runtimeFlags & ARMY_MODEL_STATE_NO_REGENERATION))) {
        continue;
      }
      if (Any((candidateEntityRuntime->common).runtimeFlags & ARMY_RUNTIME_FLAG_DESTROYED)) {
        continue;
      }
      candidateFactionIndex = candidateArmyRuntime->factionIndex;
      if (candidateFactionIndex == 0) {
        continue;
      }
      /* Non-positive class counts look for other armies of the own faction, positive ones for armies
         of other factions. */
      if (sourceClassCount < 1) {
        relationFitsSearch = (candidateFactionIndex == sourceArmyRuntime->factionIndex) &&
                             (sourceArmyRuntime != candidateArmyRuntime);
      }
      else {
        relationFitsSearch = candidateFactionIndex != sourceArmyRuntime->factionIndex;
      }
      if (!relationFitsSearch) {
        continue;
      }
      /* bit 1 of the source faction's 2-bit field in the candidate's terrainOccupancyMask0 */
      if ((candidateArmyRuntime->terrainOccupancyMask0 & 2 << ((char)sourceFactionIndex * 2 & 31U)) == 0) {
        continue;
      }
      candidateScore =
           AiCombatTarget_EvaluateCandidateScore
                     (currentBestScore,sourceClassCount,sourceDepthMask0,sourceDepthMask1,
                      candidateArmyRuntime,sourceArmyRuntime);
      if (adjustScores) {
        candidateScore = (AiCandidateScore32)AiImproved_AdjustTargetScore
                           (sourceArmyRuntime,candidateArmyRuntime,(uint32_t)candidateScore);
      }
      if (currentBestScore < candidateScore) {
        g_AiCombatTargetSelectedCommandGenerationRightShiftBits =
             g_AiCombatTargetCurrentCommandGenerationRightShiftBits;
        currentBestScore = candidateScore;
        bestCandidateArmyRuntime = candidateArmyRuntime;
      }
    }
  }
  *outSourceClassCount = returnedSourceClassCount;
  return bestCandidateArmyRuntime;
}


/* Scores candidateArmyRuntime as a target for sourceArmyRuntime; 0 rejects it. Requires overlapping depth-bin
   masks, a faction relation that fits the search (not friendly for sourceClassCount > 0, friendly otherwise,
   where only damaged candidates count) and a horizontal distance within the source radius (weaponRangeQ12) plus 2.0.
   The score adds weighted terms for the remaining clearance, the class base score, the two armies' class
   counters and the condition deficit (1.0 - ModelRuntime_QueryHierarchyConditionRatioQ12; the counter and
   deficit terms are divided by the Q12 unity); a score above currentBestScore is then
   quartered when the weapon line-of-fire test returns true, and dropped unless the source definition's
   accelerationPerTick is set. The class definitions are read through two dereferences (the definition's
   targetClassIndex), i.e. the live type.
   Called by AiCombatTarget_SelectBestCandidate and ArmyRuntime_SpawnIndexedModelPointEffectNearCandidate.
*/
AiCandidateScore32 AiCombatTarget_EvaluateCandidateScore
          (AiCandidateScore32 currentBestScore,AiSourceClassCount sourceClassCount,
          DepthBinMask32 sourceDepthMask0,DepthBinMask32 sourceDepthMask1,
          ArmyRuntimeSlot *candidateArmyRuntime,ArmyRuntimeSlot *sourceArmyRuntime)

{
  uint32_t candidateFactionIndex;
  ModelDefinition *candidateDefinition;
  int reachQ12;
  int deltaXQ12;
  int deltaYQ12;
  int64_t clearanceSquared;
  int64_t radialClearanceWeight;
  int candidateCounterForSourceClass;
  int sourceCounterForCandidateClass;
  uint32_t radialClearanceQ12;
  ModelRuntimeSlot *sourceWeaponModelRuntime;
  ModelRuntimeNode *candidateAimModelNode;
  int classBaseScore;
  bool masksOverlap;
  bool capabilityBitClear;
  bool lineOfFireTestPassed;
  Q12 conditionRatioQ12;
  uint32_t candidateScore;
  uint32_t sourceRadiusQ12;
  ModelRuntimeSlot *sourceModelRuntime;
  ModelRuntimeNode *candidateModelNode;

  candidateFactionIndex = candidateArmyRuntime->factionIndex;
  candidateModelNode = candidateArmyRuntime->modelNodeRuntime;
  masksOverlap = DepthBinMasks_Overlap
                      (candidateModelNode->depthBinMaskFar,candidateModelNode->depthBinMaskNear,sourceDepthMask0,
                       sourceDepthMask1);
  if (!masksOverlap) {
    return 0;
  }
  capabilityBitClear =
       GameFactionRuntime_TestCapabilityBitClear(candidateFactionIndex,sourceArmyRuntime->factionIndex);
  if (sourceClassCount < 1) {
    if (capabilityBitClear) {
      return 0;
    }
  }
  else if (!capabilityBitClear) {
    return 0;
  }

  /* clearance^2 = (radius + 2.0)^2 - dx^2 - dy^2 in 64 bits; negative rejects */
  reachQ12 = sourceArmyRuntime->weaponRangeQ12 + 2 * Q12_ONE;
  deltaXQ12 = (sourceArmyRuntime->modelNodeRuntime->worldTransform).translation.x -
              (candidateModelNode->worldTransform).translation.x;
  clearanceSquared = (int64_t)reachQ12 * (int64_t)reachQ12 - (int64_t)deltaXQ12 * (int64_t)deltaXQ12;
  if (clearanceSquared < 0) {
    return 0;
  }
  deltaYQ12 = (sourceArmyRuntime->modelNodeRuntime->worldTransform).translation.y -
              (candidateModelNode->worldTransform).translation.y;
  clearanceSquared = clearanceSquared - (int64_t)deltaYQ12 * (int64_t)deltaYQ12;
  if (clearanceSquared < 0) {
    return 0;
  }
  radialClearanceQ12 =
       FixedMath_UInt64Sqrt((UInt64Half32)((uint64_t)clearanceSquared >> 32),(UInt64Half32)clearanceSquared);
  radialClearanceWeight = (int64_t)g_AiCombatTargetRadialClearanceWeight;
  sourceRadiusQ12 = sourceArmyRuntime->weaponRangeQ12;
  candidateDefinition = (((candidateArmyRuntime->modelRuntimeOrSavedOffset).modelRuntime)->
          definitionOrSavedId).runtimeDefinition;
  /* class counters (targetClassShotDamage): the candidate's for the source's class and the source's for the
     candidate's class; a zero candidate counter selects the shorter command time (shift 2) */
  candidateCounterForSourceClass = candidateArmyRuntime->targetClassShotDamage
                  [(((sourceArmyRuntime->modelRuntimeOrSavedOffset).modelRuntime)->
                   definitionOrSavedId).runtimeDefinition->targetClassIndex];
  sourceCounterForCandidateClass = sourceArmyRuntime->targetClassShotDamage[candidateDefinition->targetClassIndex];
  g_AiCombatTargetCurrentCommandGenerationRightShiftBits = 0;
  if (candidateCounterForSourceClass == 0) {
    g_AiCombatTargetCurrentCommandGenerationRightShiftBits = 2;
  }
  if (sourceCounterForCandidateClass == 0) {
    return 0;
  }
  /* a negative source counter counts by its magnitude and ignores the candidate's counter */
  if (sourceCounterForCandidateClass < 0) {
    candidateCounterForSourceClass = 0;
    sourceCounterForCandidateClass = -sourceCounterForCandidateClass;
  }
  classBaseScore =
       g_AiCombatTargetClassBaseScores[candidateDefinition->runtimeClassId] *
       g_AiCombatTargetClassBaseScoreMultiplier;
  conditionRatioQ12 = ModelRuntime_QueryHierarchyConditionRatioQ12(reinterpret_cast<RuntimeModelFactionPrefix *>(candidateArmyRuntime)); /* the army slot header as the hierarchy metric view */
  /* a friendly search needs condition < 1.0 (unsigned compare as in the original) */
  if ((sourceClassCount < 0) && ((uint32_t)conditionRatioQ12 >= (uint32_t)Q12_ONE)) {
    return 0;
  }
  candidateScore =
       (int)(((int)radialClearanceQ12 * radialClearanceWeight) / (int64_t)(int)sourceRadiusQ12) +
      classBaseScore +
       (int)(((int64_t)g_AiCombatTargetSourceCounterCountWeight * (int64_t)candidateCounterForSourceClass) /
            (int64_t)Q12_ONE) +
       (int)(((int64_t)g_AiCombatTargetCandidateCounterCountWeight * (int64_t)sourceCounterForCandidateClass) /
            (int64_t)Q12_ONE) +
       (int)(((int64_t)g_AiCombatTargetScaleDeficitWeight *
             (int64_t)(int)((uint32_t)Q12_ONE - (uint32_t)conditionRatioQ12)) /
            (int64_t)Q12_ONE);
  if (candidateCounterForSourceClass == 0) {
    candidateScore = candidateScore >> 2;
  }
  if (currentBestScore >= (int)candidateScore) {
    return candidateScore;
  }

  /* the weapon is the primary or, without one, the secondary weapon slot */
  sourceModelRuntime = (sourceArmyRuntime->modelRuntimeOrSavedOffset).modelRuntime;
  candidateAimModelNode = candidateArmyRuntime->modelNodeRuntime;
  if (sourceModelRuntime->attachmentCount == 0) {
    return candidateScore;
  }
  candidateDefinition = (((candidateArmyRuntime->modelRuntimeOrSavedOffset).modelRuntime)->
          definitionOrSavedId).runtimeDefinition;
  sourceWeaponModelRuntime = sourceModelRuntime->attachments[ARMY_WEAPON_SLOT_PRIMARY].childModelRuntimeOrSavedOffset;
  if (sourceWeaponModelRuntime == nullptr) {
    if (sourceModelRuntime->attachmentCount == 1) {
      return candidateScore;
    }
    sourceWeaponModelRuntime = sourceModelRuntime->attachments[ARMY_WEAPON_SLOT_SECONDARY].childModelRuntimeOrSavedOffset;
    if (sourceWeaponModelRuntime == nullptr) {
      return candidateScore;
    }
  }
  /* aircraft are aimed at their first child node, all targets at the definition's height offset (aimHeightOffsetQ12)
     above the node */
  if (candidateDefinition->runtimeClassId == MODEL_RUNTIME_CLASS_21_AIRCRAFT) {
    candidateAimModelNode = candidateAimModelNode->childNodes[0];
  }
  lineOfFireTestPassed = ArmyWeaponRuntime_TestTargetLineOfFire
                              (candidateDefinition->aimHeightOffsetQ12 +
                               (candidateAimModelNode->worldTransform).translation.z,
                               (candidateAimModelNode->worldTransform).translation.y,
                               (candidateAimModelNode->worldTransform).translation.x,
                               &g_InGameRuntimeRoot->worldRuntime,
                               sourceWeaponModelRuntime);
  if (lineOfFireTestPassed) {
    candidateScore = candidateScore >> 2;
    if ((((sourceArmyRuntime->modelRuntimeOrSavedOffset).modelRuntime)->
         definitionOrSavedId).runtimeDefinition->accelerationPerTick == 0) {
      return 0;
    }
  }
  return candidateScore;
}

