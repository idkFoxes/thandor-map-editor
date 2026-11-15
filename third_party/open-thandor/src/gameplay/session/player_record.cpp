/*
 * Open Thandor
 * File: src/gameplay/session/player_record.cpp
 * Record of the player's own actions (not part of the original game); see
 * include/thandor/gameplay/session/player_record.h.
 */

#include <thandor/gameplay/session/player_record.h>
#include <thandor/gameplay/ai/improved.h>
#include <thandor/thandor.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* Module data. */

static constexpr uint32_t PLAYER_RECORD_TICKS_PER_SECOND = 20;
static constexpr uint32_t PLAYER_RECORD_STATUS_TICKS = 5 * 60 * PLAYER_RECORD_TICKS_PER_SECOND;

static uint32_t s_PlayerRecordLastStatusTick = 0;
static uint32_t s_PlayerRecordPendingPlacementAsset = 0; /* the building taken for placement, kept by Before */
#ifdef THANDOR_DEV_TOOLS
static uint32_t s_PlayerRecordSessionStartTick = 0; /* test aid OPEN_THANDOR_TEST_PLAYER_MOVE */
static bool s_PlayerRecordTestDone = false;
static bool s_PlayerRecordPlacementTestDone = false; /* test aid OPEN_THANDOR_TEST_PLACEMENT */
#endif

/* Helpers. */

static bool PlayerRecord_Active()
{
  return AiImproved_RecordActive();
}

/* The game clock: the improved AI's (the same "KI mm:ss" as in the log) while it runs, else the game's own. */
static uint32_t PlayerRecord_Ticks()
{
  return AiImproved_AnyActive() ? AiImproved_GameTicks() : g_GameFactionRuntimeImage.tail.simulationTick;
}

static SelectionPlayerRuntimeBlock *PlayerRecord_PlayerBlock()
{
  if (g_LocalPlayerRuntimeId >= 256) {
    return nullptr;
  }
  return g_SelectionPlayerRuntimeBlockPointers[g_LocalPlayerRuntimeId];
}

/* A line of the record: "mm:ss text". */
static void PlayerRecord_Line(const char *format,...)
{
  char text[640];
  va_list args;
  uint32_t seconds = PlayerRecord_Ticks() / PLAYER_RECORD_TICKS_PER_SECOND;

  va_start(args,format);
  vsnprintf(text,sizeof text,format,args);
  va_end(args);
  Thandor_LogPlayer("%u:%02u %s",seconds / 60,seconds % 60,text);
}

/* The model's name from the game texts, plain ASCII (umlauts written out), at most capacity - 1 characters. */
static void PlayerRecord_ModelName(const ModelDefinition *definition,char *out,size_t capacity)
{
  size_t length = 0;
  const uint16_t *text;

  out[0] = '\0';
  if (definition == nullptr || capacity < 2) {
    return;
  }
  text = TextResource_Resolve(definition->nameTextIndex + TEXT_ID_MODEL_NAME_BASE);
  if (text == nullptr) {
    return;
  }
  for (int i = 0; i < 48 && text[i] != 0 && length + 3 < capacity; i++) {
    uint16_t c = text[i];
    const char *replacement = nullptr;
    switch (c) {
    case 0xE4: replacement = "ae"; break;
    case 0xF6: replacement = "oe"; break;
    case 0xFC: replacement = "ue"; break;
    case 0xC4: replacement = "Ae"; break;
    case 0xD6: replacement = "Oe"; break;
    case 0xDC: replacement = "Ue"; break;
    case 0xDF: replacement = "ss"; break;
    default: break;
    }
    if (replacement != nullptr) {
      out[length++] = replacement[0];
      out[length++] = replacement[1];
    }
    else if (c >= 0x20 && c < 0x7F) {
      out[length++] = (char)c;
    }
  }
  while (length > 0 && out[length - 1] == ' ') {
    length--;
  }
  out[length] = '\0';
}

static ModelDefinition *PlayerRecord_ArmyDefinition(const ArmyRuntimeSlot *armyRuntime)
{
  ModelRuntimeSlot *modelRuntime;

  if (armyRuntime == nullptr || armyRuntime->modelNodeRuntime == nullptr) {
    return nullptr;
  }
  modelRuntime = (armyRuntime->modelRuntimeOrSavedOffset).modelRuntime;
  return modelRuntime != nullptr ? modelRuntime->definitionOrSavedId.runtimeDefinition : nullptr;
}

/* "12 Einheiten bei 30/-14: 5x Goliath (ARM 205), 4x ..." for the player's selection. Also gives the first
   unit that drives itself (to read where it is going) and the number of units. */
static int PlayerRecord_DescribeSelection(char *out,size_t capacity,ArmyRuntimeSlot **outFirstMobile)
{
  struct Kind {
    uint32_t armyAssetId;
    const ModelDefinition *definition;
    int count;
  };
  Kind kinds[SELECTION_ENTRY_CAPACITY];
  int kindCount = 0;
  int unitCount = 0;
  int64_t sumX = 0;
  int64_t sumY = 0;
  SelectionPlayerRuntimeBlock *block = PlayerRecord_PlayerBlock();

  out[0] = '\0';
  if (outFirstMobile != nullptr) {
    *outFirstMobile = nullptr;
  }
  if (block == nullptr) {
    return 0;
  }
  for (int entryIndex = 0; entryIndex < SELECTION_ENTRY_CAPACITY; entryIndex++) {
    GameEntityRuntime *entry = block->selection.entries[entryIndex];
    if (entry == nullptr) {
      continue;
    }
    ArmyRuntimeSlot *army = reinterpret_cast<ArmyRuntimeSlot *>(entry); /* the selection holds army slots */
    const ModelDefinition *definition = PlayerRecord_ArmyDefinition(army);
    if (definition == nullptr) {
      continue;
    }
    unitCount++;
    sumX += (army->modelNodeRuntime->worldTransform).translation.x;
    sumY += (army->modelNodeRuntime->worldTransform).translation.y;
    if (outFirstMobile != nullptr && *outFirstMobile == nullptr && definition->accelerationPerTick != 0) {
      *outFirstMobile = army;
    }
    int kindIndex = 0;
    while (kindIndex < kindCount && kinds[kindIndex].armyAssetId != (uint32_t)army->armyAssetId) {
      kindIndex++;
    }
    if (kindIndex == kindCount) {
      kinds[kindCount].armyAssetId = (uint32_t)army->armyAssetId;
      kinds[kindCount].definition = definition;
      kinds[kindCount].count = 0;
      kindCount++;
    }
    kinds[kindIndex].count++;
  }
  if (unitCount == 0) {
    snprintf(out,capacity,"keine Auswahl");
    return 0;
  }
  size_t length = (size_t)snprintf(out,capacity,"%d %s bei %d/%d:",unitCount,unitCount == 1 ? "Einheit" : "Einheiten",
                                   (int)(sumX / unitCount / Q12_ONE),(int)(sumY / unitCount / Q12_ONE));
  for (int kindIndex = 0; kindIndex < kindCount && length + 48 < capacity; kindIndex++) {
    char name[48];
    PlayerRecord_ModelName(kinds[kindIndex].definition,name,sizeof name);
    length += (size_t)snprintf(out + length,capacity - length,"%s %dx %s (ARM %u)",kindIndex == 0 ? "" : ",",
                               kinds[kindIndex].count,name[0] != '\0' ? name : "?",kinds[kindIndex].armyAssetId);
  }
  return unitCount;
}

/* An army token of a command (offset from g_ArmyRuntimeRebaseBaseMinusOne) as the army, or NULL. */
static ArmyRuntimeSlot *PlayerRecord_ArmyOfToken(uint32_t token)
{
  if (token == 0 || g_ArmyRuntimeRebaseBaseMinusOne == nullptr) {
    return nullptr;
  }
  ArmyRuntimeSlot *army = reinterpret_cast<ArmyRuntimeSlot *>(reinterpret_cast<uintptr_t>(g_ArmyRuntimeRebaseBaseMinusOne) +
                                                              token);
  if (g_ArmyRuntimeSlots == nullptr || army < g_ArmyRuntimeSlots || army >= g_ArmyRuntimeSlots + ARMY_RUNTIME_SLOT_COUNT ||
      army->modelNodeRuntime == nullptr) {
    return nullptr;
  }
  return army;
}

/* Where the selection's first self-driving unit is going now: the end of its route, as the AI log shows it
   ("Weg endet bei"), in the coordinates of the log. */
static bool PlayerRecord_RouteEnd(ArmyRuntimeSlot *army,int *outX,int *outY)
{
  if (army == nullptr || army->modelNodeRuntime == nullptr) {
    return false;
  }
  ArmyMovementRuntime *movement = reinterpret_cast<ArmyMovementRuntime *>(army); /* the army slot as its movement view */
  *outX = movement->movementTargetWorldXQ12 / Q12_ONE;
  *outY = movement->movementTargetWorldYQ12 / Q12_ONE;
  return true;
}

/* Public interface. */

void PlayerRecord_BeforeLocalCommand(uint32_t code,uint32_t payload1,uint32_t payload2,uint32_t payload3)
{
  (void)payload1;
  (void)payload2;
  (void)payload3;
  if (!PlayerRecord_Active()) {
    return;
  }
  if (code == (uint32_t)INGAME_COMMAND_PLACE_ARMY) {
    /* the handler takes the chosen building out of the player's pending slot */
    SelectionPlayerRuntimeBlock *block = PlayerRecord_PlayerBlock();
    s_PlayerRecordPendingPlacementAsset = 0;
    if (block != nullptr && block->pendingPlacementArmyAsset != 0) {
      s_PlayerRecordPendingPlacementAsset =
           (uint32_t)reinterpret_cast<ArmyAssetRecordPrefix *>((uintptr_t)block->pendingPlacementArmyAsset)->registryId;
    }
  }
}

void PlayerRecord_AfterLocalCommand(uint32_t code,uint32_t payload1,uint32_t payload2,uint32_t payload3)
{
  char selection[480];
  ArmyRuntimeSlot *firstMobile;
  int routeX;
  int routeY;

  if (!PlayerRecord_Active()) {
    return;
  }
  switch (code) {
  case INGAME_COMMAND_MOVE:
  case INGAME_COMMAND_POSITION:
    PlayerRecord_DescribeSelection(selection,sizeof selection,&firstMobile);
    if (PlayerRecord_RouteEnd(firstMobile,&routeX,&routeY)) {
      PlayerRecord_Line("%s %s, Ziel %d/%d",code == INGAME_COMMAND_MOVE ? "Fahren" : "Wegpunkt",selection,routeX,routeY);
    }
    else {
      PlayerRecord_Line("%s %s",code == INGAME_COMMAND_MOVE ? "Fahren" : "Wegpunkt",selection);
    }
    break;
  case INGAME_COMMAND_SELECT_ARMY: {
    ArmyRuntimeSlot *target = PlayerRecord_ArmyOfToken(payload3);
    PlayerRecord_DescribeSelection(selection,sizeof selection,nullptr);
    if (target != nullptr) {
      char name[48];
      PlayerRecord_ModelName(PlayerRecord_ArmyDefinition(target),name,sizeof name);
      PlayerRecord_Line("Angriff %s, auf %s (ARM %u) von Partei %d bei %d/%d",selection,name[0] != '\0' ? name : "?",
                        (unsigned)target->armyAssetId,(int)target->factionIndex,
                        (target->modelNodeRuntime->worldTransform).translation.x / Q12_ONE,
                        (target->modelNodeRuntime->worldTransform).translation.y / Q12_ONE);
    }
    break;
  }
  case INGAME_COMMAND_TARGET_POSITION:
    PlayerRecord_DescribeSelection(selection,sizeof selection,nullptr);
    PlayerRecord_Line("Feuer auf Punkt %s, Punkt %d/%d",selection,(int32_t)payload3 / Q12_ONE,(int32_t)payload2 / Q12_ONE);
    break;
  case 0xE10:
    PlayerRecord_DescribeSelection(selection,sizeof selection,nullptr);
    PlayerRecord_Line("Halt (S) %s",selection);
    break;
  case 0xE30:
    PlayerRecord_DescribeSelection(selection,sizeof selection,nullptr);
    PlayerRecord_Line("Stehen bleiben (Shift+S) %s",selection);
    break;
  case 0xE50:
    PlayerRecord_DescribeSelection(selection,sizeof selection,nullptr);
    PlayerRecord_Line("Ziele abbrechen (Alt+S) %s",selection);
    break;
  case 0xE70:
    PlayerRecord_DescribeSelection(selection,sizeof selection,nullptr);
    PlayerRecord_Line("Selbstzerstoerung (Alt+D) %s",selection);
    break;
  case INGAME_COMMAND_QUEUE_ARMY:
    PlayerRecord_Line("Kauft %ux ARM %u",payload1,payload2);
    break;
  case INGAME_COMMAND_CANCEL_QUEUED_ARMY:
    PlayerRecord_Line("Storniert %ux ARM %u",payload1,payload2);
    break;
  case INGAME_COMMAND_TAKE_ARMY_FOR_PLACEMENT:
    PlayerRecord_Line("Nimmt ARM %u zum Aufstellen",payload2);
    break;
  case INGAME_COMMAND_PLACE_ARMY: {
    SelectionPlayerRuntimeBlock *block = PlayerRecord_PlayerBlock();
    /* the handler clears the pending slot when the placement succeeded and leaves it when it was rejected */
    bool placed = block != nullptr && block->pendingPlacementArmyAsset == 0;
    PlayerRecord_Line("%s ARM %u bei %d/%d (Ausrichtung %u)",placed ? "Stellt auf" : "Aufstellen abgelehnt:",
                      s_PlayerRecordPendingPlacementAsset,(int32_t)payload3 / Q12_ONE,(int32_t)payload2 / Q12_ONE,
                      payload1 & 0xFFFF);
    break;
  }
  case INGAME_COMMAND_SELL_ARMY:
    PlayerRecord_Line("Verkauft ARM %u",payload2);
    break;
  case INGAME_COMMAND_CLOSE_TECHNOLOGY_PAGE:
    if ((int32_t)payload2 > 0) {
      PlayerRecord_Line("Forschung gestartet: Technologie %d",(int32_t)payload2);
    }
    break;
  case INGAME_COMMAND_ADVANCE_RELATION:
    PlayerRecord_Line("Diplomatie: Beziehung Partei %d zu Partei %d einen Schritt weiter",(int)payload2,(int)payload3);
    break;
  case INGAME_COMMAND_RESET_RELATION:
    PlayerRecord_Line("Diplomatie: Beziehung Partei %d zu Partei %d zurueckgesetzt",(int)payload2,(int)payload3);
    break;
  case 0x370:
    PlayerRecord_Line("Pause umgeschaltet");
    break;
  case 0x3F0:
    PlayerRecord_Line("Spieltempo %+d (jetzt %u Schritte je Takt)",(int32_t)payload3,(unsigned)g_InGameSimulationStepTicks);
    break;
  default:
    break;
  }
}

void PlayerRecord_OnSessionStart(bool loadedFromSave)
{
  s_PlayerRecordLastStatusTick = PlayerRecord_Ticks();
  s_PlayerRecordPendingPlacementAsset = 0;
#ifdef THANDOR_DEV_TOOLS
  s_PlayerRecordSessionStartTick = PlayerRecord_Ticks();
  s_PlayerRecordTestDone = false;
  s_PlayerRecordPlacementTestDone = false;
#endif
  if (!PlayerRecord_Active()) {
    return;
  }
  SelectionPlayerRuntimeBlock *block = PlayerRecord_PlayerBlock();
  PlayerRecord_Line("---- %s, du spielst Partei %d ----",loadedFromSave ? "Spielstand geladen" : "neues Spiel",
                    block != nullptr ? (int)block->factionIndex : -1);
}

#ifdef THANDOR_DEV_TOOLS
/* Test aid (developer build only): OPEN_THANDOR_TEST_PLAYER_MOVE=x,y. 20 s after the start every self-driving unit of
   the local player is put into the selection and sent to (x, y) with the player's own move command, as a click
   would; then each unit's speed, armour and route end are logged, so the recorder and the light-first formation
   can be checked without a mouse. */
static void PlayerRecord_TestPlayerMove(uint32_t now)
{
  const char *value = getenv("OPEN_THANDOR_TEST_PLAYER_MOVE");
  int targetX;
  int targetY;
  if (s_PlayerRecordTestDone || value == nullptr || sscanf(value,"%d,%d",&targetX,&targetY) != 2 ||
      now - s_PlayerRecordSessionStartTick < 20 * PLAYER_RECORD_TICKS_PER_SECOND) {
    return;
  }
  s_PlayerRecordTestDone = true;
  SelectionPlayerRuntimeBlock *block = PlayerRecord_PlayerBlock();
  if (block == nullptr || g_ArmyRuntimeSlots == nullptr) {
    return;
  }
  SelectionPointerArray_Clear32(&block->selection);
  int selected = 0;
  for (int slotIndex = 0; slotIndex < ARMY_RUNTIME_SLOT_COUNT && selected < SELECTION_ENTRY_CAPACITY; slotIndex++) {
    ArmyRuntimeSlot *army = &g_ArmyRuntimeSlots[slotIndex];
    const ModelDefinition *definition = PlayerRecord_ArmyDefinition(army);
    if ((uint32_t)army->factionIndex != block->factionIndex || definition == nullptr || definition->accelerationPerTick == 0) {
      continue;
    }
    SelectionPointerArray_InsertUniqueAndRecenter(reinterpret_cast<GameEntityRuntime *>(army),&block->selection);
    selected++;
  }
  Thandor_Log("Test: %d Einheiten des Spielers ausgewaehlt, Abstand zu gross: %s",selected,
              SelectionPointerArray_IsSpatialSpreadTooLarge(&block->selection) ? "ja" : "nein");
  InGameCommand_Issue<InGamePlayerSelection_ApplyMoveCommand>(0u,(CommandPayload)(targetY * Q12_ONE),
                                                              (CommandPayload)(targetX * Q12_ONE));
  for (int entryIndex = 0; entryIndex < SELECTION_ENTRY_CAPACITY; entryIndex++) {
    GameEntityRuntime *entry = block->selection.entries[entryIndex];
    if (entry == nullptr) {
      continue;
    }
    ArmyRuntimeSlot *army = reinterpret_cast<ArmyRuntimeSlot *>(entry);
    const ModelDefinition *definition = PlayerRecord_ArmyDefinition(army);
    int routeX = 0;
    int routeY = 0;
    if (definition == nullptr || !PlayerRecord_RouteEnd(army,&routeX,&routeY)) {
      continue;
    }
    ArmyMovementRuntime *movement = reinterpret_cast<ArmyMovementRuntime *>(army);
    Thandor_Log("Test: ARM %u Tempo %d Panzerung %u bei %d/%d, Ziel %.2f/%.2f",(unsigned)army->armyAssetId,
                definition->movementSpeed,definition->maximumHealth,
                (army->modelNodeRuntime->worldTransform).translation.x / Q12_ONE,
                (army->modelNodeRuntime->worldTransform).translation.y / Q12_ONE,
                movement->movementTargetWorldXQ12 / 4096.0,movement->movementTargetWorldYQ12 / 4096.0);
  }
}

/* Test aid (developer build only): OPEN_THANDOR_TEST_PLACEMENT=1. 20 s after the start the placement test of the
   player's build cursor (ArmyPlacement_CanPlaceAssetAtFieldPoint, the player's faction) is probed next to the
   player's headquarters and next to a factory of the player (its gate must stay free) in 16 directions: the
   smallest free centre distance per building type, against the full and the 3/4 radii. Then a high wall is created
   at the freest accepted point near the headquarters, the collision test alone is probed next to it per wall type,
   and up to five pieces are created in a row (wall chain). */
static ModelDefinition *PlayerRecord_AssetDefinition(PckArmyAssetIdCatalog armyAssetId)
{
  ArmyAssetRecordPrefix *armyAsset;
  if (ArmyAssetRegistry_FindById(armyAssetId,&armyAsset) != 0) {
    return nullptr;
  }
  return reinterpret_cast<ModelDefinition *>(ModelDefinitionRegistry_FindById(
       Thandor_U32ToPointer<AiLinkedDefinitionListView>(armyAsset->rootNodeOffsetOrPointer)->definitionIds[0])); /* 32-bit format field */
}

static bool PlayerRecord_ProbePlace(int convention,PckArmyAssetIdCatalog id,uint32_t faction,Q12 x,Q12 y)
{
  WorldRuntimeContext *world = &g_InGameRuntimeRoot->worldRuntime;
  return convention == 0 ? ArmyPlacement_CanPlaceAssetAtFieldPoint(0,0,0,x,y,id,faction,world,nullptr)
                         : ArmyPlacement_CanPlaceAssetAtFieldPoint(0,0,0,y,x,id,faction,world,nullptr);
}

/* smallest free centre distance (Q12, 0x40 steps up to 5 units) from (cx, cy) in direction i of 16; -1 = none */
static int PlayerRecord_ProbeDistance(int convention,PckArmyAssetIdCatalog id,uint32_t faction,Q12 cx,Q12 cy,int i)
{
  static const double PI = 3.14159265358979;
  const double angle = i * 2.0 * PI / 16.0;
  for (int d = 0; d <= 5 * Q12_ONE; d += 0x40) {
    const Q12 x = cx + (Q12)(d * __builtin_cos(angle));
    const Q12 y = cy + (Q12)(d * __builtin_sin(angle));
    if (PlayerRecord_ProbePlace(convention,id,faction,x,y)) {
      return d;
    }
  }
  return -1;
}

static ArmyRuntimeSlot *PlayerRecord_FindOwnBuilding(uint32_t faction,int assetIdOrMinusClass)
{
  for (int slotIndex = 0; slotIndex < ARMY_RUNTIME_SLOT_COUNT; slotIndex++) {
    ArmyRuntimeSlot *army = &g_ArmyRuntimeSlots[slotIndex];
    const ModelDefinition *definition = PlayerRecord_ArmyDefinition(army);
    if ((uint32_t)army->factionIndex != faction || definition == nullptr || definition->accelerationPerTick != 0) {
      continue;
    }
    if (assetIdOrMinusClass >= 0 ? (int)army->armyAssetId == assetIdOrMinusClass
                                 : (int)definition->runtimeClassId == -assetIdOrMinusClass) {
      return army;
    }
  }
  return nullptr;
}

static void PlayerRecord_ProbeAround(int convention,ArmyRuntimeSlot *base,PckArmyAssetIdCatalog id,uint32_t faction,
                                     int *outBestDirection)
{
  const ModelDefinition *baseDefinition = PlayerRecord_ArmyDefinition(base);
  const ModelDefinition *definition = PlayerRecord_AssetDefinition(id);
  if (baseDefinition == nullptr || definition == nullptr) {
    return;
  }
  const Q12 cx = (base->modelNodeRuntime->worldTransform).translation.x;
  const Q12 cy = (base->modelNodeRuntime->worldTransform).translation.y;
  char line[256];
  int length = 0;
  int best = -1;
  int bestDirection = -1;
  for (int i = 0; i < 16; i++) {
    const int d = PlayerRecord_ProbeDistance(convention,id,faction,cx,cy,i);
    length += snprintf(line + length,sizeof line - length," %.2f",d < 0 ? -1.0 : d / 4096.0);
    if (d >= 0 && (best < 0 || d < best)) {
      best = d;
      bestDirection = i;
    }
  }
  const double full = (double)(baseDefinition->footprintRadius + definition->footprintRadius) / Q12_ONE;
  Thandor_Log("Test Bauen: ARM %u neben ARM %u (Klasse %d): kleinster Abstand %.2f (voll %.2f, 3/4 %.2f); je Richtung:%s",
              (unsigned)id,(unsigned)base->armyAssetId,(int)baseDefinition->runtimeClassId,best < 0 ? -1.0 : best / 4096.0,
              full,full * 0.75,line);
  if (outBestDirection != nullptr) {
    *outBestDirection = bestDirection;
  }
}

static ArmyRuntimeSlot *PlayerRecord_CreateAt(int convention,ArmyRuntimeSlot *base,PckArmyAssetIdCatalog id,
                                              uint32_t faction,int direction)
{
  const Q12 cx = (base->modelNodeRuntime->worldTransform).translation.x;
  const Q12 cy = (base->modelNodeRuntime->worldTransform).translation.y;
  const int d = PlayerRecord_ProbeDistance(convention,id,faction,cx,cy,direction);
  if (d < 0) {
    return nullptr;
  }
  const double angle = direction * 2.0 * 3.14159265358979 / 16.0;
  const Q12 x = cx + (Q12)(d * __builtin_cos(angle));
  const Q12 y = cy + (Q12)(d * __builtin_sin(angle));
  WorldRuntimeContext *world = &g_InGameRuntimeRoot->worldRuntime;
  ArmyRuntimeSlot *created = convention == 0 ? ArmyRuntime_CreateInstanceFromAsset(4,0,x,y,faction,id,world,nullptr)
                                             : ArmyRuntime_CreateInstanceFromAsset(4,0,y,x,faction,id,world,nullptr);
  if (created != nullptr && created->modelNodeRuntime != nullptr) {
    const Q12 nx = (created->modelNodeRuntime->worldTransform).translation.x;
    const Q12 ny = (created->modelNodeRuntime->worldTransform).translation.y;
    Thandor_Log("Test Bauen: ARM %u gebaut bei %.2f/%.2f, Abstand zu ARM %u %.2f",(unsigned)id,nx / 4096.0,ny / 4096.0,
                (unsigned)base->armyAssetId,__builtin_sqrt((double)(nx - cx) * (nx - cx) + (double)(ny - cy) * (ny - cy)) / 4096.0);
  }
  return created;
}

static void PlayerRecord_TestPlacement(uint32_t now)
{
  const char *value = getenv("OPEN_THANDOR_TEST_PLACEMENT");
  if (s_PlayerRecordPlacementTestDone || value == nullptr || now - s_PlayerRecordSessionStartTick < 20 * PLAYER_RECORD_TICKS_PER_SECOND) {
    return;
  }
  s_PlayerRecordPlacementTestDone = true;
  SelectionPlayerRuntimeBlock *block = PlayerRecord_PlayerBlock();
  if (block == nullptr || g_ArmyRuntimeSlots == nullptr || g_InGameRuntimeRoot == nullptr) {
    return;
  }
  const uint32_t faction = block->factionIndex;
  ArmyRuntimeSlot *headquarters = PlayerRecord_FindOwnBuilding(faction,ARM_0300_BUILDING_MDL0301);
  if (headquarters == nullptr) {
    Thandor_Log("Test Bauen: kein HQ des Spielers");
    return;
  }
  /* which argument order of ArmyPlacement_CanPlaceAssetAtFieldPoint is x/y: the one that rejects the centre and
     accepts somewhere within 5 units */
  int convention = -1;
  for (int c = 0; c < 2 && convention < 0; c++) {
    const Q12 cx = (headquarters->modelNodeRuntime->worldTransform).translation.x;
    const Q12 cy = (headquarters->modelNodeRuntime->worldTransform).translation.y;
    if (PlayerRecord_ProbePlace(c,ARM_0382_BUILDING_MDL0320,faction,cx,cy)) continue;
    for (int i = 0; i < 16; i++) {
      if (PlayerRecord_ProbeDistance(c,ARM_0382_BUILDING_MDL0320,faction,cx,cy,i) > 0) {
        convention = c;
        break;
      }
    }
  }
  Thandor_Log("Test Bauen: Partei %u, dicht bauen %s, Reihenfolge %d",faction,
              AiImproved_TightBuildingFor(faction) ? "an" : "aus",convention);
  if (convention < 0) {
    return;
  }
  static const PckArmyAssetIdCatalog probes[] = {ARM_0310_BUILDING_MDL0305,ARM_0302_BUILDING_MDL0300,
                                                 ARM_0320_BUILDING_MDL0311,ARM_0360_BUILDING_MDL0314,
                                                 ARM_0380_BUILDING_MDL0329,ARM_0381_BUILDING_MDL0319,
                                                 ARM_0382_BUILDING_MDL0320};
  for (PckArmyAssetIdCatalog id : probes) {
    PlayerRecord_ProbeAround(convention,headquarters,id,faction,nullptr);
  }
  ArmyRuntimeSlot *factory = PlayerRecord_FindOwnBuilding(faction,-(int)MODEL_RUNTIME_CLASS_13);
  if (factory != nullptr) {
    PlayerRecord_ProbeAround(convention,factory,ARM_0310_BUILDING_MDL0305,faction,nullptr);
  }
  /* a free spot for the wall test: the accepted high-wall point near the headquarters (16 directions, up to 5
     units) farthest from every other model */
  ArmyRuntimeSlot *wall = nullptr;
  {
    WorldRuntimeContext *world = &g_InGameRuntimeRoot->worldRuntime;
    const Q12 cx = (headquarters->modelNodeRuntime->worldTransform).translation.x;
    const Q12 cy = (headquarters->modelNodeRuntime->worldTransform).translation.y;
    double bestClearance = 0.0;
    Q12 bestX = 0;
    Q12 bestY = 0;
    for (int i = 0; i < 16; i++) {
      const double angle = i * 2.0 * 3.14159265358979 / 16.0;
      for (int d = Q12_ONE; d <= 5 * Q12_ONE; d += 0x200) {
        const Q12 x = cx + (Q12)(d * __builtin_cos(angle));
        const Q12 y = cy + (Q12)(d * __builtin_sin(angle));
        if (!PlayerRecord_ProbePlace(convention,ARM_0380_BUILDING_MDL0329,faction,x,y)) continue;
        double clearance = 1e9;
        for (WorldOwnerListNode *node = world->ownerListHead; node != nullptr; node = node->nextNode) {
          if (node->ownerClassId != WORLD_OWNER_RUNTIME_MODEL) continue;
          ModelRuntimeSlot *model = WorldOwnerNode_ModelRuntime(node);
          const Q12 mx = model->rootModelNodeOrSavedOffset.modelNode->worldTransform.translation.x;
          const Q12 my = model->rootModelNodeOrSavedOffset.modelNode->worldTransform.translation.y;
          const double distance = __builtin_sqrt((double)(mx - x) * (mx - x) + (double)(my - y) * (my - y)) / 4096.0;
          if (distance < clearance) clearance = distance;
        }
        if (clearance > bestClearance) {
          bestClearance = clearance;
          bestX = x;
          bestY = y;
        }
      }
    }
    Thandor_Log("Test Bauen: freier Platz fuer die Mauer %.2f/%.2f, naechstes Objekt %.2f",bestX / 4096.0,bestY / 4096.0,
                bestClearance);
    if (bestClearance < 1.0) {
      return;
    }
    wall = convention == 0 ? ArmyRuntime_CreateInstanceFromAsset(4,0,bestX,bestY,faction,ARM_0380_BUILDING_MDL0329,world,nullptr)
                           : ArmyRuntime_CreateInstanceFromAsset(4,0,bestY,bestX,faction,ARM_0380_BUILDING_MDL0329,world,nullptr);
  }
  if (wall == nullptr) {
    return;
  }
  /* the collision part alone (no terrain): smallest free centre distance of a second wall piece per type */
  {
    WorldRuntimeContext *world = &g_InGameRuntimeRoot->worldRuntime;
    const Q12 cx = (wall->modelNodeRuntime->worldTransform).translation.x;
    const Q12 cy = (wall->modelNodeRuntime->worldTransform).translation.y;
    static const PckArmyAssetIdCatalog pieces[] = {ARM_0380_BUILDING_MDL0329,ARM_0381_BUILDING_MDL0319,
                                                   ARM_0382_BUILDING_MDL0320,ARM_0310_BUILDING_MDL0305};
    for (PckArmyAssetIdCatalog id : pieces) {
      ModelDefinition *definition = PlayerRecord_AssetDefinition(id);
      if (definition == nullptr) continue;
      char line[200];
      int length = 0;
      for (int i = 0; i < 16; i += 2) {
        const double angle = i * 2.0 * 3.14159265358979 / 16.0;
        int found = -1;
        for (int d = 0; d <= 3 * Q12_ONE && found < 0; d += 0x20) {
          const Q12 x = cx + (Q12)(d * __builtin_cos(angle));
          const Q12 y = cy + (Q12)(d * __builtin_sin(angle));
          if (!ArmyPlacementCollision_TestBuildingAgainstRuntimeList(0,definition,faction,convention == 0 ? x : y,
                                                                    convention == 0 ? y : x,world)) {
            found = d;
          }
        }
        length += snprintf(line + length,sizeof line - length," %.2f",found / 4096.0);
      }
      Thandor_Log("Test Bauen: nur Kollision, ARM %u neben der Mauer:%s",(unsigned)id,line);
    }
  }
  int nextDirection = -1;
  PlayerRecord_ProbeAround(convention,wall,ARM_0380_BUILDING_MDL0329,faction,&nextDirection);
  PlayerRecord_ProbeAround(convention,wall,ARM_0381_BUILDING_MDL0319,faction,nullptr);
  PlayerRecord_ProbeAround(convention,wall,ARM_0382_BUILDING_MDL0320,faction,nullptr);
  PlayerRecord_ProbeAround(convention,wall,ARM_0310_BUILDING_MDL0305,faction,nullptr);
  ArmyRuntimeSlot *previous = wall;
  for (int piece = 0; piece < 4 && nextDirection >= 0; piece++) {
    ArmyRuntimeSlot *next = PlayerRecord_CreateAt(convention,previous,ARM_0380_BUILDING_MDL0329,faction,nextDirection);
    if (next == nullptr) break;
    previous = next;
  }
}

#endif

void PlayerRecord_OnSimulationStep()
{
  if (!PlayerRecord_Active()) {
    return;
  }
  uint32_t now = PlayerRecord_Ticks();
#ifdef THANDOR_DEV_TOOLS
  PlayerRecord_TestPlayerMove(now);
  PlayerRecord_TestPlacement(now);
#endif
  if (now - s_PlayerRecordLastStatusTick < PLAYER_RECORD_STATUS_TICKS) {
    return;
  }
  s_PlayerRecordLastStatusTick = now;
  SelectionPlayerRuntimeBlock *block = PlayerRecord_PlayerBlock();
  if (block == nullptr || block->factionIndex < 1 || block->factionIndex > 7 || g_ArmyRuntimeSlots == nullptr) {
    return;
  }
  int units = 0;
  int buildings = 0;
  for (int slotIndex = 0; slotIndex < ARMY_RUNTIME_SLOT_COUNT; slotIndex++) {
    const ArmyRuntimeSlot *army = &g_ArmyRuntimeSlots[slotIndex];
    if ((uint32_t)army->factionIndex != block->factionIndex) {
      continue;
    }
    const ModelDefinition *definition = PlayerRecord_ArmyDefinition(army);
    if (definition == nullptr) {
      continue;
    }
    if (definition->accelerationPerTick != 0) {
      units++;
    }
    else {
      buildings++;
    }
  }
  const GameFactionRuntimeRecord *record = &g_GameFactionRuntimeImage.records[block->factionIndex];
  PlayerRecord_Line("Stand: %d Einheiten, %d Gebaeude, Xenit %u, Tritium %u, Energie %u/%u",units,buildings,
                    record->xeniteCurrentQ4 / 16,record->tritiumCurrentQ4 / 16,record->suppliedEnergyDemandQ4 / 16,
                    record->energyGenerationCapacityQ4 / 16);
}
