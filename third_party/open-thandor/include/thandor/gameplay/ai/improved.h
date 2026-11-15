/*
 * Open Thandor
 * File: include/thandor/gameplay/ai/improved.h
 * Improved AI for the Thandor remake (not part of the original game).
 */

#ifndef THANDOR_GAMEPLAY_AI_IMPROVED_H
#define THANDOR_GAMEPLAY_AI_IMPROVED_H

#include <stdint.h>
#include <thandor/gameplay/ai/types.h>
#include <thandor/gameplay/army/types.h>

/* Three independent switches, read from ki.ini (next to thandor.ini; see AiImproved_LoadSettings) at the start of
   every session. A networked session always runs the original AI with none of them: every client computes the AI
   itself, and a setting that differs between clients would desynchronise the game.

   1. AI level (ki.ini "stufe", 0..7):
        0, 1  original AI. The mission page's "Einstellung der Computergegner" slider (80..120 % AI extraction) is
              the original's only difficulty; 0 and 1 run the same code, 1 with a 20 minute grace period.
        2..5  improved AI: easy, normal, hard, very hard (4 and up: a second headquarters and a second power plant
              in reserve; 5 and up: at least one heavy factory)
        6     extreme (as 5 with 180 % extraction, alarm radius 26 and 30 % more armour against the player)
        7     hell (twice the extraction, twice the armour against the player, attack groups from 12 units, no unit
              limit but the 64 the AI can track)
   2. AI repairs (ki.ini "reparaturen", default on): fixes of original AI faults that stall the AI (units that never
      attack again, attack groups sent without a route after a rally move, units that keep shooting walls or
      unreachable targets, the pioneer vehicle that waits forever, mines in a foreign base, a purchase dearer than
      the storage that blocks all others, no factory or power plant rebuilt under lasting fire, two reading faults of
      the original). They apply to every AI faction of a single player
      session, also with the original AI (levels 0 and 1).
   3. Grace period (ki.ini "schonfrist_minuten"): for this many minutes of game time the AI sends no attack groups
      against human players and its units leave the players' buildings alone; its defence stays active. What the
      player puts up inside the AI's own base is the exception and may be attacked throughout. It ends early for an
      AI faction once a human player hits one of its buildings, brings an armed unit into its base, or puts up a
      building there (taking the deposit the faction was going to mine).
      -1 (default) = by level: 0 for level 0, 20 for level 1, 60 for levels 2 to 4, 30 for 5 and 6, 10 for 7 (the
      user's plan of 05.11.2026: the harder the level, the sooner the AI comes; before, it grew with the level up to
      120 minutes).
      A savegame without the improved AI's state (the original game's) counts its time from the game's own step
      counter, so a game saved at the start of a mission still gets the full grace period.

   Environment variables override ki.ini (for the test tools): OPEN_THANDOR_AI_LEVEL, OPEN_THANDOR_AI_REPAIRS (0/1),
   OPEN_THANDOR_AI_GRACE_MINUTES, OPEN_THANDOR_AI_LOG (0/1). With level 0 and repairs off the original AI runs bit for
   bit (determinism tests). */
inline constexpr int AI_IMPROVED_LEVEL_ORIGINAL = 0;
inline constexpr int AI_IMPROVED_LEVEL_ORIGINAL_HARD = 1;
inline constexpr int AI_IMPROVED_LEVEL_FIRST_IMPROVED = 2;
inline constexpr int AI_IMPROVED_LEVEL_HELL = 7;
inline constexpr int AI_IMPROVED_LEVEL_MAXIMUM = 7;

/* ArmyRuntimeSlot.aiUnitFlags bit of the improved AI: the unit is retreating to the base to repair. The original
   uses only bit 0 (AI_UNIT_STATE94_GROUP_ASSIGNED). */
inline constexpr uint32_t AI_IMPROVED_UNIT_FLAG_RETREATING = 0x2;

/* Retreat below 40 % of the hierarchy condition, rejoin the attack groups from 75 %. */
inline constexpr int AI_IMPROVED_RETREAT_CONDITION_Q12 = 0x666;  /* 0.40 */
inline constexpr int AI_IMPROVED_REJOIN_CONDITION_Q12 = 0xC00;   /* 0.75 */

/* ---- settings and session ---- */

/* Reads ki.ini (current directory, then the executable directory) and the environment overrides. Called at every
   session start; writes a commented ki.ini with the defaults when none exists. */
void AiImproved_LoadSettings();

/* Session hooks: a new level (state reset) or a loaded savegame (the persistent state comes from the save). Both
   are called once the world is built, before the first simulation step. */
void AiImproved_OnSessionStart(bool loadedFromSave);

/* Once per simulation step (InGameTick_RunSimulationStep): advances the AI game clock by the step's ticks. */
void AiImproved_OnSimulationStep(uint32_t stepTicks);

/* A new army was created in this slot (ArmyRuntime_CreateInstanceFromAsset): forget what the slot's previous
   unit left in the improved AI's unit memory. */
void AiImproved_OnArmyCreated(ArmyRuntimeSlot *armyRuntime);

/* A hostile shot hit targetModelRuntime (ShotRuntime_ApplyArmyHitRelationAndNotifications, pair already hostile,
   after the alarm anchor moved to the hit): a human hit on a building of an AI faction ends that faction's grace
   period; for the improved AI a shooter firing from at least 6 world units becomes the anchor's response point. */
void AiImproved_OnHostileHit(ModelRuntimeSlot *targetModelRuntime,ArmyRuntimeSlot *shooterArmy);

/* Improved AI: where the defenders of an alarm drive (AiUnitCommand_AssignFactionAnchorPoint) instead of the
   anchor: the position of a shooter that hit from afar during the last 30 s. False when there is none. */
bool AiImproved_ResponsePoint(FactionRuntimeIndex factionIndex,Q12 *outWorldX,Q12 *outWorldY);

/* Alarm response radius (AiUnitBehavior_ScoreFactionAnchorPoint): the knowledge data's 8 world units for the
   original AI, 12 / 16 / 20 / 24 / 26 / 28 for levels 2..7. */
uint32_t AiImproved_AnchorResponseBiasQ12(uint32_t originalBiasQ12);

int AiImproved_Level();
void AiImproved_SetLevel(int level);
/* The improved AI (level 2 and up) runs. */
bool AiImproved_IsActive();
/* The AI repairs run (single player). */
bool AiImproved_RepairsActive();
/* Making way in narrow places runs (ki.ini "ausweichen", single player): a standing vehicle of the own or an
   allied faction steps aside for a unit that runs into it. See gameplay/army/evade.h. */
bool AiImproved_EvasionActive();
/* Spreading a group's move target runs (ki.ini "aufstellung", single player): a widely spread selection is no
   longer sent to one and the same point. See gameplay/army/evade.h. */
bool AiImproved_FormationActive();
/* The player's actions are recorded in a file of their own (ki.ini "aufzeichnung", single player). See
   gameplay/session/player_record.h. */
bool AiImproved_RecordActive();
/* Something of this module is switched on (improved AI, repairs or grace period). */
bool AiImproved_AnyActive();

bool AiImproved_IsHumanFaction(int factionIndex);
/* ki.ini "zuschauen = an" (or OPEN_THANDOR_AI_ALL=1), single player only: the computer also plays the local
   player's faction, every faction is computer controlled (AI against AI, to watch or test). */
bool AiImproved_ComputerPlaysLocalFaction();
/* An active faction 1..7 without a player: run by the AI. */
bool AiImproved_IsAiFaction(int factionIndex);

/* AI game clock: game ticks since the start of the level (20 per second at normal speed; saved with the game). */
uint32_t AiImproved_GameTicks();

/* ---- planning hooks ---- */

/* Start of a planning pass of an AI faction, before its workspaces are rebuilt: collects the foreign buildings for
   the territory test of the site scans. */
void AiImproved_BeginFactionPlanning(FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime);

/* After the faction's workspaces were rebuilt: grace period intrusion test. */
void AiImproved_AfterWorkspacesRebuilt(FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime);

/* Repairs: the point lies in the territory of another faction (within the support radius of one of its buildings
   plus a margin), whatever the relation. The AI does not put resource or base buildings there. Uses the list of
   AiImproved_BeginFactionPlanning. */
bool AiImproved_InForeignTerritory(Q12 worldX,Q12 worldY);

/* ---- grace period ---- */

/* The faction's grace period runs: no attack groups against human players. */
bool AiImproved_GraceActive(int factionIndex);

/* ---- unit behaviour (repairs and improved AI) ---- */

/* Repairs, per AI unit and planning pass (AiUnitBehavior_UpdateOwnUnits): position and target progress tracking.
   A unit whose target has not lost 1 % of its condition for 20 s while in reach, or for 30 s while the unit stood
   still out of reach, gives the target up (dropped command; the target is ignored by this unit for 2 minutes and,
   when it was the group target, by the faction's attack groups for 3 minutes). Returns true when the unit lost its
   order and needs nothing else this pass. */
bool AiImproved_TrackUnit(FactionRuntimeIndex factionIndex,ArmyRuntimeSlot *armyRuntime);

/* Repairs: called before the group-assigned test of AiUnitBehavior_CollectUnassignedEntity. A unit stays marked
   as group-assigned for ever in the original (only exploration and alarm orders clear the mark), so once its target
   is destroyed it is never collected for an attack again. Clears the mark when the group target is gone and the
   unit is not fighting, or when the target lives but the unit has had no attack order and stood still for 30 s. */
void AiImproved_ReleaseStaleGroupAssignment(ArmyRuntimeSlot *armyRuntime);

/* Repairs: game ticks since the unit last moved more than one world unit (its movement is noted by this call). */
int32_t AiImproved_UnitTicksStandingStill(ArmyRuntimeSlot *armyRuntime);
/* Repairs: the unit got a new move order; its "standing still" clock starts again. */
void AiImproved_RestartMovementClock(ArmyRuntimeSlot *armyRuntime);

/* Repairs / grace period: adjusts the per-step target score of AiCombatTarget_SelectBestCandidate for a unit of an
   AI faction. 0 rejects the candidate. */
uint32_t AiImproved_AdjustTargetScore(ArmyRuntimeSlot *sourceArmyRuntime,ArmyRuntimeSlot *candidateArmyRuntime,
                                      uint32_t candidateScore);

/* Repairs / grace period / improved AI: true when the per-step target choice of this army needs
   AiImproved_AdjustTargetScore (an AI faction's army). The improved AI also gives unarmed intruders near its base
   (radar, scouts) their full score instead of the original's quarter. */
bool AiImproved_AdjustsTargetsOf(const ArmyRuntimeSlot *sourceArmyRuntime);

/* Repairs / grace period: the group target filter of AiUnitGroup_AssignCollectedEntitiesToBestTarget. */
bool AiImproved_GroupTargetAllowed(FactionRuntimeIndex factionIndex,ModelRuntimeSlot *targetModelRuntime);

/* Repairs: the original places its ordinary buildings (factories, power plants, storage, ARM < 340 other than mines,
   pumps and Tritium storage) only while none of its buildings has been hit for 60 s (primaryAnchorCooldown 0). Under
   lasting fire, e.g. a hostile group shooting from beyond a cliff, it then built only towers, never replaced a lost
   factory or power plant and hoarded its Xenite with finished buildings waiting. BuildDuringAlarm is true once the
   alarm has lasted 90 s without a break; the placement then skips cells within 12 units (|dx| + |dy|) of the last
   hit (AlarmKeepsCellFree). */
bool AiImproved_BuildDuringAlarm(FactionRuntimeIndex factionIndex);
bool AiImproved_AlarmKeepsCellFree(FactionRuntimeIndex factionIndex,Q12 cellWorldX,Q12 cellWorldY);

/* Repairs: path check before a group attack. A dry run of the route planning for the group's ground or water unit
   nearest to the target (aircraft are not checked): false when the route would end farther than weapon range + 4
   units from the target (a base on a plateau, across water); the target's area is then avoided for 10 minutes like
   one a group gave up on, so the next choice skips it. True without repairs. */
bool AiImproved_GroupCanReachTarget(FactionRuntimeIndex factionIndex,ArmyRuntimeSlot **collectedArmies,
                                    uint32_t collectedCount,ModelRuntimeSlot *targetModelRuntime);

/* Repairs: how many of the collected armies have a weapon that damages the target's class (the original sends a
   group also against targets none of its units can hurt, e.g. aircraft without anti-air). */
uint32_t AiImproved_CountGroupUnitsDamaging(ArmyRuntimeSlot **collectedArmies,uint32_t collectedCount,
                                           ModelRuntimeSlot *targetModelRuntime);

/* Repairs: memory of a pioneer vehicle (class 18) for the repaired behaviour in AiUnitBehavior_UpdatePioneerVehicle
   (units.cpp): the site it drives to or waits at, and up to four sites where it waited in vain. */
inline constexpr int AI_IMPROVED_PIONEER_FAILED_SITES = 4;
struct AiImprovedPioneerMemory {
    uint32_t hasSite;
    Q12 siteX;
    Q12 siteY;
    uint32_t waitSinceTick;   /* game tick the pioneer reached the site, 0 = not yet */
    uint32_t reparked;        /* the pioneer was moved off the site once */
    Q12 failedX[AI_IMPROVED_PIONEER_FAILED_SITES];
    Q12 failedY[AI_IMPROVED_PIONEER_FAILED_SITES];
    uint32_t failedUntilTick[AI_IMPROVED_PIONEER_FAILED_SITES];
    /* a faction without any building (Paradis: every faction starts with a pioneer and its headquarters still to
       place): whether the pioneer is on its way to an unexplored place, and where that is */
    uint32_t exploreDirectionPlusOne;
    Q12 exploreX;
    Q12 exploreY;
    uint32_t exploreExhausted;   /* nothing unexplored left in reach: any deposit will do */
};
/* Distances of the repaired pioneer (world units in Q12): it parks this far from the deposit (out of the mine's
   footprint, within its own support radius 3.66), counts as "at the site" within the second, and is moved off the
   site when it stopped closer than the third. */
inline constexpr int AI_IMPROVED_PIONEER_PARK_DISTANCE_Q12 = 2 * 0x1000;
inline constexpr int AI_IMPROVED_PIONEER_AT_SITE_DISTANCE_Q12 = 5 * 0x1000;
inline constexpr int AI_IMPROVED_PIONEER_TOO_CLOSE_Q12 = 0x1A00;   /* 1.6 */
/* Game ticks a pioneer waits at its site for the building that turns it into a pioneer base, and how long a site
   where it waited in vain is avoided. */
inline constexpr uint32_t AI_IMPROVED_PIONEER_WAIT_TICKS = 3 * 60 * 20;
inline constexpr uint32_t AI_IMPROVED_PIONEER_FAILED_SITE_TICKS = 10 * 60 * 20;
/* A pioneer that has a move order but has not moved for this long is stopped and gives its site up. */
inline constexpr uint32_t AI_IMPROVED_PIONEER_BLOCKED_TICKS = 60 * 20;

AiImprovedPioneerMemory *AiImproved_PioneerMemory(ArmyRuntimeSlot *armyRuntime);
void AiImproved_PioneerRememberFailedSite(AiImprovedPioneerMemory *memory,Q12 worldX,Q12 worldY);
bool AiImproved_PioneerSiteFailed(const AiImprovedPioneerMemory *memory,Q12 worldX,Q12 worldY);

/* ---- improved AI (level 2 and up) ---- */

/* Purchase limit: own units (workspace 01) must be below this count (the original tests < 11). */
uint32_t AiImproved_ArmyUnitLimit();

/* Group attack threshold in hierarchy condition units (256 = one intact unit; the original uses 0x200): the
   level's group, raised to a third of the faction's army (at most twice the level's group) so a big army does not
   trickle out four units at a time, and lowered again when the faction has not attacked for three minutes. */
uint32_t AiImproved_GroupAttackStrength(FactionRuntimeIndex factionIndex,uint32_t ownUnitCount);

/* Extraction scale of an AI faction: the game speed times the level's extraction percentage. */
uint32_t AiImproved_ScaleAiExtraction(uint32_t gameSpeedQ8);

/* Shots one firing of a weapon definition launches together: the largest number of shot points (class 2) in one
   node of its model, 1..12. The original purchase score ignores it (one shot per reload). */
int AiImproved_WeaponShotsPerReload(const ModelDefinition *weaponDefinition);

/* Retreat handling of a military unit, called before the busy test of AiUnitBehavior_UpdateOwnUnits.
   Below 40 % it drops its target and drives to the base (first own structure); at the base it gets no new
   orders until 75 %, but still defends itself. Returns true when the unit must not get another order now. */
bool AiImproved_HandleDamagedUnit(FactionRuntimeIndex factionIndex,ArmyRuntimeSlot *armyRuntime);

/* True when the unit may join an attack group (not retreating, condition at least 75 %). */
bool AiImproved_UnitReadyForAttack(ArmyRuntimeSlot *armyRuntime);

/* True while the unit retreats; its own target search is skipped then. */
bool AiImproved_UnitRetreating(const ArmyRuntimeSlot *armyRuntime);

/* Group target of the improved AI: the class base score of each candidate (workspace 07, else the unseen
   hostiles of workspace 03 read with their real stride) reduced by its distance to the group's centre and by the
   armed hostiles standing near it, and multiplied by four for a hostile building within 16 world units of one of
   the faction's own buildings (a mine on its deposit, a tower at its gate: the original left those standing for
   the rest of the game). Returns NULL when nothing scores. */
ModelRuntimeSlot *AiImproved_SelectGroupTarget(FactionRuntimeIndex factionIndex,ArmyRuntimeSlot **collectedArmies,
                                               uint32_t collectedCount,const uint32_t *classBaseScores);

/* Improved AI: share (Q8, 256 = all) of a weapon's damage the purchase score counts against a target class: a
   ballistic or lead-aimed shot of launch speed s hits units of speed v (the fastest of that class the faction has
   had in sight) with s / (s + 3 v); straight-line shots, fixed-range beams and guided missiles, buildings and
   classes it has not seen count fully. So the AI answers fast light units (Moskito, Wiesel, Delfin) with fast
   shots instead of ion weapons. */
uint32_t AiImproved_ShotHitFactorQ8(FactionRuntimeIndex factionIndex,const ShotDefinition *shotDefinition,int targetClass);

/* Repairs and improved AI: bridge rule. A deposit of workspace 08 that the base cannot reach yet, at most 8 world
   units (Manhattan) from the nearest base site and without a hostile in sight near it, gets a chain of at most 3
   cheap storage buildings (ARM 333) toward it, one at a time at the base site nearest to it; then the mine
   candidate builds the mine. The short reach is deliberate: a deposit beyond a mountain slope is not bridged with
   buildings, it is reached with the pioneer vehicle. Each building has to come at least one world unit closer than
   every building so far, so a chain that leads nowhere ends after one wasted building; the deposit is then given
   up for 10 minutes. The chain waits a minute for a closer site before giving up, because the storage building
   just placed has to finish before its support area opens the ground beyond it. Xenite before Tritium. */
void AiImproved_AddBridgeCandidate(FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime);
/* The deposit the planning faction's storage chain grows toward (false: none). */
bool AiImproved_BridgeTarget(FactionRuntimeIndex factionIndex,Q12 *outWorldX,Q12 *outWorldY);
/* A storage building of the chain was placed. */
void AiImproved_OnBridgeStoragePlaced(FactionRuntimeIndex factionIndex,Q12 worldX,Q12 worldY);
/* Manhattan distance from worldX/worldY to the nearest building of the planning faction (INT64_MAX: none); valid
   during its planning pass. A chain step must come at least one world unit closer to the deposit than that. */
int64_t AiImproved_NearestOwnStructureManhattanQ12(Q12 worldX,Q12 worldY);
/* No base site brings the chain closer to its deposit: after a minute of that, the deposit is given up for 10
   minutes. */
void AiImproved_BridgeNoProgress(FactionRuntimeIndex factionIndex);

/* Improved AI: without a standing Xenite mine (all destroyed, or none yet) the candidates on the way to one (mine,
   bridge storage, pioneer vehicle and its research) get four times their weight, so the purchase planner buys them
   before army and research. Called on the fresh candidate list, before the purchase. */
void AiImproved_BoostXeniteRecovery(FactionRuntimeIndex factionIndex);

/* Improved AI from level 3: a reserve Xenite mine. With a mine already standing and none waiting, the AI also buys
   one more (a quarter of the mine weight); the planner places it at once where a deposit is free, so a destroyed
   mine is replaced without the build time. */
void AiImproved_AddReserveMineCandidate(FactionRuntimeIndex factionIndex);

/* Improved AI from level 4: the base keeps a second headquarters and a second power plant in reserve, so losing
   one does not stop the faction (without a headquarters the AI cannot build at all). The power plant comes as soon
   as the first Xenite mine stands, the headquarters (2500 Xenite) once the storage can hold its price; the
   construction planner puts the second headquarters as far from the first and from the enemy as the base allows
   (AiImproved_ReserveHeadquartersCellScore). One of each at a time, and only while the faction has fewer than two. */
void AiImproved_AddReserveBaseCandidates(FactionRuntimeIndex factionIndex);
/* Placement score (lower is better) of a cell for the second headquarters: the original score is the Manhattan
   distance to the headquarters plus up to 16 world units of chance; this one subtracts twice the distance to the
   standing headquarters and once the distance to the nearest threat the faction has seen. False when the rule does
   not apply (no improved AI, no standing headquarters), and the original score stays. */
bool AiImproved_ReserveHeadquartersCellScore(Q12 cellWorldX,Q12 cellWorldY,uint32_t randomBits,int *outScore);

/* Improved AI from level 5: once Arms Factories is researched, a faction without a heavy factory (tank, walker or
   glider factory, ARM 302..304) proposes the one the terrain suits best among those it may build, at three times
   the factory weight and past the planning capacity, so it never fights with light vehicles alone. */
void AiImproved_AddHeavyFactoryCandidate(FactionRuntimeIndex factionIndex,RuntimeToken terrainChoice);

/* From level 1 on: the airport (ARM 306) takes part in the factory choice. The original scores it but drops the
   score (AiStrategicClass_SelectTerrainSuitedBuilding), so it only wins over a negative best score, which never
   happens: the AI never built an airport and never had aircraft. */
bool AiImproved_ScoresAirport();

/* From level 1 on (the user's wish of 06.11.2026, also for the original AI of level 1; level 0 stays the
   original): the AI keeps aircraft and laser coils, 2 of each on level 1 and one more on each level above (8 in
   Hell), and researches more. False in a network game and on level 0. */
bool AiImproved_ExtrasActive();
/* Planning round, after the original candidates: the airport once Arms Factories is researched and another
   factory stands (factory weight); besides the round's best research the cheapest technology an own structure
   offers now (walls excepted), at the research weight x (level + 3) / 8, so below the army: over the game the AI
   researches everything it can. */
void AiImproved_AddLevelExtrasCandidates(FactionRuntimeIndex factionIndex);
/* Aircraft (ARM 240..249, on the map, queued or in production) / laser coils (ARM 345, standing, planned or
   bought) still missing to the level's number; 0 without extras. Laser coils one at a time: 0 while one waits for
   its place. */
uint32_t AiImproved_AircraftWanted(FactionRuntimeIndex factionIndex);
uint32_t AiImproved_LaserCoilsWanted(FactionRuntimeIndex factionIndex);
/* A bought laser coil is placed also in peacetime (AiConstructionPlanner_PlaceExtendedAssetNearFactionAnchor)
   while fewer than the level's number stand. */
bool AiImproved_LaserCoilPlacementWanted(PckArmyAssetIdCatalog armyAssetId);
/* No aircraft or no laser coil can be bought yet: the technology that opens one (the cheapest of Kraehe, Habicht,
   Adler offered by the airport; Laserspule TEC 186), at 1.5 times the research weight. */
void AiImproved_AddQuotaResearch(FactionRuntimeIndex factionIndex,bool needAircraftType,bool needLaserCoil,
                                 bool needRadarUnit);
/* Improved AI from level 3 (the user's wish of 06.11.2026): radar Goliaths (ARM 104) for reconnaissance, 1 on
   levels 3 and 4, 2 on 5 and 6, 3 in Hell; missing ones (on the map, queued or in production) to that number. */
uint32_t AiImproved_RadarUnitsWanted(FactionRuntimeIndex factionIndex);
/* Unit update of a radar Goliath: once the faction has seen a hostile, the unit holds a post 40 % of the way from
   the base toward the nearest one it sees (at most 24 units out) instead of joining attacks; before that it
   explores like any scout (false). True when it took care of the unit. */
bool AiImproved_HandleRadarUnit(FactionRuntimeIndex factionIndex,ArmyRuntimeSlot *armyRuntime);
/* Protocol: an aircraft or laser coil candidate of the quota (logged once per count). */
void AiImproved_NoteQuotaCandidate(FactionRuntimeIndex factionIndex,uint32_t armyAssetId,uint32_t own,uint32_t quota);
/* Unit phase of a planning round, from level 1 on: an airport whose hangar holds parked aircraft and has no launch
   under way sends them all (as the player's pad command does, lane by aircraft type) against the nearest hostile
   building the faction sees, else the nearest hostile unit; not against a human player during the grace period.
   At most one air strike per faction a minute. Without this the AI never launched an aircraft: finished aircraft
   wait in the hangar until a pad command sends them. */
void AiImproved_LaunchAircraft(FactionRuntimeIndex factionIndex);
/* From level 1 on: an aircraft or laser coil candidate past the level's number (counted afresh in the purchase round,
   the candidate list may come from the cache), so the purchase skips it. */
bool AiImproved_PurchaseOverQuota(FactionRuntimeIndex factionIndex,uint32_t entityId,uint32_t kind);
/* Protocol: a purchase was carried out (research, aircraft, laser coils, airport, headquarters). */
void AiImproved_OnPurchase(FactionRuntimeIndex factionIndex,uint32_t entityId,uint32_t kind,uint32_t costQ4);

/* Building placement (single player only, a network game keeps the original's rule), both tested in
   ArmyPlacementCollision_TestBuildingAgainstRuntimeList:
   AiImproved_WallChainFor: a wall (class 23) of the player next to a wall counts both with at most
   ARMY_PLACEMENT_WALL_CHAIN_RADIUS_Q12, so the high wall (radius 0.49) can be built piece to piece like the wall
   and the tank trap; independent of "dicht_bauen".
   AiImproved_TightBuildingFor: ki.ini "dicht_bauen = an" (default) and a building of the player (not under
   "zuschauen", where the computer builds for the local faction): building next to building (walls and towers included, wall next to wall excepted)
   with 3/4 of both radii, so they can touch. The AI's own buildings keep the original's spacing; driving and the
   path grids keep the full radius either way. */
bool AiImproved_WallChainFor(uint32_t ownerFactionIndex);
bool AiImproved_TightBuildingFor(uint32_t ownerFactionIndex);

/* Improved AI levels 6 and 7: armour of the AI factions against the player. A hit by a human player's weapon (in
   "zuschauen" the local faction counts as the player) on an object of an AI faction does 100 / armorPercent of its
   damage: 77 % on level 6 (30 % more armour), 50 % in Hell (double armour). Hits between AI factions and repairs
   are unchanged. Called for every share of a shot's damage (ArmyRuntime_ApplyImpactDamageToRuntimeAndParent). */
int32_t AiImproved_ScaleDamageFromPlayer(FactionRuntimeIndex sourceFactionIndex,ModelRuntimeSlot *targetModelRuntime,
                                         int32_t impactValue);

/* Units of this asset the planning faction has (workspace 01: on the map, queued or in production). */
uint32_t AiImproved_CountOwnUnitsOfAsset(PckArmyAssetIdCatalog armyAssetId);

/* Defence buildings (ARM 340..379) of the planning faction. The improved AI places up to ProactiveTowerLimit of
   them without an attack (5 / 7 / 9 / 11 / 13 / 15 for levels 2..7; the original only places them where an attack
   happens) at the edge of its base facing the nearest hostile building it has seen (else the map centre), and stops
   buying more in peacetime once placed plus pending ones reach the limit + 2 (the spare ones wait for the next
   attack, as in the original). */
uint32_t AiImproved_ProactiveTowerLimit(FactionRuntimeIndex factionIndex);

/* Improved AI: defence mode. When the faction has seen (workspace 02, never the unseen hostiles of workspace 03) a
   hostile army of at least 6 units that beats its own army on numbers or on armour by a factor of two (a swarm of
   cheap light vehicles wins on numbers long before it wins on armour), it reacts for three minutes after that
   sighting: AiImproved_ProactiveTowerLimit grows by 4, defence buildings are bought with
   AiImproved_DefenceWeightFactor times their weight, and no attack group leaves the base, so the collected units
   hold their guard posts instead (the counter the towers carry is the one the purchase scores pick against the
   classes it has seen: machine guns against a swarm of light vehicles, not a rocket ramp). */
bool AiImproved_DefenceMode(FactionRuntimeIndex factionIndex);
uint32_t AiImproved_DefenceWeightFactor(FactionRuntimeIndex factionIndex);
uint32_t AiImproved_CountPlacedDefenceBuildings();
uint32_t AiImproved_CountPendingDefenceBuildings();
/* An own defence building stands within this Manhattan distance of the point. */
bool AiImproved_DefenceBuildingNear(Q12 worldX,Q12 worldY,int64_t radiusQ12);
/* Where the enemy is expected as far as the faction has seen (workspace 02 only, so nothing it never saw): the
   seen hostile building nearest to the point, else the nearest hostile in sight, else the centre of the map. */
bool AiImproved_NearestThreat(Q12 worldX,Q12 worldY,Q12 *outThreatX,Q12 *outThreatY);
/* The planning faction's headquarters (else its first structure). */
bool AiImproved_FactionBaseCentre(Q12 *outWorldX,Q12 *outWorldY);

/* Score of the research of a technology whose definition is neither a unit nor a ground vehicle class; the
   original returns the definition address there. */
uint32_t AiImproved_OtherTechnologyScore(uint32_t baseCandidateScore);

/* Improved AI: the collected idle units were not sent to an attack (too few, too weak, grace period, no target):
   the armed ground units and gliders among them take position on a ring around a guard post at the edge of the
   base facing the threat (AiImproved_NearestThreat: only what the faction has seen), up to 8 world units from the
   headquarters, instead of standing in a clump in the middle of the base. */
void AiImproved_PositionIdleDefenders(ArmyRuntimeSlot **collectedArmies,uint32_t collectedCount);

/* Improved AI: a group attack leaves in waves, light units first. The original sends the whole group at once; a
   large group (up to a third of the army) then starts from its guard posts all in the same moment, and in a
   cramped base or a pass the units block each other (Asgard: 24 units at the gate for 40 minutes).
   AiImproved_SortGroupLightFirst puts the group in order (fastest first, among equally fast ones the lighter
   armour first), so the light units lead and screen the heavy ones that follow. AiImproved_AttackWaveDelay gives
   the delay of the unit at that place in the order: at least AI_IMPROVED_ATTACK_WAVE_SIZE units per wave and at
   most AI_IMPROVED_ATTACK_WAVE_COUNT waves (a large group has larger waves), one wave every
   AI_IMPROVED_ATTACK_WAVE_TICKS; the last wave thus leaves after 10 s, well before a held unit could count as
   stuck (AI_IMPROVED_STUCK_TICKS, 15 s). AiImproved_QueueGroupOrder holds back a unit's attack order for that long;
   false when the queue is full, and the caller sends it at once. The held orders go out in
   AiImproved_OnSimulationStep, unless the unit or the target is gone or the unit got another order meanwhile. */
inline constexpr uint32_t AI_IMPROVED_ATTACK_WAVE_SIZE = 4;
inline constexpr uint32_t AI_IMPROVED_ATTACK_WAVE_COUNT = 5;
inline constexpr uint32_t AI_IMPROVED_ATTACK_WAVE_TICKS = 50; /* 2.5 s */
void AiImproved_SortGroupLightFirst(ArmyRuntimeSlot **collectedArmies,uint32_t collectedCount);
uint32_t AiImproved_AttackWaveSize(uint32_t groupSize);
uint32_t AiImproved_AttackWaveDelay(uint32_t orderIndex,uint32_t groupSize);
bool AiImproved_QueueGroupOrder(ArmyRuntimeSlot *armyRuntime,GameEntityRuntime *targetRuntime,
                                ModelRuntimeSlot *targetModelRuntime,uint32_t delayTicks);

/* A group attack was sent (for the log and the attack bookkeeping; the units' "standing still" clocks restart,
   since they waited in the base until now). */
void AiImproved_OnGroupAttack(FactionRuntimeIndex factionIndex,ArmyRuntimeSlot **collectedArmies,uint32_t unitCount,
                              ModelRuntimeSlot *targetModelRuntime);

/* Improved AI: a deposit that no building of the faction can ever reach (not placeable now and beyond the reach of
   the storage chain) is only opened by the pioneer vehicle, whose base carries its own building area. While such a
   deposit exists and the faction has no pioneer on the map, queued or waiting, the pioneer vehicle and its
   technology are bought at three times their weight, at most once every five minutes so that a deposit nobody can
   reach does not swallow one pioneer after the other. */
void AiImproved_BoostPioneerForFarDeposit(FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime);

/* Squared distance between two world points (Q12), for the placement rules of the construction planner. */
int64_t AiImproved_DistanceSquaredQ12(Q12 ax,Q12 ay,Q12 bx,Q12 by);

/* Diagnostic log of AI decisions (ki.ini "protokoll = an"), written to thandor.log. */
bool AiImproved_LogEnabled();
void AiImproved_Log(const char *format, ...);

/* Protocol of a purchase round (AiPurchasePlanner_ExecuteAffordableCandidates) while the faction's Xenite storage is
   at least 90 % full, at most every 5 minutes per faction: Begin returns true when this round is logged, Note
   records one candidate in sort order (outcome 0 bought, 1 no free producer, 2 not affordable: saves up,
   3 dearer than the storage: skipped, 4 not affordable with full storage: skipped), End writes the line.
   Reads only; the game runs the same with and without it. */
bool AiImproved_PurchaseLogBegin(FactionRuntimeIndex factionIndex);
void AiImproved_PurchaseLogNote(uint32_t weightedScoreAndKind,uint32_t entityIdAndMultiplicity,uint32_t costQ4,
                                int outcome);
void AiImproved_PurchaseLogEnd(FactionRuntimeIndex factionIndex);

#endif /* THANDOR_GAMEPLAY_AI_IMPROVED_H */
