/*
 * Open Thandor
 * File: include/thandor/gameplay/army/evade.h
 * Making way in narrow places (not part of the original game).
 */

#ifndef THANDOR_GAMEPLAY_ARMY_EVADE_H
#define THANDOR_GAMEPLAY_ARMY_EVADE_H

#include <stdint.h>
#include <thandor/gameplay/army/types.h>

/* In the original a unit that runs into another unit simply stops; the unit in its way never reacts. In a narrow
   pass a unit that has arrived, is waiting at a guard post or is itself blocked becomes a permanent obstacle, the
   units behind it pile up and a large army jams.

   This module lets the unit in the way step aside: every vehicle, of the unit's own faction and of an allied one
   (relation 4 and up). An allied unit is only moved while it has no order of its own, so an order of the ally is
   never overridden. Enemy units and buildings are obstacles as before.

   The request is recorded by the movement code (drive_ground.cpp, drive_banking.cpp, walker.cpp) and carried out
   once per simulation step in ArmyEvade_OnSimulationStep, so no route is planned while a unit is being moved.

   This changes the course of the simulation, so it only runs in a single player session and only with
   ki.ini "ausweichen = an" (AiImproved_EvasionActive). A networked session keeps the original behaviour, otherwise
   the clients would drift apart. */

/* Step length: the two clearance radii plus this margin, and twice that on the second attempt. */
inline constexpr int ARMY_EVADE_STEP_MARGIN_Q12 = 0x200;

/* A unit that has stepped aside is left alone for this many game ticks (20 per second), so a jam does not make
   the same unit drive to and fro. */
inline constexpr uint32_t ARMY_EVADE_COOLDOWN_TICKS = 60;

/* Asked again from within this distance (Manhattan) of the spot it last stepped aside from, a unit counts as not
   having got anywhere, and its pause doubles, up to ARMY_EVADE_MAX_BACKOFF_STEPS doublings (3 s ... 48 s). */
inline constexpr int ARMY_EVADE_SAME_SPOT_Q12 = 0x800;
inline constexpr uint32_t ARMY_EVADE_MAX_BACKOFF_STEPS = 5;

/* A unit that is on its way itself is asked only once it has been standing on the same spot for this long while
   others keep running into it (two seconds): that is a real jam, not a moment of passing each other. Its order
   is kept in that case; only its next route point moves to the side, and it carries on afterwards. */
inline constexpr uint32_t ARMY_EVADE_STUCK_TICKS = 40;

/* Longer than this between two bumps and the unit is counted as freshly seen instead of long stuck. */
inline constexpr uint32_t ARMY_EVADE_STUCK_GAP_TICKS = 20;

/* Requests of one simulation step. */
inline constexpr int ARMY_EVADE_REQUEST_CAPACITY = 64;

/* Rings of spots around a group's move target: 6 + 12 + ... + 36 spots around the point itself, enough for a
   whole selection (32 units) even when many spots are blocked. */
inline constexpr int ARMY_FORMATION_RING_COUNT = 6;

/* The movement code ran into blockingModelRuntime on its way to the route point (waypointWorldXQ12,
   waypointWorldYQ12). Notes the request; nothing is moved here. Does nothing when the evasion is switched off,
   when the blocker is not a vehicle that may be asked, or when it already stepped aside recently. */
void ArmyEvade_RequestStepAside
          (ModelRuntimeSlot *blockedModelRuntime,Q12 waypointWorldXQ12,Q12 waypointWorldYQ12,
          ModelRuntimeSlot *blockingModelRuntime,WorldRuntimeContext *worldRuntime);

/* The same for the walkers, whose feet run into things one at a time (walker.cpp): the direction comes from the
   walker's own movement target instead of a route point of the step. */
void ArmyEvade_RequestStepAsideForMovementTarget
          (ModelRuntimeSlot *blockedModelRuntime,ModelRuntimeSlot *blockingModelRuntime,
          WorldRuntimeContext *worldRuntime);

/* Once per simulation step (InGameTick_RunSimulationStep, next to AiImproved_OnSimulationStep): advances the
   module's clock and carries out the noted requests in the order they came in. */
void ArmyEvade_OnSimulationStep(uint32_t stepTicks,WorldRuntimeContext *worldRuntime);

/* A new level or a loaded savegame: forgets the clock and the pending requests. */
void ArmyEvade_OnSessionStart();

/* ---- spreading a group's move target ---- */

/* A selection that stands too widely apart keeps no formation in the original: every unit is sent to the very
   same world point, they arrive one after the other, the first ones take the spot and the rest pile up behind
   them, often still inside the pass they came through.

   With ki.ini "aufstellung = an" every unit of such a selection gets a free spot of its own around the point
   instead: the point itself, then rings of 6, 12, 18 ... spots around it, each at the unit's own width. Spots
   on rock, in lava, in the water or inside a building are skipped, so the group lands on ground it can really
   stand on. Only for a single player session, as with the evasion. */
bool ArmyFormation_Active();

/* The spotNumber-th free spot around (targetWorldXQ12, targetWorldYQ12) for a unit of this definition, counting
   the spots that a unit of its size can stand on. False when even the outermost ring has none; the caller then
   keeps the plain target. */
bool ArmyFormation_GroupSpot
          (int spotNumber,ModelDefinition *modelDefinition,Q12 targetWorldXQ12,Q12 targetWorldYQ12,
          Q12 *outWorldXQ12,Q12 *outWorldYQ12);

/* Sends every self-driving unit of a widely spread selection to a free spot of its own around the target, as
   ArmyFormation_GroupSpot, but light units in front: the units are taken fastest first (lighter first among
   equally fast ones), and each takes the free spot lying furthest along the way from the selection's centre to
   the target. The fast, light units thus stand at the front edge and the heavy ones behind them, and the light
   ones, which arrive first anyway, are no longer in the way of the heavy ones on the last stretch. Returns a
   bit mask of the selection entries it sent (bit i = entries[i]); the caller handles the others as before. */
uint32_t ArmyFormation_SendSelectionLightFirst
          (struct SelectionPointerArray32 *selection,Q12 targetWorldXQ12,Q12 targetWorldYQ12);

#endif /* THANDOR_GAMEPLAY_ARMY_EVADE_H */
