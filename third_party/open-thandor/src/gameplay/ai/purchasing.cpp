/*
 * Open Thandor
 * Project: https://github.com/idkFoxes/open-thandor/tree/main
 * File: https://github.com/idkFoxes/open-thandor/blob/main/src/gameplay/ai/purchasing.cpp
 * Reverse engineering by idkFoxes 2026
 */

#include <thandor/gameplay/ai/purchasing.h>
#include <thandor/thandor.h>
#include <thandor/gameplay/ai/improved.h>

/* Module data. */

const AiArmyScoreWeights g_AiArmyCandidateScoreWeightsVariantC15 = {
    .pressureCategoryWeights = {0, 384, 384, 384, 384, 448, 384, 512},
    .definitionValue60Weight = 384,
    .definitionValue0CWeight = 192,
    .armyRecord74Weight = 256,
    .armyRecord78Weight = 512,
    .nonzeroDefinition18Bonus = 4096,
    .baseScore = -4096};

static uint32_t g_AiPurchaseAppliedArmyClassMask = 0;

static const AiArmyScoreWeights g_AiArmyCandidateScoreWeightsVariantB15 = {
    .pressureCategoryWeights = {0, 128, 128, 128, 128, 128, 128, 128},
    .definitionValue0CWeight = 512,
    .armyRecord74Weight = 128,
    .armyRecord70Weight = 512,
    .nonzeroDefinition18Bonus = 4096,
    .baseScore = -4096};

static const AiArmyScoreWeights g_AiArmyCandidateScoreWeightsVariantA15 = {
    .pressureCategoryWeights = {0, 512, 512, 512, 512, 640, 512},
    .definitionValue60Weight = 512,
    .definitionValue0CWeight = -64,
    .armyRecord74Weight = 512,
    .armyRecord78Weight = 256,
    .nonzeroDefinition18Bonus = -256};

/* The army asset registry and workspace 11 keep ArmyAssetRecordPrefix pointers; every registered record is a whole
   ArmyAssetRecord (the prefix is its first 0x10 bytes), which the scoring reads. */
static ArmyAssetRecord *AiPurchase_FullAssetRecord(ArmyAssetRecordPrefix *assetPrefix)
{
  return reinterpret_cast<ArmyAssetRecord *>(assetPrefix);
}

/* Average faction-weighted score (AiArmyCandidate_ComputeFactionWeightedScore) of the enabled army assets
   that can carry the model definition of runtimeClassId: those whose own linked-definition list names it,
   or, once the technology of the asset's first linked definition is unlocked, one of its (up to two) child
   lists. Only positive scores count; 0 when the class has no definition or no asset scores. Only caller:
   AiTechnologyScore_ComputeCategoryCompatibleCandidateValue, which passes a technology id as runtimeClassId.
*/
AiCandidateScore32
AiArmyCandidate_ComputeAverageCompatibleAssetScore
          (const AiArmyScoreWeights *scoreWeights,FactionRuntimeIndex factionIndex,
          ModelRuntimeClassId runtimeClassId)

{
  ArmyAssetRecord *armyAssetRecord;
  AiLinkedDefinitionListView *assetDefinitionList;
  ModelDefinitionRecordPrefix *modelDefinition;
  AiCandidateScore32 candidateScore;
  int registryEntriesRemaining;
  ArmyAssetRecordPrefix **armyAssetRegistryCursor;
  bool technologyLocked;
  int compatibleAssetCount;
  uint32_t compatibleAssetScoreSum;
  AiLinkedDefinitionListView *nestedLinkedDefinitions;
  PckModelDefinitionIdCatalog candidateModelDefinitionId;
  AiLinkedDefinitionListView *secondNestedLinkedDefinitions;
  
  modelDefinition = ModelDefinitionRegistry_FindByRuntimeClassId(runtimeClassId);
  candidateScore = 0;
  if (modelDefinition != nullptr) {
    candidateModelDefinitionId = modelDefinition->definitionId;
    armyAssetRegistryCursor = g_ArmyAssetRecordRegistry;
    registryEntriesRemaining = ARMY_ASSET_REGISTRY_SLOT_COUNT;
    compatibleAssetScoreSum = 0;
    compatibleAssetCount = 0;
    for (; registryEntriesRemaining != 0;
        armyAssetRegistryCursor++,
        registryEntriesRemaining--) {
      armyAssetRecord = AiPurchase_FullAssetRecord(*armyAssetRegistryCursor);
      if ((armyAssetRecord != nullptr) &&
         ((armyAssetRecord->flags & 1) != 0)) { /* bit 0: asset enabled */
        /* the asset's root node is an AiLinkedDefinitionListView */
        assetDefinitionList = Thandor_U32ToPointer<AiLinkedDefinitionListView>(armyAssetRecord->rootNodeOffsetOrPointer); /* 32-bit format field: ArmyAssetRecord.rootNodeOffsetOrPointer */
        if ((((candidateModelDefinitionId !=
               assetDefinitionList->definitionIds[0]) &&
             (((candidateModelDefinitionId !=
                assetDefinitionList->definitionIds[1] &&
               (candidateModelDefinitionId !=
                assetDefinitionList->definitionIds[2])) &&
              (candidateModelDefinitionId !=
               assetDefinitionList->definitionIds[3])))) &&
            (((candidateModelDefinitionId !=
               assetDefinitionList->definitionIds[4] &&
              (candidateModelDefinitionId !=
               assetDefinitionList->definitionIds[5])) &&
             (candidateModelDefinitionId !=
              assetDefinitionList->definitionIds[6])))) &&
           (candidateModelDefinitionId !=
            assetDefinitionList->definitionIds[7])) {
          /* true while the technology is still locked */
          technologyLocked = ModelDefinition_IsFactionTechnologyLocked
                            (g_GameFactionRuntimeImage.records[factionIndex].technologyMasks256Bits,
                             assetDefinitionList->definitionIds[0]);
          if (((technologyLocked) ||
              (nestedLinkedDefinitions =
               Thandor_U32ToPointer<AiLinkedDefinitionListView>(assetDefinitionList->childList0Address), /* 32-bit format field: ArmyModelTreeNode.childList0Address */
              assetDefinitionList->childListCount == 0)) ||
             ((((candidateModelDefinitionId != nestedLinkedDefinitions->definitionIds[0] &&
                ((((candidateModelDefinitionId != nestedLinkedDefinitions->definitionIds[1] &&
                   (candidateModelDefinitionId != nestedLinkedDefinitions->definitionIds[2])) &&
                  (candidateModelDefinitionId != nestedLinkedDefinitions->definitionIds[3])) &&
                 ((candidateModelDefinitionId != nestedLinkedDefinitions->definitionIds[4] &&
                  (candidateModelDefinitionId != nestedLinkedDefinitions->definitionIds[5])))))) &&
               ((candidateModelDefinitionId != nestedLinkedDefinitions->definitionIds[6] &&
                (candidateModelDefinitionId != nestedLinkedDefinitions->definitionIds[7])))) &&
              ((secondNestedLinkedDefinitions =
                Thandor_U32ToPointer<AiLinkedDefinitionListView>(assetDefinitionList->childList1Address), /* 32-bit format field: ArmyModelTreeNode.childList1Address */
               assetDefinitionList->childListCount < 2 ||
               (((candidateModelDefinitionId != secondNestedLinkedDefinitions->definitionIds[0] &&
                 (candidateModelDefinitionId != secondNestedLinkedDefinitions->definitionIds[1])) &&
                (((candidateModelDefinitionId != secondNestedLinkedDefinitions->definitionIds[2] &&
                  (((candidateModelDefinitionId != secondNestedLinkedDefinitions->definitionIds[3]
                    && (candidateModelDefinitionId !=
                        secondNestedLinkedDefinitions->definitionIds[4])) &&
                   (candidateModelDefinitionId != secondNestedLinkedDefinitions->definitionIds[5])))
                  ) && ((candidateModelDefinitionId !=
                         secondNestedLinkedDefinitions->definitionIds[6] &&
                        (candidateModelDefinitionId !=
                         secondNestedLinkedDefinitions->definitionIds[7]))))))))))))
          continue;
        }
        candidateScore = AiArmyCandidate_ComputeFactionWeightedScore
                          (scoreWeights,factionIndex,armyAssetRecord);
        if (0 < candidateScore) {
          compatibleAssetScoreSum = compatibleAssetScoreSum + candidateScore;
          compatibleAssetCount++;
        }
      }
    }
    candidateScore = 0;
    if (compatibleAssetCount != 0) {
      candidateScore = (AiCandidateScore32)
              ((int64_t)(uint64_t)compatibleAssetScoreSum / (int64_t)compatibleAssetCount);
    }
  }
  return candidateScore;
}

/* Improved AI: chassis class of an army asset as the faction would build it (the targetClassIndex of its unlocked
   definition: 1 wheeled, 2 tracked, 3 walker, 4 glider, 5 aircraft, 6 ship), 0 when unknown. */
static int AiPurchase_AssetChassisClass(ArmyAssetRecordPrefix *asset,FactionRuntimeIndex factionIndex)
{
  if (asset == nullptr) {
    return 0;
  }
  AiLinkedDefinitionListView *linkedDefinitionList =
       Thandor_U32ToPointer<AiLinkedDefinitionListView>(AiPurchase_FullAssetRecord(asset)->rootNodeOffsetOrPointer); /* 32-bit format field: ArmyAssetRecord.rootNodeOffsetOrPointer */
  if (linkedDefinitionList == nullptr) {
    return 0;
  }
  ModelDefinitionResolveView *definition = reinterpret_cast<ModelDefinitionResolveView *>(
       ModelDefinition_SelectFactionUnlockedLinkedDefinition(factionIndex,reinterpret_cast<uintptr_t>(linkedDefinitionList)));
  if (definition == nullptr || definition->targetClassIndex >= 8) {
    return 0;
  }
  return (int)definition->targetClassIndex;
}

/* Improved AI: an idle structure of workspace 00 could produce the asset now (the producer tests of
   AiPurchaseCandidate_HasEligibleProducer without its once-per-round mask, which still holds the last round's
   purchases while the candidates are proposed). */
static bool AiPurchase_HasIdleProducer(ArmyAssetRecordPrefix *asset)
{
  uint32_t producerClassMask = asset[1].selectionDetailTemplateVariantIndex;
  AiStructureWorkspaceEntry *workspaceEntry = g_AiWorkspace00Structures;
  for (uint32_t remainingEntries = g_AiWorkspace00Count; remainingEntries != 0; remainingEntries--, workspaceEntry++) {
    ModelRuntimeSlot *entitySlot = workspaceEntry->runtimeSlotAddressOrZero;
    if ((entitySlot == nullptr) || Any(entitySlot->classState.stateFlags & ARMY_MODEL_STATE_BUILD_BLOCKING_MASK)) {
      continue;
    }
    ModelDefinition *definition = entitySlot->definitionOrSavedId.runtimeDefinition;
    if ((producerClassMask & ARMY_ASSET_FLAG_BUILT_BY_CLASS11) != 0) {
      if ((definition->runtimeClassId == MODEL_RUNTIME_CLASS_11) && (entitySlot->classState.behaviorState == 0)) {
        return true;
      }
    }
    else if ((producerClassMask & ARMY_ASSET_FLAG_BUILT_AT_AIRCRAFT_PAD) != 0) {
      if ((definition->runtimeClassId == MODEL_RUNTIME_CLASS_22) && (entitySlot->classState.classStateAC == 0)) {
        return true;
      }
    }
    else if ((producerClassMask & ARMY_ASSET_FLAGS_BUILT_BY_FACTORY) != 0) {
      if ((definition->runtimeClassId == MODEL_RUNTIME_CLASS_13) &&
          ((definition->classParameterC4 & producerClassMask & ARMY_ASSET_FLAGS_BUILT_BY_FACTORY) != 0) &&
          (entitySlot->classState.behaviorState == 0)) {
        return true;
      }
    }
  }
  return false;
}

/* Improved AI: the faction's units per chassis class (workspace 01: on the map, queued or in production). */
static uint32_t AiPurchase_CountOwnUnitClasses(FactionRuntimeIndex factionIndex,uint32_t classCounts[8])
{
  uint32_t total = 0;
  for (int c = 0; c < 8; c++) classCounts[c] = 0;
  AiRuntimeWorkspaceEntry *entry = g_AiWorkspace01Units;
  for (uint32_t i = 0; i < g_AiWorkspace01Count; i++, entry++) {
    int unitClass = 0;
    if (entry->modelRuntime != nullptr) {
      unitClass = entry->modelRuntime->definitionOrSavedId.runtimeDefinition->targetClassIndex;
    }
    else {
      ArmyAssetRecordPrefix *asset;
      if (ArmyAssetRegistry_FindById(entry->armyAssetId,&asset) == 0) {
        unitClass = AiPurchase_AssetChassisClass(asset,factionIndex);
      }
    }
    if (unitClass >= 1 && unitClass <= 6) {
      classCounts[unitClass]++;
      total++;
    }
  }
  return total;
}

/* Improved AI: the purchase choice with variety. Scores the enabled assets of workspace 11 like the original loops
   (the best raw score, first one on ties). When that best one is a unit (ARM < 300), it picks among the units
   scoring at least half of it the one with the highest
     score * 4 / (4 + units of that asset the faction has, queued or in production),
   divided by 3 when its chassis class (wheeled, tracked, walker, glider, aircraft, ship) would make up more than
   40 % of an army of at least 4 units; other units than the best one count only when an idle factory can build
   them now. So the army becomes a mix of types and classes as far as the factories allow, still of strong units.
   Buildings keep their raw score. Returns the asset and its raw score in
   *outRawScore (<= 0: none). */
static int g_AiPurchaseVarietyRawScores[AI_WORKSPACE11_CAPACITY];

static ArmyAssetRecordPrefix *AiArmyCandidate_SelectAssetWithVariety
          (const AiArmyScoreWeights *scoreWeights,FactionRuntimeIndex factionIndex,int *outRawScore)
{
  ArmyAssetRecordPrefix *bestRawAsset = nullptr;
  int bestRawScore = 0;
  uint32_t count = g_AiWorkspace11Count;
  if (count > (uint32_t)AI_WORKSPACE11_CAPACITY) count = AI_WORKSPACE11_CAPACITY;
  for (uint32_t i = 0; i < count; i++) {
    ArmyAssetRecordPrefix *asset = g_AiWorkspace11ProducibleAssets[i];
    int candidateScore = 0;
    if ((AiPurchase_FullAssetRecord(asset)->flags & 1) != 0) {
      candidateScore = AiArmyCandidate_ComputeFactionWeightedScore(scoreWeights,factionIndex,
                                                                   AiPurchase_FullAssetRecord(asset));
    }
    g_AiPurchaseVarietyRawScores[i] = candidateScore;
    if (bestRawScore < candidateScore) {
      bestRawScore = candidateScore;
      bestRawAsset = asset;
    }
  }
  *outRawScore = bestRawScore;
  if (bestRawScore <= 0 || bestRawAsset->registryId >= ARM_0300_BUILDING_MDL0301) {
    return bestRawAsset;
  }
  uint32_t classCounts[8];
  uint32_t ownUnits = AiPurchase_CountOwnUnitClasses(factionIndex,classCounts);
  ArmyAssetRecordPrefix *chosenAsset = bestRawAsset;
  int64_t chosenScore = -1;
  for (uint32_t i = 0; i < count; i++) {
    ArmyAssetRecordPrefix *asset = g_AiWorkspace11ProducibleAssets[i];
    int rawScore = g_AiPurchaseVarietyRawScores[i];
    if (rawScore <= 0 || asset->registryId >= ARM_0300_BUILDING_MDL0301 ||
        (int64_t)rawScore * 2 < (int64_t)bestRawScore) {
      continue;
    }
    /* only what a free factory can build now (the best one stays the fallback) */
    if ((asset != bestRawAsset) && !AiPurchase_HasIdleProducer(asset)) {
      continue;
    }
    int64_t varietyScore = (int64_t)rawScore * 4 / (4 + (int64_t)AiImproved_CountOwnUnitsOfAsset(asset->registryId));
    int chassisClass = AiPurchase_AssetChassisClass(asset,factionIndex);
    if (ownUnits >= 4 && chassisClass >= 1 && chassisClass <= 6 &&
        (int64_t)(classCounts[chassisClass] + 1) * 5 > (int64_t)(ownUnits + 1) * 2) {
      varietyScore = varietyScore / 3;
    }
    if (chosenScore < varietyScore) {
      chosenScore = varietyScore;
      chosenAsset = asset;
    }
  }
  for (uint32_t i = 0; i < count; i++) {
    if (g_AiWorkspace11ProducibleAssets[i] == chosenAsset) {
      *outRawScore = g_AiPurchaseVarietyRawScores[i];
      break;
    }
  }
  return chosenAsset;
}

/* Buys the AI's candidates in descending weight order: each candidate's cost is taken from a running copy of
   the faction's Xenite, and every candidate with an eligible producer is applied, until one is no longer
   affordable. The cost is deducted even when no producer is found. Returns true when candidates
   existed but none was applied, false otherwise.
*/
bool AiPurchasePlanner_ExecuteAffordableCandidates(FactionRuntimeIndex factionIndex)

{
  bool noCandidateApplied;
  uint32_t candidateCost;
  int remainingCandidates;
  XeniteAmountQ4 remainingXenite;
  AiCandidateWorkspaceEntry *entry;
  bool insufficientXenite;

  remainingCandidates = g_AiCandidateWorkspaceEntryCount;
  entry = g_AiWorkspace13Candidates;
  g_AiPurchaseAppliedArmyClassMask = 0;
  /* protocol only (full Xenite storage): which candidates the round saw and what became of them */
  const bool logRound = AiImproved_PurchaseLogBegin(factionIndex);
  if (g_AiCandidateWorkspaceEntryCount == 0) {
    if (logRound) AiImproved_PurchaseLogEnd(factionIndex);
    return false;
  }
  noCandidateApplied = true;
  AiCandidateWorkspace_SortDescending();
  remainingXenite = g_GameFactionRuntimeImage.records[factionIndex].xeniteCurrentQ4;
  /* Repairs: the original stops at the first candidate it cannot pay and saves up for it. A candidate dearer than
     the whole storage can never be paid, so it blocked every purchase below it for good (seen: a 1500-Xenite
     building with 1250 storage, the storage building that would have helped ranked lower), and with full storage
     saving up is pointless, the income is lost. Repairs skip a candidate dearer than the storage; with the storage
     at least 90 % full they set no Xenite aside for candidates without a free producer and pass over a candidate
     they cannot pay instead of stopping. */
  const bool repairPurchases = AiImproved_RepairsActive();
  const XeniteAmountQ4 storageLimit = g_GameFactionRuntimeImage.records[factionIndex].xeniteStorageLimitQ4;
  const bool storageFull = repairPurchases && (storageLimit != 0) &&
      ((uint64_t)remainingXenite * 10 >= (uint64_t)storageLimit * 9);
  for (; remainingCandidates != 0; remainingCandidates--, entry++) {
    /* from level 1 on: no aircraft and no laser coil past the level's number (the candidate list is reused from the
       cache for several rounds, so a quota candidate would otherwise be bought again after the quota was met) */
    if (AiImproved_PurchaseOverQuota(factionIndex,entry->entityIdAndMultiplicity & AI_CANDIDATE_ID_MASK,
                                     entry->weightedScoreAndKind & AI_CANDIDATE_KIND_MASK)) {
      continue;
    }
    candidateCost = AiCandidateWorkspace_GetEntryXeniteCost(entry);
    if (repairPurchases && (storageLimit != 0) && (candidateCost > storageLimit)) {
      if (logRound) AiImproved_PurchaseLogNote(entry->weightedScoreAndKind,entry->entityIdAndMultiplicity,candidateCost,3);
      continue;
    }
    insufficientXenite = remainingXenite < candidateCost;
    if (storageFull && insufficientXenite) {
      if (logRound) AiImproved_PurchaseLogNote(entry->weightedScoreAndKind,entry->entityIdAndMultiplicity,candidateCost,4);
      continue;
    }
    if (storageFull && AiPurchaseCandidate_HasEligibleProducer(entry,factionIndex)) {
      /* no free producer now: nothing set aside */
      if (logRound) AiImproved_PurchaseLogNote(entry->weightedScoreAndKind,entry->entityIdAndMultiplicity,candidateCost,1);
      continue;
    }
    remainingXenite = remainingXenite - candidateCost;
    if (insufficientXenite) {
      if (logRound) {
        AiImproved_PurchaseLogNote(entry->weightedScoreAndKind,entry->entityIdAndMultiplicity,candidateCost,2);
        AiImproved_PurchaseLogEnd(factionIndex);
      }
      return noCandidateApplied;
    }
    if (!AiPurchaseCandidate_HasEligibleProducer(entry,factionIndex)) {
      AiPurchaseCandidate_ApplyToFaction(entry,factionIndex);
      noCandidateApplied = false;
      AiImproved_OnPurchase(factionIndex,entry->entityIdAndMultiplicity & AI_CANDIDATE_ID_MASK,
                            entry->weightedScoreAndKind & AI_CANDIDATE_KIND_MASK,candidateCost);
      if (logRound) AiImproved_PurchaseLogNote(entry->weightedScoreAndKind,entry->entityIdAndMultiplicity,candidateCost,0);
    }
    else if (logRound) {
      AiImproved_PurchaseLogNote(entry->weightedScoreAndKind,entry->entityIdAndMultiplicity,candidateCost,1);
    }
  }
  if (logRound) AiImproved_PurchaseLogEnd(factionIndex);
  return noCandidateApplied;
}

/* Proposes the best producible army asset for base defence. Once Arms Factories is researched and the planning
   capacity allows it, scores the eligible army assets of workspace 11 with weight profile A and proposes the best
   one. Its weight (armyVariantABaseWeight) is divided
   by 1 + the number of pending requests with ARM ids 340..379, then taken x3/4 while the faction's primary
   anchor cooldown is 0, else x2 (the cooldown runs after one of the faction's buildings was attacked). Assets below ARM 300 are only proposed while workspace 01 has at most 10
   entries.
*/
void AiArmyCandidate_AddBestDefenseAsset(FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime)

{
  AiCandidateScore32 candidateScore;
  uint32_t weightRange;
  int remainingEntries;
  uint32_t pendingRequestCount;
  uint32_t baseWeight;
  int bestScore;
  ArmyAssetRecordPrefix *bestArmyAsset = nullptr;
  AiRuntimeWorkspaceEntry *runtimeWorkspaceEntry;
  ArmyAssetRecordPrefix **armyAssetRegistryCursor;

  if (g_AiWorkspace11Count == 0) {
    return;
  }
  bestScore = 0;
  if (AiFactionRuntime_TestPlanningCapacityExceeded(4,factionIndex)) {
    return;
  }
  /* Technology_IsUnlockedForFaction returns true while the technology is still locked */
  if (Technology_IsUnlockedForFaction(TEC_001_ARMS_FACTORIES,factionIndex)) {
    return;
  }
  if (AiImproved_IsActive()) {
    /* Improved AI: the same scores with variety among the units */
    bestArmyAsset = AiArmyCandidate_SelectAssetWithVariety(&g_AiArmyCandidateScoreWeightsVariantA15,factionIndex,
                                                           &bestScore);
  }
  else {
    armyAssetRegistryCursor = g_AiWorkspace11ProducibleAssets;
    for (remainingEntries = g_AiWorkspace11Count; remainingEntries != 0; remainingEntries--) {
      if ((AiPurchase_FullAssetRecord(*armyAssetRegistryCursor)->flags & 1) != 0) {
        candidateScore = AiArmyCandidate_ComputeFactionWeightedScore
                           (&g_AiArmyCandidateScoreWeightsVariantA15,factionIndex,
                            AiPurchase_FullAssetRecord(*armyAssetRegistryCursor));
        if (bestScore < candidateScore) {
          bestArmyAsset = *armyAssetRegistryCursor;
          bestScore = candidateScore;
        }
      }
      armyAssetRegistryCursor++;
    }
  }
  if (bestScore <= 0) {
    return;
  }
  /* Improved AI: in peacetime no more defence buildings once placed plus pending ones reach the level's limit + 2 */
  if (AiImproved_IsActive() && (g_GameFactionRuntimeImage.records[factionIndex].primaryAnchorCooldown == 0) &&
      (bestArmyAsset->registryId >= ARM_0340_BUILDING_MDL0314) && (bestArmyAsset->registryId < ARM_0380_BUILDING_MDL0329) &&
      (AiImproved_CountPlacedDefenceBuildings() + AiImproved_CountPendingDefenceBuildings() >=
       AiImproved_ProactiveTowerLimit(factionIndex) + 2)) {
    return;
  }
  pendingRequestCount = 1;
  runtimeWorkspaceEntry = g_AiWorkspace04RequestedAssets;
  for (remainingEntries = g_AiWorkspace04Count; remainingEntries != 0; remainingEntries--) {
    if (((ARM_0340_BUILDING_MDL0314 - 1) < runtimeWorkspaceEntry->armyAssetId) &&
       (runtimeWorkspaceEntry->armyAssetId < ARM_0380_BUILDING_MDL0329)) {
      pendingRequestCount++;
    }
    runtimeWorkspaceEntry++;
  }
  baseWeight = g_AiKnowledgeData->parameters.armyVariantABaseWeight / pendingRequestCount;
  if (g_GameFactionRuntimeImage.records[factionIndex].primaryAnchorCooldown == 0) {
    weightRange = baseWeight * 3 >> 2;
  }
  else {
    weightRange = baseWeight * 2;
  }
  /* Improved AI: defence mode (a scouted mass attack) buys defence buildings ahead of units */
  if ((bestArmyAsset->registryId >= ARM_0340_BUILDING_MDL0314) &&
      (bestArmyAsset->registryId < ARM_0380_BUILDING_MDL0329)) {
    weightRange = weightRange * AiImproved_DefenceWeightFactor(factionIndex);
  }
  if ((299 < bestArmyAsset->registryId) || (g_AiWorkspace01Count < AiImproved_ArmyUnitLimit())) {
    AiCandidateWorkspace_AddOrAccumulateWeightedEntry(bestArmyAsset->registryId,weightRange,1);
  }
  /* Improved AI (Testversion 5): the best scored object above was nearly always a unit, so outside the defence
     mode the AI bought no tower. Until the level's tower count (placed plus pending) is reached, the best defence
     building it can build joins the list as well, with the same weight. */
  if (AiImproved_IsActive() &&
      ((bestArmyAsset->registryId < ARM_0340_BUILDING_MDL0314) || (bestArmyAsset->registryId >= ARM_0380_BUILDING_MDL0329)) &&
      (AiImproved_CountPlacedDefenceBuildings() + AiImproved_CountPendingDefenceBuildings() <
       AiImproved_ProactiveTowerLimit(factionIndex))) {
    ArmyAssetRecordPrefix *bestTower = nullptr;
    int bestTowerScore = 0;
    armyAssetRegistryCursor = g_AiWorkspace11ProducibleAssets;
    for (remainingEntries = g_AiWorkspace11Count; remainingEntries != 0; remainingEntries--, armyAssetRegistryCursor++) {
      ArmyAssetRecordPrefix *asset = *armyAssetRegistryCursor;
      if ((asset->registryId < ARM_0340_BUILDING_MDL0314) || (asset->registryId >= ARM_0380_BUILDING_MDL0329) ||
          ((AiPurchase_FullAssetRecord(asset)->flags & 1) == 0)) {
        continue;
      }
      candidateScore = AiArmyCandidate_ComputeFactionWeightedScore
                         (&g_AiArmyCandidateScoreWeightsVariantA15,factionIndex,AiPurchase_FullAssetRecord(asset));
      if (bestTowerScore < candidateScore) {
        bestTowerScore = candidateScore;
        bestTower = asset;
      }
    }
    if (bestTower != nullptr) {
      AiCandidateWorkspace_AddOrAccumulateWeightedEntry(bestTower->registryId,
                                                        weightRange * AiImproved_DefenceWeightFactor(factionIndex),1);
    }
  }
}

/* Exploration: while there are general sites (workspace 05) and workspace 01 has at most 10 entries, scores
   the eligible army assets of workspace 11 with weight profile B and proposes the best one. Its weight grows
   with the unexplored share of the terrain, (100 - explored %) * coefficient / (32 * (workspace 01 count + 1)),
   halved while there are targets (workspace 07).
*/
void AiArmyCandidate_AddBestExplorationAsset(FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime)

{
  AiCandidateScore32 candidateScore;
  uint32_t weightRange;
  int remainingEntries;
  int bestScore;
  ArmyAssetRecordPrefix *bestArmyAsset = nullptr;
  ArmyAssetRecordPrefix **armyAssetRegistryCursor;
  
  if (((g_AiWorkspace05Count != 0) && (g_AiWorkspace11Count != 0)) && (g_AiWorkspace01Count < AiImproved_ArmyUnitLimit()))
  {
    bestScore = 0;
    if (AiImproved_IsActive()) {
      /* Improved AI: the same scores with variety among the units */
      bestArmyAsset = AiArmyCandidate_SelectAssetWithVariety(&g_AiArmyCandidateScoreWeightsVariantB15,factionIndex,
                                                             &bestScore);
    }
    else {
      remainingEntries = g_AiWorkspace11Count;
      armyAssetRegistryCursor = g_AiWorkspace11ProducibleAssets;
      for (; remainingEntries != 0; remainingEntries--) {
        if ((AiPurchase_FullAssetRecord(*armyAssetRegistryCursor)->flags & 1) != 0) {
          candidateScore = AiArmyCandidate_ComputeFactionWeightedScore
                            (&g_AiArmyCandidateScoreWeightsVariantB15,factionIndex,
                             AiPurchase_FullAssetRecord(*armyAssetRegistryCursor));
          if (bestScore < candidateScore) {
            bestArmyAsset = *armyAssetRegistryCursor;
            bestScore = candidateScore;
          }
        }
        armyAssetRegistryCursor++;
      }
    }
    if (0 < bestScore) {
      weightRange = (uint32_t)(((int64_t)
                             (int)(100 - g_GameFactionRuntimeImage.records[factionIndex].exploredTerrainPercent) *
                            (int64_t)
                            (int)g_AiKnowledgeData->parameters.armyVariantBUnexploredTerrainWeightCoefficient) /
                           (int64_t)(int)((g_AiWorkspace01Count + 1) * 32));
      if (g_AiWorkspace07Count != 0) {
        weightRange = weightRange >> 1;
      }
      AiCandidateWorkspace_AddOrAccumulateWeightedEntry(bestArmyAsset->registryId,weightRange,1);
    }
  }
}

/* Attack: while there are targets (workspace 07) and workspace 01 has at most 10 entries, scores the eligible
   army assets of workspace 11 with weight profile C and proposes the best one with armyVariantCBaseWeight.
   The halving for an empty workspace 07 can never apply (the entry check requires targets); the original
   has the same dead test.
*/
void AiArmyCandidate_AddBestAttackAsset(FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime)

{
  AiCandidateScore32 candidateScore;
  uint32_t weightRange;
  int remainingEntries;
  int bestScore;
  ArmyAssetRecordPrefix *bestArmyAsset = nullptr;
  ArmyAssetRecordPrefix **armyAssetRegistryCursor;
  
  if (((g_AiWorkspace07Count != 0) && (g_AiWorkspace11Count != 0)) && (g_AiWorkspace01Count < AiImproved_ArmyUnitLimit()))
  {
    bestScore = 0;
    if (AiImproved_IsActive()) {
      /* Improved AI: the same scores with variety among the units */
      bestArmyAsset = AiArmyCandidate_SelectAssetWithVariety(&g_AiArmyCandidateScoreWeightsVariantC15,factionIndex,
                                                             &bestScore);
    }
    else {
      remainingEntries = g_AiWorkspace11Count;
      armyAssetRegistryCursor = g_AiWorkspace11ProducibleAssets;
      for (; remainingEntries != 0; remainingEntries--) {
        if ((AiPurchase_FullAssetRecord(*armyAssetRegistryCursor)->flags & 1) != 0) {
          candidateScore = AiArmyCandidate_ComputeFactionWeightedScore
                            (&g_AiArmyCandidateScoreWeightsVariantC15,factionIndex,
                             AiPurchase_FullAssetRecord(*armyAssetRegistryCursor));
          if (bestScore < candidateScore) {
            bestArmyAsset = *armyAssetRegistryCursor;
            bestScore = candidateScore;
          }
        }
        armyAssetRegistryCursor++;
      }
    }
    if (0 < bestScore) {
      weightRange = g_AiKnowledgeData->parameters.armyVariantCBaseWeight;
      if (g_AiWorkspace07Count == 0) {
        weightRange = weightRange >> 1;
      }
      AiCandidateWorkspace_AddOrAccumulateWeightedEntry(bestArmyAsset->registryId,weightRange,1);
    }
  }
}

/* From level 1 on (AiImproved_ExtrasActive): aircraft and laser coils up to the level's number. The best aircraft
   the faction can buy (weight profile C, as the attack units) with 3/4 of the attack weight, so the ground army
   comes first (at 1.5 times it a faction that lost many aircraft spent most of its Xenite replacing them; also at
   most one aircraft every two minutes, AiImproved_PurchaseOverQuota), a laser coil at 1.5 times the defence
   weight, from level 3 the radar Goliaths with the attack weight; when none can be bought yet, the technology
   that opens it. */
void AiArmyCandidate_AddQuotaAssets(FactionRuntimeIndex factionIndex)
{
  const uint32_t aircraftWanted = AiImproved_AircraftWanted(factionIndex);
  const uint32_t laserCoilsWanted = AiImproved_LaserCoilsWanted(factionIndex);
  const uint32_t radarUnitsWanted = AiImproved_RadarUnitsWanted(factionIndex);
  if (g_AiKnowledgeData == nullptr || (aircraftWanted == 0 && laserCoilsWanted == 0 && radarUnitsWanted == 0)) {
    return;
  }
  ArmyAssetRecordPrefix *bestAircraft = nullptr;
  int bestAircraftScore = -1;
  bool laserCoilBuyable = false;
  bool radarUnitBuyable = false;
  ArmyAssetRecordPrefix **cursor = g_AiWorkspace11ProducibleAssets;
  for (uint32_t remaining = g_AiWorkspace11Count; remaining != 0; remaining--, cursor++) {
    ArmyAssetRecordPrefix *asset = *cursor;
    if ((AiPurchase_FullAssetRecord(asset)->flags & 1) == 0) {
      continue;
    }
    const uint32_t id = (uint32_t)asset->registryId;
    if (aircraftWanted != 0 && id >= ARM_0240_UNIT_MDL0143 && id < ARM_0250_UNIT_MDL0150) {
      int score = AiArmyCandidate_ComputeFactionWeightedScore(&g_AiArmyCandidateScoreWeightsVariantC15,factionIndex,
                                                              AiPurchase_FullAssetRecord(asset));
      if (score > bestAircraftScore) {
        bestAircraftScore = score;
        bestAircraft = asset;
      }
    }
    if (laserCoilsWanted != 0 && id == ARM_0345_BUILDING_MDL0315) {
      laserCoilBuyable = true;
    }
    if (radarUnitsWanted != 0 && id == ARM_0104_UNIT_MDL0113) {
      radarUnitBuyable = true;
    }
  }
  const uint32_t quota = (uint32_t)AiImproved_Level() + 1;
  if (bestAircraft != nullptr && g_AiWorkspace01Count < AiImproved_ArmyUnitLimit()) {
    AiCandidateWorkspace_AddOrAccumulateWeightedEntry(bestAircraft->registryId,
                                                      g_AiKnowledgeData->parameters.armyVariantCBaseWeight * 3 / 4,1);
    AiImproved_NoteQuotaCandidate(factionIndex,(uint32_t)bestAircraft->registryId,quota - aircraftWanted,quota);
  }
  if (laserCoilBuyable) {
    AiCandidateWorkspace_AddOrAccumulateWeightedEntry(ARM_0345_BUILDING_MDL0315,
                                                      g_AiKnowledgeData->parameters.armyVariantABaseWeight * 3 / 2,1);
    AiImproved_NoteQuotaCandidate(factionIndex,ARM_0345_BUILDING_MDL0315,quota - laserCoilsWanted,quota);
  }
  if (radarUnitBuyable && g_AiWorkspace01Count < AiImproved_ArmyUnitLimit()) {
    AiCandidateWorkspace_AddOrAccumulateWeightedEntry(ARM_0104_UNIT_MDL0113,
                                                      g_AiKnowledgeData->parameters.armyVariantCBaseWeight,1);
  }
  AiImproved_AddQuotaResearch(factionIndex,aircraftWanted != 0 && bestAircraft == nullptr,
                              laserCoilsWanted != 0 && !laserCoilBuyable,radarUnitsWanted != 0 && !radarUnitBuyable);
}

/* True when technologyIndex is one of the research slots researchTechnologyIds[1..28] of the structure
   definition (slot 0 is not a research slot). */
static bool AiPurchaseCandidate_DefinitionListsResearch(ModelDefinition *definition,RuntimeToken technologyIndex)
{
  int technologySlotIndex;

  for (technologySlotIndex = 28; technologySlotIndex != 0; technologySlotIndex--) {
    if (technologyIndex == definition->researchTechnologyIds[technologySlotIndex]) {
      return true;
    }
  }
  return false;
}

/* Returns false when an idle structure of workspace 00 can carry out a purchase candidate. A
   technology must be available to the faction and listed in the 28 research slots (researchTechnologyIds[1..28])
   of the definition of a structure whose runtimeFlags have none of 0x89. An army asset's producer class mask (the
   asset's flags) must not overlap g_AiPurchaseAppliedArmyClassMask (one purchase per producer class and round);
   bit 0x10 needs a class-11 structure with classState.behaviorState clear, bit 0x08 a class-22 structure with
   classState.classStateAC clear, the bits 0xEE a class-13 structure whose definition mask classParameterC4
   shares them and with classState.behaviorState clear (runtimeFlags without 0xC9 each).
*/
bool AiPurchaseCandidate_HasEligibleProducer(AiCandidateWorkspaceEntry *candidateEntry,FactionRuntimeIndex factionIndex)

{
  uint32_t producerClassMask;
  ModelRuntimeSlot *entitySlot;
  uint32_t remainingEntries;
  RuntimeToken candidateId;
  AiStructureWorkspaceEntry *workspaceEntry;
  ArmyAssetRecordPrefix *armyAsset;

  candidateId = candidateEntry->entityIdAndMultiplicity & AI_CANDIDATE_ID_MASK;
  workspaceEntry = g_AiWorkspace00Structures;
  if ((candidateEntry->weightedScoreAndKind & AI_CANDIDATE_KIND_MASK) == AI_CANDIDATE_KIND_TECHNOLOGY) {
    if (!Technology_IsAvailableForFaction(candidateId,factionIndex)) {
      return true;
    }
    for (remainingEntries = g_AiWorkspace00Count; remainingEntries != 0; remainingEntries--, workspaceEntry++) {
      entitySlot = workspaceEntry->runtimeSlotAddressOrZero;
      if ((entitySlot != nullptr) &&
          (!Any(entitySlot->classState.stateFlags & (ARMY_MODEL_STATE_INACTIVE_MASK | ARMY_MODEL_STATE_RESEARCH_UNPAID))) &&
          AiPurchaseCandidate_DefinitionListsResearch
            (entitySlot->definitionOrSavedId.runtimeDefinition,candidateId)) {
        return false;
      }
    }
    return true;
  }
  /* Original quirk: the lookup status is not checked; the class mask (the asset's flags) is read through
     whatever armyAsset holds (the error code for an unknown id) */
  ArmyAssetRegistry_FindById(candidateId,&armyAsset);
  producerClassMask = armyAsset[1].selectionDetailTemplateVariantIndex;
  if ((g_AiWorkspace00Count == 0) || ((g_AiPurchaseAppliedArmyClassMask & producerClassMask) != 0)) {
    return true;
  }
  if ((producerClassMask & ARMY_ASSET_FLAG_BUILT_BY_CLASS11) != 0) {
    for (remainingEntries = g_AiWorkspace00Count; remainingEntries != 0; remainingEntries--, workspaceEntry++) {
      entitySlot = workspaceEntry->runtimeSlotAddressOrZero;
      if ((entitySlot != nullptr) &&
          (entitySlot->definitionOrSavedId.runtimeDefinition->runtimeClassId == MODEL_RUNTIME_CLASS_11) &&
          (!Any(entitySlot->classState.stateFlags & ARMY_MODEL_STATE_BUILD_BLOCKING_MASK)) && (entitySlot->classState.behaviorState == 0)) {
        return false;
      }
    }
  }
  else if ((producerClassMask & ARMY_ASSET_FLAG_BUILT_AT_AIRCRAFT_PAD) != 0) {
    for (remainingEntries = g_AiWorkspace00Count; remainingEntries != 0; remainingEntries--, workspaceEntry++) {
      entitySlot = workspaceEntry->runtimeSlotAddressOrZero;
      if ((entitySlot != nullptr) &&
          (entitySlot->definitionOrSavedId.runtimeDefinition->runtimeClassId == MODEL_RUNTIME_CLASS_22) &&
          (!Any(entitySlot->classState.stateFlags & ARMY_MODEL_STATE_BUILD_BLOCKING_MASK)) && (entitySlot->classState.classStateAC == 0)) {
        return false;
      }
    }
  }
  else if ((producerClassMask & ARMY_ASSET_FLAGS_BUILT_BY_FACTORY) != 0) {
    for (remainingEntries = g_AiWorkspace00Count; remainingEntries != 0; remainingEntries--, workspaceEntry++) {
      entitySlot = workspaceEntry->runtimeSlotAddressOrZero;
      if ((entitySlot != nullptr) &&
          (entitySlot->definitionOrSavedId.runtimeDefinition->runtimeClassId == MODEL_RUNTIME_CLASS_13) &&
          (!Any(entitySlot->classState.stateFlags & ARMY_MODEL_STATE_BUILD_BLOCKING_MASK)) &&
          ((entitySlot->definitionOrSavedId.runtimeDefinition->classParameterC4 &
            producerClassMask & ARMY_ASSET_FLAGS_BUILT_BY_FACTORY) != 0) &&
          (entitySlot->classState.behaviorState == 0)) {
        return false;
      }
    }
  }
  return true;
}

/* Carries out an affordable purchase candidate. A technology starts researching at the first idle workspace-00
   structure that lists it (the checks of AiPurchaseCandidate_HasEligibleProducer), whose workspace slot is then
   cleared so it takes no second job this round. An army asset is appended once to the faction's pending asset
   list, and its producer class mask is added to g_AiPurchaseAppliedArmyClassMask.
*/
void AiPurchaseCandidate_ApplyToFaction(AiCandidateWorkspaceEntry *candidateEntry,FactionRuntimeIndex factionIndex)

{
  GameEntityRuntime *entity;
  int remainingEntries;
  AiStructureWorkspaceEntry *workspaceEntry;
  RuntimeToken candidateId;
  ArmyAssetRecordPrefix *armyAsset;

  candidateId = candidateEntry->entityIdAndMultiplicity & AI_CANDIDATE_ID_MASK;
  workspaceEntry = g_AiWorkspace00Structures;
  if ((candidateEntry->weightedScoreAndKind & AI_CANDIDATE_KIND_MASK) == AI_CANDIDATE_KIND_TECHNOLOGY) {
    for (remainingEntries = g_AiWorkspace00Count; remainingEntries != 0; remainingEntries--, workspaceEntry++) {
      /* the entity view of the model runtime (runtimeFlags = classState.stateFlags), as Technology_ApplyRecordToEntity
         takes it */
      entity = reinterpret_cast<GameEntityRuntime *>(workspaceEntry->runtimeSlotAddressOrZero.get());
      if ((entity != nullptr) &&
          (!Any((entity->common).runtimeFlags & (ARMY_MODEL_STATE_INACTIVE_MASK | ARMY_MODEL_STATE_RESEARCH_UNPAID))) &&
          AiPurchaseCandidate_DefinitionListsResearch
            (entity->common.ownership.modelDefinition(),candidateId)) {
        Technology_ApplyRecordToEntity(candidateId,entity);
        workspaceEntry->runtimeSlotAddressOrZero = nullptr;
        return;
      }
    }
  }
  else {
    GameFactionRuntime_RegisterArmyAssetPointers(UINT32_MAX,1,candidateId,factionIndex);
    if (ArmyAssetRegistry_FindById(candidateId,&armyAsset) == 0) {
      g_AiPurchaseAppliedArmyClassMask =
           g_AiPurchaseAppliedArmyClassMask | armyAsset[1].selectionDetailTemplateVariantIndex;
    }
  }
}

/* Proposes a resource storage building when the faction's free storage runs low (ARM 331 for Xenite, the
   caller's candidate, ARM 333, for Tritium), plus extra weight per usable resource site.
   Proposes the resource structure candidateArmyAssetId unless one of it is still unassigned. ARM 331 (0x14B):
   with an ARM 330 present and the Xenite storage limit below the knowledge limit, when the free storage
   (limit - current Xenite) is below the gap limit; a full storage quadruples the weight. Any other candidate
   (Tritium): with an ARM 332 present, the same test on the Tritium storage; independently of that the
   first workspace 08 site that still allows an extra special site adds the candidate again with a weight
   3 * derived / (existing count of that site's structure + 3).
*/
void AiStructureCandidate_AddResourceStorage
          (AiCandidateScore32 baseWeight,PckArmyAssetIdCatalog candidateArmyAssetId,
          FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime)

{
  uint32_t storageLimit;
  uint32_t xeniteCurrent;
  AiTerrainFeatureWorkspaceEntry *terrainFeatureEntry;
  int remainingEntries;
  int assignedSiteStructureCount;
  uint32_t derivedWeight;
  bool hasPrerequisite;
  AiKnowledgeDataImage *knowledgeData;

  if (AiPrimaryWorkspace_HasUnassignedEntryById(candidateArmyAssetId)) {
    return;
  }
  knowledgeData = g_AiKnowledgeData;
  if (candidateArmyAssetId == ARM_0331_BUILDING_MDL0308) {
    storageLimit = g_GameFactionRuntimeImage.records[factionIndex].xeniteStorageLimitQ4;
    hasPrerequisite = AiPrimaryWorkspace_HasEntryById(ARM_0330_BUILDING_MDL0303);
    if (hasPrerequisite && (storageLimit != 0) &&
        (storageLimit < knowledgeData->parameters.structure14bPrerequisite14aCountLimit)) {
      xeniteCurrent = g_GameFactionRuntimeImage.records[factionIndex].xeniteCurrentQ4;
      if (storageLimit == xeniteCurrent) {
        baseWeight = baseWeight << 2;
      }
      if (storageLimit - xeniteCurrent < knowledgeData->parameters.structure14bCountGapLimit) {
        AiCandidateWorkspace_AddOrAccumulateWeightedEntry(ARM_0331_BUILDING_MDL0308,baseWeight,1);
      }
    }
    return;
  }
  storageLimit = g_GameFactionRuntimeImage.records[factionIndex].tritiumStorageLimitQ4;
  hasPrerequisite = AiPrimaryWorkspace_HasEntryById(ARM_0332_BUILDING_MDL0302);
  if (hasPrerequisite && (storageLimit != 0) &&
      (storageLimit < knowledgeData->parameters.structure14dPrerequisite14cCountLimit) &&
      (storageLimit - g_GameFactionRuntimeImage.records[factionIndex].tritiumCurrentQ4 <
       knowledgeData->parameters.structure14dCountGapLimit)) {
    AiCandidateWorkspace_AddOrAccumulateWeightedEntry(candidateArmyAssetId,baseWeight,1);
  }
  terrainFeatureEntry = g_AiWorkspace08TerrainFeatureSites;
  for (remainingEntries = g_AiWorkspace08Count; remainingEntries != 0; remainingEntries--, terrainFeatureEntry++) {
    if (!AiPlacement_ReserveAdditionalSpecialSite
           (terrainFeatureEntry->armyAssetId,terrainFeatureEntry->cell,factionIndex,worldRuntime)) {
      derivedWeight = knowledgeData->parameters.workspace08Id14aDerivedWeight;
      if (terrainFeatureEntry->armyAssetId != ARM_0330_BUILDING_MDL0303) {
        derivedWeight = knowledgeData->parameters.workspace08OtherDerivedWeight;
      }
      assignedSiteStructureCount = AiPrimaryWorkspace_CountAssignedEntriesById(terrainFeatureEntry->armyAssetId);
      AiCandidateWorkspace_AddOrAccumulateWeightedEntry
                (candidateArmyAssetId,(derivedWeight * 3) / (assignedSiteStructureCount + 3U),1);
      return;
    }
  }
}

/* Proposes ARM 310 (0x136) once an ARM 330 exists and no ARM 310 is unassigned, when baseline Energy supply
   plus the record's rate tritiumExtractionRateQ4PerTick exceeds the Energy generation capacity.
   Weight: surplus * demand / capacity * resource136DeficitScoreNumerator / resource136DeficitScoreDenominator
   (Q4 values taken as integers, demand at least 1).
*/
void AiResourceCandidate_AddPowerPlant(FactionRuntimeIndex factionIndex)

{
  int energySupply;
  int energySurplus;
  int energyDemand;
  int generationCapacity;
  bool conditionMet;
  
  conditionMet = AiPrimaryWorkspace_HasEntryById(ARM_0330_BUILDING_MDL0303);
  if (conditionMet) {
    conditionMet = AiPrimaryWorkspace_HasUnassignedEntryById(ARM_0310_BUILDING_MDL0305);
    if (!conditionMet) {
      generationCapacity = (int)g_GameFactionRuntimeImage.records[factionIndex].energyGenerationCapacityQ4 >> 4;
      energyDemand = (int)(g_GameFactionRuntimeImage.records[factionIndex].suppliedEnergyDemandQ4 +
                   g_GameFactionRuntimeImage.records[factionIndex].unpoweredEnergyDemandQ4) >> 4;
      if (energyDemand == 0) {
        energyDemand = 1;
      }
      energySupply = ((int)g_GameFactionRuntimeImage.records[factionIndex].baselineEnergySupplyQ4 >> 4) +
              g_GameFactionRuntimeImage.records[factionIndex].tritiumExtractionRateQ4PerTick;
      energySurplus = energySupply - generationCapacity;
      if (energySurplus != 0 && generationCapacity <= energySupply) {
        AiCandidateWorkspace_AddOrAccumulateWeightedEntry
                  (ARM_0310_BUILDING_MDL0305,
                   (uint32_t)(((int64_t)
                              (int)(((int64_t)energySurplus * (int64_t)energyDemand) / (int64_t)generationCapacity) *
                              (int64_t)(int)g_AiKnowledgeData->parameters.resource136DeficitScoreNumerator) /
                             (int64_t)(int)g_AiKnowledgeData->parameters.resource136DeficitScoreDenominator),1);
      }
    }
  }
}

/* Pressure-weighted damage score of one weapon (a linked child definition) for
   AiArmyCandidate_ComputeFactionWeightedScore: for each of the 8 target classes the shot's impact damage times the
   profile weight / reloadTicks, scaled by the faction's AI pressure on that class / its maximum AI pressure, added
   as << 10 / the class maximum (g_TechnologyCategoryMaximums[0..7]). reloadTicks and the maximum pressure must be
   nonzero. */
static int AiArmyCandidate_ScoreWeaponPressureDamage
          (const AiArmyScoreWeights *scoreWeights,FactionRuntimeIndex factionIndex,ShotDefinition *shotDefinition,
          int reloadTicks,int shotsPerReload,int pressureDivisor)
{
  int targetClass;
  uint32_t pressureWeightedDamage;
  int damageScore;
  const bool improved = AiImproved_IsActive();

  damageScore = 0;
  for (targetClass = 0; targetClass < 8; targetClass++) {
    int64_t impactDamage = (int64_t)shotDefinition->targetClassImpactDamageQ12[targetClass];
    if (improved) {
      /* Improved AI: slow shots miss fast units (the fastest of the class this faction has seen) */
      impactDamage = impactDamage * AiImproved_ShotHitFactorQ8(factionIndex,shotDefinition,targetClass) >> 8;
    }
    pressureWeightedDamage =
         (uint32_t)(((int64_t)
                 (int)((impactDamage * (int64_t)shotsPerReload *
                       (int64_t)scoreWeights->pressureCategoryWeights[targetClass]) /
                      (int64_t)reloadTicks) *
                (int64_t)g_GameFactionRuntimeImage.records[factionIndex].aiPressureValues[targetClass]) /
               (int64_t)pressureDivisor);
    damageScore = damageScore +
         (int)((int64_t)((uint64_t)pressureWeightedDamage << 10) /
              (int64_t)(int)g_TechnologyCategoryMaximums[targetClass]);
  }
  return damageScore;
}

/* Scores an army asset for the purchase planner with one weight profile (scoreWeights). The faction's unlocked
   variant of the asset's model definition gives baseScore (+ a bonus when accelerationPerTick is nonzero) plus
   its movementSpeed and maximumHealth values scaled to their maxima, x8; for each of up to two linked child
   definitions (the weapons) with a nonzero divisor reloadTicks (and while the faction has AI pressure), the
   shot's impact damage against each of the 8 target classes is weighted
   by the profile, divided by that divisor, scaled by the faction's AI pressure on that class / maximum pressure
   and added as << 10 / the class maximum. Result: 12 * that + the weighted asset values definitionClassValue70/74/78;
   0 when a definition is not available to the faction.
*/
AiCandidateScore32 AiArmyCandidate_ComputeFactionWeightedScore
          (const AiArmyScoreWeights *scoreWeights,FactionRuntimeIndex factionIndex,
          ArmyAssetRecord *armyAssetRecord)

{
  ModelDefinitionResolveView *selectedModelDefinition;
  ModelDefinitionResolveView *selectedChildModelDefinition0;
  ModelDefinitionResolveView *selectedChildModelDefinition1;
  uint32_t childCount;
  int weightedDefinitionScore;
  AiLinkedDefinitionListView *linkedDefinitionList;

  linkedDefinitionList = Thandor_U32ToPointer<AiLinkedDefinitionListView>(armyAssetRecord->rootNodeOffsetOrPointer); /* 32-bit format field: ArmyAssetRecord.rootNodeOffsetOrPointer */
  /* the pressure of each target class is taken relative to the largest one: in the original the largest of all
     eight classes, for the improved AI the largest of the classes this weight profile scores (the defence profile
     ignores buildings, so a big enemy base does not drown the enemy units it has to counter) */
  int pressureDivisor = (int)g_GameFactionRuntimeImage.records[factionIndex].maximumAiPressure;
  if (AiImproved_IsActive()) {
    pressureDivisor = 0;
    for (int targetClass = 0; targetClass < 8; targetClass++) {
      if ((scoreWeights->pressureCategoryWeights[targetClass] != 0) &&
          (pressureDivisor < g_GameFactionRuntimeImage.records[factionIndex].aiPressureValues[targetClass])) {
        pressureDivisor = g_GameFactionRuntimeImage.records[factionIndex].aiPressureValues[targetClass];
      }
    }
  }
  /* the original tests the selector's status after each of the three selections below, but it is always
     "found"; the selected record (a ModelDefinition) is read through the ModelDefinitionResolveView field view */
  selectedModelDefinition = reinterpret_cast<ModelDefinitionResolveView *>(ModelDefinition_SelectFactionUnlockedLinkedDefinition
                    (factionIndex,reinterpret_cast<uintptr_t>(linkedDefinitionList)));
  weightedDefinitionScore = scoreWeights->baseScore;
  if (selectedModelDefinition->accelerationPerTick != 0) {
    weightedDefinitionScore = weightedDefinitionScore + scoreWeights->nonzeroDefinition18Bonus;
  }
  childCount = linkedDefinitionList->childListCount;
  weightedDefinitionScore =
       ((int)(((int64_t)(int)selectedModelDefinition->movementSpeed *
              (int64_t)scoreWeights->definitionValue0CWeight) /
             (int64_t)(int)g_AiArmyCandidateFlaggedDefinitionValueMaximum) +
       weightedDefinitionScore +
       (int)(((int64_t)(int)selectedModelDefinition->maximumHealth *
             (int64_t)scoreWeights->definitionValue60Weight) /
            (int64_t)
            g_TechnologyCategoryMaximums[selectedModelDefinition->targetClassIndex])) * 8;
  if (childCount != 0) {
    selectedChildModelDefinition0 = reinterpret_cast<ModelDefinitionResolveView *>(ModelDefinition_SelectFactionUnlockedLinkedDefinition
                      (factionIndex,linkedDefinitionList->childList0Address)); /* 32-bit format field: ArmyModelTreeNode.childList*Address */
    if ((selectedChildModelDefinition0->reloadTicks != 0) &&
        (g_GameFactionRuntimeImage.records[factionIndex].maximumAiPressure != 0) && (pressureDivisor != 0)) {
      weightedDefinitionScore = weightedDefinitionScore +
           AiArmyCandidate_ScoreWeaponPressureDamage
             (scoreWeights,factionIndex,selectedChildModelDefinition0->shotDefinitionReference,
              selectedChildModelDefinition0->reloadTicks,
              AiImproved_IsActive() ?
                AiImproved_WeaponShotsPerReload(reinterpret_cast<ModelDefinition *>(selectedChildModelDefinition0)) : 1,
              pressureDivisor);
    }
    if (1 < childCount) {
      selectedChildModelDefinition1 = reinterpret_cast<ModelDefinitionResolveView *>(ModelDefinition_SelectFactionUnlockedLinkedDefinition
                        (factionIndex,linkedDefinitionList->childList1Address)); /* 32-bit format field: ArmyModelTreeNode.childList*Address */
      if ((selectedChildModelDefinition1->reloadTicks != 0) &&
          (g_GameFactionRuntimeImage.records[factionIndex].maximumAiPressure != 0) && (pressureDivisor != 0)) {
        weightedDefinitionScore = weightedDefinitionScore +
             AiArmyCandidate_ScoreWeaponPressureDamage
               (scoreWeights,factionIndex,selectedChildModelDefinition1->shotDefinitionReference,
                selectedChildModelDefinition1->reloadTicks,
                AiImproved_IsActive() ?
                  AiImproved_WeaponShotsPerReload(reinterpret_cast<ModelDefinition *>(selectedChildModelDefinition1)) : 1,
                pressureDivisor);
      }
    }
  }
  return scoreWeights->armyRecord74Weight * armyAssetRecord->definitionClassValue74 +
         weightedDefinitionScore * 12 +
         scoreWeights->armyRecord78Weight * armyAssetRecord->definitionClassValue78 +
         scoreWeights->armyRecord70Weight * armyAssetRecord->definitionClassValue70;
}
