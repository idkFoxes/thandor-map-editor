/*
 * Open Thandor
 * File: src/gameplay/army/evade.cpp
 * Making way in narrow places (not part of the original game); see include/thandor/gameplay/army/evade.h.
 */

#include <thandor/gameplay/army/evade.h>
#include <thandor/gameplay/selection/types.h>
#include <thandor/gameplay/ai/improved.h>
#include <thandor/thandor.h>

/* Module data. */

/* One noted request: the unit that shall step aside and the heading of the unit it is blocking. */
struct ArmyEvadeRequest {
  ArmyRuntimeSlot *blockerArmy;
  ModelRuntimeSlot *blockerModelRuntime; /* the unit's model when the request was noted (see ArmyEvade_StepAside) */
  AngleTurn32 blockedHeading;  /* where the blocked unit wants to go */
  int stepLengthQ12;           /* the two clearance radii plus the margin */
};

static ArmyEvadeRequest s_ArmyEvadeRequests[ARMY_EVADE_REQUEST_CAPACITY] = {};
static int s_ArmyEvadeRequestCount = 0;

/* Game ticks since the start of the level, the same clock the improved AI uses. */
static uint32_t s_ArmyEvadeTicks = 0;

/* What the module remembers about a unit others keep running into. Only units that are actually bumped into are
   written here, so this costs nothing while nothing is jammed. */
struct ArmyEvadeUnitState {
  uint32_t cooldownUntilTick;  /* left alone until then after a step aside */
  uint32_t stuckTicks;         /* how long it has been standing on the same spot while being bumped into */
  uint32_t lastSeenTick;
  Q12 lastWorldX;
  Q12 lastWorldY;
  Q12 evadedFromWorldX;        /* where it stood at its last step aside */
  Q12 evadedFromWorldY;
  uint32_t evadeRepeats;       /* steps aside in a row from (about) that same spot */
};

static ArmyEvadeUnitState s_ArmyEvadeUnitState[ARMY_RUNTIME_SLOT_COUNT] = {};

/* Helpers. */

static int ArmyEvade_SlotIndex(const ArmyRuntimeSlot *armyRuntime)
{
  if (armyRuntime == nullptr || g_ArmyRuntimeSlots == nullptr) {
    return -1;
  }
  ptrdiff_t index = armyRuntime - g_ArmyRuntimeSlots;
  if (index < 0 || index >= ARMY_RUNTIME_SLOT_COUNT) {
    return -1;
  }
  return (int)index;
}

static ArmyMovementRuntime *ArmyEvade_MovementView(ArmyRuntimeSlot *armyRuntime)
{
  return reinterpret_cast<ArmyMovementRuntime *>(armyRuntime); /* the army slot as its movement view, as in ai/units.cpp */
}

/* The two factions are the same or allied (relation state 4 and up, the "same bloc" of the original's
   diplomacy). Faction 0 is the neutral one and never counts. */
static bool ArmyEvade_SameOrAllied(int blockedFactionIndex,int blockerFactionIndex)
{
  if (blockedFactionIndex == blockerFactionIndex) {
    return blockedFactionIndex >= 1 && blockedFactionIndex <= 7;
  }
  if (blockedFactionIndex < 1 || blockedFactionIndex > 7 || blockerFactionIndex < 1 || blockerFactionIndex > 7) {
    return false;
  }
  uint32_t relationState =
       g_GameFactionRuntimeImage.records[blockedFactionIndex].packedRelationStates >>
       ((uint32_t)blockerFactionIndex * 4 & 31u) & 0xfu;
  return relationState >= (uint32_t)FACTION_RELATION_STATE_FRIENDLY;
}

/* The unit stands still: it is not on its way anywhere and no orders are being queued for it. */
static bool ArmyEvade_IsStanding(const ArmyRuntimeSlot *armyRuntime)
{
  return !Any(armyRuntime->movementStateFlags & (ARMY_MOVEMENT_ACTIVE | ARMY_MOVEMENT_LOCKED |
                                                 ARMY_MOVEMENT_WAYPOINTS_QUEUED));
}

/* The unit may be moved at all: not while orders are being queued for it (a unit rolling out of a factory) and
   not while it runs its own special behaviour (the pioneer vehicle looking for a building site). */
static bool ArmyEvade_MayBeMoved(const ArmyRuntimeSlot *armyRuntime)
{
  return !Any(armyRuntime->movementStateFlags & (ARMY_MOVEMENT_LOCKED | ARMY_MOVEMENT_WAYPOINTS_QUEUED |
                                                 ARMY_MOVEMENT_SPECIAL_BEHAVIOR));
}

/* Counts how long a unit others keep running into has been standing on the same spot. Returns its state. */
static ArmyEvadeUnitState *ArmyEvade_TrackStanding(int slotIndex,Q12 worldXQ12,Q12 worldYQ12)
{
  ArmyEvadeUnitState *state = &s_ArmyEvadeUnitState[slotIndex];

  if (state->lastWorldX != worldXQ12 || state->lastWorldY != worldYQ12 ||
      s_ArmyEvadeTicks - state->lastSeenTick > ARMY_EVADE_STUCK_GAP_TICKS) {
    /* it moved, or nobody ran into it for a while: start counting again */
    state->stuckTicks = 0;
    state->lastWorldX = worldXQ12;
    state->lastWorldY = worldYQ12;
  }
  else if (s_ArmyEvadeTicks > state->lastSeenTick) {
    state->stuckTicks += s_ArmyEvadeTicks - state->lastSeenTick;
  }
  state->lastSeenTick = s_ArmyEvadeTicks;
  return state;
}

/* The unit is in the way long enough to be asked: either it is simply standing there, or it is on its way
   itself but has not moved for ARMY_EVADE_STUCK_TICKS, which is a real jam. */
static bool ArmyEvade_IsInTheWay(const ArmyRuntimeSlot *armyRuntime,const ArmyEvadeUnitState *state)
{
  return ArmyEvade_IsStanding(armyRuntime) || state->stuckTicks >= ARMY_EVADE_STUCK_TICKS;
}

/* The unit has an order of its own (a target or a position command). An allied unit with one is left alone. */
static bool ArmyEvade_HasOwnOrder(const ArmyRuntimeSlot *armyRuntime)
{
  return Any(armyRuntime->commandModeFlags & (ARMY_COMMAND_MODE_TARGET_ARMY | ARMY_COMMAND_MODE_TARGET_POSITION));
}

/* The entity view the movement and pathing code works with: the unit's model runtime (ArmyMovementRuntime.entityRuntime
   is +0 of the army slot, the model; its +0 is the model definition, as GridInfluence_* and the route planning
   read it). Not ArmyRuntimeSlot.linkedEntityRuntime: that holds a value copied from the army asset record, 0 or a
   small number for the units, and is no pointer to the unit at all. */
static GameEntityRuntime *ArmyEvade_EntityOf(ArmyRuntimeSlot *armyRuntime)
{
  ModelRuntimeSlot *modelRuntime = (armyRuntime->modelRuntimeOrSavedOffset).modelRuntime;
  return modelRuntime != nullptr ? ModelView_Cast<GameEntityRuntime>(modelRuntime) : nullptr;
}

/* A vehicle that can be asked to make way: it moves under its own power, takes up room and is still alive. */
static bool ArmyEvade_IsMovableVehicle(ArmyRuntimeSlot *armyRuntime,ModelRuntimeSlot *modelRuntime)
{
  ModelDefinition *modelDefinition;

  if (armyRuntime == nullptr || modelRuntime == nullptr) {
    return false;
  }
  if (armyRuntime->modelNodeRuntime == nullptr) {
    return false;
  }
  if (modelRuntime->health == 0) {
    return false;
  }
  modelDefinition = modelRuntime->definitionOrSavedId.runtimeDefinition;
  if (modelDefinition == nullptr) {
    return false;
  }
  /* no acceleration: a building, a wall or a turret; no clearance radius: nothing the pathing knows about */
  return modelDefinition->accelerationPerTick != 0 && modelDefinition->footprintRadius != 0;
}

/* Is the world point free for a unit of this definition? Asks the scratch grid, which carries the map edge, the
   terrain classes and the clearance rings of every model. The asking unit's own rings are lifted by the caller,
   so it does not block its own step. */
static bool ArmyEvade_CanStandAt(ModelDefinition *modelDefinition,Q12 worldXQ12,Q12 worldYQ12)
{
  return !GridScratch_TestProjectedCellMaskBands
              (worldYQ12,worldXQ12,(uint8_t)modelDefinition->footprintRadiusClass,
               (uint8_t)modelDefinition->terrainTraversalClass);
}

/* Carries out one noted request: picks a free spot next to the unit and sends it there.
   The directions, in this order, relative to where the blocked unit wants to go: to the right, to the left, on
   ahead (in a pass only this one is left, and it moves the queue along), then the two forward diagonals. Each is
   tried at one and at two step lengths. */
static void ArmyEvade_StepAside(const ArmyEvadeRequest *request,WorldRuntimeContext *worldRuntime)
{
  static const int32_t s_HeadingOffsets[5] = {
      FIXED_ANGLE16_FULL_TURN / 4,      /* right */
      -FIXED_ANGLE16_FULL_TURN / 4,     /* left */
      0,                                /* on ahead */
      FIXED_ANGLE16_FULL_TURN / 8,      /* forward right */
      -FIXED_ANGLE16_FULL_TURN / 8,     /* forward left */
  };

  ArmyRuntimeSlot *blockerArmy;
  ModelRuntimeSlot *blockerModelRuntime;
  ModelDefinition *blockerDefinition;
  GameEntityRuntime *blockerEntity;
  ModelRuntimeClassId runtimeClassId;
  FixedSinCos stepOffset;
  Q12 standingWorldX;
  Q12 standingWorldY;
  Q12 candidateWorldX;
  Q12 candidateWorldY;
  bool spotFound;
  int directionIndex;
  int attemptIndex;
  int slotIndex;

  blockerArmy = request->blockerArmy;
  slotIndex = ArmyEvade_SlotIndex(blockerArmy);
  if (slotIndex < 0) {
    return;
  }
  /* The request was noted during the last simulation step and the unit may have been destroyed since: its army
     slot is then free (model node NULL, ArmyRuntime_DestroyInstanceAndRefreshUi) and its model pointer leads into
     released memory, so nothing behind it may be read. A slot that was freed and taken again by a new army carries
     another model; that one was never asked. */
  if (blockerArmy->modelNodeRuntime == nullptr) {
    return;
  }
  blockerModelRuntime = (blockerArmy->modelRuntimeOrSavedOffset).modelRuntime;
  if (blockerModelRuntime == nullptr || blockerModelRuntime != request->blockerModelRuntime) {
    return;
  }
  blockerEntity = ArmyEvade_EntityOf(blockerArmy);
  if (!ArmyEvade_IsMovableVehicle(blockerArmy,blockerModelRuntime)) {
    return;
  }
  /* it may have been given an order, or started moving, since the request was noted */
  if (!ArmyEvade_MayBeMoved(blockerArmy) ||
      !ArmyEvade_IsInTheWay(blockerArmy,&s_ArmyEvadeUnitState[slotIndex])) {
    return;
  }
  blockerDefinition = blockerModelRuntime->definitionOrSavedId.runtimeDefinition;
  standingWorldX = (blockerArmy->modelNodeRuntime->worldTransform).translation.x;
  standingWorldY = (blockerArmy->modelNodeRuntime->worldTransform).translation.y;

  /* lift the unit's own clearance rings, as the pathing does, so it does not find itself in the way */
  runtimeClassId = blockerDefinition->runtimeClassId;
  g_ArmyRuntimeOrderHandlerMatrix11Columns24Classes.gridInfluenceRemove[runtimeClassId](blockerEntity);

  spotFound = false;
  candidateWorldX = standingWorldX;
  candidateWorldY = standingWorldY;
  for (directionIndex = 0; !spotFound && directionIndex < 5; directionIndex++) {
    AngleTurn32 heading = (AngleTurn32)((request->blockedHeading + s_HeadingOffsets[directionIndex]) & FIXED_ANGLE16_MASK);
    for (attemptIndex = 1; attemptIndex <= 2; attemptIndex++) {
      stepOffset = FixedMath_SinCosScaled(heading,request->stepLengthQ12 * attemptIndex);
      candidateWorldX = standingWorldX + stepOffset.cosValue;
      candidateWorldY = standingWorldY + stepOffset.sinValue;
      if (ArmyEvade_CanStandAt(blockerDefinition,candidateWorldX,candidateWorldY)) {
        spotFound = true;
        break;
      }
    }
  }

  g_ArmyRuntimeOrderHandlerMatrix11Columns24Classes.gridInfluenceAdd[runtimeClassId](blockerEntity);

  if (!spotFound) {
    /* boxed in on every side: try again after the cooldown */
    return;
  }
  /* A unit that is asked again and again from the same spot does not get out of it (wedged against rock or a
     building, or a route that keeps leading it back): the pause doubles each time, 3, 6, 12, 24 up to 48 s,
     instead of trying every three seconds for the rest of the game. Moving on starts at 3 s again. */
  ArmyEvadeUnitState *unitState = &s_ArmyEvadeUnitState[slotIndex];
  int64_t fromDistance = (int64_t)abs(standingWorldX - unitState->evadedFromWorldX) +
                         (int64_t)abs(standingWorldY - unitState->evadedFromWorldY);
  if (unitState->evadeRepeats != 0 && fromDistance < (int64_t)ARMY_EVADE_SAME_SPOT_Q12) {
    if (unitState->evadeRepeats < ARMY_EVADE_MAX_BACKOFF_STEPS) unitState->evadeRepeats++;
  }
  else {
    unitState->evadeRepeats = 1;
  }
  unitState->evadedFromWorldX = standingWorldX;
  unitState->evadedFromWorldY = standingWorldY;
  unitState->cooldownUntilTick = s_ArmyEvadeTicks + (ARMY_EVADE_COOLDOWN_TICKS << (unitState->evadeRepeats - 1));
  unitState->stuckTicks = 0;
  bool standing = ArmyEvade_IsStanding(blockerArmy);
  if (standing) {
    /* it has nothing to do: a proper move order to the side */
    ArmyRuntime_StartRoutedMoveCommand(candidateWorldY,candidateWorldX,ArmyEvade_MovementView(blockerArmy));
  }
  else {
    /* it is jammed on its own way: only the next route point moves to the side, its order and its final target
       stay untouched, and it carries on from there (ArmyRuntime_UpdateMovementAndWaypoints) */
    ArmyRuntime_SetPendingMoveTarget(candidateWorldY,candidateWorldX,ArmyEvade_MovementView(blockerArmy));
  }
  AiImproved_Log("Ausweichen: Partei %d macht Platz, Einheit %d (ARM %u, KI-Zustand %u) %s von %d/%d nach %d/%d",
                 (int)blockerArmy->factionIndex,slotIndex,(unsigned)blockerArmy->armyAssetId,
                 (unsigned)blockerArmy->aiUnitState,standing ? "stand" : "steckte fest",standingWorldX >> 12,
                 standingWorldY >> 12,candidateWorldX >> 12,candidateWorldY >> 12);
  (void)worldRuntime;
}

/* Public interface. */

void ArmyEvade_RequestStepAside
          (ModelRuntimeSlot *blockedModelRuntime,Q12 waypointWorldXQ12,Q12 waypointWorldYQ12,
          ModelRuntimeSlot *blockingModelRuntime,WorldRuntimeContext *worldRuntime)

{
  ArmyRuntimeSlot *blockedArmy;
  ArmyRuntimeSlot *blockerArmy;
  ModelDefinition *blockedDefinition;
  ModelDefinition *blockerDefinition;
  ArmyEvadeUnitState *blockerState;
  FixedLengthAngle headingAndLength;
  Q12 blockedWorldX;
  Q12 blockedWorldY;
  int requestIndex;
  int slotIndex;

  if (!AiImproved_EvasionActive()) {
    return;
  }
  if (blockedModelRuntime == nullptr || blockingModelRuntime == nullptr) {
    return;
  }
  blockedArmy = blockedModelRuntime->ownerArmyRuntimeOrSavedOffset.armyRuntime;
  blockerArmy = blockingModelRuntime->ownerArmyRuntimeOrSavedOffset.armyRuntime;
  if (blockedArmy == nullptr || blockerArmy == nullptr || blockedArmy == blockerArmy) {
    return;
  }
  if (!ArmyEvade_IsMovableVehicle(blockerArmy,blockingModelRuntime)) {
    return; /* a building, a wall, a turret or a wreck stays where it is */
  }
  if (!ArmyEvade_SameOrAllied((int)blockedArmy->factionIndex,(int)blockerArmy->factionIndex)) {
    return; /* an enemy is an obstacle, as in the original */
  }
  if (!ArmyEvade_MayBeMoved(blockerArmy) || blockerArmy->modelNodeRuntime == nullptr) {
    return; /* a unit rolling out of a factory or a pioneer at work is left alone */
  }
  if (blockedArmy->factionIndex != blockerArmy->factionIndex && ArmyEvade_HasOwnOrder(blockerArmy)) {
    return; /* an ally carrying out an order of its own is never pushed around */
  }
  slotIndex = ArmyEvade_SlotIndex(blockerArmy);
  if (slotIndex < 0) {
    return;
  }
  blockerState = ArmyEvade_TrackStanding(slotIndex,(blockerArmy->modelNodeRuntime->worldTransform).translation.x,
                                         (blockerArmy->modelNodeRuntime->worldTransform).translation.y);
  if (s_ArmyEvadeTicks < blockerState->cooldownUntilTick) {
    return; /* it stepped aside a moment ago */
  }
  if (!ArmyEvade_IsInTheWay(blockerArmy,blockerState)) {
    return; /* it is driving and getting along; it clears the spot by itself */
  }
  if (s_ArmyEvadeRequestCount >= ARMY_EVADE_REQUEST_CAPACITY) {
    return; /* this step's requests are full; the next one brings it up again */
  }
  for (requestIndex = 0; requestIndex < s_ArmyEvadeRequestCount; requestIndex++) {
    if (s_ArmyEvadeRequests[requestIndex].blockerArmy == blockerArmy) {
      return; /* several units are waiting behind it; one step aside is enough */
    }
  }
  blockedDefinition = blockedModelRuntime->definitionOrSavedId.runtimeDefinition;
  blockerDefinition = blockingModelRuntime->definitionOrSavedId.runtimeDefinition;
  if (blockedDefinition == nullptr || blockerDefinition == nullptr || blockedArmy->modelNodeRuntime == nullptr) {
    return;
  }
  /* where the blocked unit wants to go */
  blockedWorldX = (blockedArmy->modelNodeRuntime->worldTransform).translation.x;
  blockedWorldY = (blockedArmy->modelNodeRuntime->worldTransform).translation.y;
  if (waypointWorldXQ12 == blockedWorldX && waypointWorldYQ12 == blockedWorldY) {
    return; /* no direction to make way from */
  }
  headingAndLength = FixedMath_Vector2AngleAndLength(waypointWorldYQ12 - blockedWorldY,
                                                     waypointWorldXQ12 - blockedWorldX);
  s_ArmyEvadeRequests[s_ArmyEvadeRequestCount].blockerArmy = blockerArmy;
  s_ArmyEvadeRequests[s_ArmyEvadeRequestCount].blockerModelRuntime = blockingModelRuntime;
  s_ArmyEvadeRequests[s_ArmyEvadeRequestCount].blockedHeading = headingAndLength.angle;
  s_ArmyEvadeRequests[s_ArmyEvadeRequestCount].stepLengthQ12 =
       (int)blockedDefinition->footprintRadius + (int)blockerDefinition->footprintRadius +
       ARMY_EVADE_STEP_MARGIN_Q12;
  s_ArmyEvadeRequestCount++;
  (void)worldRuntime;
}

void ArmyEvade_RequestStepAsideForMovementTarget
          (ModelRuntimeSlot *blockedModelRuntime,ModelRuntimeSlot *blockingModelRuntime,
          WorldRuntimeContext *worldRuntime)

{
  ArmyRuntimeSlot *blockedArmy;
  ArmyMovementRuntime *blockedMovement;

  if (!AiImproved_EvasionActive() || blockedModelRuntime == nullptr) {
    return;
  }
  blockedArmy = blockedModelRuntime->ownerArmyRuntimeOrSavedOffset.armyRuntime;
  if (blockedArmy == nullptr || !Any(blockedArmy->movementStateFlags & ARMY_MOVEMENT_ACTIVE)) {
    return; /* the walker is not on its way anywhere; nobody has to make room */
  }
  blockedMovement = ArmyEvade_MovementView(blockedArmy);
  ArmyEvade_RequestStepAside(blockedModelRuntime,blockedMovement->movementWorldXQ12,
                             blockedMovement->movementWorldYQ12,blockingModelRuntime,worldRuntime);
}

void ArmyEvade_OnSimulationStep(uint32_t stepTicks,WorldRuntimeContext *worldRuntime)
{
  int requestIndex;
  int pendingCount;

  s_ArmyEvadeTicks += stepTicks;
  pendingCount = s_ArmyEvadeRequestCount;
  s_ArmyEvadeRequestCount = 0; /* the requests carried out below may note new ones for the next step */
  if (!AiImproved_EvasionActive()) {
    return;
  }
  for (requestIndex = 0; requestIndex < pendingCount; requestIndex++) {
    ArmyEvade_StepAside(&s_ArmyEvadeRequests[requestIndex],worldRuntime);
  }
}

/* Spreading a group's move target. */

/* Rings of free spots around a point: number 0 is the point itself, then ring r (r = 1, 2, ...) with 6 * r spots
   evenly around it at r times the spacing. False beyond the outermost ring. */
static bool ArmyFormation_RingSpot
          (int spotIndex,int spacingQ12,Q12 targetWorldXQ12,Q12 targetWorldYQ12,Q12 *outWorldXQ12,
          Q12 *outWorldYQ12)
{
  FixedSinCos ringOffset;
  AngleTurn32 ringAngle;
  int indexInRing;
  int ring;

  if (spotIndex == 0) {
    *outWorldXQ12 = targetWorldXQ12;
    *outWorldYQ12 = targetWorldYQ12;
    return true;
  }
  indexInRing = spotIndex - 1;
  ring = 1;
  while (indexInRing >= 6 * ring) {
    indexInRing -= 6 * ring;
    ring++;
    if (ring > ARMY_FORMATION_RING_COUNT) {
      return false;
    }
  }
  ringAngle = (AngleTurn32)((uint32_t)((int64_t)indexInRing * FIXED_ANGLE16_FULL_TURN / (6 * ring)) &
                            FIXED_ANGLE16_MASK);
  ringOffset = FixedMath_SinCosScaled(ringAngle,ring * spacingQ12);
  *outWorldXQ12 = targetWorldXQ12 + ringOffset.cosValue;
  *outWorldYQ12 = targetWorldYQ12 + ringOffset.sinValue;
  return true;
}

bool ArmyFormation_Active()
{
  return AiImproved_FormationActive();
}

bool ArmyFormation_GroupSpot
          (int spotNumber,ModelDefinition *modelDefinition,Q12 targetWorldXQ12,Q12 targetWorldYQ12,
          Q12 *outWorldXQ12,Q12 *outWorldYQ12)

{
  Q12 candidateWorldX;
  Q12 candidateWorldY;
  int spacingQ12;
  int freeSpotsPassed;
  int spotIndex;

  if (modelDefinition == nullptr || spotNumber < 0) {
    return false;
  }
  /* the unit's own width plus a little air, so neighbours do not stand in each other's clearance ring */
  spacingQ12 = (int)modelDefinition->footprintRadius * 2 + ARMY_EVADE_STEP_MARGIN_Q12;
  freeSpotsPassed = 0;
  for (spotIndex = 0; ; spotIndex++) {
    if (!ArmyFormation_RingSpot(spotIndex,spacingQ12,targetWorldXQ12,targetWorldYQ12,&candidateWorldX,
                                &candidateWorldY)) {
      return false;
    }
    if (!ArmyEvade_CanStandAt(modelDefinition,candidateWorldX,candidateWorldY)) {
      continue; /* rock, lava, water or a building */
    }
    if (freeSpotsPassed == spotNumber) {
      *outWorldXQ12 = candidateWorldX;
      *outWorldYQ12 = candidateWorldY;
      return true;
    }
    freeSpotsPassed++;
  }
}

/* Light first: faster first, among equally fast ones the one with less armour first. */
static bool ArmyFormation_LighterThan(const ModelDefinition *a,const ModelDefinition *b)
{
  if (a->movementSpeed != b->movementSpeed) {
    return a->movementSpeed > b->movementSpeed;
  }
  return a->maximumHealth < b->maximumHealth;
}

uint32_t ArmyFormation_SendSelectionLightFirst
          (SelectionPointerArray32 *selection,Q12 targetWorldXQ12,Q12 targetWorldYQ12)

{
  /* how many free spots past the ones still needed a unit may choose from: enough to reach the front of the next
     ring, few enough that the group stays together around the point */
  static constexpr int FORMATION_SPOT_CHOICE_EXTRA = 6;

  ArmyMovementRuntime *movement[SELECTION_ENTRY_CAPACITY];
  ModelDefinition *definition[SELECTION_ENTRY_CAPACITY];
  int order[SELECTION_ENTRY_CAPACITY];
  Q12 usedX[SELECTION_ENTRY_CAPACITY];
  Q12 usedY[SELECTION_ENTRY_CAPACITY];
  int usedRadius[SELECTION_ENTRY_CAPACITY];
  int usedCount = 0;
  int unitCount = 0;
  int64_t sumX = 0;
  int64_t sumY = 0;
  uint32_t sentMask = 0;

  if (selection == nullptr) {
    return 0;
  }
  for (int entryIndex = 0; entryIndex < SELECTION_ENTRY_CAPACITY; entryIndex++) {
    GameEntityRuntime *entry = selection->entries[entryIndex];
    if (entry == nullptr) {
      continue;
    }
    /* the selection holds army slots (their ArmyMovementRuntime view, as in SelectionPointerArray_ApplyMoveCommand);
       the definition is reached through the model, as the original does there */
    ModelDefinition *entryDefinition = THANDOR_PTR32_AT(ModelDefinition,(entry->common).ownership.definitionOrClassRecord);
    ArmyMovementRuntime *entryMovement = static_cast<ArmyMovementRuntime *>(selection->entries[entryIndex]);
    if (entryDefinition == nullptr || entryDefinition->accelerationPerTick == 0 ||
        entryMovement->modelNodeRuntime == nullptr) {
      continue;
    }
    movement[entryIndex] = entryMovement;
    definition[entryIndex] = entryDefinition;
    sumX += (entryMovement->modelNodeRuntime->worldTransform).translation.x;
    sumY += (entryMovement->modelNodeRuntime->worldTransform).translation.y;
    /* insertion by weight class: lighter units further ahead in the order */
    int position = unitCount;
    while (position > 0 && ArmyFormation_LighterThan(entryDefinition,definition[order[position - 1]])) {
      order[position] = order[position - 1];
      position--;
    }
    order[position] = entryIndex;
    unitCount++;
  }
  if (unitCount == 0) {
    return 0;
  }
  /* the way the group travels: from its centre to the target */
  int64_t wayX = (int64_t)targetWorldXQ12 - sumX / unitCount;
  int64_t wayY = (int64_t)targetWorldYQ12 - sumY / unitCount;
  bool haveWay = (wayX < 0 ? -wayX : wayX) + (wayY < 0 ? -wayY : wayY) >= Q12_ONE;

  for (int orderIndex = 0; orderIndex < unitCount; orderIndex++) {
    int entryIndex = order[orderIndex];
    ModelDefinition *unitDefinition = definition[entryIndex];
    int choices = unitCount - orderIndex + FORMATION_SPOT_CHOICE_EXTRA;
    bool found = false;
    int64_t bestScore = 0;
    Q12 bestX = targetWorldXQ12;
    Q12 bestY = targetWorldYQ12;

    for (int spotNumber = 0; choices > 0; spotNumber++) {
      Q12 spotX;
      Q12 spotY;
      if (!ArmyFormation_GroupSpot(spotNumber,unitDefinition,targetWorldXQ12,targetWorldYQ12,&spotX,&spotY)) {
        break; /* past the outermost ring */
      }
      /* a spot another unit of this order already took (the spots of differently sized units differ a little) */
      bool taken = false;
      for (int usedIndex = 0; usedIndex < usedCount && !taken; usedIndex++) {
        int64_t dx = (int64_t)spotX - usedX[usedIndex];
        int64_t dy = (int64_t)spotY - usedY[usedIndex];
        int64_t minimum = (int64_t)unitDefinition->footprintRadius + usedRadius[usedIndex];
        taken = dx * dx + dy * dy < minimum * minimum;
      }
      if (taken) {
        continue;
      }
      choices--;
      /* further along the way scores higher; without a way (the group stands on the target) the inner spots */
      int64_t score = haveWay ? (((int64_t)spotX - targetWorldXQ12) * wayX + ((int64_t)spotY - targetWorldYQ12) * wayY) >> 12
                              : -(int64_t)spotNumber;
      if (!found || score > bestScore) {
        found = true;
        bestScore = score;
        bestX = spotX;
        bestY = spotY;
      }
    }
    if (!found) {
      continue; /* no free spot: the caller sends it to the plain target */
    }
    usedX[usedCount] = bestX;
    usedY[usedCount] = bestY;
    usedRadius[usedCount] = (int)unitDefinition->footprintRadius;
    usedCount++;
    ArmyRuntime_StartRoutedMoveCommand(bestY,bestX,movement[entryIndex]);
    sentMask |= 1u << entryIndex;
  }
  return sentMask;
}

void ArmyEvade_OnSessionStart()
{
  s_ArmyEvadeTicks = 0;
  s_ArmyEvadeRequestCount = 0;
  memset(s_ArmyEvadeUnitState,0,sizeof s_ArmyEvadeUnitState);
}
