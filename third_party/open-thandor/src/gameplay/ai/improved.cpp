/*
 * Open Thandor
 * File: src/gameplay/ai/improved.cpp
 * Improved AI for the Thandor remake (not part of the original game): the settings (ki.ini), the AI repairs, the
 * grace period and the improved AI of levels 2..7 (see improved.h). With level 0 or 1, repairs off and no grace
 * period nothing here changes the original AI, and networked sessions always run that way.
 * Integer arithmetic only, so the decisions stay deterministic.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thandor/thandor.h>
#include <thandor/gameplay/ai/improved.h>
#include <thandor/gameplay/army/evade.h>
#include <thandor/platform/bootstrap/image.h>

/* ------------------------------------------------------------------------------------------------------------ */
/* Constants                                                                                                    */

/* game ticks per second at normal speed */
static constexpr uint32_t AI_IMPROVED_TICKS_PER_SECOND = 20;

/* A unit counts as moved when it is this far from the position it was last seen moving at. */
static constexpr int64_t AI_IMPROVED_MOVE_EPSILON_Q12 = Q12_ONE;
/* Target tracking: a condition drop of 1 % counts as progress. */
static constexpr Q12 AI_IMPROVED_PROGRESS_CONDITION_Q12 = Q12_ONE / 100;
/* No damage for 20 s (at least one and a half reloads + 5 s) although the target is in reach: give it up. */
static constexpr uint32_t AI_IMPROVED_STALL_IN_REACH_TICKS = 20 * AI_IMPROVED_TICKS_PER_SECOND;
/* Out of reach and standing still without damage for 30 s: give it up. */
static constexpr uint32_t AI_IMPROVED_STALL_OUT_OF_REACH_TICKS = 30 * AI_IMPROVED_TICKS_PER_SECOND;
/* A given-up target is ignored by the unit for 4 minutes (up to three at a time), by the faction's attack groups
   for 3 minutes. */
static constexpr uint32_t AI_IMPROVED_IGNORE_UNIT_TICKS = 4 * 60 * AI_IMPROVED_TICKS_PER_SECOND;
static constexpr uint32_t AI_IMPROVED_IGNORE_FACTION_TICKS = 3 * 60 * AI_IMPROVED_TICKS_PER_SECOND;
/* A group-assigned unit without a target order that has stood still for 30 s is free for a new group. */
static constexpr uint32_t AI_IMPROVED_GROUP_IDLE_RELEASE_TICKS = 30 * AI_IMPROVED_TICKS_PER_SECOND;
/* A unit that has not moved for 15 s on its way counts as stuck (it may then shoot the wall in its way). */
static constexpr uint32_t AI_IMPROVED_STUCK_TICKS = 15 * AI_IMPROVED_TICKS_PER_SECOND;
/* Grace period: an armed human unit this close to a core building of an AI faction is in its base. */
static constexpr int64_t AI_IMPROVED_GRACE_BASE_RADIUS_Q12 = 12 * (int64_t)Q12_ONE;
/* Foreign territory: the support radius of a foreign building plus this margin, where that building is nearer
   than every building of the planning faction. */
static constexpr int AI_IMPROVED_FOREIGN_MARGIN_Q12 = 2 * Q12_ONE;

/* Deposits the storage chain tried to reach and failed: the user's game showed the same deposit started 14 times
   and given up 11 times, always without a single building, while the faction's Tritium deposit stayed unused.
   A deposit that failed AI_IMPROVED_BRIDGE_FAILED_FOR_GOOD times is left alone for the rest of the game. */
static constexpr int AI_IMPROVED_BRIDGE_FAILED_SITES = 8;
static constexpr uint32_t AI_IMPROVED_BRIDGE_FAILED_FOR_GOOD = 2;
static constexpr int AI_IMPROVED_FACTION_IGNORED_TARGETS = 8;
/* Targets at which units stood within weapon range without doing any damage (a building behind a terrain edge, on
   a plateau, across lava): once this many different units of the faction have reported the same target, the whole
   faction leaves it alone for AI_IMPROVED_NO_DAMAGE_TICKS instead of each unit trying again every few minutes.
   Seen in the user's game: 284 reports, nearly all of them against one single building. */
static constexpr int AI_IMPROVED_NO_DAMAGE_TARGETS = 12;
static constexpr uint32_t AI_IMPROVED_NO_DAMAGE_REPORTS = 2;
static constexpr uint32_t AI_IMPROVED_NO_DAMAGE_TICKS = 20 * 60 * AI_IMPROVED_TICKS_PER_SECOND;
/* Places a group could not reach (its units stood still out of reach): no group targets within this distance
   of one for 10 minutes. */
static constexpr int AI_IMPROVED_UNREACHABLE_AREAS = 6;
static constexpr int64_t AI_IMPROVED_UNREACHABLE_RADIUS_Q12 = 8 * (int64_t)Q12_ONE;
static constexpr uint32_t AI_IMPROVED_UNREACHABLE_TICKS = 10 * 60 * AI_IMPROVED_TICKS_PER_SECOND;
/* Defence mode: the faction has seen a hostile army at least this many times its own strength (sum of the units'
   maximum health), with at least this many hostile units, and reacts for this long after the last such sighting. */
static constexpr uint32_t AI_IMPROVED_THREAT_FACTOR = 2;
static constexpr uint32_t AI_IMPROVED_THREAT_MIN_UNITS = 6;
static constexpr uint32_t AI_IMPROVED_THREAT_TICKS = 3 * 60 * AI_IMPROVED_TICKS_PER_SECOND;
/* The AI waits at most this long for a big attack group; after that the level's group is enough. */
static constexpr uint32_t AI_IMPROVED_GROUP_WAIT_TICKS = 3 * 60 * AI_IMPROVED_TICKS_PER_SECOND;
/* extra defence buildings allowed while the mode lasts, and the factor on their purchase weight */
static constexpr uint32_t AI_IMPROVED_THREAT_EXTRA_TOWERS = 4;
static constexpr uint32_t AI_IMPROVED_THREAT_TOWER_WEIGHT = 3;
static constexpr int AI_IMPROVED_CORE_POSITIONS = 48;
static constexpr int AI_IMPROVED_FOREIGN_STRUCTURES = 256;

/* A unit counts as at home within this distance of the home point. */
static constexpr int64_t AI_IMPROVED_HOME_RADIUS_Q12 = 12 * (int64_t)Q12_ONE;

/* Armed hostiles within this Manhattan distance of a group target count as its defence (the weapons reach 1..13
   world units, the Laserspule's shot about 13.4). */
static constexpr int64_t AI_IMPROVED_DEFENCE_RADIUS_Q12 = 14 * (int64_t)Q12_ONE;

/* Group target score = base * scale / (scale + distance in world units): DISTANCE_SCALE for buildings,
   MOBILE_DISTANCE_SCALE for units (they move away and are better left to the per-step target choice when far). */
static constexpr int64_t AI_IMPROVED_DISTANCE_SCALE = 48;
static constexpr int64_t AI_IMPROVED_MOBILE_DISTANCE_SCALE = 24;

/* A shooter at least this far from what it hit fires from afar (artillery, long-range towers): the defenders of
   the improved AI drive at it for AI_IMPROVED_RESPONSE_TICKS after its last hit. */
static constexpr int64_t AI_IMPROVED_DISTANT_SHOOTER_Q12 = 6 * (int64_t)Q12_ONE;
static constexpr uint32_t AI_IMPROVED_RESPONSE_TICKS = 30 * AI_IMPROVED_TICKS_PER_SECOND;

/* From these levels on: the second headquarters and power plant, at least one heavy factory. */
static constexpr int AI_IMPROVED_RESERVE_BASE_LEVEL = 4;
static constexpr int AI_IMPROVED_HEAVY_FACTORY_LEVEL = 5;
/* From this level on: aircraft, laser coils and the research sweep (also for the original AI of level 1). */
static constexpr int AI_IMPROVED_EXTRAS_LEVEL = 1;
/* The aircraft (ARM 240 Kraehe, 242 Habicht, 244 Adler). */
static constexpr uint32_t AI_IMPROVED_AIRCRAFT_FIRST_ASSET = 240;
static constexpr uint32_t AI_IMPROVED_AIRCRAFT_LAST_ASSET = 249;
/* Aircraft one airport's hangar holds (stock pads), and the pause between two air strikes of a faction. */
static constexpr uint32_t AI_IMPROVED_HANGAR_AIRCRAFT = 6;
static constexpr uint32_t AI_IMPROVED_AIRCRAFT_LAUNCH_TICKS = 60 * AI_IMPROVED_TICKS_PER_SECOND;
/* At most one aircraft bought per faction in this time (lost aircraft are replaced, but the ground army keeps
   its share: in Hell a faction bought 62 Adler in two hours). */
static constexpr uint32_t AI_IMPROVED_AIRCRAFT_PURCHASE_TICKS = 120 * AI_IMPROVED_TICKS_PER_SECOND;
/* From this level on: air strikes on every kind of building; radar Goliaths for reconnaissance. */
static constexpr int AI_IMPROVED_AIR_ALL_BUILDINGS_LEVEL = 3;
static constexpr int AI_IMPROVED_RADAR_LEVEL = 3;
/* A radar Goliath's post: this share of the way from the base toward the nearest hostile it has seen, at most
   AI_IMPROVED_RADAR_POST_MAX_Q12 from the base; it drives again when it stands farther than the slack from it. */
static constexpr int64_t AI_IMPROVED_RADAR_POST_SHARE_PERCENT = 40;
static constexpr int64_t AI_IMPROVED_RADAR_POST_MAX_Q12 = 24 * (int64_t)Q12_ONE;
static constexpr int64_t AI_IMPROVED_RADAR_POST_SLACK_Q12 = 4 * (int64_t)Q12_ONE;

/* ------------------------------------------------------------------------------------------------------------ */
/* Settings                                                                                                     */

struct AiImprovedSettings {
  int level;          /* 0..7 */
  bool repairs;
  int graceMinutes;   /* -1 = by level */
  bool log;
  bool spectate;      /* the computer plays the local faction too */
  bool evade;         /* standing vehicles make way in narrow places (gameplay/army/evade.h) */
  bool formation;     /* a group's move target is spread over free spots instead of one point */
  bool record;        /* the player's actions in a file of their own (gameplay/session/player_record.h) */
  bool waves;         /* an AI group attack leaves in waves, light units first (AiImproved_AttackWaveDelay) */
  bool tightBuilding; /* the player's buildings may stand closer together (AiImproved_TightBuildingFor) */
};

/* The defaults of every setting in one place: AiImproved_LoadSettings starts from these, so a key that ki.ini
   does not mention keeps its default instead of silently becoming false. */
static constexpr AiImprovedSettings AI_IMPROVED_DEFAULT_SETTINGS = {
    AI_IMPROVED_LEVEL_ORIGINAL, /* level */
    true,                       /* repairs */
    -1,                         /* graceMinutes: by level */
    false,                      /* log */
    false,                      /* spectate */
    true,                       /* evade */
    true,                       /* formation */
    true,                       /* record */
    true,                       /* waves */
    true,                       /* tightBuilding */
};

static AiImprovedSettings g_AiImprovedSettings = AI_IMPROVED_DEFAULT_SETTINGS;
static bool g_AiImprovedSettingsLoaded = false;

struct AiImprovedLevelParameters {
  uint32_t armyUnitLimit;       /* own units must stay below this count */
  uint32_t groupAttackUnits;    /* intact units an attack group needs */
  uint32_t extractionPercent;   /* AI extraction scale */
  int graceMinutes;             /* default grace period */
  uint32_t responseUnits;       /* alarm response radius in world units (0 = the knowledge data's 8) */
  uint32_t proactiveTowers;     /* defence buildings placed without an attack */
  uint32_t armorPercent;        /* armour of the AI factions against the player's weapons (100 = as in the game) */
  const char *name;
};

/* The user's level plan of 05.11.2026: the grace period now shrinks with the level (level 1, the original AI,
   gets 20 minutes, 2 to 4 an hour, 5 and 6 half an hour, Hell 10 minutes), level 2 keeps five towers in peace,
   level 6 extracts 180 % with an alarm radius of 26 and 30 % more armour, Hell sends groups of 12 and has double
   armour. Levels 4 and up also keep a second headquarters and a second power plant (AiImproved_AddReserveBase-
   Candidates), levels 5 and up at least one heavy factory (AiImproved_AddHeavyFactoryCandidate).
   06.11.2026: towers in peace 5 on level 2 and two more on each level above (7, 9, 11, 13, 15); from level 1 on
   aircraft, laser coils and fuller research (AiImproved_AddLevelExtrasCandidates). */
static const AiImprovedLevelParameters g_AiImprovedLevelParameters[AI_IMPROVED_LEVEL_MAXIMUM + 1] = {
    /* 0 */ {11, 2, 100, 0, 0, 0, 100, "Original-KI"},
    /* 1 */ {11, 2, 100, 20, 0, 0, 100, "Original-KI"},
    /* 2 */ {16, 3, 80, 60, 12, 5, 100, "leicht"},
    /* 3 */ {26, 4, 100, 60, 16, 7, 100, "normal"},
    /* 4 */ {36, 6, 130, 60, 20, 9, 100, "schwer"},
    /* 5 */ {51, 8, 160, 30, 24, 11, 100, "sehr schwer"},
    /* 6 */ {51, 8, 180, 30, 26, 13, 130, "extrem"},
    /* 7 */ {AI_WORKSPACE01_CAPACITY, 12, 200, 10, 28, 15, 200, "Hoelle"},
};

/* L"ki.ini" */
static uint16_t s_AiImprovedIniLeaf[] = {'a', 'd', 'v', 'a', 'n', 'c', 'e', 'd', '_', 'a', 'i', '_', 'j', 'a', 'n', 'd', 'r', 'e', 'n', 'a', 't', 'o', 'r', '.', 'i', 'n', 'i', 0};

static const char s_AiImprovedDefaultIni[] =
    "# Open Thandor: Einstellungen der Computergegner (KI)\r\n"
    "# Das Spiel liest diese Datei bei jedem Missionsstart. Aendern mit einem Texteditor (z. B. Editor/Notepad).\r\n"
    "# Gilt nur fuer Spiele allein gegen den Computer. Netzwerkspiele laufen immer mit der Original-KI.\r\n"
    "\r\n"
    "# Stufe der KI:\r\n"
    "#   0 = Original-KI, wie im Originalspiel. Die Staerke stellt der Regler\r\n"
    "#       \"Einstellung der Computergegner\" in der Missionsbeschreibung ein (80 bis 120 %).\r\n"
    "#   1 = Original-KI mit 20 Minuten Schonfrist, dazu Flughafen mit 2 Flugzeugen,\r\n"
    "#       2 Laserspulen und mehr Forschung\r\n"
    "#   Ab Stufe 2 je Stufe ein Flugzeug und eine Laserspule mehr (Stufe 7: je 8).\r\n"
    "#   2 = verbesserte KI, leicht (1 Stunde Schonfrist, 5 Tuerme schon im Frieden)\r\n"
    "#   3 = verbesserte KI, normal (1 Stunde Schonfrist, 7 Tuerme)\r\n"
    "#   4 = verbesserte KI, schwer (1 Stunde Schonfrist, 9 Tuerme, zweites Hauptquartier\r\n"
    "#       und zweites Kraftwerk als Reserve)\r\n"
    "#   5 = verbesserte KI, sehr schwer (30 Minuten Schonfrist, 11 Tuerme, dazu mindestens\r\n"
    "#       eine schwere Fabrik)\r\n"
    "#   6 = extrem (wie 5, dazu 13 Tuerme, 180 % Foerderung, groesserer Alarmradius und 30 %\r\n"
    "#       mehr Panzerung gegen deine Waffen)\r\n"
    "#   7 = Hoelle (10 Minuten Schonfrist, 15 Tuerme, doppelte Foerderung, doppelte Panzerung\r\n"
    "#       gegen deine Waffen, Angriffsgruppen ab 12 Einheiten, fast keine Einheitengrenze)\r\n"
    "stufe = 0\r\n"
    "\r\n"
    "# Reparaturen: behebt Fehler der Original-KI. Ohne sie greifen Einheiten nach ihrem ersten Angriff\r\n"
    "# nie wieder an, Angriffsgruppen bleiben nach einem Sammelpunkt stehen, Einheiten beschiessen Mauern\r\n"
    "# statt Tuerme, kommen bei Alarm kaum zur Verteidigung, das Pionierfahrzeug wartet ewig, nahe\r\n"
    "# Vorkommen bleiben ungenutzt, die KI baut Minen mitten in fremde Basen, hortet Xenit, weil ein zu\r\n"
    "# teurer Kauf alles blockiert, und baut unter Dauerbeschuss keine Fabrik und kein Kraftwerk mehr.\r\n"
    "# Gilt auch fuer die Original-KI (Stufe 0 und 1). an oder aus.\r\n"
    "reparaturen = an\r\n"
    "\r\n"
    "# Schonfrist in Minuten Spielzeit: so lange schickt die KI keine Angriffe gegen dich und laesst\r\n"
    "# deine Gebaeude in Ruhe. Ihre Verteidigung bleibt aktiv. Die Schonfrist endet sofort, wenn du ein\r\n"
    "# Gebaeude der KI triffst oder mit bewaffneten Einheiten in ihre Basis faehrst.\r\n"
    "# -1 = nach Stufe (0: keine, 1: 20, 2 bis 4: 60, 5 und 6: 30, 7: 10 Minuten), 0 = keine.\r\n"
    "# Eine Zahl (bis 600) gilt als eigene Schonfrist fuer alle Stufen, z. B. schonfrist_minuten = 45.\r\n"
    "schonfrist_minuten = -1\r\n"
    "\r\n"
    "# Protokoll: schreibt die Entscheidungen der KI mit (fuer die Fehlersuche), je Spielstart eine eigene\r\n"
    "# Datei im Ordner protokolle neben dem Spiel. an oder aus.\r\n"
    "protokoll = aus\r\n"
    "\r\n"
    "# Zuschauen: an = der Computer spielt auch deine Partei, alle Parteien spielen KI gegen KI. Deine Befehle\r\n"
    "# gelten weiter, die KI gibt deinen Einheiten aber eigene. Keine Schonfrist. an oder aus.\r\n"
    "zuschauen = aus\r\n"
    "\r\n"
    "# Ausweichen: an = ein stehendes Fahrzeug macht Platz, wenn eine eigene oder verbuendete Einheit\r\n"
    "# dagegen faehrt. Im Original bleibt es stehen, und an Engstellen staut sich die ganze Armee dahinter.\r\n"
    "# Gilt fuer alle Fahrzeuge und fuer deine Einheiten genauso wie fuer die der KI. Einheiten eines\r\n"
    "# Verbuendeten werden nur bewegt, wenn sie gerade keinen eigenen Befehl haben. an oder aus.\r\n"
    "ausweichen = an\r\n"
    "\r\n"
    "# Aufstellung: an = schickst du eine weit auseinander stehende Auswahl irgendwohin, bekommt jede Einheit\r\n"
    "# einen eigenen freien Platz rund um den Zielpunkt, die schnellen und leichten vorn Richtung Ziel, die\r\n"
    "# schweren dahinter. Im Original fahren alle auf denselben Punkt und draengeln sich dort. Ausserdem\r\n"
    "# plant das Spiel die Wege von mehr Einheiten gemeinsam. an oder aus.\r\n"
    "aufstellung = an\r\n"
    "\r\n"
    "# Aufzeichnung: an = deine eigenen Befehle (Fahren, Angreifen, Bauen, Kaufen, Forschen, Diplomatie)\r\n"
    "# kommen in eine eigene Datei im Ordner protokolle (Name des Protokolls mit _spieler), dazu alle\r\n"
    "# 5 Minuten dein Stand. Nur fuer die Auswertung, das Spiel aendert sich dadurch nicht. an oder aus.\r\n"
    "aufzeichnung = an\r\n"
    "\r\n"
    "# Wellen (nur verbesserte KI, ab Stufe 2): an = eine Angriffsgruppe der KI faehrt in bis zu 5 Wellen\r\n"
    "# los, alle 2,5 Sekunden eine, die schnellen und leichten Einheiten zuerst. aus = alle auf einmal wie\r\n"
    "# bisher. an oder aus.\r\n"
    "wellen = an\r\n"
    "\r\n"
    "# Dicht bauen (nur deine Gebaeude): an = deine Gebaeude, Tuerme und Mauern duerfen beim Bauen naeher\r\n"
    "# an andere Gebaeude (3/4 des Abstands), sodass sie sich beruehren. Durchfahren koennen Einheiten dort\r\n"
    "# nicht, sie fahren wie vorher drumherum. Die KI baut weiter mit dem alten Abstand. aus = wie im\r\n"
    "# Original. Deine hohe Mauer laesst sich immer, auch mit aus, wie die anderen Mauern Stueck an Stueck\r\n"
    "# bauen.\r\n"
    "dicht_bauen = an\r\n";

static bool AiImproved_SinglePlayer()
{
  return (g_SessionNetworkRoleFlags & SESSION_NETWORK_ROLE_NETWORKED_MASK) == SESSION_NETWORK_ROLE_LOCAL;
}

static bool AiImproved_IsSpace(char c)
{
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Lower-case ASCII copy of [begin, end) without surrounding blanks, cut to capacity - 1. */
static void AiImproved_CopyToken(const char *begin,const char *end,char *out,size_t capacity)
{
  while (begin < end && AiImproved_IsSpace(*begin)) begin++;
  while (end > begin && AiImproved_IsSpace(end[-1])) end--;
  size_t length = 0;
  for (; begin < end && length + 1 < capacity; begin++, length++) {
    char c = *begin;
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    out[length] = c;
  }
  out[length] = 0;
}

static bool AiImproved_ParseBool(const char *value,bool *out)
{
  if (!strcmp(value,"an") || !strcmp(value,"ja") || !strcmp(value,"1") || !strcmp(value,"true") ||
      !strcmp(value,"on") || !strcmp(value,"ein") || !strcmp(value,"yes")) {
    *out = true;
    return true;
  }
  if (!strcmp(value,"aus") || !strcmp(value,"nein") || !strcmp(value,"0") || !strcmp(value,"false") ||
      !strcmp(value,"off") || !strcmp(value,"no")) {
    *out = false;
    return true;
  }
  return false;
}

static bool AiImproved_ParseInt(const char *value,int *out)
{
  char *end = nullptr;
  long parsed = strtol(value,&end,10);
  if (end == value || (end != nullptr && *end != 0)) {
    return false;
  }
  *out = (int)parsed;
  return true;
}

static void AiImproved_ApplySetting(AiImprovedSettings *settings,const char *key,const char *value)
{
  int number;
  bool flag;

  if (!strcmp(key,"stufe") || !strcmp(key,"level")) {
    if (AiImproved_ParseInt(value,&number)) settings->level = number;
  }
  else if (!strcmp(key,"reparaturen") || !strcmp(key,"repairs")) {
    if (AiImproved_ParseBool(value,&flag)) settings->repairs = flag;
  }
  else if (!strcmp(key,"schonfrist_minuten") || !strcmp(key,"schonfrist") || !strcmp(key,"grace_minutes")) {
    if (AiImproved_ParseInt(value,&number)) settings->graceMinutes = number;
  }
  else if (!strcmp(key,"protokoll") || !strcmp(key,"log")) {
    if (AiImproved_ParseBool(value,&flag)) settings->log = flag;
  }
  else if (!strcmp(key,"zuschauen") || !strcmp(key,"spectate")) {
    if (AiImproved_ParseBool(value,&flag)) settings->spectate = flag;
  }
  else if (!strcmp(key,"ausweichen") || !strcmp(key,"evade")) {
    if (AiImproved_ParseBool(value,&flag)) settings->evade = flag;
  }
  else if (!strcmp(key,"aufstellung") || !strcmp(key,"formation")) {
    if (AiImproved_ParseBool(value,&flag)) settings->formation = flag;
  }
  else if (!strcmp(key,"aufzeichnung") || !strcmp(key,"record")) {
    if (AiImproved_ParseBool(value,&flag)) settings->record = flag;
  }
  else if (!strcmp(key,"wellen") || !strcmp(key,"waves")) {
    if (AiImproved_ParseBool(value,&flag)) settings->waves = flag;
  }
  else if (!strcmp(key,"dicht_bauen") || !strcmp(key,"tight_building")) {
    if (AiImproved_ParseBool(value,&flag)) settings->tightBuilding = flag;
  }
}

/* key = value lines; '#' and ';' start comments, [sections] are ignored, a UTF-8 byte order mark is skipped. */
static void AiImproved_ParseIni(AiImprovedSettings *settings,const char *text,uint32_t length)
{
  const char *cursor = text;
  const char *end = text + length;
  if (length >= 3 && (uint8_t)text[0] == 0xEF && (uint8_t)text[1] == 0xBB && (uint8_t)text[2] == 0xBF) {
    cursor += 3;
  }
  while (cursor < end) {
    const char *lineEnd = cursor;
    while (lineEnd < end && *lineEnd != '\n') lineEnd++;
    const char *contentEnd = cursor;
    while (contentEnd < lineEnd && *contentEnd != '#' && *contentEnd != ';') contentEnd++;
    const char *equals = cursor;
    while (equals < contentEnd && *equals != '=') equals++;
    if (equals < contentEnd) {
      char key[40];
      char value[40];
      AiImproved_CopyToken(cursor,equals,key,sizeof key);
      AiImproved_CopyToken(equals + 1,contentEnd,value,sizeof value);
      AiImproved_ApplySetting(settings,key,value);
    }
    cursor = lineEnd + 1;
  }
}

/* Reads ki.ini from the current directory, else from the executable directory. False when there is none. */
static bool AiImproved_ReadIni(AiImprovedSettings *settings)
{
  void *fileHandle;
  uint32_t fileSize;

  if (g_FileSystemOpen(FileSystemOpenFlags::FILESYSTEM_OPEN_NONE,s_AiImprovedIniLeaf,&fileHandle) != 0) {
    WidePath_CombineDirectoryAndLeaf(g_FileSystemCombinedPathScratchUtf16,s_AiImprovedIniLeaf,g_ExecutableDirectoryUtf16);
    if (g_FileSystemOpen(FileSystemOpenFlags::FILESYSTEM_OPEN_NONE,g_FileSystemCombinedPathScratchUtf16,&fileHandle) != 0) {
      return false;
    }
  }
  if (!g_FileSystemGetSize(fileHandle,&fileSize) || fileSize > 0x10000) {
    g_FileSystemClose(fileHandle);
    return false;
  }
  char *text = static_cast<char *>(malloc(fileSize + 1));
  if (text == nullptr) {
    g_FileSystemClose(fileHandle);
    return false;
  }
  bool readOk = (fileSize == 0) || (g_FileSystemReadExact(fileSize,text,fileHandle) == 0);
  g_FileSystemClose(fileHandle);
  if (readOk) {
    text[fileSize] = 0;
    AiImproved_ParseIni(settings,text,fileSize);
  }
  free(text);
  return readOk;
}

static void AiImproved_WriteDefaultIni()
{
  WidePath_CombineDirectoryAndLeaf(g_FileSystemCombinedPathScratchUtf16,s_AiImprovedIniLeaf,g_ExecutableDirectoryUtf16);
  FileSystem_WriteBufferToPath((uint32_t)(sizeof s_AiImprovedDefaultIni - 1),
                               const_cast<char *>(s_AiImprovedDefaultIni),g_FileSystemCombinedPathScratchUtf16);
}

static void AiImproved_ApplyEnvironment(AiImprovedSettings *settings)
{
  const char *value;
  int number;
  bool flag;
  char token[40];

  value = getenv("OPEN_THANDOR_AI_LEVEL");
  if (value != nullptr && AiImproved_ParseInt(value,&number)) settings->level = number;
  value = getenv("OPEN_THANDOR_AI_REPAIRS");
  if (value != nullptr) {
    AiImproved_CopyToken(value,value + strlen(value),token,sizeof token);
    if (AiImproved_ParseBool(token,&flag)) settings->repairs = flag;
  }
  value = getenv("OPEN_THANDOR_AI_GRACE_MINUTES");
  if (value != nullptr && AiImproved_ParseInt(value,&number)) settings->graceMinutes = number;
  value = getenv("OPEN_THANDOR_AI_LOG");
  if (value != nullptr) {
    AiImproved_CopyToken(value,value + strlen(value),token,sizeof token);
    if (AiImproved_ParseBool(token,&flag)) settings->log = flag;
  }
  value = getenv("OPEN_THANDOR_AI_ALL");
  if (value != nullptr) {
    AiImproved_CopyToken(value,value + strlen(value),token,sizeof token);
    if (AiImproved_ParseBool(token,&flag)) settings->spectate = flag;
  }
  value = getenv("OPEN_THANDOR_AI_EVADE");
  if (value != nullptr) {
    AiImproved_CopyToken(value,value + strlen(value),token,sizeof token);
    if (AiImproved_ParseBool(token,&flag)) settings->evade = flag;
  }
  value = getenv("OPEN_THANDOR_AI_WAVES");
  if (value != nullptr) {
    AiImproved_CopyToken(value,value + strlen(value),token,sizeof token);
    if (AiImproved_ParseBool(token,&flag)) settings->waves = flag;
  }
  value = getenv("OPEN_THANDOR_RECORD");
  if (value != nullptr) {
    AiImproved_CopyToken(value,value + strlen(value),token,sizeof token);
    if (AiImproved_ParseBool(token,&flag)) settings->record = flag;
  }
  value = getenv("OPEN_THANDOR_AI_FORMATION");
  if (value != nullptr) {
    AiImproved_CopyToken(value,value + strlen(value),token,sizeof token);
    if (AiImproved_ParseBool(token,&flag)) settings->formation = flag;
  }
}

void AiImproved_LoadSettings()
{
  AiImprovedSettings settings = AI_IMPROVED_DEFAULT_SETTINGS;
  if (!AiImproved_ReadIni(&settings)) {
    AiImproved_WriteDefaultIni();
  }
  AiImproved_ApplyEnvironment(&settings);
  if (settings.level < AI_IMPROVED_LEVEL_ORIGINAL) settings.level = AI_IMPROVED_LEVEL_ORIGINAL;
  if (settings.level > AI_IMPROVED_LEVEL_MAXIMUM) settings.level = AI_IMPROVED_LEVEL_MAXIMUM;
  if (settings.graceMinutes < -1) settings.graceMinutes = -1;
  if (settings.graceMinutes > 600) settings.graceMinutes = 600;
  g_AiImprovedSettings = settings;
  g_AiImprovedSettingsLoaded = true;
}

static const AiImprovedSettings &AiImproved_Settings()
{
  if (!g_AiImprovedSettingsLoaded) {
    AiImproved_LoadSettings();
  }
  return g_AiImprovedSettings;
}

int AiImproved_Level()
{
  if (!AiImproved_SinglePlayer()) {
    return AI_IMPROVED_LEVEL_ORIGINAL;
  }
  return AiImproved_Settings().level;
}

void AiImproved_SetLevel(int level)
{
  AiImproved_Settings();
  if (level < AI_IMPROVED_LEVEL_ORIGINAL) level = AI_IMPROVED_LEVEL_ORIGINAL;
  if (level > AI_IMPROVED_LEVEL_MAXIMUM) level = AI_IMPROVED_LEVEL_MAXIMUM;
  g_AiImprovedSettings.level = level;
}

bool AiImproved_IsActive()
{
  return AiImproved_Level() >= AI_IMPROVED_LEVEL_FIRST_IMPROVED;
}

bool AiImproved_RepairsActive()
{
  return AiImproved_SinglePlayer() && AiImproved_Settings().repairs;
}

bool AiImproved_EvasionActive()
{
  return AiImproved_SinglePlayer() && AiImproved_Settings().evade;
}

bool AiImproved_FormationActive()
{
  return AiImproved_SinglePlayer() && AiImproved_Settings().formation;
}

bool AiImproved_RecordActive()
{
  return AiImproved_SinglePlayer() && AiImproved_Settings().record;
}

static int AiImproved_GraceMinutes()
{
  if (!AiImproved_SinglePlayer()) {
    return 0;
  }
  const AiImprovedSettings &settings = AiImproved_Settings();
  if (settings.graceMinutes >= 0) {
    return settings.graceMinutes;
  }
  return g_AiImprovedLevelParameters[settings.level].graceMinutes;
}

bool AiImproved_AnyActive()
{
  return AiImproved_IsActive() || AiImproved_RepairsActive() || AiImproved_GraceMinutes() > 0 ||
         AiImproved_ExtrasActive();
}

bool AiImproved_LogEnabled()
{
  return AiImproved_SinglePlayer() && AiImproved_Settings().log;
}

bool AiImproved_ComputerPlaysLocalFaction()
{
  return AiImproved_SinglePlayer() && AiImproved_Settings().spectate;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Persistent state (saved with the game) and the transient memories                                            */

/* The improved AI keeps its saved state in the 16 bytes GameFactionRuntimeRecord.reserved78_87 of faction record
   0 (the neutral faction, never an AI): the original never touches them, and the faction image is written to and
   read from the savegame as a whole (daten.hex). */
static constexpr uint32_t AI_IMPROVED_STATE_MAGIC = 0x314B4941; /* "AIK1" */
struct AiImprovedPersistentState {
  uint32_t magic;
  uint32_t elapsedTicks;     /* AI game clock */
  uint32_t graceEndedMask;   /* bit f: the grace period of faction f has ended early */
  uint32_t graceNoticeMask;  /* bit f: the end of faction f's grace period was logged */
};
static_assert(sizeof(AiImprovedPersistentState) == sizeof(((GameFactionRuntimeRecord *)nullptr)->reserved78_87),
              "the improved AI state fills the reserved bytes of faction record 0");

static AiImprovedPersistentState *AiImproved_RawState()
{
  return reinterpret_cast<AiImprovedPersistentState *>(g_GameFactionRuntimeImage.records[0].reserved78_87);
}

static AiImprovedPersistentState *AiImproved_State()
{
  AiImprovedPersistentState *state = AiImproved_RawState();
  if (state->magic != AI_IMPROVED_STATE_MAGIC) {
    state->magic = AI_IMPROVED_STATE_MAGIC;
    state->elapsedTicks = 0;
    state->graceEndedMask = 0;
    state->graceNoticeMask = 0;
  }
  return state;
}

uint32_t AiImproved_GameTicks()
{
  return AiImproved_State()->elapsedTicks;
}

/* Signed tick difference a - b (the clock may wrap after 6.8 years of game time). */
static int32_t AiImproved_TicksSince(uint32_t now,uint32_t since)
{
  return (int32_t)(now - since);
}

struct AiImprovedUnitMemory {
  uint32_t valid;
  uint32_t lastMovedTick;
  Q12 lastX;
  Q12 lastY;
  uint32_t trackedTargetSlotPlusOne;
  uint32_t trackedSinceTick;               /* last progress against the tracked target */
  Q12 trackedReferenceConditionQ12;        /* the target's condition at that progress */
  /* targets the unit gave up (it alternated between two it could not hurt from where it stood) */
  uint32_t ignoredTargetSlotPlusOne[3];
  uint32_t ignoreUntilTick[3];
  AiImprovedPioneerMemory pioneer;
  /* improved AI: the guard post the idle unit was last sent to, when, and whether it stopped short of it for good */
  Q12 guardPostX;
  Q12 guardPostY;
  uint32_t guardOrderTick;
  uint32_t guardSettled;
};

/* Not saved: after loading a game the tracking simply starts again. */
static AiImprovedUnitMemory g_AiImprovedUnitMemory[ARMY_RUNTIME_SLOT_COUNT];

/* Response point of an alarm anchor (0 = primary: a building was hit, 1 = secondary: a unit was hit): where the
   shooter stood when it hit from afar. */
struct AiImprovedResponsePoint {
  uint32_t valid;
  Q12 x;
  Q12 y;
  uint32_t untilTick;
};

struct AiImprovedFactionMemory {
  uint32_t ignoredSlotPlusOne[AI_IMPROVED_FACTION_IGNORED_TARGETS];
  uint32_t ignoredUntilTick[AI_IMPROVED_FACTION_IGNORED_TARGETS];
  /* targets nothing of the faction could hurt from weapon range (see AI_IMPROVED_NO_DAMAGE_TARGETS) */
  uint32_t noDamageSlotPlusOne[AI_IMPROVED_NO_DAMAGE_TARGETS];
  uint32_t noDamageReporterPlusOne[AI_IMPROVED_NO_DAMAGE_TARGETS]; /* the unit of the last report */
  uint32_t noDamageReports[AI_IMPROVED_NO_DAMAGE_TARGETS];         /* different units that reported it */
  uint32_t noDamageUntilTick[AI_IMPROVED_NO_DAMAGE_TARGETS];
  uint32_t corePositionCount;
  Q12 coreX[AI_IMPROVED_CORE_POSITIONS];
  Q12 coreY[AI_IMPROVED_CORE_POSITIONS];
  uint32_t lastGroupAttackTick;
  AiImprovedResponsePoint response[2];
  /* fastest movementSpeed of the hostile units of each target class the faction has had in sight */
  int32_t seenClassSpeed[8];
  /* bridge rule: the deposit the storage chain grows toward, its storage buildings so far, failed deposits */
  uint32_t bridgeValid;
  Q12 bridgeX;
  Q12 bridgeY;
  uint32_t bridgeAssetId;
  uint32_t bridgeSteps;
  Q12 bridgeFailedX[AI_IMPROVED_BRIDGE_FAILED_SITES];
  Q12 bridgeFailedY[AI_IMPROVED_BRIDGE_FAILED_SITES];
  uint32_t bridgeFailedUntilTick[AI_IMPROVED_BRIDGE_FAILED_SITES];
  uint32_t bridgeFailedCount[AI_IMPROVED_BRIDGE_FAILED_SITES];     /* attempts that led nowhere */
  uint32_t bridgeFailedForGood[AI_IMPROVED_BRIDGE_FAILED_SITES];   /* given up for the rest of the game */
  /* since when no site brought the chain closer (0 = progress; the zone needs a moment to grow after a building) */
  uint32_t bridgeNoProgressSince;
  /* "no Xenite mine" was logged and no mine has stood since */
  uint32_t xeniteRecoveryLogged;
  /* protocol: game tick of the last status line, group attacks since then */
  uint32_t lastStatusTick;
  uint32_t attacksSinceStatus;
  /* places the faction's groups could not reach, until when they are avoided */
  Q12 unreachableX[AI_IMPROVED_UNREACHABLE_AREAS];
  Q12 unreachableY[AI_IMPROVED_UNREACHABLE_AREAS];
  uint32_t unreachableUntilTick[AI_IMPROVED_UNREACHABLE_AREAS];
  /* game tick since which the faction's buildings are under alarm without a break (primaryAnchorCooldown != 0),
     0 = no alarm; whether building during the alarm was logged */
  uint32_t alarmSinceTick;
  uint32_t alarmBuildLogged;
  /* when a pioneer vehicle was last bought for a far deposit (cooldown, so a site nobody can reach does not
     swallow one pioneer after the other) */
  uint32_t pioneerBoostTick;
  /* defence mode: until when it lasts (0 = off) and the strengths of the sighting that started it */
  uint32_t threatUntilTick;
  uint32_t threatSeenUnits;
  uint32_t threatSeenStrength;
  uint32_t threatOwnStrength;
  /* protocol: the reserve power plant, reserve headquarters and missing heavy factory were logged */
  uint32_t reservePowerLogged;
  uint32_t reserveHeadquartersLogged;
  uint32_t heavyFactoryLogged;
  /* protocol: airport proposed; aircraft / laser coils proposed (own count + 1 when last logged) */
  uint32_t airportLogged;
  uint32_t aircraftLogged;
  uint32_t laserCoilLogged;
  /* the last air strike (AiImproved_LaunchAircraft) */
  uint32_t aircraftLaunchValid;
  uint32_t aircraftLaunchTick;
  /* the last aircraft bought (AI_IMPROVED_AIRCRAFT_PURCHASE_TICKS) */
  uint32_t aircraftPurchaseValid;
  uint32_t aircraftPurchaseTick;
};

static AiImprovedFactionMemory g_AiImprovedFactionMemory[8];

/* protocol: the knowledge data's purchase weights and the first armoured hit were logged this session */
static bool g_AiImprovedWeightsLogged = false;
static bool g_AiImprovedArmorLogged = false;

/* Foreign buildings of the faction whose planning pass runs (AiImproved_BeginFactionPlanning). */
static Q12 g_AiImprovedForeignX[AI_IMPROVED_FOREIGN_STRUCTURES];
static Q12 g_AiImprovedForeignY[AI_IMPROVED_FOREIGN_STRUCTURES];
static int64_t g_AiImprovedForeignRadiusSquared[AI_IMPROVED_FOREIGN_STRUCTURES];
static uint32_t g_AiImprovedForeignCount = 0;
static bool g_AiImprovedForeignValid = false;
/* Buildings of the planning faction itself: a place counts as foreign only where a foreign building is nearer. */
static Q12 g_AiImprovedOwnStructureX[AI_IMPROVED_FOREIGN_STRUCTURES];
static Q12 g_AiImprovedOwnStructureY[AI_IMPROVED_FOREIGN_STRUCTURES];
static uint32_t g_AiImprovedOwnStructureCount = 0;
/* Buildings of the human factions (refreshed in every planning pass): the grace period's base test compares a
   human object's distance to the AI base with its distance to the human's own base. */
static Q12 g_AiImprovedHumanStructureX[AI_IMPROVED_FOREIGN_STRUCTURES];
static Q12 g_AiImprovedHumanStructureY[AI_IMPROVED_FOREIGN_STRUCTURES];
static int8_t g_AiImprovedHumanStructureFaction[AI_IMPROVED_FOREIGN_STRUCTURES];
static int16_t g_AiImprovedHumanStructureArmySlot[AI_IMPROVED_FOREIGN_STRUCTURES];
static uint32_t g_AiImprovedHumanStructureCount = 0;
static bool g_AiImprovedHumanStructuresValid = false;

/* ------------------------------------------------------------------------------------------------------------ */
/* Log                                                                                                          */

void AiImproved_Log(const char *format, ...)
{
  if (!AiImproved_LogEnabled()) {
    return;
  }
  char buffer[320];
  va_list arguments;
  va_start(arguments,format);
  vsnprintf(buffer,sizeof buffer,format,arguments);
  va_end(arguments);
  uint32_t seconds = AiImproved_GameTicks() / AI_IMPROVED_TICKS_PER_SECOND;
  Thandor_Log("KI %u:%02u %s",seconds / 60,seconds % 60,buffer);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Helpers                                                                                                      */

bool AiImproved_IsHumanFaction(int factionIndex)
{
  if (factionIndex <= 0 || factionIndex > 7 || g_FrontendPlayerRuntimeBlocks == nullptr) {
    return false;
  }
  if (AiImproved_ComputerPlaysLocalFaction()) {
    return false; /* "zuschauen": every faction is computer controlled */
  }
  uint32_t count = g_FrontendPlayerRuntimeBlockCount;
  if (count > FRONTEND_PLAYER_RUNTIME_RECORD_ALLOC_COUNT) count = FRONTEND_PLAYER_RUNTIME_RECORD_ALLOC_COUNT;
  for (uint32_t i = 0; i < count; i++) {
    if ((int)g_FrontendPlayerRuntimeBlocks[i].factionAssignment.factionAssignmentIndex == factionIndex) {
      return true;
    }
  }
  return false;
}

bool AiImproved_IsAiFaction(int factionIndex)
{
  if (factionIndex < 1 || factionIndex > 7) {
    return false;
  }
  if (g_GameFactionRuntimeImage.tail.factionLifecycleStates[factionIndex] != FACTION_RUNTIME_LIFECYCLE_ACTIVE) {
    return false;
  }
  return !AiImproved_IsHumanFaction(factionIndex);
}

static int AiImproved_SlotIndex(const ArmyRuntimeSlot *armyRuntime)
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

static bool AiImproved_ArmyPosition(const ArmyRuntimeSlot *armyRuntime,Q12 *outX,Q12 *outY)
{
  if (armyRuntime == nullptr || armyRuntime->modelNodeRuntime == nullptr) {
    return false;
  }
  *outX = (armyRuntime->modelNodeRuntime->worldTransform).translation.x;
  *outY = (armyRuntime->modelNodeRuntime->worldTransform).translation.y;
  return true;
}

static int64_t AiImproved_DistanceSquared(Q12 ax,Q12 ay,Q12 bx,Q12 by)
{
  int64_t dx = (int64_t)ax - (int64_t)bx;
  int64_t dy = (int64_t)ay - (int64_t)by;
  return dx * dx + dy * dy;
}

int64_t AiImproved_DistanceSquaredQ12(Q12 ax,Q12 ay,Q12 bx,Q12 by)
{
  return AiImproved_DistanceSquared(ax,ay,bx,by);
}

static int64_t AiImproved_ManhattanQ12(Q12 ax,Q12 ay,Q12 bx,Q12 by)
{
  int64_t dx = (int64_t)ax - (int64_t)bx;
  int64_t dy = (int64_t)ay - (int64_t)by;
  if (dx < 0) dx = -dx;
  if (dy < 0) dy = -dy;
  return dx + dy;
}

static ModelDefinition *AiImproved_RootDefinition(const ArmyRuntimeSlot *armyRuntime)
{
  if (armyRuntime == nullptr) {
    return nullptr;
  }
  ModelRuntimeSlot *modelRuntime = (armyRuntime->modelRuntimeOrSavedOffset).modelRuntime;
  if (modelRuntime == nullptr) {
    return nullptr;
  }
  return modelRuntime->definitionOrSavedId.runtimeDefinition;
}

/* The army has a weapon that damages some target class (repair modules count negative). */
static bool AiImproved_ArmyArmed(const ArmyRuntimeSlot *armyRuntime)
{
  for (int targetClass = 0; targetClass < 8; targetClass++) {
    if (armyRuntime->targetClassShotDamage[targetClass] > 0) {
      return true;
    }
  }
  return false;
}

static Q12 AiImproved_ConditionQ12(ArmyRuntimeSlot *armyRuntime)
{
  return ModelRuntime_QueryHierarchyConditionRatioQ12(reinterpret_cast<RuntimeModelFactionPrefix *>(armyRuntime)); /* the army slot header as the hierarchy metric view */
}

static AiImprovedUnitMemory *AiImproved_UnitMemory(ArmyRuntimeSlot *armyRuntime)
{
  int index = AiImproved_SlotIndex(armyRuntime);
  if (index < 0) {
    return nullptr;
  }
  AiImprovedUnitMemory *memory = &g_AiImprovedUnitMemory[index];
  if (!memory->valid) {
    memset(memory,0,sizeof *memory);
    memory->valid = 1;
    memory->lastMovedTick = AiImproved_GameTicks();
    AiImproved_ArmyPosition(armyRuntime,&memory->lastX,&memory->lastY);
  }
  return memory;
}

/* The unit's memory with its movement noted: lastMovedTick is the last time it was seen more than
   AI_IMPROVED_MOVE_EPSILON_Q12 away from the position recorded then. */
static AiImprovedUnitMemory *AiImproved_NoteMovement(ArmyRuntimeSlot *armyRuntime)
{
  AiImprovedUnitMemory *memory = AiImproved_UnitMemory(armyRuntime);
  Q12 unitX;
  Q12 unitY;
  if (memory == nullptr || !AiImproved_ArmyPosition(armyRuntime,&unitX,&unitY)) {
    return memory;
  }
  if (AiImproved_DistanceSquared(unitX,unitY,memory->lastX,memory->lastY) >
      AI_IMPROVED_MOVE_EPSILON_Q12 * AI_IMPROVED_MOVE_EPSILON_Q12) {
    memory->lastX = unitX;
    memory->lastY = unitY;
    memory->lastMovedTick = AiImproved_GameTicks();
  }
  return memory;
}

/* Game ticks since the unit last moved (0 without memory). */
static int32_t AiImproved_TicksStandingStill(ArmyRuntimeSlot *armyRuntime)
{
  AiImprovedUnitMemory *memory = AiImproved_NoteMovement(armyRuntime);
  if (memory == nullptr) {
    return 0;
  }
  return AiImproved_TicksSince(AiImproved_GameTicks(),memory->lastMovedTick);
}

int32_t AiImproved_UnitTicksStandingStill(ArmyRuntimeSlot *armyRuntime)
{
  return AiImproved_TicksStandingStill(armyRuntime);
}

void AiImproved_RestartMovementClock(ArmyRuntimeSlot *armyRuntime)
{
  AiImprovedUnitMemory *memory = AiImproved_UnitMemory(armyRuntime);
  if (memory == nullptr) {
    return;
  }
  memory->lastMovedTick = AiImproved_GameTicks();
  AiImproved_ArmyPosition(armyRuntime,&memory->lastX,&memory->lastY);
}

/* The army fights a unit or building of another faction (an attack order on a live target). */
static bool AiImproved_ArmyFighting(const ArmyRuntimeSlot *armyRuntime)
{
  const ArmyRuntimeSlot *target = armyRuntime->commandTargetArmyRuntime;
  return Any(armyRuntime->commandModeFlags & ARMY_COMMAND_MODE_TARGET_ARMY) && target != nullptr &&
         target->factionIndex != armyRuntime->factionIndex;
}

/* A core building (ARM 300..339: headquarters, factories, power plants, labs, mines, silos) of the faction lies
   within radius of the point; the positions come from the faction's last planning pass. */
static bool AiImproved_NearOwnCore(int factionIndex,Q12 worldX,Q12 worldY,int64_t radiusQ12)
{
  if (factionIndex < 0 || factionIndex > 7) {
    return false;
  }
  const AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
  int64_t radiusSquared = radiusQ12 * radiusQ12;
  for (uint32_t i = 0; i < memory->corePositionCount; i++) {
    if (AiImproved_DistanceSquared(worldX,worldY,memory->coreX[i],memory->coreY[i]) <= radiusSquared) {
      return true;
    }
  }
  return false;
}

static bool AiImproved_FactionIgnoresTarget(int factionIndex,int targetSlot)
{
  if (factionIndex < 0 || factionIndex > 7 || targetSlot < 0) {
    return false;
  }
  const AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
  uint32_t now = AiImproved_GameTicks();
  for (int i = 0; i < AI_IMPROVED_FACTION_IGNORED_TARGETS; i++) {
    if (memory->ignoredSlotPlusOne[i] == (uint32_t)targetSlot + 1 &&
        AiImproved_TicksSince(memory->ignoredUntilTick[i],now) > 0) {
      return true;
    }
  }
  return false;
}

static void AiImproved_FactionIgnoreTarget(int factionIndex,int targetSlot,uint32_t untilTick)
{
  if (factionIndex < 0 || factionIndex > 7 || targetSlot < 0) {
    return;
  }
  AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
  uint32_t now = AiImproved_GameTicks();
  int chosen = 0;
  for (int i = 0; i < AI_IMPROVED_FACTION_IGNORED_TARGETS; i++) {
    if (memory->ignoredSlotPlusOne[i] == (uint32_t)targetSlot + 1 ||
        memory->ignoredSlotPlusOne[i] == 0 || AiImproved_TicksSince(memory->ignoredUntilTick[i],now) <= 0) {
      chosen = i;
      break;
    }
    /* all in use: replace the one that runs out first */
    if (AiImproved_TicksSince(memory->ignoredUntilTick[i],memory->ignoredUntilTick[chosen]) < 0) {
      chosen = i;
    }
  }
  memory->ignoredSlotPlusOne[chosen] = (uint32_t)targetSlot + 1;
  memory->ignoredUntilTick[chosen] = untilTick;
}

/* The faction has learned that nothing of it hurts this target from weapon range (enough different units reported
   it, see AI_IMPROVED_NO_DAMAGE_TARGETS). */
static bool AiImproved_FactionCannotHurt(int factionIndex,int targetSlot)
{
  if (factionIndex < 0 || factionIndex > 7 || targetSlot < 0) {
    return false;
  }
  const AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
  uint32_t now = AiImproved_GameTicks();
  for (int i = 0; i < AI_IMPROVED_NO_DAMAGE_TARGETS; i++) {
    if (memory->noDamageSlotPlusOne[i] == (uint32_t)targetSlot + 1 &&
        memory->noDamageReports[i] >= AI_IMPROVED_NO_DAMAGE_REPORTS &&
        AiImproved_TicksSince(memory->noDamageUntilTick[i],now) > 0) {
      return true;
    }
  }
  return false;
}

/* A unit gave the target up although it stood within weapon range: notes the report and returns true when this
   report makes the faction leave the target alone (for the log). */
static bool AiImproved_NoteTargetWithoutDamage(int factionIndex,int targetSlot,int reporterSlot)
{
  if (factionIndex < 0 || factionIndex > 7 || targetSlot < 0) {
    return false;
  }
  AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
  uint32_t now = AiImproved_GameTicks();
  int chosen = -1;
  for (int i = 0; i < AI_IMPROVED_NO_DAMAGE_TARGETS; i++) {
    if (memory->noDamageSlotPlusOne[i] == (uint32_t)targetSlot + 1 &&
        AiImproved_TicksSince(memory->noDamageUntilTick[i],now) > 0) {
      chosen = i;
      break;
    }
  }
  if (chosen < 0) {
    /* a free entry, else the one that runs out first */
    chosen = 0;
    for (int i = 0; i < AI_IMPROVED_NO_DAMAGE_TARGETS; i++) {
      if (memory->noDamageSlotPlusOne[i] == 0 || AiImproved_TicksSince(memory->noDamageUntilTick[i],now) <= 0) {
        chosen = i;
        break;
      }
      if (AiImproved_TicksSince(memory->noDamageUntilTick[i],memory->noDamageUntilTick[chosen]) < 0) {
        chosen = i;
      }
    }
    memory->noDamageSlotPlusOne[chosen] = (uint32_t)targetSlot + 1;
    memory->noDamageReporterPlusOne[chosen] = 0;
    memory->noDamageReports[chosen] = 0;
  }
  bool reached = false;
  if (memory->noDamageReporterPlusOne[chosen] != (uint32_t)reporterSlot + 1) {
    memory->noDamageReporterPlusOne[chosen] = (uint32_t)reporterSlot + 1;
    memory->noDamageReports[chosen]++;
    reached = memory->noDamageReports[chosen] == AI_IMPROVED_NO_DAMAGE_REPORTS;
  }
  uint32_t until = now + AI_IMPROVED_NO_DAMAGE_TICKS;
  memory->noDamageUntilTick[chosen] = until == 0 ? 1 : until;
  return reached;
}

/* A new army in this slot: the faction memories must forget what the slot's previous army was (a target the
   faction gave up must not block the new one). */
static void AiImproved_ForgetFactionTarget(int targetSlot)
{
  if (targetSlot < 0) {
    return;
  }
  for (int faction = 0; faction <= 7; faction++) {
    AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[faction];
    for (int i = 0; i < AI_IMPROVED_FACTION_IGNORED_TARGETS; i++) {
      if (memory->ignoredSlotPlusOne[i] == (uint32_t)targetSlot + 1) {
        memory->ignoredSlotPlusOne[i] = 0;
        memory->ignoredUntilTick[i] = 0;
      }
    }
    for (int i = 0; i < AI_IMPROVED_NO_DAMAGE_TARGETS; i++) {
      if (memory->noDamageSlotPlusOne[i] == (uint32_t)targetSlot + 1) {
        memory->noDamageSlotPlusOne[i] = 0;
        memory->noDamageReporterPlusOne[i] = 0;
        memory->noDamageReports[i] = 0;
        memory->noDamageUntilTick[i] = 0;
      }
    }
  }
}

static bool AiImproved_InUnreachableArea(int factionIndex,Q12 worldX,Q12 worldY)
{
  if (factionIndex < 0 || factionIndex > 7) {
    return false;
  }
  const AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
  uint32_t now = AiImproved_GameTicks();
  for (int i = 0; i < AI_IMPROVED_UNREACHABLE_AREAS; i++) {
    if (memory->unreachableUntilTick[i] != 0 && AiImproved_TicksSince(memory->unreachableUntilTick[i],now) > 0 &&
        AiImproved_DistanceSquared(worldX,worldY,memory->unreachableX[i],memory->unreachableY[i]) <=
        AI_IMPROVED_UNREACHABLE_RADIUS_Q12 * AI_IMPROVED_UNREACHABLE_RADIUS_Q12) {
      return true;
    }
  }
  return false;
}

/* A group unit stood still out of reach of its target: the place is avoided for a while (an unreachable base,
   e.g. on a plateau or across water, would otherwise draw one group after the other). */
static void AiImproved_NoteUnreachableArea(int factionIndex,Q12 worldX,Q12 worldY)
{
  if (factionIndex < 0 || factionIndex > 7 || AiImproved_InUnreachableArea(factionIndex,worldX,worldY)) {
    return;
  }
  AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
  uint32_t now = AiImproved_GameTicks();
  int chosen = 0;
  for (int i = 0; i < AI_IMPROVED_UNREACHABLE_AREAS; i++) {
    if (memory->unreachableUntilTick[i] == 0 || AiImproved_TicksSince(memory->unreachableUntilTick[i],now) <= 0) {
      chosen = i;
      break;
    }
    if (AiImproved_TicksSince(memory->unreachableUntilTick[i],memory->unreachableUntilTick[chosen]) < 0) {
      chosen = i;
    }
  }
  memory->unreachableX[chosen] = worldX;
  memory->unreachableY[chosen] = worldY;
  uint32_t until = now + AI_IMPROVED_UNREACHABLE_TICKS;
  memory->unreachableUntilTick[chosen] = until == 0 ? 1 : until;
  AiImproved_Log("Partei %d: Ziele bei %d/%d nicht erreichbar, 10 Minuten keine Angriffe dorthin",factionIndex,
                 worldX / Q12_ONE,worldY / Q12_ONE);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Defence mode (improved AI): react to a scouted mass attack                                                   */

/* Strength of a unit for the comparison: the maximum health of its root definition (a Dingo counts 3000, a
   Goliath about seven times that), 0 for anything that cannot move. */
static uint32_t AiImproved_UnitStrength(ModelRuntimeSlot *modelRuntime)
{
  if (modelRuntime == nullptr) {
    return 0;
  }
  ModelDefinition *definition = modelRuntime->definitionOrSavedId.runtimeDefinition;
  if (definition == nullptr || definition->accelerationPerTick == 0) {
    return 0; /* buildings */
  }
  return (uint32_t)definition->maximumHealth;
}

/* Compares what the faction has seen of hostile armies (workspace 02: in sight now, never the unseen workspace 03)
   with its own units, and switches the faction to defence for AI_IMPROVED_THREAT_TICKS when the hostiles are
   AI_IMPROVED_THREAT_FACTOR times stronger. Called once per planning pass. */
static void AiImproved_UpdateThreatAssessment(FactionRuntimeIndex factionIndex)
{
  if (!AiImproved_IsActive() || factionIndex < 1 || factionIndex > 7) {
    return;
  }
  AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
  uint32_t now = AiImproved_GameTicks();
  uint32_t seenUnits = 0;
  uint32_t seenStrength = 0;
  AiRuntimeWorkspaceEntry *seenEntry = g_AiWorkspace02VisibleHostiles;
  for (uint32_t i = 0; i < g_AiWorkspace02Count; i++, seenEntry++) {
    uint32_t strength = AiImproved_UnitStrength(seenEntry->modelRuntime);
    if (strength != 0) {
      seenUnits++;
      seenStrength += strength;
    }
  }
  uint32_t ownUnits = 0;
  uint32_t ownStrength = 0;
  AiRuntimeWorkspaceEntry *ownEntry = g_AiWorkspace01Units;
  for (uint32_t i = 0; i < g_AiWorkspace01Count; i++, ownEntry++) {
    uint32_t strength = AiImproved_UnitStrength(ownEntry->modelRuntime);
    if (strength != 0) {
      ownUnits++;
      ownStrength += strength;
    }
  }
  /* Either measure may raise the alarm: a swarm of cheap light vehicles beats the AI on numbers long before it
     beats it on armour, a few heavy walkers the other way round. */
  bool outnumbered = (seenUnits >= AI_IMPROVED_THREAT_MIN_UNITS) &&
                     (((uint64_t)seenStrength >= (uint64_t)ownStrength * AI_IMPROVED_THREAT_FACTOR) ||
                      ((uint64_t)seenUnits >= (uint64_t)ownUnits * AI_IMPROVED_THREAT_FACTOR));
  if (!outnumbered) {
    if (memory->threatUntilTick != 0 && AiImproved_TicksSince(memory->threatUntilTick,now) <= 0) {
      memory->threatUntilTick = 0;
      AiImproved_Log("Partei %d: Abwehr beendet, keine Uebermacht mehr in Sicht",factionIndex);
    }
    return;
  }
  bool entering = memory->threatUntilTick == 0 || AiImproved_TicksSince(memory->threatUntilTick,now) <= 0;
  uint32_t until = now + AI_IMPROVED_THREAT_TICKS;
  memory->threatUntilTick = until == 0 ? 1 : until;
  memory->threatSeenUnits = seenUnits;
  memory->threatSeenStrength = seenStrength;
  memory->threatOwnStrength = ownStrength;
  if (entering) {
    AiImproved_Log("Partei %d: Abwehr! %u gegnerische Einheiten gesehen (Panzerung %u) gegen eigene %u (%u), mehr "
                   "Tuerme, keine Angriffe",factionIndex,seenUnits,seenStrength,ownUnits,ownStrength);
  }
}

bool AiImproved_DefenceMode(FactionRuntimeIndex factionIndex)
{
  if (!AiImproved_IsActive() || factionIndex < 1 || factionIndex > 7) {
    return false;
  }
  const AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
  return memory->threatUntilTick != 0 &&
         AiImproved_TicksSince(memory->threatUntilTick,AiImproved_GameTicks()) > 0;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Session hooks                                                                                                */

/* Attack orders held back for a later wave (AiImproved_QueueGroupOrder). Not saved: after loading a game the
   held units simply have no order and are freed like any idle group unit. */
struct AiImprovedQueuedOrder {
  ArmyRuntimeSlot *army;
  ModelRuntimeSlot *armyModel;        /* to tell a slot that was freed and taken again */
  GameEntityRuntime *target;          /* the target army as the entity the order takes */
  ModelRuntimeSlot *targetModel;
  uint32_t releaseTick;
};
static constexpr int AI_IMPROVED_QUEUED_ORDER_CAPACITY = 64;
static AiImprovedQueuedOrder g_AiImprovedQueuedOrders[AI_IMPROVED_QUEUED_ORDER_CAPACITY];
static int g_AiImprovedQueuedOrderCount = 0;

void AiImproved_OnSessionStart(bool loadedFromSave)
{
  AiImproved_LoadSettings();
  memset(g_AiImprovedUnitMemory,0,sizeof g_AiImprovedUnitMemory);
  memset(g_AiImprovedFactionMemory,0,sizeof g_AiImprovedFactionMemory);
  g_AiImprovedQueuedOrderCount = 0;
  g_AiImprovedWeightsLogged = false;
  g_AiImprovedArmorLogged = false;
  g_AiImprovedForeignCount = 0;
  g_AiImprovedForeignValid = false;
  g_AiImprovedOwnStructureCount = 0;
  g_AiImprovedHumanStructureCount = 0;
  g_AiImprovedHumanStructuresValid = false;
  if (!AiImproved_SinglePlayer()) {
    Thandor_Log("KI: Netzwerkspiel, Original-KI ohne Reparaturen und Schonfrist");
    return;
  }
  const AiImprovedSettings &settings = AiImproved_Settings();
  Thandor_Log("KI: Stufe %d (%s), Reparaturen %s, Schonfrist %d Minuten, Protokoll %s, Ausweichen %s, Aufstellung %s, "
              "Aufzeichnung %s, Wellen %s, dicht bauen %s%s",
              settings.level,g_AiImprovedLevelParameters[settings.level].name,settings.repairs ? "an" : "aus",
              AiImproved_GraceMinutes(),settings.log ? "an" : "aus",AiImproved_EvasionActive() ? "an" : "aus",
              AiImproved_FormationActive() ? "an" : "aus",AiImproved_RecordActive() ? "an" : "aus",
              settings.waves ? "an" : "aus",settings.tightBuilding ? "an" : "aus",
              settings.spectate ? ", Zuschauen an (KI spielt alle Parteien)" : "");
  if (!AiImproved_AnyActive()) {
    /* the original AI: the faction image stays untouched */
    return;
  }
  AiImprovedPersistentState *state = AiImproved_RawState();
  if (!loadedFromSave) {
    state->magic = AI_IMPROVED_STATE_MAGIC;
    state->elapsedTicks = 0;
    state->graceEndedMask = 0;
    state->graceNoticeMask = 0;
  }
  else if (state->magic != AI_IMPROVED_STATE_MAGIC) {
    /* A game saved without the improved AI (the original game, or this one with the original AI): its AI clock
       starts at the game's own step counter, which runs one step per tick at normal speed, so the grace period
       counts from the start of the mission as in a new game. (Before, such a save had no grace period at all,
       and the player's saves from the start of a mission were attacked at once.) A save made with a faster
       game speed gets a little more grace than it should, never less. */
    state->magic = AI_IMPROVED_STATE_MAGIC;
    state->elapsedTicks = g_GameFactionRuntimeImage.tail.simulationTick;
    state->graceEndedMask = 0;
    state->graceNoticeMask = 0;
    Thandor_Log("KI: Spielstand ohne KI-Zustand, Spielzeit nach dem Spielstand %u:%02u, Schonfrist zaehlt ab Missionsbeginn",
                state->elapsedTicks / AI_IMPROVED_TICKS_PER_SECOND / 60,
                state->elapsedTicks / AI_IMPROVED_TICKS_PER_SECOND % 60);
  }
}


static bool AiImproved_LighterThan(const ArmyRuntimeSlot *a,const ArmyRuntimeSlot *b)
{
  const ModelDefinition *da = AiImproved_RootDefinition(a);
  const ModelDefinition *db = AiImproved_RootDefinition(b);
  if (da == nullptr || db == nullptr) {
    return da != nullptr;
  }
  if (da->movementSpeed != db->movementSpeed) {
    return da->movementSpeed > db->movementSpeed;
  }
  return da->maximumHealth < db->maximumHealth;
}

void AiImproved_SortGroupLightFirst(ArmyRuntimeSlot **collectedArmies,uint32_t collectedCount)
{
  /* insertion sort, stable: units of the same kind keep the order the original collected them in */
  for (uint32_t i = 1; i < collectedCount; i++) {
    ArmyRuntimeSlot *unit = collectedArmies[i];
    uint32_t j = i;
    while (j > 0 && AiImproved_LighterThan(unit,collectedArmies[j - 1])) {
      collectedArmies[j] = collectedArmies[j - 1];
      j--;
    }
    collectedArmies[j] = unit;
  }
}

uint32_t AiImproved_AttackWaveSize(uint32_t groupSize)
{
  uint32_t size = (groupSize + AI_IMPROVED_ATTACK_WAVE_COUNT - 1) / AI_IMPROVED_ATTACK_WAVE_COUNT;
  return size < AI_IMPROVED_ATTACK_WAVE_SIZE ? AI_IMPROVED_ATTACK_WAVE_SIZE : size;
}

uint32_t AiImproved_AttackWaveDelay(uint32_t orderIndex,uint32_t groupSize)
{
  /* ki.ini "wellen = aus" (or OPEN_THANDOR_AI_WAVES=0) sends the whole group at once as before */
  if (!AiImproved_Settings().waves) {
    return 0;
  }
  return orderIndex / AiImproved_AttackWaveSize(groupSize) * AI_IMPROVED_ATTACK_WAVE_TICKS;
}

bool AiImproved_QueueGroupOrder(ArmyRuntimeSlot *armyRuntime,GameEntityRuntime *targetRuntime,
                                ModelRuntimeSlot *targetModelRuntime,uint32_t delayTicks)
{
  if (armyRuntime == nullptr || targetRuntime == nullptr || targetModelRuntime == nullptr ||
      g_AiImprovedQueuedOrderCount >= AI_IMPROVED_QUEUED_ORDER_CAPACITY) {
    return false;
  }
  AiImprovedQueuedOrder &order = g_AiImprovedQueuedOrders[g_AiImprovedQueuedOrderCount++];
  order.army = armyRuntime;
  order.armyModel = (armyRuntime->modelRuntimeOrSavedOffset).modelRuntime;
  order.target = targetRuntime;
  order.targetModel = targetModelRuntime;
  order.releaseTick = AiImproved_GameTicks() + delayTicks;
  return true;
}

/* The held orders whose wave is due: the same order the group's first wave got (combat.cpp, send_group). */
static void AiImproved_ReleaseQueuedOrders()
{
  uint32_t now = AiImproved_GameTicks();
  int index = 0;
  while (index < g_AiImprovedQueuedOrderCount) {
    AiImprovedQueuedOrder order = g_AiImprovedQueuedOrders[index];
    if ((int32_t)(now - order.releaseTick) < 0) {
      index++;
      continue;
    }
    g_AiImprovedQueuedOrders[index] = g_AiImprovedQueuedOrders[--g_AiImprovedQueuedOrderCount];
    ArmyRuntimeSlot *army = order.army;
    ArmyRuntimeSlot *targetArmy = reinterpret_cast<ArmyRuntimeSlot *>(order.target); /* the target entity is its army slot */
    /* the unit still alive, the same one, still in this group with this target */
    if (army->modelNodeRuntime == nullptr || (army->modelRuntimeOrSavedOffset).modelRuntime != order.armyModel ||
        !Any(army->aiUnitFlags & AI_UNIT_STATE94_GROUP_ASSIGNED) ||
        army->assignedTargetArmyRuntime != Thandor_PointerToU32(order.target)) {
      continue;
    }
    /* the target still alive */
    if (targetArmy->modelNodeRuntime == nullptr ||
        (targetArmy->modelRuntimeOrSavedOffset).modelRuntime != order.targetModel || order.targetModel->health == 0) {
      continue;
    }
    army->movementStateFlags = army->movementStateFlags & ~ARMY_MOVEMENT_ROUTED;
    reinterpret_cast<ArmyMovementRuntime *>(army)->retryCountdown = 0; /* the army slot as its movement view, as in units.cpp */
    ArmyRuntime_ResolveCommandTargetAndRoute(order.target,army);
    army->movementStateFlags = army->movementStateFlags & ~ARMY_MOVEMENT_ROUTED;
    AiImproved_RestartMovementClock(army);
  }
}

void AiImproved_OnSimulationStep(uint32_t stepTicks)
{
  if (!AiImproved_AnyActive()) {
    return;
  }
  AiImproved_State()->elapsedTicks += stepTicks;
  if (g_AiImprovedQueuedOrderCount != 0) {
    AiImproved_ReleaseQueuedOrders();
  }
}

void AiImproved_OnArmyCreated(ArmyRuntimeSlot *armyRuntime)
{
  int index = AiImproved_SlotIndex(armyRuntime);
  if (index >= 0) {
    memset(&g_AiImprovedUnitMemory[index],0,sizeof g_AiImprovedUnitMemory[index]);
    AiImproved_ForgetFactionTarget(index);
  }
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Grace period                                                                                                 */

bool AiImproved_GraceActive(int factionIndex)
{
  int minutes = AiImproved_GraceMinutes();
  if (minutes <= 0 || factionIndex < 1 || factionIndex > 7) {
    return false;
  }
  AiImprovedPersistentState *state = AiImproved_State();
  uint32_t factionBit = 1u << factionIndex;
  if ((state->graceEndedMask & factionBit) != 0) {
    return false;
  }
  if (state->elapsedTicks >= (uint32_t)minutes * 60u * AI_IMPROVED_TICKS_PER_SECOND) {
    if ((state->graceNoticeMask & factionBit) == 0) {
      state->graceNoticeMask |= factionBit;
      AiImproved_Log("Partei %d: Schonfrist nach %d Minuten abgelaufen",factionIndex,minutes);
    }
    return false;
  }
  return true;
}

static void AiImproved_EndGrace(int factionIndex,const char *reason)
{
  AiImprovedPersistentState *state = AiImproved_State();
  uint32_t factionBit = 1u << factionIndex;
  state->graceEndedMask |= factionBit;
  state->graceNoticeMask |= factionBit;
  AiImproved_Log("Partei %d: Schonfrist beendet (%s)",factionIndex,reason);
}

/* Improved AI: a shooter that hits an AI object from afar becomes the response point of the alarm anchor the hit
   moved (primary for a building, secondary for a unit). Aircraft are left to the anti-aircraft weapons, towers
   firing at AI units to the units themselves. */
static void AiImproved_NoteDistantShooter(int targetFaction,ArmyRuntimeSlot *targetArmy,ModelDefinition *targetDefinition,
                                          ArmyRuntimeSlot *shooterArmy)
{
  Q12 targetX;
  Q12 targetY;
  Q12 shooterX;
  Q12 shooterY;
  if (!AiImproved_ArmyPosition(targetArmy,&targetX,&targetY) ||
      !AiImproved_ArmyPosition(shooterArmy,&shooterX,&shooterY)) {
    return;
  }
  ModelDefinition *shooterDefinition = AiImproved_RootDefinition(shooterArmy);
  if (shooterDefinition == nullptr || shooterDefinition->runtimeClassId == MODEL_RUNTIME_CLASS_21_AIRCRAFT) {
    return;
  }
  /* a tower firing at the AI's attacking units is the attack's business, not a reason to pull defenders there */
  if (shooterDefinition->accelerationPerTick == 0 && targetDefinition->accelerationPerTick != 0) {
    return;
  }
  if (AiImproved_DistanceSquared(targetX,targetY,shooterX,shooterY) <
      AI_IMPROVED_DISTANT_SHOOTER_Q12 * AI_IMPROVED_DISTANT_SHOOTER_Q12) {
    return;
  }
  uint32_t now = AiImproved_GameTicks();
  AiImprovedResponsePoint *response =
       &g_AiImprovedFactionMemory[targetFaction].response[targetDefinition->accelerationPerTick == 0 ? 0 : 1];
  bool active = response->valid != 0 && AiImproved_TicksSince(response->untilTick,now) > 0;
  bool moved = !active || AiImproved_DistanceSquared(response->x,response->y,shooterX,shooterY) >
                          (int64_t)(8 * (int64_t)Q12_ONE) * (8 * (int64_t)Q12_ONE);
  response->valid = 1;
  response->x = shooterX;
  response->y = shooterY;
  response->untilTick = now + AI_IMPROVED_RESPONSE_TICKS;
  if (moved) {
    AiImproved_Log("Partei %d: Beschuss aus der Ferne (ARM %u bei %d/%d), Verteidiger fahren hin",targetFaction,
                   (unsigned)shooterArmy->armyAssetId,shooterX / Q12_ONE,shooterY / Q12_ONE);
  }
}

void AiImproved_OnHostileHit(ModelRuntimeSlot *targetModelRuntime,ArmyRuntimeSlot *shooterArmy)
{
  if (shooterArmy == nullptr || targetModelRuntime == nullptr || !AiImproved_AnyActive()) {
    return;
  }
  ArmyRuntimeSlot *targetArmy = targetModelRuntime->ownerArmyRuntimeOrSavedOffset.armyRuntime;
  if (targetArmy == nullptr) {
    return;
  }
  int targetFaction = targetArmy->factionIndex;
  if (!AiImproved_IsAiFaction(targetFaction)) {
    return;
  }
  ModelDefinition *targetDefinition = AiImproved_RootDefinition(targetArmy);
  if (targetDefinition == nullptr) {
    return;
  }
  if (AiImproved_IsActive()) {
    AiImproved_NoteDistantShooter(targetFaction,targetArmy,targetDefinition,shooterArmy);
  }
  /* grace period: a human hit on a building ends it (a unit outside its base is no reason) */
  if (AiImproved_GraceMinutes() <= 0 || !AiImproved_IsHumanFaction(shooterArmy->factionIndex) ||
      targetDefinition->accelerationPerTick != 0) {
    return;
  }
  if (AiImproved_GraceActive(targetFaction)) {
    AiImproved_EndGrace(targetFaction,"Gebaeude beschossen");
  }
}

bool AiImproved_ResponsePoint(FactionRuntimeIndex factionIndex,Q12 *outWorldX,Q12 *outWorldY)
{
  if (factionIndex < 1 || factionIndex > 7) {
    return false;
  }
  /* the anchor the defenders are sent to: the primary one while its alarm runs, else the secondary one */
  int kind = g_GameFactionRuntimeImage.records[factionIndex].primaryAnchorCooldown != 0 ? 0 : 1;
  const AiImprovedResponsePoint *response = &g_AiImprovedFactionMemory[factionIndex].response[kind];
  if (response->valid == 0 || AiImproved_TicksSince(response->untilTick,AiImproved_GameTicks()) <= 0) {
    return false;
  }
  *outWorldX = response->x;
  *outWorldY = response->y;
  return true;
}

uint32_t AiImproved_AnchorResponseBiasQ12(uint32_t originalBiasQ12)
{
  uint32_t units = g_AiImprovedLevelParameters[AiImproved_Level()].responseUnits;
  if (units == 0) {
    return originalBiasQ12;
  }
  return units * (uint32_t)Q12_ONE;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Planning hooks                                                                                               */

void AiImproved_BeginFactionPlanning(FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime)
{
  g_AiImprovedForeignCount = 0;
  g_AiImprovedForeignValid = false;
  g_AiImprovedOwnStructureCount = 0;
  if (!AiImproved_AnyActive() || !AiImproved_IsAiFaction(factionIndex)) {
    return;
  }
  uint32_t humanCount = 0;
  for (WorldOwnerListNode *node = worldRuntime->ownerListHead; node != nullptr; node = node->nextNode) {
    if (node->ownerClassId != WORLD_OWNER_RUNTIME_MODEL) {
      continue;
    }
    ModelRuntimeSlot *modelRuntime = WorldOwnerNode_ModelRuntime(node);
    ArmyRuntimeSlot *ownerArmy = modelRuntime->ownerArmyRuntimeOrSavedOffset.armyRuntime;
    if (ownerArmy == nullptr || ownerArmy->factionIndex == 0) {
      continue;
    }
    ModelDefinition *definition = modelRuntime->definitionOrSavedId.runtimeDefinition;
    if (definition->accelerationPerTick != 0 || definition->supportRadius <= 0 ||
        definition->runtimeClassId == MODEL_RUNTIME_CLASS_00 || definition->runtimeClassId == MODEL_RUNTIME_CLASS_12 ||
        definition->runtimeClassId == MODEL_RUNTIME_CLASS_23) {
      continue;
    }
    if (Any(modelRuntime->classState.stateFlags & ARMY_RUNTIME_FLAG_DESTROYED)) {
      continue;
    }
    ModelRuntimeNode *modelNode = modelRuntime->rootModelNodeOrSavedOffset.modelNode;
    Q12 x = (modelNode->worldTransform).translation.x;
    Q12 y = (modelNode->worldTransform).translation.y;
    if (ownerArmy->factionIndex == factionIndex) {
      if (g_AiImprovedOwnStructureCount < (uint32_t)AI_IMPROVED_FOREIGN_STRUCTURES) {
        g_AiImprovedOwnStructureX[g_AiImprovedOwnStructureCount] = x;
        g_AiImprovedOwnStructureY[g_AiImprovedOwnStructureCount] = y;
        g_AiImprovedOwnStructureCount++;
      }
      continue;
    }
    if (AiImproved_IsHumanFaction(ownerArmy->factionIndex) && humanCount < (uint32_t)AI_IMPROVED_FOREIGN_STRUCTURES) {
      g_AiImprovedHumanStructureX[humanCount] = x;
      g_AiImprovedHumanStructureY[humanCount] = y;
      g_AiImprovedHumanStructureFaction[humanCount] = (int8_t)ownerArmy->factionIndex;
      g_AiImprovedHumanStructureArmySlot[humanCount] = (int16_t)AiImproved_SlotIndex(ownerArmy);
      humanCount++;
    }
    if (g_AiImprovedForeignCount < (uint32_t)AI_IMPROVED_FOREIGN_STRUCTURES) {
      int64_t radius = (int64_t)definition->supportRadius + AI_IMPROVED_FOREIGN_MARGIN_Q12;
      g_AiImprovedForeignX[g_AiImprovedForeignCount] = x;
      g_AiImprovedForeignY[g_AiImprovedForeignCount] = y;
      g_AiImprovedForeignRadiusSquared[g_AiImprovedForeignCount] = radius * radius;
      g_AiImprovedForeignCount++;
    }
  }
  g_AiImprovedHumanStructureCount = humanCount;
  g_AiImprovedHumanStructuresValid = true;
  g_AiImprovedForeignValid = AiImproved_RepairsActive();
}

bool AiImproved_InForeignTerritory(Q12 worldX,Q12 worldY)
{
  if (!g_AiImprovedForeignValid) {
    return false;
  }
  /* where the bases touch (several factions start side by side), the nearer base owns the place: a deposit next to
     the own headquarters stays the faction's even inside the reach of a neighbour's building */
  int64_t nearestOwn = INT64_MAX;
  for (uint32_t i = 0; i < g_AiImprovedOwnStructureCount; i++) {
    int64_t distance = AiImproved_DistanceSquared(worldX,worldY,g_AiImprovedOwnStructureX[i],g_AiImprovedOwnStructureY[i]);
    if (distance < nearestOwn) nearestOwn = distance;
  }
  for (uint32_t i = 0; i < g_AiImprovedForeignCount; i++) {
    int64_t distance = AiImproved_DistanceSquared(worldX,worldY,g_AiImprovedForeignX[i],g_AiImprovedForeignY[i]);
    if (distance < g_AiImprovedForeignRadiusSquared[i] && distance < nearestOwn) {
      return true;
    }
  }
  return false;
}

/* Grace period: whether a human object at worldX/worldY stands in the base of AI faction factionIndex: within
   AI_IMPROVED_GRACE_BASE_RADIUS of one of its core buildings and nearer to that core than to every building of its
   own faction (where the bases touch, the human's own base is not the AI's). A building does not count itself
   (objectSlot). Without the building list (before the first planning pass after loading) nothing counts. */
static bool AiImproved_HumanObjectInAiBase(int factionIndex,int humanFaction,Q12 worldX,Q12 worldY,int objectSlot)
{
  if (factionIndex < 1 || factionIndex > 7 || !g_AiImprovedHumanStructuresValid) {
    return false;
  }
  const AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
  int64_t radiusSquared = AI_IMPROVED_GRACE_BASE_RADIUS_Q12 * AI_IMPROVED_GRACE_BASE_RADIUS_Q12;
  int64_t nearestCore = INT64_MAX;
  for (uint32_t i = 0; i < memory->corePositionCount; i++) {
    int64_t distance = AiImproved_DistanceSquared(worldX,worldY,memory->coreX[i],memory->coreY[i]);
    if (distance < nearestCore) nearestCore = distance;
  }
  if (nearestCore > radiusSquared) {
    return false;
  }
  for (uint32_t i = 0; i < g_AiImprovedHumanStructureCount; i++) {
    if (g_AiImprovedHumanStructureFaction[i] != humanFaction ||
        (objectSlot >= 0 && g_AiImprovedHumanStructureArmySlot[i] == objectSlot)) {
      continue;
    }
    if (AiImproved_DistanceSquared(worldX,worldY,g_AiImprovedHumanStructureX[i],g_AiImprovedHumanStructureY[i]) <=
        nearestCore) {
      return false;
    }
  }
  return true;
}

/* Largest visibility radius of a model hierarchy, as ModelRuntimeHierarchy_AccumulateDerivedSelectionMetrics
   computes it (switched-off models count with their switched-off radius). */
static uint32_t AiImproved_HierarchyVisibilityRadius(ModelRuntimeSlot *modelRuntime,int depth)
{
  if (modelRuntime == nullptr || depth > 8) {
    return 0;
  }
  ModelDefinition *definition = modelRuntime->definitionOrSavedId.runtimeDefinition;
  uint32_t radius = Any(modelRuntime->classState.stateFlags & ARMY_MODEL_STATE_SWITCHED_OFF) ?
                    definition->switchedOffVisibilityRadius : definition->visibilityRadius;
  uint32_t attachmentCount = (uint32_t)modelRuntime->attachmentCount;
  if (attachmentCount > 6) attachmentCount = 6;
  for (uint32_t i = 0; i < attachmentCount; i++) {
    uint32_t childRadius = AiImproved_HierarchyVisibilityRadius(modelRuntime->attachments[i].childModelRuntimeOrSavedOffset,
                                                                depth + 1);
    if (radius < childRadius) radius = childRadius;
  }
  return radius;
}

/* Hell (level 7): the faction's units and buildings see 20 % further. Set again every planning pass from the
   definitions (the game recomputes the radius on every change of the model hierarchy, and a savegame keeps the
   raised value). */
static void AiImproved_ApplyHellVisibility(ModelRuntimeSlot *modelRuntime)
{
  if (modelRuntime == nullptr) {
    return;
  }
  ArmyRuntimeSlot *army = modelRuntime->ownerArmyRuntimeOrSavedOffset.armyRuntime;
  if (army == nullptr || army->modelRuntimeOrSavedOffset.modelRuntime == nullptr) {
    return;
  }
  uint32_t radius = AiImproved_HierarchyVisibilityRadius(army->modelRuntimeOrSavedOffset.modelRuntime,0);
  uint32_t raised = radius + radius / 5;
  if (radius != 0 && army->visibilityRadius != raised) {
    army->visibilityRadius = raised;
  }
}

/* Protocol: every 5 minutes of game time one status line per AI faction (units and units ordered, buildings, Xenite
   mines, Tritium pumps, defence buildings, stock, energy used / produced, group attacks since the last line,
   relations to the other factions: the hex digit is the relation state, 0 hostile .. 8 allied), so a game can be
   followed in thandor.log. Runs in the faction's planning pass (the workspaces are its own). */
static constexpr uint32_t AI_IMPROVED_STATUS_TICKS = 5 * 60 * AI_IMPROVED_TICKS_PER_SECOND;

static void AiImproved_LogStatus(FactionRuntimeIndex factionIndex)
{
  if (!AiImproved_LogEnabled() || factionIndex < 1 || factionIndex > 7) {
    return;
  }
  AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
  uint32_t now = AiImproved_GameTicks();
  if (memory->lastStatusTick != 0 && AiImproved_TicksSince(now,memory->lastStatusTick) < (int32_t)AI_IMPROVED_STATUS_TICKS) {
    return;
  }
  if (memory->lastStatusTick == 0 && now < AI_IMPROVED_STATUS_TICKS) {
    return;
  }
  memory->lastStatusTick = now;
  uint32_t units = 0;
  uint32_t jammed = 0; /* on the way somewhere but not moved for AI_IMPROVED_STUCK_TICKS, and not fighting */
  for (uint32_t i = 0; i < g_AiWorkspace01Count; i++) {
    ModelRuntimeSlot *unitModel = g_AiWorkspace01Units[i].modelRuntime;
    if (unitModel == nullptr) continue;
    units++;
    ArmyRuntimeSlot *unitArmy = unitModel->ownerArmyRuntimeOrSavedOffset.armyRuntime;
    if (unitArmy == nullptr || !Any(unitArmy->movementStateFlags & ARMY_MOVEMENT_ACTIVE)) continue;
    if (AiImproved_ArmyFighting(unitArmy)) continue;
    if (AiImproved_TicksStandingStill(unitArmy) >= (int32_t)AI_IMPROVED_STUCK_TICKS) jammed++;
  }
  uint32_t ordered = g_AiWorkspace01Count - units; /* queued or in production */
  uint32_t buildings = 0;
  for (uint32_t i = 0; i < g_AiWorkspace00Count; i++) {
    if (g_AiWorkspace00Structures[i].runtimeSlotAddressOrZero != nullptr) buildings++;
  }
  const GameFactionRuntimeRecord *record = &g_GameFactionRuntimeImage.records[factionIndex];
  char relations[8];
  for (int other = 1; other <= 7; other++) {
    uint32_t state = (record->packedRelationStates >> (other * 4 & 31)) & 0xF;
    bool active = g_GameFactionRuntimeImage.tail.factionLifecycleStates[other] == FACTION_RUNTIME_LIFECYCLE_ACTIVE;
    relations[other - 1] = other == factionIndex ? '-' : (!active ? '.' : "0123456789ABCDEF"[state]);
  }
  relations[7] = 0;
  const int heavyFactories = AiPrimaryWorkspace_CountAssignedEntriesById(ARM_0302_BUILDING_MDL0300) +
                             AiPrimaryWorkspace_CountAssignedEntriesById(ARM_0303_BUILDING_MDL0316) +
                             AiPrimaryWorkspace_CountAssignedEntriesById(ARM_0304_BUILDING_MDL0324);
  AiImproved_Log("Partei %d: Stand %u Einheiten (+%u bestellt), %u Gebaeude (HQ %d, Kraftwerke %d, schwere Fabriken %d, "
                 "Flughaefen %d), Xenitminen %d, Pumpen %d, Tuerme %u, "
                 "Xenit %u, Tritium %u, Energie %u/%u%s, Angriffe %u, festgefahren %u, Beziehungen %s",factionIndex,units,ordered,buildings,
                 AiPrimaryWorkspace_CountAssignedEntriesById(ARM_0300_BUILDING_MDL0301),
                 AiPrimaryWorkspace_CountAssignedEntriesById(ARM_0310_BUILDING_MDL0305),heavyFactories,
                 AiPrimaryWorkspace_CountAssignedEntriesById(ARM_0306_BUILDING_MDL0310),
                 AiPrimaryWorkspace_CountAssignedEntriesById(ARM_0330_BUILDING_MDL0303),
                 AiPrimaryWorkspace_CountAssignedEntriesById(ARM_0332_BUILDING_MDL0302),
                 AiImproved_CountPlacedDefenceBuildings(),record->xeniteCurrentQ4 / 16,record->tritiumCurrentQ4 / 16,
                 record->suppliedEnergyDemandQ4 / 16,record->energyGenerationCapacityQ4 / 16,
                 record->unpoweredEnergyDemandQ4 != 0 ? " (zu wenig)" : "",memory->attacksSinceStatus,jammed,relations);
  memory->attacksSinceStatus = 0;
}

/* Purchase round protocol (improved.h). */
static uint32_t g_AiImprovedPurchaseLogTick[8];
static bool g_AiImprovedPurchaseLogActive = false;
static char g_AiImprovedPurchaseLogText[200];
static size_t g_AiImprovedPurchaseLogLength = 0;
static uint32_t g_AiImprovedPurchaseLogCount = 0;

bool AiImproved_PurchaseLogBegin(FactionRuntimeIndex factionIndex)
{
  g_AiImprovedPurchaseLogActive = false;
  if (!AiImproved_LogEnabled() || factionIndex < 1 || factionIndex > 7) {
    return false;
  }
  const GameFactionRuntimeRecord *record = &g_GameFactionRuntimeImage.records[factionIndex];
  if (record->xeniteStorageLimitQ4 == 0 ||
      (uint64_t)record->xeniteCurrentQ4 * 10 < (uint64_t)record->xeniteStorageLimitQ4 * 9) {
    return false;
  }
  uint32_t now = AiImproved_GameTicks();
  if (g_AiImprovedPurchaseLogTick[factionIndex] != 0 &&
      AiImproved_TicksSince(now,g_AiImprovedPurchaseLogTick[factionIndex]) < (int32_t)AI_IMPROVED_STATUS_TICKS) {
    return false;
  }
  g_AiImprovedPurchaseLogTick[factionIndex] = now == 0 ? 1 : now;
  g_AiImprovedPurchaseLogActive = true;
  g_AiImprovedPurchaseLogLength = 0;
  g_AiImprovedPurchaseLogCount = 0;
  g_AiImprovedPurchaseLogText[0] = 0;
  return true;
}

void AiImproved_PurchaseLogNote(uint32_t weightedScoreAndKind,uint32_t entityIdAndMultiplicity,uint32_t costQ4,
                                int outcome)
{
  if (!g_AiImprovedPurchaseLogActive) {
    return;
  }
  g_AiImprovedPurchaseLogCount++;
  if (g_AiImprovedPurchaseLogCount > 6 || g_AiImprovedPurchaseLogLength >= sizeof g_AiImprovedPurchaseLogText - 1) {
    return;
  }
  bool technology = (weightedScoreAndKind & AI_CANDIDATE_KIND_MASK) == AI_CANDIDATE_KIND_TECHNOLOGY;
  static const char *const outcomes[5] = {"gekauft", "kein freier Hersteller", "zu teuer, spart",
                                          "teurer als das Lager, uebersprungen", "zu teuer, uebersprungen"};
  int written = snprintf(g_AiImprovedPurchaseLogText + g_AiImprovedPurchaseLogLength,
                         sizeof g_AiImprovedPurchaseLogText - g_AiImprovedPurchaseLogLength,"%s%s %u (%u Xenit) %s",
                         g_AiImprovedPurchaseLogLength != 0 ? "; " : "",technology ? "TEC" : "ARM",
                         entityIdAndMultiplicity & AI_CANDIDATE_ID_MASK,costQ4 / 16,
                         outcomes[outcome < 0 || outcome > 4 ? 2 : outcome]);
  if (written > 0) {
    g_AiImprovedPurchaseLogLength += (size_t)written;
    if (g_AiImprovedPurchaseLogLength >= sizeof g_AiImprovedPurchaseLogText) {
      g_AiImprovedPurchaseLogLength = sizeof g_AiImprovedPurchaseLogText - 1;
    }
  }
}

void AiImproved_PurchaseLogEnd(FactionRuntimeIndex factionIndex)
{
  if (!g_AiImprovedPurchaseLogActive) {
    return;
  }
  g_AiImprovedPurchaseLogActive = false;
  /* the faction's producers: factories (class 13), class-11 structures and aircraft pads (class 22) */
  uint32_t producers = 0;
  uint32_t idleProducers = 0;
  for (uint32_t i = 0; i < g_AiWorkspace00Count; i++) {
    ModelRuntimeSlot *slot = g_AiWorkspace00Structures[i].runtimeSlotAddressOrZero;
    if (slot == nullptr) {
      continue;
    }
    ModelRuntimeClassId classId = slot->definitionOrSavedId.runtimeDefinition->runtimeClassId;
    if (classId != MODEL_RUNTIME_CLASS_13 && classId != MODEL_RUNTIME_CLASS_11 && classId != MODEL_RUNTIME_CLASS_22) {
      continue;
    }
    producers++;
    bool blocked = Any(slot->classState.stateFlags & ARMY_MODEL_STATE_BUILD_BLOCKING_MASK);
    bool busy = classId == MODEL_RUNTIME_CLASS_22 ? slot->classState.classStateAC != 0 : slot->classState.behaviorState != 0;
    if (!blocked && !busy) {
      idleProducers++;
    }
  }
  AiImproved_Log("Partei %d: Xenitlager voll, Einkauf: %u Kandidaten, %u Hersteller (%u frei), %u Auftraege offen: %s",
                 factionIndex,g_AiImprovedPurchaseLogCount,producers,idleProducers,g_AiWorkspace04Count,
                 g_AiImprovedPurchaseLogCount != 0 ? g_AiImprovedPurchaseLogText : "keine");
  /* second line: each producer's class, state (factories: 0 idle, 1 building, 2 door opens, 3 waiting for the new
     unit to leave, 4 closing) and the asset it builds, then the open building orders (workspace 04) */
  char text[260];
  size_t length = 0;
  text[0] = 0;
  for (uint32_t i = 0; i < g_AiWorkspace00Count && length < sizeof text - 40; i++) {
    ModelRuntimeSlot *slot = g_AiWorkspace00Structures[i].runtimeSlotAddressOrZero;
    if (slot == nullptr) {
      continue;
    }
    ModelRuntimeClassId classId = slot->definitionOrSavedId.runtimeDefinition->runtimeClassId;
    if (classId != MODEL_RUNTIME_CLASS_13 && classId != MODEL_RUNTIME_CLASS_11 && classId != MODEL_RUNTIME_CLASS_22) {
      continue;
    }
    const int *payload = reinterpret_cast<const int *>(slot);
    uint32_t state = classId == MODEL_RUNTIME_CLASS_22 ? slot->classState.classStateAC : slot->classState.behaviorState;
    int written = snprintf(text + length,sizeof text - length,"%sARM %u Kl.%d Zustand %u baut %d",length != 0 ? ", " : "",
                           (unsigned)g_AiWorkspace00Structures[i].armyAssetId,(int)classId,state,payload[24]);
    if (written > 0) length += (size_t)written;
  }
  if (length < sizeof text - 20) {
    int written = snprintf(text + length,sizeof text - length,"; offen:");
    if (written > 0) length += (size_t)written;
  }
  for (uint32_t i = 0; i < g_AiWorkspace04Count && length < sizeof text - 10; i++) {
    int written = snprintf(text + length,sizeof text - length," %u",(unsigned)g_AiWorkspace04RequestedAssets[i].armyAssetId);
    if (written > 0) length += (size_t)written;
  }
  if (length >= sizeof text) length = sizeof text - 1;
  AiImproved_Log("Partei %d: Hersteller %s",factionIndex,text);
}

void AiImproved_AfterWorkspacesRebuilt(FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime)
{
  (void)worldRuntime;
  if (!AiImproved_AnyActive() || !AiImproved_IsAiFaction(factionIndex)) {
    return;
  }
  /* core building positions for the per-step tests outside the planning pass */
  AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
  /* how long the buildings are under alarm without a break (AiImproved_BuildDuringAlarm) */
  if (g_GameFactionRuntimeImage.records[factionIndex].primaryAnchorCooldown == 0) {
    memory->alarmSinceTick = 0;
    memory->alarmBuildLogged = 0;
  }
  else if (memory->alarmSinceTick == 0) {
    uint32_t now = AiImproved_GameTicks();
    memory->alarmSinceTick = now == 0 ? 1 : now;
  }
  memory->corePositionCount = 0;
  AiStructureWorkspaceEntry *structureEntry = g_AiWorkspace00Structures;
  for (uint32_t i = 0; i < g_AiWorkspace00Count; i++, structureEntry++) {
    ModelRuntimeSlot *structure = structureEntry->runtimeSlotAddressOrZero;
    if (structure == nullptr || structureEntry->armyAssetId < ARM_0300_BUILDING_MDL0301 ||
        structureEntry->armyAssetId >= ARM_0340_BUILDING_MDL0314) {
      continue;
    }
    if (memory->corePositionCount >= (uint32_t)AI_IMPROVED_CORE_POSITIONS) {
      break;
    }
    ModelRuntimeNode *structureNode = structure->rootModelNodeOrSavedOffset.modelNode;
    memory->coreX[memory->corePositionCount] = (structureNode->worldTransform).translation.x;
    memory->coreY[memory->corePositionCount] = (structureNode->worldTransform).translation.y;
    memory->corePositionCount++;
  }
  if (AiImproved_Level() == AI_IMPROVED_LEVEL_HELL) {
    for (uint32_t i = 0; i < g_AiWorkspace00Count; i++) {
      AiImproved_ApplyHellVisibility(g_AiWorkspace00Structures[i].runtimeSlotAddressOrZero);
    }
    for (uint32_t i = 0; i < g_AiWorkspace01Count; i++) {
      AiImproved_ApplyHellVisibility(g_AiWorkspace01Units[i].modelRuntime);
    }
  }
  /* defence mode: is a scouted hostile army about to overrun the base? */
  AiImproved_UpdateThreatAssessment(factionIndex);
  /* the fastest hostile units of each class in sight (for the hit factor of the purchase scores) */
  {
    AiRuntimeWorkspaceEntry *seenEntry = g_AiWorkspace02VisibleHostiles;
    for (uint32_t i = 0; i < g_AiWorkspace02Count; i++, seenEntry++) {
      ModelRuntimeSlot *hostile = seenEntry->modelRuntime;
      if (hostile == nullptr) continue;
      ModelDefinition *definition = hostile->definitionOrSavedId.runtimeDefinition;
      if (definition->accelerationPerTick == 0 || definition->targetClassIndex < 1 || definition->targetClassIndex > 6) {
        continue;
      }
      if (memory->seenClassSpeed[definition->targetClassIndex] < definition->movementSpeed) {
        memory->seenClassSpeed[definition->targetClassIndex] = definition->movementSpeed;
      }
    }
  }
  AiImproved_LogStatus(factionIndex);
  /* grace period: an armed human unit the faction sees in its base ends it */
  if (!AiImproved_GraceActive(factionIndex)) {
    return;
  }
  AiRuntimeWorkspaceEntry *hostileEntry = g_AiWorkspace02VisibleHostiles;
  for (uint32_t i = 0; i < g_AiWorkspace02Count; i++, hostileEntry++) {
    ModelRuntimeSlot *hostile = hostileEntry->modelRuntime;
    if (hostile == nullptr) continue;
    ArmyRuntimeSlot *hostileArmy = hostile->ownerArmyRuntimeOrSavedOffset.armyRuntime;
    if (hostileArmy == nullptr || !AiImproved_IsHumanFaction(hostileArmy->factionIndex)) continue;
    bool building = hostile->definitionOrSavedId.runtimeDefinition->accelerationPerTick == 0;
    /* an armed unit driving in, or a building the player puts up in the AI's base (taking its deposit) */
    if (!building && !AiImproved_ArmyArmed(hostileArmy)) continue;
    Q12 hostileX;
    Q12 hostileY;
    if (!AiImproved_ArmyPosition(hostileArmy,&hostileX,&hostileY)) continue;
    if (AiImproved_HumanObjectInAiBase(factionIndex,hostileArmy->factionIndex,hostileX,hostileY,
                                       building ? AiImproved_SlotIndex(hostileArmy) : -1)) {
      AiImproved_Log("Partei %d: %s der Partei %d bei %d/%d in der Basis",factionIndex,
                     building ? "Gebaeude" : "bewaffnete Einheit",hostileArmy->factionIndex,hostileX / Q12_ONE,
                     hostileY / Q12_ONE);
      AiImproved_EndGrace(factionIndex,building ? "Gebaeude des Spielers in der Basis" : "bewaffnete Einheit in der Basis");
      return;
    }
  }
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Repairs: unit behaviour                                                                                      */

/* Longest reload of the weapons of a model hierarchy, in game ticks. */
static uint32_t AiImproved_HierarchyMaxReloadTicks(ModelRuntimeSlot *modelRuntime,int depth)
{
  if (modelRuntime == nullptr || depth > 8) {
    return 0;
  }
  int reload = modelRuntime->definitionOrSavedId.runtimeDefinition->reloadTicks;
  uint32_t longest = reload > 0 ? (uint32_t)reload : 0;
  uint32_t attachmentCount = (uint32_t)modelRuntime->attachmentCount;
  if (attachmentCount > 6) attachmentCount = 6;
  for (uint32_t i = 0; i < attachmentCount; i++) {
    uint32_t childReload = AiImproved_HierarchyMaxReloadTicks(modelRuntime->attachments[i].childModelRuntimeOrSavedOffset,
                                                              depth + 1);
    if (longest < childReload) longest = childReload;
  }
  return longest;
}

bool AiImproved_TrackUnit(FactionRuntimeIndex factionIndex,ArmyRuntimeSlot *armyRuntime)
{
  if (!AiImproved_RepairsActive()) {
    return false;
  }
  AiImprovedUnitMemory *memory = AiImproved_NoteMovement(armyRuntime);
  Q12 unitX;
  Q12 unitY;
  if (memory == nullptr || !AiImproved_ArmyPosition(armyRuntime,&unitX,&unitY)) {
    return false;
  }
  uint32_t now = AiImproved_GameTicks();
  ArmyRuntimeSlot *target = armyRuntime->commandTargetArmyRuntime;
  if (!Any(armyRuntime->commandModeFlags & ARMY_COMMAND_MODE_TARGET_ARMY) || target == nullptr ||
      target->factionIndex == armyRuntime->factionIndex) {
    /* no attack order (a repair order on an own unit is not tracked) */
    memory->trackedTargetSlotPlusOne = 0;
    return false;
  }
  int targetSlot = AiImproved_SlotIndex(target);
  Q12 targetX;
  Q12 targetY;
  if (targetSlot < 0 || !AiImproved_ArmyPosition(target,&targetX,&targetY)) {
    return false;
  }
  Q12 condition = AiImproved_ConditionQ12(target);
  if (memory->trackedTargetSlotPlusOne != (uint32_t)targetSlot + 1) {
    memory->trackedTargetSlotPlusOne = (uint32_t)targetSlot + 1;
    memory->trackedSinceTick = now;
    memory->trackedReferenceConditionQ12 = condition;
    return false;
  }
  /* Progress is measured from the condition at the last progress: small hits add up until they reach 1 %. A
     target repaired above that condition is measured from its new condition (net damage counts, not hits). */
  if (condition > memory->trackedReferenceConditionQ12) {
    memory->trackedReferenceConditionQ12 = condition;
  }
  else if (condition <= memory->trackedReferenceConditionQ12 - AI_IMPROVED_PROGRESS_CONDITION_Q12) {
    memory->trackedReferenceConditionQ12 = condition;
    memory->trackedSinceTick = now;
    return false;
  }
  int64_t reachQ12 = (int64_t)armyRuntime->weaponRangeQ12 + 2 * (int64_t)Q12_ONE;
  bool inReach = AiImproved_DistanceSquared(unitX,unitY,targetX,targetY) <= reachQ12 * reachQ12;
  int32_t sinceProgress = AiImproved_TicksSince(now,memory->trackedSinceTick);
  bool stalled;
  if (inReach) {
    /* slow weapons (rocket ramp, artillery: up to 20 s reload plus the flight) get one and a half reloads */
    uint32_t stallTicks = AI_IMPROVED_STALL_IN_REACH_TICKS;
    uint32_t reloadTicks = AiImproved_HierarchyMaxReloadTicks((armyRuntime->modelRuntimeOrSavedOffset).modelRuntime,0);
    if (stallTicks < reloadTicks * 3 / 2 + 5 * AI_IMPROVED_TICKS_PER_SECOND) {
      stallTicks = reloadTicks * 3 / 2 + 5 * AI_IMPROVED_TICKS_PER_SECOND;
    }
    stalled = sinceProgress >= (int32_t)stallTicks;
  }
  else {
    stalled = sinceProgress >= (int32_t)AI_IMPROVED_STALL_OUT_OF_REACH_TICKS &&
              AiImproved_TicksSince(now,memory->lastMovedTick) >= (int32_t)AI_IMPROVED_STALL_OUT_OF_REACH_TICKS;
  }
  if (!stalled) {
    return false;
  }
  /* give the target up */
  {
    int chosen = 0;
    for (int i = 0; i < 3; i++) {
      if (memory->ignoredTargetSlotPlusOne[i] == 0 || AiImproved_TicksSince(memory->ignoreUntilTick[i],now) <= 0) {
        chosen = i;
        break;
      }
      if (AiImproved_TicksSince(memory->ignoreUntilTick[i],memory->ignoreUntilTick[chosen]) < 0) {
        chosen = i;
      }
    }
    memory->ignoredTargetSlotPlusOne[chosen] = (uint32_t)targetSlot + 1;
    memory->ignoreUntilTick[chosen] = now + AI_IMPROVED_IGNORE_UNIT_TICKS;
  }
  memory->trackedTargetSlotPlusOne = 0;
  /* In range and still no damage: the shot does not get there (a building behind a terrain edge or on a plateau,
     across lava). Once a second unit reports the same target, the whole faction leaves it alone instead of each
     unit trying it again every few minutes. */
  bool factionGivesUp = false;
  if (inReach) {
    factionGivesUp = AiImproved_NoteTargetWithoutDamage(factionIndex,targetSlot,AiImproved_SlotIndex(armyRuntime));
  }
  bool groupTarget = Thandor_U32ToPointer<ArmyRuntimeSlot>(armyRuntime->assignedTargetArmyRuntime) == target; /* 32-bit format field: ArmyRuntimeSlot.assignedTargetArmyRuntime */
  if (groupTarget) {
    AiImproved_FactionIgnoreTarget(factionIndex,targetSlot,now + AI_IMPROVED_IGNORE_FACTION_TICKS);
    if (!inReach) {
      AiImproved_NoteUnreachableArea(factionIndex,targetX,targetY);
    }
    armyRuntime->assignedTargetArmyRuntime = 0;
    armyRuntime->aiUnitFlags = armyRuntime->aiUnitFlags & ~AI_UNIT_STATE94_GROUP_ASSIGNED;
  }
  if (!inReach && AiImproved_LogEnabled()) {
    /* protocol: the unit's move at the moment it gives up */
    ArmyMovementRuntime *movement = ModelView_Cast<ArmyMovementRuntime>(armyRuntime);
    if (movement != nullptr) {
      AiImproved_Log("Partei %d: Einheit %d: Weg endet bei %d/%d, naechster Punkt %d/%d, Bewegung %03X, Kommando %X",
                     factionIndex,AiImproved_SlotIndex(armyRuntime),(movement->fallbackPosition).worldXQ12 / Q12_ONE,
                     (movement->fallbackPosition).worldYQ12 / Q12_ONE,movement->movementWorldXQ12 / Q12_ONE,
                     movement->movementWorldYQ12 / Q12_ONE,(unsigned)movement->movementStateFlags,
                     (unsigned)armyRuntime->commandModeFlags);
    }
  }
  ArmyRuntime_ResolveCommandTarget(nullptr,armyRuntime);
  AiImproved_Log("Partei %d: Einheit %d (ARM %u) bei %d/%d gibt Ziel %d (ARM %u) bei %d/%d auf: %s%s",factionIndex,
                 AiImproved_SlotIndex(armyRuntime),(unsigned)armyRuntime->armyAssetId,unitX / Q12_ONE,unitY / Q12_ONE,
                 targetSlot,(unsigned)target->armyAssetId,targetX / Q12_ONE,targetY / Q12_ONE,
                 inReach ? "in Reichweite kein Schaden" : "steht fest, kein Schaden",groupTarget ? " (Gruppenziel)" : "");
  if (factionGivesUp) {
    AiImproved_Log("Partei %d: Ziel %d (ARM %u) bei %d/%d laesst sich nicht treffen, die ganze Partei laesst es "
                   "%u Minuten in Ruhe",factionIndex,targetSlot,(unsigned)target->armyAssetId,targetX / Q12_ONE,
                   targetY / Q12_ONE,AI_IMPROVED_NO_DAMAGE_TICKS / (60 * AI_IMPROVED_TICKS_PER_SECOND));
  }

  return true;
}

void AiImproved_ReleaseStaleGroupAssignment(ArmyRuntimeSlot *armyRuntime)
{
  if (!AiImproved_RepairsActive() || !Any(armyRuntime->aiUnitFlags & AI_UNIT_STATE94_GROUP_ASSIGNED)) {
    return;
  }
  bool release;
  if (armyRuntime->assignedTargetArmyRuntime == 0) {
    /* the group's target is gone: free once the unit has finished the fight it is in */
    release = !AiImproved_ArmyFighting(armyRuntime);
  }
  else {
    /* the target lives, but the unit has no attack order and stands still (target out of sight, way blocked) */
    release = !Any(armyRuntime->commandModeFlags & ARMY_COMMAND_MODE_TARGET_ARMY) &&
              AiImproved_TicksStandingStill(armyRuntime) >= (int32_t)AI_IMPROVED_GROUP_IDLE_RELEASE_TICKS;
  }
  if (release) {
    armyRuntime->aiUnitFlags = armyRuntime->aiUnitFlags & ~AI_UNIT_STATE94_GROUP_ASSIGNED;
    AiImproved_Log("Partei %d: Einheit %d (ARM %u) wieder frei fuer Angriffe",armyRuntime->factionIndex,
                   AiImproved_SlotIndex(armyRuntime),(unsigned)armyRuntime->armyAssetId);
  }
}

bool AiImproved_AdjustsTargetsOf(const ArmyRuntimeSlot *sourceArmyRuntime)
{
  int factionIndex = sourceArmyRuntime->factionIndex;
  if (!AiImproved_IsAiFaction(factionIndex)) {
    return false;
  }
  return AiImproved_RepairsActive() || AiImproved_GraceActive(factionIndex) || AiImproved_IsActive();
}

uint32_t AiImproved_AdjustTargetScore(ArmyRuntimeSlot *sourceArmyRuntime,ArmyRuntimeSlot *candidateArmyRuntime,
                                      uint32_t candidateScore)
{
  if ((int32_t)candidateScore <= 0) {
    return candidateScore; /* rejected (0) or below every taken candidate: left as it is */
  }
  int factionIndex = sourceArmyRuntime->factionIndex;
  ModelDefinition *candidateDefinition = AiImproved_RootDefinition(candidateArmyRuntime);
  if (candidateDefinition == nullptr || candidateArmyRuntime->factionIndex == factionIndex) {
    return candidateScore; /* own units (repair targets) stay as they are */
  }
  bool repairs = AiImproved_RepairsActive();
  if (repairs) {
    /* a target none of the unit's weapons can damage (MG and Flak against buildings): the original chose it all the
       same, and the unit stood shooting without effect */
    uint32_t candidateClass = (uint32_t)candidateDefinition->targetClassIndex;
    if (candidateClass < 8 && sourceArmyRuntime->targetClassShotDamage[candidateClass] <= 0) {
      return 0;
    }
    /* a target the unit gave up, or one the whole faction has learned it cannot hurt */
    AiImprovedUnitMemory *memory = AiImproved_UnitMemory(sourceArmyRuntime);
    int candidateSlot = AiImproved_SlotIndex(candidateArmyRuntime);
    if (AiImproved_FactionCannotHurt(factionIndex,candidateSlot)) {
      return 0;
    }
    if (memory != nullptr && candidateSlot >= 0) {
      uint32_t now = AiImproved_GameTicks();
      for (int i = 0; i < 3; i++) {
        if (memory->ignoredTargetSlotPlusOne[i] == (uint32_t)candidateSlot + 1 &&
            AiImproved_TicksSince(memory->ignoreUntilTick[i],now) > 0) {
          return 0;
        }
      }
    }
  }
  if (AiImproved_GraceActive(factionIndex) && AiImproved_IsHumanFaction(candidateArmyRuntime->factionIndex)) {
    /* grace period: armed human units may be fought anywhere, everything else of the player only in the own base */
    bool armedMobile = candidateDefinition->accelerationPerTick != 0 && AiImproved_ArmyArmed(candidateArmyRuntime);
    if (!armedMobile) {
      Q12 candidateX;
      Q12 candidateY;
      if (!AiImproved_ArmyPosition(candidateArmyRuntime,&candidateX,&candidateY) ||
          !AiImproved_HumanObjectInAiBase(factionIndex,candidateArmyRuntime->factionIndex,candidateX,candidateY,
                                          AiImproved_SlotIndex(candidateArmyRuntime))) {
        return 0;
      }
    }
  }
  if (repairs && candidateDefinition->runtimeClassId == MODEL_RUNTIME_CLASS_23) {
    /* walls and tank traps: last choice, and not at all on the way to a group target, unless that target is a
       wall itself or the unit is stuck (then the wall is in its way) */
    ArmyRuntimeSlot *groupTarget = Thandor_U32ToPointer<ArmyRuntimeSlot>(sourceArmyRuntime->assignedTargetArmyRuntime); /* 32-bit format field: ArmyRuntimeSlot.assignedTargetArmyRuntime */
    if (groupTarget != nullptr) {
      ModelDefinition *groupTargetDefinition = AiImproved_RootDefinition(groupTarget);
      bool groupTargetIsWall = groupTargetDefinition != nullptr &&
                               groupTargetDefinition->runtimeClassId == MODEL_RUNTIME_CLASS_23;
      bool stuck = AiImproved_TicksStandingStill(sourceArmyRuntime) >= (int32_t)AI_IMPROVED_STUCK_TICKS;
      if (groupTargetIsWall || stuck) {
        return candidateScore;
      }
      return 0;
    }
    return 1;
  }
  if (AiImproved_IsActive() && candidateDefinition->accelerationPerTick != 0 &&
      !AiImproved_ArmyArmed(candidateArmyRuntime) && candidateScore <= (uint32_t)INT32_MAX / 4) {
    /* Improved AI: an unarmed intruder (radar, scout, repair unit) near the own base spots for the enemy's guns;
       the original quarters the score of targets that cannot shoot back, this gives it its full weight again */
    Q12 candidateX;
    Q12 candidateY;
    if (AiImproved_ArmyPosition(candidateArmyRuntime,&candidateX,&candidateY) &&
        AiImproved_NearOwnCore(factionIndex,candidateX,candidateY,AI_IMPROVED_GRACE_BASE_RADIUS_Q12)) {
      return candidateScore * 4;
    }
  }
  return candidateScore;
}

bool AiImproved_GroupTargetAllowed(FactionRuntimeIndex factionIndex,ModelRuntimeSlot *targetModelRuntime)
{
  if (targetModelRuntime == nullptr) {
    return false;
  }
  ArmyRuntimeSlot *targetArmy = targetModelRuntime->ownerArmyRuntimeOrSavedOffset.armyRuntime;
  if (targetArmy == nullptr) {
    return false;
  }
  /* from level 1 on, where aircraft are about: a ground attack group does not chase an aircraft (seen in the test:
     groups of 14 and 16 units sent after a Habicht or Adler); its airport is a target, and the units' own fire
     still takes aircraft in reach */
  if (AiImproved_ExtrasActive() &&
      targetModelRuntime->definitionOrSavedId.runtimeDefinition->runtimeClassId == MODEL_RUNTIME_CLASS_21_AIRCRAFT) {
    return false;
  }
  if (AiImproved_GraceActive(factionIndex) && AiImproved_IsHumanFaction(targetArmy->factionIndex)) {
    /* what the player puts up inside the AI's base (a mine on its deposit, a tower at its gate) is not protected:
       the same exception the single units already have in AiImproved_AdjustTargetScore */
    Q12 targetX;
    Q12 targetY;
    if (!AiImproved_ArmyPosition(targetArmy,&targetX,&targetY) ||
        !AiImproved_HumanObjectInAiBase(factionIndex,targetArmy->factionIndex,targetX,targetY,
                                        AiImproved_SlotIndex(targetArmy))) {
      return false;
    }
  }
  if (AiImproved_RepairsActive() && (AiImproved_FactionIgnoresTarget(factionIndex,AiImproved_SlotIndex(targetArmy)) ||
                                     AiImproved_FactionCannotHurt(factionIndex,AiImproved_SlotIndex(targetArmy)))) {
    return false;
  }
  if (AiImproved_RepairsActive()) {
    Q12 targetX;
    Q12 targetY;
    if (AiImproved_ArmyPosition(targetArmy,&targetX,&targetY) && AiImproved_InUnreachableArea(factionIndex,targetX,targetY)) {
      return false;
    }
  }
  return true;
}

/* ---- building during a long alarm (repairs) ---- */

static constexpr uint32_t AI_IMPROVED_ALARM_BUILD_TICKS = 90 * AI_IMPROVED_TICKS_PER_SECOND;
static constexpr int AI_IMPROVED_ALARM_BUILD_KEEP_OUT_Q12 = 12 * Q12_ONE;

bool AiImproved_BuildDuringAlarm(FactionRuntimeIndex factionIndex)
{
  if (!AiImproved_RepairsActive() || factionIndex < 1 || factionIndex > 7 ||
      !AiImproved_IsAiFaction(factionIndex)) {
    return false;
  }
  AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
  if (memory->alarmSinceTick == 0 ||
      AiImproved_TicksSince(AiImproved_GameTicks(),memory->alarmSinceTick) < (int32_t)AI_IMPROVED_ALARM_BUILD_TICKS) {
    return false;
  }
  if (memory->alarmBuildLogged == 0) {
    memory->alarmBuildLogged = 1;
    AiImproved_Log("Partei %d: seit %d s unter Beschuss, baut trotzdem weiter (nicht nahe der Einschlaege)",factionIndex,
                   AiImproved_TicksSince(AiImproved_GameTicks(),memory->alarmSinceTick) / (int32_t)AI_IMPROVED_TICKS_PER_SECOND);
  }
  return true;
}

bool AiImproved_AlarmKeepsCellFree(FactionRuntimeIndex factionIndex,Q12 cellWorldX,Q12 cellWorldY)
{
  const GameFactionRuntimeRecord *record = &g_GameFactionRuntimeImage.records[factionIndex];
  if (record->primaryAnchorCooldown == 0) {
    return false;
  }
  /* the anchor holds the hit point with its fields swapped (GameFactionRuntime_MoveImpactAlertAnchor): primaryAnchorYQ12
     is the world X, primaryAnchorXQ12 the world Y */
  int64_t distance = (int64_t)record->primaryAnchorYQ12 - cellWorldX;
  if (distance < 0) distance = -distance;
  int64_t distanceY = (int64_t)record->primaryAnchorXQ12 - cellWorldY;
  if (distanceY < 0) distanceY = -distanceY;
  return distance + distanceY < AI_IMPROVED_ALARM_BUILD_KEEP_OUT_Q12;
}

/* ---- path check before a group attack (repairs) ---- */

static bool AiImproved_ModelPosition(ModelRuntimeSlot *modelRuntime,Q12 *outWorldX,Q12 *outWorldY);

/* Scratch cell of a world point, clamped to 1..size-2 (as EntityPathing_ResolveDestinationAndRebuildRoutes). */
static void AiImproved_WorldToScratchCell(Q12 worldX,Q12 worldY,int *outRow,int *outColumn)
{
  int64_t wideProductX = (int64_t)worldX * FIELD_GRID_WORLD_X_TO_COLUMN_Q20;
  int64_t wideProductY = (int64_t)worldY * FIELD_GRID_WORLD_Y_TO_ROW_Q20;
  uint32_t scaledRowTerm = (int)((uint64_t)wideProductY >> 32) << 11 | (uint32_t)wideProductY >> 21;
  int column = (int)((((int)((uint64_t)wideProductX >> 32) << 12 | (uint32_t)wideProductX >> 20) - scaledRowTerm) +
                     GRID_SCRATCH_INDEX_BIAS_Q12) >> GRID_SCRATCH_CELL_SHIFT;
  int row = (int)(scaledRowTerm * 2 + GRID_SCRATCH_INDEX_BIAS_Q12) >> GRID_SCRATCH_CELL_SHIFT;
  if (column < 1) column = 1;
  if (row < 1) row = 1;
  uint32_t columnLimit = column + 2U;
  if ((int)g_GridScratchWidth < (int)(column + 2U)) columnLimit = g_GridScratchWidth;
  uint32_t rowLimit = row + 2U;
  if ((int)g_GridScratchHeight < (int)(row + 2U)) rowLimit = g_GridScratchHeight;
  *outColumn = (int)columnLimit - 2;
  *outRow = (int)rowLimit - 2;
}

/* World position of the centre of scratch cell (row, column) (as GridScratch_CellCenterWorldPosition). */
static void AiImproved_ScratchCellCenter(int row,int column,Q12 *outWorldX,Q12 *outWorldY)
{
  int rowCenterQ12 = row * GRID_SCRATCH_CELL_Q12 - GRID_SCRATCH_CELL_CENTER_Q12;
  int64_t wideProductX = (int64_t)(rowCenterQ12 + (column * GRID_SCRATCH_CELL_Q12 - GRID_SCRATCH_CELL_CENTER_Q12) * 2) *
                         FIELD_GRID_WORLD_COLUMN_STEP_X;
  int64_t wideProductY = (int64_t)rowCenterQ12 * FIELD_GRID_WORLD_ROW_STEP_Y;
  *outWorldX = (int)((uint64_t)wideProductX >> 32) << 19 | (uint32_t)wideProductX >> 13;
  *outWorldY = (int)((uint64_t)wideProductY >> 32) << 20 | (uint32_t)wideProductY >> 12;
}

/* Dry run of the route planning of EntityPathing_ResolveDestinationAndRebuildRoutes for one mover towards a world
   point, without giving an order: the same masks, the same straight-line test and cost propagation. Returns true
   when the mover can get within reachQ12 of the point: the line is clear, the propagation reaches it, or the
   nearest cell of its own region (where the real route would end) lies within reachQ12. Only the scratch grid's
   path costs and visited marks change (every route planning resets them first); the mover's grid influence and the
   pathing masks are restored. */
static bool AiImproved_MoverCanReach(ArmyRuntimeSlot *mover,Q12 targetX,Q12 targetY,int64_t reachQ12,Q12 *outEndX,
                                     Q12 *outEndY)
{
  /* the entity view of the route planning is the unit's model runtime (ArmyMovementRuntime.entityRuntime, +0 of the
     army slot): its +0 is the model definition and its +8 the link the grid influence handlers keep their point
     in. The army slot itself is no such view (its +0 is the model, its +8 a value from the army asset record). */
  ModelRuntimeSlot *moverModel = mover != nullptr ? (mover->modelRuntimeOrSavedOffset).modelRuntime.get() : nullptr;
  GameEntityRuntime *entity = moverModel != nullptr ? ModelView_Cast<GameEntityRuntime>(moverModel) : nullptr;
  if (entity == nullptr || g_GridScratchPrimary == nullptr || g_GridScratchWidth < 8 || g_GridScratchHeight < 8 ||
      g_GridPathCostQueueBegin == nullptr) {
    return true;
  }
  ModelRuntimeNode *node = (entity->common).ownership.modelNode;
  ModelDefinition *definition = (entity->common).ownership.modelDefinition();
  if (node == nullptr || definition == nullptr || definition->footprintRadiusClass < 0 ||
      (int)definition->terrainTraversalClass < 0) {
    return true;
  }
  Q12 startX = (node->worldTransform).translation.x;
  Q12 startY = (node->worldTransform).translation.y;
  *outEndX = targetX;
  *outEndY = targetY;
  int startRow;
  int startColumn;
  int targetRow;
  int targetColumn;
  AiImproved_WorldToScratchCell(startX,startY,&startRow,&startColumn);
  AiImproved_WorldToScratchCell(targetX,targetY,&targetRow,&targetColumn);

  const uint32_t savedEntityMask = g_GridPathEntityClassMask;
  const uint32_t savedBlockingMask = g_GridPathBlockingMask;
  const uint32_t savedHighCostMask = g_GridPathHighCostMask;
  /* Unlike the route planning, the entity the mover overlaps keeps its grid influence: the planning runs at order
     time, when that reference is fresh, while during the AI planning it can name a model being removed. A start it
     blocks is left like any blocked start (GridPathCost_RelocateFromBlockedCell). */
  ModelRuntimeClassId classId = definition->runtimeClassId;
  if ((uint32_t)classId >= 24 || g_ArmyRuntimeOrderHandlerMatrix11Columns24Classes.gridInfluenceRemove[classId] == nullptr ||
      g_ArmyRuntimeOrderHandlerMatrix11Columns24Classes.gridInfluenceAdd[classId] == nullptr) {
    return true;
  }
  g_ArmyRuntimeOrderHandlerMatrix11Columns24Classes.gridInfluenceRemove[classId](entity);
  g_GridPathEntityClassMask = 1u << ((uint8_t)mover->factionIndex & 31);
  uint8_t gridClassShift = (uint8_t)definition->footprintRadiusClass;
  g_GridPathHighCostMask = GRID_SCRATCH_HIGH_BAND0 << (gridClassShift & 31);
  g_GridPathBlockingMask = GRID_SCRATCH_LOW_BAND0 << (gridClassShift & 31) |
                           GRID_SCRATCH_TERRAIN_CLASS_BIT24 << ((uint8_t)definition->terrainTraversalClass & 31);

  bool reachable = true;
  FieldGridCellCoordinate nearestRow;
  FieldGridCellCoordinate nearestColumn;
  if (GridPathCost_RelocateFromBlockedCell(startRow,startColumn,&nearestRow,&nearestColumn)) {
    /* a blocked start: the route starts from the nearest open cell (none: the mover cannot move, no verdict) */
    if (nearestRow == startRow && nearestColumn == startColumn) {
      goto restore;
    }
    startRow = nearestRow;
    startColumn = nearestColumn;
  }
  {
    const uint32_t scratchWidth = g_GridScratchWidth;
    GridScratchCell *startCell = g_GridScratchPrimary + (int32_t)(startRow * scratchWidth) + startColumn;
    GridScratchCell *targetCell = g_GridScratchPrimary + (int32_t)(targetRow * scratchWidth) + targetColumn;
    if (GridPathLine_TestHexSegmentBlocked(g_GridPathHighCostMask,startRow,startColumn,startCell,targetCell)) {
      GridScratch_ResetTraversalFlagsAndCosts();
      GridPathCost_PropagateWeightedHexNeighbors(GRID_PATH_PROPAGATION_PASSES,startCell,targetRow,targetColumn);
      GridScratchCell *rowAbove = startCell - scratchWidth;
      if ((GRID_PATH_COST_MAX_REACHED < rowAbove[scratchWidth].pathCost) &&
          (GRID_PATH_COST_MAX_REACHED < rowAbove->pathCost) && (GRID_PATH_COST_MAX_REACHED < rowAbove[1].pathCost) &&
          (GRID_PATH_COST_MAX_REACHED < rowAbove[scratchWidth - 1].pathCost) &&
          (GRID_PATH_COST_MAX_REACHED < rowAbove[scratchWidth + 1].pathCost) &&
          (GRID_PATH_COST_MAX_REACHED < rowAbove[scratchWidth * 2 - 1].pathCost) &&
          (GRID_PATH_COST_MAX_REACHED < rowAbove[scratchWidth * 2].pathCost)) {
        /* cut off: the real route would end at the cell of the start's region nearest to the target */
        FieldGridCellCoordinate reachableRow;
        FieldGridCellCoordinate reachableColumn;
        GridPathRegion_MarkUnreachableFromCell(targetRow,targetColumn,startRow,startColumn,&reachableRow,
                                               &reachableColumn);
        Q12 endX;
        Q12 endY;
        AiImproved_ScratchCellCenter(reachableRow,reachableColumn,&endX,&endY);
        *outEndX = endX;
        *outEndY = endY;
        reachable = AiImproved_DistanceSquared(endX,endY,targetX,targetY) <= reachQ12 * reachQ12;
      }
      GridScratch_ResetTraversalFlagsAndCosts();
    }
  }
restore:
  g_ArmyRuntimeOrderHandlerMatrix11Columns24Classes.gridInfluenceAdd[classId](entity);
  g_GridPathEntityClassMask = savedEntityMask;
  g_GridPathBlockingMask = savedBlockingMask;
  g_GridPathHighCostMask = savedHighCostMask;
  return reachable;
}

bool AiImproved_GroupCanReachTarget(FactionRuntimeIndex factionIndex,ArmyRuntimeSlot **collectedArmies,
                                    uint32_t collectedCount,ModelRuntimeSlot *targetModelRuntime)
{
  if (!AiImproved_RepairsActive() || targetModelRuntime == nullptr || collectedCount == 0) {
    return true;
  }
  Q12 targetX;
  Q12 targetY;
  if (!AiImproved_ModelPosition(targetModelRuntime,&targetX,&targetY)) {
    return true;
  }
  /* the mover: the ground or water unit of the group nearest to the target (aircraft fly over everything) */
  ArmyRuntimeSlot *mover = nullptr;
  int64_t bestDistance = INT64_MAX;
  for (uint32_t i = 0; i < collectedCount; i++) {
    ArmyRuntimeSlot *army = collectedArmies[i];
    Q12 x;
    Q12 y;
    ModelDefinition *definition = army != nullptr ? AiImproved_RootDefinition(army) : nullptr;
    if (definition == nullptr || definition->accelerationPerTick == 0 || definition->targetClassIndex == 5 ||
        !AiImproved_ArmyPosition(army,&x,&y)) {
      continue;
    }
    int64_t distance = AiImproved_DistanceSquared(x,y,targetX,targetY);
    if (distance < bestDistance) {
      bestDistance = distance;
      mover = army;
    }
  }
  if (mover == nullptr) {
    return true;
  }
  /* within weapon range of the target's centre plus room for a building's footprint */
  int64_t reachQ12 = (int64_t)mover->weaponRangeQ12 + 4 * (int64_t)Q12_ONE;
  Q12 endX;
  Q12 endY;
  if (AiImproved_MoverCanReach(mover,targetX,targetY,reachQ12,&endX,&endY)) {
    return true;
  }
  ArmyRuntimeSlot *targetArmy = targetModelRuntime->ownerArmyRuntimeOrSavedOffset.armyRuntime;
  AiImproved_Log("Partei %d: Weg zum Ziel %d (ARM %u) bei %d/%d fehlt, die Einheiten kaemen nur bis %d/%d",factionIndex,
                 targetArmy != nullptr ? AiImproved_SlotIndex(targetArmy) : -1,
                 targetArmy != nullptr ? (unsigned)targetArmy->armyAssetId : 0u,targetX / Q12_ONE,targetY / Q12_ONE,
                 endX / Q12_ONE,endY / Q12_ONE);
  AiImproved_NoteUnreachableArea(factionIndex,targetX,targetY);
  return false;
}

uint32_t AiImproved_CountGroupUnitsDamaging(ArmyRuntimeSlot **collectedArmies,uint32_t collectedCount,
                                           ModelRuntimeSlot *targetModelRuntime)
{
  if (targetModelRuntime == nullptr) {
    return 0;
  }
  ModelDefinition *targetDefinition = AiImproved_RootDefinition(targetModelRuntime->ownerArmyRuntimeOrSavedOffset.armyRuntime);
  if (targetDefinition == nullptr) {
    return 0;
  }
  uint32_t targetClass = (uint32_t)targetDefinition->targetClassIndex;
  if (targetClass >= 8) {
    return 0;
  }
  uint32_t damaging = 0;
  for (uint32_t i = 0; i < collectedCount; i++) {
    if (collectedArmies[i] != nullptr && collectedArmies[i]->targetClassShotDamage[targetClass] > 0) {
      damaging++;
    }
  }
  return damaging;
}

/* Guard post of idle defenders: at most this far from the headquarters toward the threat (and at most 2/5 of the
   way), on the base site nearest to that point; the units stand on a ring of GUARD_RING around it, at one of
   eight places chosen by their slot so that they keep their place from pass to pass. */
static constexpr int64_t AI_IMPROVED_GUARD_DISTANCE_Q12 = 8 * (int64_t)Q12_ONE;
static constexpr int AI_IMPROVED_GUARD_RING_Q12 = 5 * Q12_ONE / 2;
static constexpr int64_t AI_IMPROVED_GUARD_TOLERANCE_Q12 = 3 * (int64_t)Q12_ONE;
/* A unit sent to its post gets no new order for a minute (it is on its way); one that then stands still for 10 s
   short of it (a building or a slope takes the place) keeps where it stands until the post moves. */
static constexpr uint32_t AI_IMPROVED_GUARD_TRAVEL_TICKS = 60 * AI_IMPROVED_TICKS_PER_SECOND;
static constexpr uint32_t AI_IMPROVED_GUARD_SETTLE_TICKS = 10 * AI_IMPROVED_TICKS_PER_SECOND;

void AiImproved_PositionIdleDefenders(ArmyRuntimeSlot **collectedArmies,uint32_t collectedCount)
{
  if (!AiImproved_IsActive() || collectedCount == 0 || collectedArmies[0] == nullptr || g_AiWorkspace09Count == 0) {
    return;
  }
  int factionIndex = collectedArmies[0]->factionIndex;
  if (!AiImproved_IsAiFaction(factionIndex)) {
    return;
  }
  Q12 baseX;
  Q12 baseY;
  Q12 threatX;
  Q12 threatY;
  if (!AiImproved_FactionBaseCentre(&baseX,&baseY) || !AiImproved_NearestThreat(baseX,baseY,&threatX,&threatY)) {
    return;
  }
  int64_t deltaX = (int64_t)threatX - baseX;
  int64_t deltaY = (int64_t)threatY - baseY;
  int64_t distance = FixedMath_Length2((int)deltaX,(int)deltaY);
  if (distance < 4 * (int64_t)Q12_ONE) {
    return; /* the threat is in the base: the alarm handles it */
  }
  int64_t guardDistance = distance * 2 / 5;
  if (guardDistance > AI_IMPROVED_GUARD_DISTANCE_Q12) guardDistance = AI_IMPROVED_GUARD_DISTANCE_Q12;
  Q12 pointX = (Q12)(baseX + deltaX * guardDistance / distance);
  Q12 pointY = (Q12)(baseY + deltaY * guardDistance / distance);
  /* on solid ground of the base: the nearest base site */
  FieldGridCell *guardCell = nullptr;
  int64_t bestDistance = INT64_MAX;
  FieldGridCell **gridCellCursor = g_AiWorkspace09Cells;
  for (uint32_t i = 0; i < g_AiWorkspace09Count; i++, gridCellCursor++) {
    int64_t cellDistance = AiImproved_ManhattanQ12((*gridCellCursor)->worldX,(*gridCellCursor)->worldY,pointX,pointY);
    if (cellDistance < bestDistance) {
      bestDistance = cellDistance;
      guardCell = *gridCellCursor;
    }
  }
  if (guardCell == nullptr) {
    return;
  }
  uint32_t moved = 0;
  for (uint32_t i = 0; i < collectedCount; i++) {
    ArmyRuntimeSlot *army = collectedArmies[i];
    ModelDefinition *definition = AiImproved_RootDefinition(army);
    if (definition == nullptr) continue;
    ModelRuntimeClassId unitClass = definition->runtimeClassId;
    if (unitClass != MODEL_RUNTIME_CLASS_01_GROUND && unitClass != MODEL_RUNTIME_CLASS_02_TRACKED &&
        unitClass != MODEL_RUNTIME_CLASS_03_ARTICULATED_WALKER && unitClass != MODEL_RUNTIME_CLASS_17_DEPLOYING_GLIDER) {
      continue; /* ships and aircraft keep their place */
    }
    if (!AiImproved_ArmyArmed(army)) continue;
    Q12 unitX;
    Q12 unitY;
    if (!AiImproved_ArmyPosition(army,&unitX,&unitY)) continue;
    int slot = AiImproved_SlotIndex(army);
    /* three rings of eight places (2.5, 5 and 7.5 units, the outer ones turned by half a place), chosen by the
       unit's slot: a dozen units no longer crowd onto one small ring */
    uint32_t ring = (uint32_t)(slot >> 3) % 3;
    FixedSinCos ringOffset = FixedMath_SinCosScaled((uint32_t)(slot & 7) * (FIXED_ANGLE16_FULL_TURN / 8) +
                                                    ring * (FIXED_ANGLE16_FULL_TURN / 16),
                                                    AI_IMPROVED_GUARD_RING_Q12 * (int)(ring + 1));
    Q12 postX = guardCell->worldX + ringOffset.cosValue;
    Q12 postY = guardCell->worldY + ringOffset.sinValue;
    int64_t postDistance = AiImproved_ManhattanQ12(unitX,unitY,postX,postY);
    if (postDistance <= AI_IMPROVED_GUARD_TOLERANCE_Q12) continue;
    AiImprovedUnitMemory *unitMemory = AiImproved_UnitMemory(army);
    if (unitMemory == nullptr) continue;
    uint32_t now = AiImproved_GameTicks();
    if (unitMemory->guardOrderTick != 0 &&
        AiImproved_ManhattanQ12(unitMemory->guardPostX,unitMemory->guardPostY,postX,postY) < (int64_t)Q12_ONE) {
      /* settled counts only near the post (a unit that went on an attack since is sent back) */
      if ((unitMemory->guardSettled != 0 && postDistance <= 8 * (int64_t)Q12_ONE) ||
          AiImproved_TicksSince(now,unitMemory->guardOrderTick) < (int32_t)AI_IMPROVED_GUARD_TRAVEL_TICKS) {
        continue;
      }
      if (AiImproved_TicksStandingStill(army) >= (int32_t)AI_IMPROVED_GUARD_SETTLE_TICKS) {
        unitMemory->guardSettled = 1;
        continue;
      }
    }
    army->aiUnitState = AI_UNIT_COMMANDED_STATE;
    ArmyRuntime_StartRoutedMoveCommand(postY,postX,reinterpret_cast<ArmyMovementRuntime *>(army)); /* the army slot as its movement view, as in units.cpp */
    AiImproved_RestartMovementClock(army);
    unitMemory->guardPostX = postX;
    unitMemory->guardPostY = postY;
    unitMemory->guardOrderTick = now == 0 ? 1 : now;
    unitMemory->guardSettled = 0;
    moved++;
  }
  if (moved != 0) {
    AiImproved_Log("Partei %d: %u Verteidiger beziehen Stellung bei %d/%d (Richtung %d/%d)",factionIndex,moved,
                   guardCell->worldX / Q12_ONE,guardCell->worldY / Q12_ONE,threatX / Q12_ONE,threatY / Q12_ONE);
  }
}

void AiImproved_OnGroupAttack(FactionRuntimeIndex factionIndex,ArmyRuntimeSlot **collectedArmies,uint32_t unitCount,
                              ModelRuntimeSlot *targetModelRuntime)
{
  if (!AiImproved_AnyActive()) {
    return;
  }
  uint32_t now = AiImproved_GameTicks();
  if (factionIndex >= 0 && factionIndex <= 7) {
    g_AiImprovedFactionMemory[factionIndex].lastGroupAttackTick = now;
    g_AiImprovedFactionMemory[factionIndex].attacksSinceStatus++;
  }
  /* a new order: the units waited in the base until now, which is no sign of being stuck */
  for (uint32_t i = 0; i < unitCount; i++) {
    AiImproved_RestartMovementClock(collectedArmies[i]);
  }
  if (!AiImproved_LogEnabled() || targetModelRuntime == nullptr) {
    return;
  }
  ArmyRuntimeSlot *targetArmy = targetModelRuntime->ownerArmyRuntimeOrSavedOffset.armyRuntime;
  if (targetArmy == nullptr) {
    return;
  }
  AiImproved_Log("Partei %d: Angriff mit %u Einheiten auf Partei %d, Ziel %d (ARM %u, Klasse %d), %u Wellen, "
                 "zuerst ARM %u",factionIndex,unitCount,targetArmy->factionIndex,AiImproved_SlotIndex(targetArmy),
                 (unsigned)targetArmy->armyAssetId,targetModelRuntime->definitionOrSavedId.runtimeDefinition->runtimeClassId,
                 AiImproved_IsActive() && AiImproved_Settings().waves ?
                      (unitCount + AiImproved_AttackWaveSize(unitCount) - 1) / AiImproved_AttackWaveSize(unitCount) : 1u,
                 unitCount != 0 && collectedArmies[0] != nullptr ? (unsigned)collectedArmies[0]->armyAssetId : 0u);
  /* the first unit's move after the order: where it stands, where the route leads (fallback = end of the route,
     primary = next route point) and its movement flags */
  if (unitCount != 0 && collectedArmies[0] != nullptr) {
    ArmyMovementRuntime *movement = ModelView_Cast<ArmyMovementRuntime>(collectedArmies[0]);
    Q12 x;
    Q12 y;
    Q12 tx;
    Q12 ty;
    if (movement != nullptr && AiImproved_ArmyPosition(collectedArmies[0],&x,&y) &&
        AiImproved_ArmyPosition(targetArmy,&tx,&ty)) {
      AiImproved_Log("Partei %d: Einheit %d bei %d/%d, Ziel bei %d/%d, Weg endet bei %d/%d, naechster Punkt %d/%d, "
                     "Bewegung %03X, Wartezeit %d",factionIndex,AiImproved_SlotIndex(collectedArmies[0]),x / Q12_ONE,
                     y / Q12_ONE,tx / Q12_ONE,ty / Q12_ONE,(movement->fallbackPosition).worldXQ12 / Q12_ONE,
                     (movement->fallbackPosition).worldYQ12 / Q12_ONE,movement->movementWorldXQ12 / Q12_ONE,
                     movement->movementWorldYQ12 / Q12_ONE,(unsigned)movement->movementStateFlags,
                     (int)movement->retryCountdown);
    }
  }
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Repairs: pioneer memory                                                                                      */

AiImprovedPioneerMemory *AiImproved_PioneerMemory(ArmyRuntimeSlot *armyRuntime)
{
  AiImprovedUnitMemory *memory = AiImproved_UnitMemory(armyRuntime);
  return memory != nullptr ? &memory->pioneer : nullptr;
}

void AiImproved_PioneerRememberFailedSite(AiImprovedPioneerMemory *memory,Q12 worldX,Q12 worldY)
{
  uint32_t now = AiImproved_GameTicks();
  int chosen = 0;
  for (int i = 0; i < AI_IMPROVED_PIONEER_FAILED_SITES; i++) {
    if (AiImproved_TicksSince(memory->failedUntilTick[i],now) <= 0) {
      chosen = i;
      break;
    }
    if (AiImproved_TicksSince(memory->failedUntilTick[i],memory->failedUntilTick[chosen]) < 0) {
      chosen = i;
    }
  }
  memory->failedX[chosen] = worldX;
  memory->failedY[chosen] = worldY;
  memory->failedUntilTick[chosen] = now + AI_IMPROVED_PIONEER_FAILED_SITE_TICKS;
}

bool AiImproved_PioneerSiteFailed(const AiImprovedPioneerMemory *memory,Q12 worldX,Q12 worldY)
{
  uint32_t now = AiImproved_GameTicks();
  for (int i = 0; i < AI_IMPROVED_PIONEER_FAILED_SITES; i++) {
    if (AiImproved_TicksSince(memory->failedUntilTick[i],now) > 0 &&
        AiImproved_ManhattanQ12(worldX,worldY,memory->failedX[i],memory->failedY[i]) < 3 * (int64_t)Q12_ONE) {
      return true;
    }
  }
  return false;
}

/* ------------------------------------------------------------------------------------------------------------ */
/* Improved AI (level 2 and up)                                                                                 */

uint32_t AiImproved_ArmyUnitLimit()
{
  return g_AiImprovedLevelParameters[AiImproved_Level()].armyUnitLimit;
}

uint32_t AiImproved_GroupAttackStrength(FactionRuntimeIndex factionIndex,uint32_t ownUnitCount)
{
  const uint32_t levelUnits = g_AiImprovedLevelParameters[AiImproved_Level()].groupAttackUnits;
  uint32_t units = levelUnits;
  /* A big army attacks in a big group: up to a third of it, at most twice the level's group. The user's battle
     showed 33 attacks, every one of them four to six units, while the faction had twenty-five. */
  uint32_t wanted = ownUnitCount / 3;
  if (wanted > 2 * levelUnits) wanted = 2 * levelUnits;
  if (wanted > units) units = wanted;
  /* but after AI_IMPROVED_GROUP_WAIT_TICKS without an attack the level's group is enough again, so a faction
     whose units are tied up does not stop attacking altogether */
  if ((factionIndex >= 1) && (factionIndex <= 7)) {
    const AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
    if (memory->lastGroupAttackTick != 0 &&
        AiImproved_TicksSince(AiImproved_GameTicks(),memory->lastGroupAttackTick) >=
        (int32_t)AI_IMPROVED_GROUP_WAIT_TICKS) {
      units = levelUnits;
    }
  }
  /* A small army (early game, heavy losses) attacks with three quarters of what it has, at least two units,
     instead of waiting for a group it cannot reach. */
  uint32_t reachable = ownUnitCount * 3 / 4;
  if (reachable < 2) reachable = 2;
  if (units > reachable) units = reachable;
  return units * 256;
}

uint32_t AiImproved_ScaleAiExtraction(uint32_t gameSpeedQ8)
{
  return gameSpeedQ8 * g_AiImprovedLevelParameters[AiImproved_Level()].extractionPercent / 100;
}

/* Largest number of class-2 (shot) points in one node of the serialized model tree below node. */
static int AiImproved_CountShotPointsPerNode(const MdlSerializedNodeHeader *node,int depth)
{
  int best = 0;
  if (node == nullptr || depth > 8) {
    return 0;
  }
  ModelResource *modelResource = node->spriteAssetReference.modelResource;
  if (modelResource != nullptr) {
    int count = 0;
    ModelPackedPointRecord *record = modelResource->packedPointRecords();
    for (uint32_t i = 0; i < (uint32_t)modelResource->packedLookupTableEntryCount; i++) {
      if ((record[i].packedLookupKey & 0xf) == (uint32_t)MODEL_POINT_CLASS_SHOT) {
        count++;
      }
    }
    best = count;
  }
  uint32_t childCount = (uint32_t)node->childCount;
  if (childCount > 6) childCount = 6;
  for (uint32_t i = 0; i < childCount; i++) {
    if (node->childSerializedOffsets[i] == 0) continue;
    int childBest = AiImproved_CountShotPointsPerNode(
         Thandor_U32ToPointer<MdlSerializedNodeHeader>(node->childSerializedOffsets[i]),depth + 1); /* 32-bit format field */
    if (childBest > best) best = childBest;
  }
  return best;
}

int AiImproved_WeaponShotsPerReload(const ModelDefinition *weaponDefinition)
{
  if (weaponDefinition == nullptr || weaponDefinition->rootNodeOffsetOrPointer == 0) {
    return 1;
  }
  int shots = AiImproved_CountShotPointsPerNode(
       Thandor_U32ToPointer<MdlSerializedNodeHeader>(weaponDefinition->rootNodeOffsetOrPointer),0); /* 32-bit format field */
  if (shots < 1) shots = 1;
  if (shots > 12) shots = 12;
  return shots;
}

bool AiImproved_UnitRetreating(const ArmyRuntimeSlot *armyRuntime)
{
  return ((uint32_t)armyRuntime->aiUnitFlags & AI_IMPROVED_UNIT_FLAG_RETREATING) != 0;
}

static void AiImproved_SetRetreating(ArmyRuntimeSlot *armyRuntime,bool retreating)
{
  uint32_t flags = (uint32_t)armyRuntime->aiUnitFlags;
  flags = retreating ? (flags | AI_IMPROVED_UNIT_FLAG_RETREATING) : (flags & ~AI_IMPROVED_UNIT_FLAG_RETREATING);
  armyRuntime->aiUnitFlags = (ArmyAiUnitFlags)flags;
}

/* Home point of the faction: its first own structure (workspace 00). */
static bool AiImproved_FindHomePoint(Q12 *outWorldX,Q12 *outWorldY)
{
  AiStructureWorkspaceEntry *entry = g_AiWorkspace00Structures;
  for (uint32_t i = 0; i < g_AiWorkspace00Count; i++, entry++) {
    ModelRuntimeSlot *structure = entry->runtimeSlotAddressOrZero;
    if (structure == nullptr) continue;
    ArmyRuntimeSlot *structureArmy = structure->ownerArmyRuntimeOrSavedOffset.armyRuntime;
    if (structureArmy == nullptr || structureArmy->modelNodeRuntime == nullptr) continue;
    *outWorldX = (structureArmy->modelNodeRuntime->worldTransform).translation.x;
    *outWorldY = (structureArmy->modelNodeRuntime->worldTransform).translation.y;
    return true;
  }
  return false;
}

/* The spot a retreating unit drives to. The original home point is the faction's first building, so every damaged
   unit used to drive onto one and the same point inside the base: they piled up around that building, blocked
   the factory doors and each other, and a unit that arrived last stood in the gate. With ki.ini
   "aufstellung = an" each unit gets a free spot of its own around the home point instead (by its slot, so it
   keeps the same one), from the rings of ArmyFormation_GroupSpot. Only spots well inside the home radius
   count, so the unit is "at home" when it arrives; otherwise it keeps the plain home point. */
static constexpr int AI_IMPROVED_HOME_SPOT_COUNT = 19;  /* the point and two rings: 1 + 6 + 12 */
static constexpr int64_t AI_IMPROVED_HOME_SPOT_MARGIN_Q12 = 3 * (int64_t)Q12_ONE;

static void AiImproved_HomeSpot(ArmyRuntimeSlot *armyRuntime,Q12 homeX,Q12 homeY,Q12 *outWorldX,Q12 *outWorldY)
{
  *outWorldX = homeX;
  *outWorldY = homeY;
  if (!ArmyFormation_Active()) return;
  ModelDefinition *definition = AiImproved_RootDefinition(armyRuntime);
  int slot = AiImproved_SlotIndex(armyRuntime);
  if (definition == nullptr || slot < 0) return;
  Q12 spotX;
  Q12 spotY;
  if (!ArmyFormation_GroupSpot(slot % AI_IMPROVED_HOME_SPOT_COUNT,definition,homeX,homeY,&spotX,&spotY)) return;
  if (AiImproved_ManhattanQ12(spotX,spotY,homeX,homeY) > AI_IMPROVED_HOME_RADIUS_Q12 - AI_IMPROVED_HOME_SPOT_MARGIN_Q12) {
    return;
  }
  *outWorldX = spotX;
  *outWorldY = spotY;
}

bool AiImproved_HandleDamagedUnit(FactionRuntimeIndex factionIndex,ArmyRuntimeSlot *armyRuntime)
{
  (void)factionIndex;
  if (armyRuntime == nullptr || armyRuntime->modelNodeRuntime == nullptr) {
    return false;
  }
  Q12 condition = AiImproved_ConditionQ12(armyRuntime);
  bool retreating = AiImproved_UnitRetreating(armyRuntime);

  if ((int)condition >= AI_IMPROVED_REJOIN_CONDITION_Q12) {
    /* repaired (or never badly hit): normal duty */
    if (retreating) AiImproved_SetRetreating(armyRuntime,false);
    return false;
  }
  Q12 homeX;
  Q12 homeY;
  if (!AiImproved_FindHomePoint(&homeX,&homeY)) {
    /* no base to return to: fight on */
    if (retreating) AiImproved_SetRetreating(armyRuntime,false);
    return false;
  }
  Q12 unitX = (armyRuntime->modelNodeRuntime->worldTransform).translation.x;
  Q12 unitY = (armyRuntime->modelNodeRuntime->worldTransform).translation.y;
  if (AiImproved_ManhattanQ12(unitX,unitY,homeX,homeY) <= AI_IMPROVED_HOME_RADIUS_Q12) {
    /* at home: no new orders until repaired, but it defends the base (the retreat flag is cleared, so its own
       target search runs again) */
    if (retreating) AiImproved_SetRetreating(armyRuntime,false);
    return true;
  }
  if (retreating) {
    /* on the way home; order again if the move ended or was replaced */
    if (!Any(armyRuntime->movementStateFlags & ARMY_MOVEMENT_ACTIVE)) {
      Q12 spotX;
      Q12 spotY;
      AiImproved_HomeSpot(armyRuntime,homeX,homeY,&spotX,&spotY);
      ArmyRuntime_StartRoutedMoveCommand(spotY,spotX,reinterpret_cast<ArmyMovementRuntime *>(armyRuntime)); /* the army slot as its movement view, as in units.cpp */
    }
    return true;
  }
  if ((int)condition < AI_IMPROVED_RETREAT_CONDITION_Q12) {
    /* drop the attack order and drive home */
    ArmyRuntime_ResolveCommandTarget(nullptr,armyRuntime);
    armyRuntime->aiUnitState = AI_UNIT_COMMANDED_STATE;
    armyRuntime->aiUnitFlags = armyRuntime->aiUnitFlags & ~AI_UNIT_STATE94_GROUP_ASSIGNED;
    Q12 spotX;
    Q12 spotY;
    AiImproved_HomeSpot(armyRuntime,homeX,homeY,&spotX,&spotY);
    ArmyRuntime_StartRoutedMoveCommand(spotY,spotX,reinterpret_cast<ArmyMovementRuntime *>(armyRuntime)); /* the army slot as its movement view, as in units.cpp */
    AiImproved_SetRetreating(armyRuntime,true);
    AiImproved_Log("Partei %d: Einheit %d (ARM %u) zieht sich zur Reparatur zurueck",armyRuntime->factionIndex,
                   AiImproved_SlotIndex(armyRuntime),(unsigned)armyRuntime->armyAssetId);
    return true;
  }
  /* 40..75 %: keeps its current job, but joins no new attack group (AiImproved_UnitReadyForAttack) */
  return false;
}

bool AiImproved_UnitReadyForAttack(ArmyRuntimeSlot *armyRuntime)
{
  if (AiImproved_UnitRetreating(armyRuntime)) {
    return false;
  }
  if ((int)AiImproved_ConditionQ12(armyRuntime) >= AI_IMPROVED_REJOIN_CONDITION_Q12) {
    return true;
  }
  /* Between the retreat and the rejoin condition (40 to 75 %) a unit keeps its job: it does not drive home, so
     it never repairs either. Demanding 75 % of it as well left those units standing at their guard posts for the
     rest of the game, and the attack groups stayed at four to six units while the faction had twenty-five.
     A damaged unit that is not at home therefore joins an attack; one standing at home waits for its repair. */
  Q12 homeX;
  Q12 homeY;
  Q12 unitX;
  Q12 unitY;
  if (!AiImproved_FindHomePoint(&homeX,&homeY) || !AiImproved_ArmyPosition(armyRuntime,&unitX,&unitY)) {
    return true;
  }
  return AiImproved_ManhattanQ12(unitX,unitY,homeX,homeY) > AI_IMPROVED_HOME_RADIUS_Q12;
}

static bool AiImproved_ModelPosition(ModelRuntimeSlot *modelRuntime,Q12 *outWorldX,Q12 *outWorldY)
{
  if (modelRuntime == nullptr) return false;
  return AiImproved_ArmyPosition(modelRuntime->ownerArmyRuntimeOrSavedOffset.armyRuntime,outWorldX,outWorldY);
}

/* Armed hostiles (workspace 02) near a point. Static ones (towers: accelerationPerTick 0) count double. */
static int AiImproved_CountDefenders(Q12 worldX,Q12 worldY)
{
  int defenders = 0;
  AiRuntimeWorkspaceEntry *entry = g_AiWorkspace02VisibleHostiles;
  for (uint32_t i = 0; i < g_AiWorkspace02Count; i++, entry++) {
    ModelRuntimeSlot *hostile = entry->modelRuntime;
    Q12 hx;
    Q12 hy;
    if (!AiImproved_ModelPosition(hostile,&hx,&hy)) continue;
    if (AiImproved_ManhattanQ12(worldX,worldY,hx,hy) > AI_IMPROVED_DEFENCE_RADIUS_Q12) continue;
    ArmyRuntimeSlot *hostileArmy = hostile->ownerArmyRuntimeOrSavedOffset.armyRuntime;
    if (hostileArmy == nullptr || !AiImproved_ArmyArmed(hostileArmy)) continue;
    defenders += (hostile->definitionOrSavedId.runtimeDefinition->accelerationPerTick == 0) ? 2 : 1;
  }
  return defenders;
}

/* A hostile building standing this close (Manhattan) to one of the planning faction's own buildings is a squatter:
   a mine on the deposit the faction wanted, a tower at its gate. The group attack prefers it by this factor. */
static constexpr int64_t AI_IMPROVED_SQUATTER_RADIUS_Q12 = 16 * (int64_t)Q12_ONE;
static constexpr int64_t AI_IMPROVED_SQUATTER_BONUS = 4;

static int64_t AiImproved_ScoreGroupTarget(ModelRuntimeSlot *target,Q12 targetX,Q12 targetY,Q12 centreX,
                                           Q12 centreY,const uint32_t *classBaseScores,ArmyRuntimeSlot **collectedArmies,
                                           uint32_t collectedCount)
{
  int64_t score = (int64_t)classBaseScores[target->definitionOrSavedId.runtimeDefinition->runtimeClassId] << 10;
  if (score == 0) return 0;
  /* only the share of the group that can damage the target counts */
  uint32_t damaging = AiImproved_CountGroupUnitsDamaging(collectedArmies,collectedCount,target);
  if (damaging == 0) return 0;
  score = score * (int64_t)damaging / (int64_t)collectedCount;
  int64_t distanceUnits = AiImproved_ManhattanQ12(targetX,targetY,centreX,centreY) / Q12_ONE;
  int64_t distanceScale = target->definitionOrSavedId.runtimeDefinition->accelerationPerTick != 0 ?
                          AI_IMPROVED_MOBILE_DISTANCE_SCALE : AI_IMPROVED_DISTANCE_SCALE;
  score = score * distanceScale / (distanceScale + distanceUnits);
  int defenders = AiImproved_CountDefenders(targetX,targetY);
  score = score * 4 / (4 + defenders);
  /* a hostile building in the faction's own area comes first: it takes ground and resources the faction counts on,
     and the original AI left such a building standing for the rest of the game */
  if (target->definitionOrSavedId.runtimeDefinition->accelerationPerTick == 0 &&
      AiImproved_NearestOwnStructureManhattanQ12(targetX,targetY) < AI_IMPROVED_SQUATTER_RADIUS_Q12) {
    score = score * AI_IMPROVED_SQUATTER_BONUS;
  }
  return score < 1 ? 1 : score;
}

ModelRuntimeSlot *AiImproved_SelectGroupTarget(FactionRuntimeIndex factionIndex,ArmyRuntimeSlot **collectedArmies,
                                               uint32_t collectedCount,const uint32_t *classBaseScores)
{
  if (collectedCount == 0) return nullptr;
  /* centre of the group */
  int64_t sumX = 0;
  int64_t sumY = 0;
  uint32_t counted = 0;
  for (uint32_t i = 0; i < collectedCount; i++) {
    ArmyRuntimeSlot *army = collectedArmies[i];
    if (army == nullptr || army->modelNodeRuntime == nullptr) continue;
    sumX += (army->modelNodeRuntime->worldTransform).translation.x;
    sumY += (army->modelNodeRuntime->worldTransform).translation.y;
    counted++;
  }
  if (counted == 0) return nullptr;
  Q12 centreX = (Q12)(sumX / (int64_t)counted);
  Q12 centreY = (Q12)(sumY / (int64_t)counted);

  int64_t bestScore = 0;
  ModelRuntimeSlot *bestTarget = nullptr;
  if (g_AiWorkspace07Count != 0) {
    AiTargetWorkspaceEntry *entry = g_AiWorkspace07Targets;
    for (uint32_t i = 0; i < g_AiWorkspace07Count; i++, entry++) {
      if (entry->modelRuntime == nullptr || !AiImproved_GroupTargetAllowed(factionIndex,entry->modelRuntime)) continue;
      int64_t score = AiImproved_ScoreGroupTarget(entry->modelRuntime,entry->worldXQ12,entry->worldYQ12,centreX,
                                                  centreY,classBaseScores,collectedArmies,collectedCount);
      /* ties go to the earlier (nearer in list order) candidate: deterministic and stable */
      if (score > bestScore) {
        bestScore = score;
        bestTarget = entry->modelRuntime;
      }
    }
  }
  else {
    /* unseen hostiles, read as the 8-byte records they are (the original walks them with a 16-byte stride) */
    AiRuntimeWorkspaceEntry *entry = g_AiWorkspace03UnseenHostiles;
    for (uint32_t i = 0; i < g_AiWorkspace03Count; i++, entry++) {
      Q12 tx;
      Q12 ty;
      if (!AiImproved_ModelPosition(entry->modelRuntime,&tx,&ty)) continue;
      if (!AiImproved_GroupTargetAllowed(factionIndex,entry->modelRuntime)) continue;
      int64_t score = AiImproved_ScoreGroupTarget(entry->modelRuntime,tx,ty,centreX,centreY,classBaseScores,
                                                  collectedArmies,collectedCount);
      if (score > bestScore) {
        bestScore = score;
        bestTarget = entry->modelRuntime;
      }
    }
  }
  return bestTarget;
}

/* ---- defence buildings (improved AI) ---- */

static bool AiImproved_IsDefenceBuilding(PckArmyAssetIdCatalog armyAssetId)
{
  return armyAssetId >= ARM_0340_BUILDING_MDL0314 && armyAssetId < ARM_0380_BUILDING_MDL0329;
}

uint32_t AiImproved_ProactiveTowerLimit(FactionRuntimeIndex factionIndex)
{
  uint32_t limit = g_AiImprovedLevelParameters[AiImproved_Level()].proactiveTowers;
  if (AiImproved_DefenceMode(factionIndex)) {
    limit += AI_IMPROVED_THREAT_EXTRA_TOWERS;
  }
  return limit;
}

uint32_t AiImproved_DefenceWeightFactor(FactionRuntimeIndex factionIndex)
{
  return AiImproved_DefenceMode(factionIndex) ? AI_IMPROVED_THREAT_TOWER_WEIGHT : 1;
}

uint32_t AiImproved_CountPlacedDefenceBuildings()
{
  uint32_t count = 0;
  AiStructureWorkspaceEntry *entry = g_AiWorkspace00Structures;
  for (uint32_t i = 0; i < g_AiWorkspace00Count; i++, entry++) {
    if (entry->runtimeSlotAddressOrZero != nullptr && AiImproved_IsDefenceBuilding(entry->armyAssetId)) {
      count++;
    }
  }
  return count;
}

uint32_t AiImproved_CountPendingDefenceBuildings()
{
  uint32_t count = 0;
  AiRuntimeWorkspaceEntry *entry = g_AiWorkspace04RequestedAssets;
  for (uint32_t i = 0; i < g_AiWorkspace04Count; i++, entry++) {
    if (AiImproved_IsDefenceBuilding(entry->armyAssetId)) {
      count++;
    }
  }
  return count;
}

bool AiImproved_DefenceBuildingNear(Q12 worldX,Q12 worldY,int64_t radiusQ12)
{
  AiStructureWorkspaceEntry *entry = g_AiWorkspace00Structures;
  for (uint32_t i = 0; i < g_AiWorkspace00Count; i++, entry++) {
    ModelRuntimeSlot *structure = entry->runtimeSlotAddressOrZero;
    if (structure == nullptr || !AiImproved_IsDefenceBuilding(entry->armyAssetId)) continue;
    ModelRuntimeNode *node = structure->rootModelNodeOrSavedOffset.modelNode;
    if (AiImproved_ManhattanQ12(worldX,worldY,(node->worldTransform).translation.x,(node->worldTransform).translation.y) <
        radiusQ12) {
      return true;
    }
  }
  return false;
}

/* Where the enemy is expected, as far as the faction has seen (workspace 02: hostiles in sight and the buildings it
   has seen; never workspace 03, which also holds what the faction never saw): the seen hostile building nearest to
   the point, else the nearest hostile in sight, else the centre of the map. */
bool AiImproved_NearestThreat(Q12 worldX,Q12 worldY,Q12 *outThreatX,Q12 *outThreatY)
{
  int64_t bestStructure = INT64_MAX;
  int64_t bestAny = INT64_MAX;
  Q12 structureX = 0;
  Q12 structureY = 0;
  Q12 anyX = 0;
  Q12 anyY = 0;
  AiRuntimeWorkspaceEntry *entry = g_AiWorkspace02VisibleHostiles;
  for (uint32_t i = 0; i < g_AiWorkspace02Count; i++, entry++) {
    ModelRuntimeSlot *hostile = entry->modelRuntime;
    if (hostile == nullptr) continue;
    ModelRuntimeNode *node = hostile->rootModelNodeOrSavedOffset.modelNode;
    Q12 hx = (node->worldTransform).translation.x;
    Q12 hy = (node->worldTransform).translation.y;
    int64_t distance = AiImproved_ManhattanQ12(worldX,worldY,hx,hy);
    if (distance < bestAny) {
      bestAny = distance;
      anyX = hx;
      anyY = hy;
    }
    if (hostile->definitionOrSavedId.runtimeDefinition->accelerationPerTick == 0 && distance < bestStructure) {
      bestStructure = distance;
      structureX = hx;
      structureY = hy;
    }
  }
  if (bestStructure != INT64_MAX) {
    *outThreatX = structureX;
    *outThreatY = structureY;
    return true;
  }
  if (bestAny != INT64_MAX) {
    *outThreatX = anyX;
    *outThreatY = anyY;
    return true;
  }
  if (g_InGameRuntimeRoot == nullptr || g_InGameRuntimeRoot->worldRuntime.fieldGrid == nullptr) {
    return false;
  }
  FieldGridAsset *grid = g_InGameRuntimeRoot->worldRuntime.fieldGrid;
  if (grid->gridWidth == 0 || grid->gridHeight == 0) {
    return false;
  }
  FieldGridCell *centre = &grid->cells[(grid->gridHeight / 2) * grid->gridWidth + grid->gridWidth / 2];
  *outThreatX = centre->worldX;
  *outThreatY = centre->worldY;
  return true;
}

/* Centre of the planning faction's base: its headquarters, else its first own structure. */
bool AiImproved_FactionBaseCentre(Q12 *outWorldX,Q12 *outWorldY)
{
  if (g_AiWorkspaceOwnedAsset300Runtime != nullptr) {
    ModelRuntimeNode *node = g_AiWorkspaceOwnedAsset300Runtime->rootModelNodeOrSavedOffset.modelNode;
    *outWorldX = (node->worldTransform).translation.x;
    *outWorldY = (node->worldTransform).translation.y;
    return true;
  }
  return AiImproved_FindHomePoint(outWorldX,outWorldY);
}

uint32_t AiImproved_ShotHitFactorQ8(FactionRuntimeIndex factionIndex,const ShotDefinition *shotDefinition,int targetClass)
{
  if (shotDefinition == nullptr || targetClass < 1 || targetClass > 6 || factionIndex < 0 || factionIndex > 7) {
    return 256;
  }
  /* straight lines, beams of fixed range and guided missiles hit what they aim at */
  if (shotDefinition->trajectoryMode == SHOT_TRAJECTORY_DIRECT_LINE ||
      shotDefinition->trajectoryMode == SHOT_TRAJECTORY_FIXED_RANGE || shotDefinition->guidanceTurnLimitAngle16 != 0) {
    return 256;
  }
  int64_t targetSpeed = g_AiImprovedFactionMemory[factionIndex].seenClassSpeed[targetClass];
  int64_t shotSpeed = shotDefinition->launchSpeedQ12;
  if (targetSpeed <= 0 || shotSpeed <= 0) {
    return 256;
  }
  return (uint32_t)(256 * shotSpeed / (shotSpeed + 3 * targetSpeed));
}

/* ---- bridge rule (improved AI) ---- */

/* A deposit at most this far (Manhattan) from the nearest base site is bridged; at most this many storage
   buildings are built toward one deposit; a deposit the chain did not reach is left alone for 10 minutes.
   The reach stays short on purpose: a deposit beyond a mountain slope is not meant to be built over, it is reached
   with the pioneer vehicle, whose base opens a building area of its own there. */
static constexpr int64_t AI_IMPROVED_BRIDGE_MAX_GAP_Q12 = 8 * (int64_t)Q12_ONE;
static constexpr uint32_t AI_IMPROVED_BRIDGE_MAX_STEPS = 3;
static constexpr uint32_t AI_IMPROVED_BRIDGE_FAILED_TICKS = 10 * 60 * AI_IMPROVED_TICKS_PER_SECOND;
/* A chain is only given up after this long without a site that brings it closer: the storage building just placed
   has to finish before its support area opens the ground beyond it. */
static constexpr uint32_t AI_IMPROVED_BRIDGE_PATIENCE_TICKS = 60 * AI_IMPROVED_TICKS_PER_SECOND;

static bool AiImproved_BridgeFailed(const AiImprovedFactionMemory *memory,Q12 worldX,Q12 worldY)
{
  uint32_t now = AiImproved_GameTicks();
  for (int i = 0; i < AI_IMPROVED_BRIDGE_FAILED_SITES; i++) {
    if (memory->bridgeFailedUntilTick[i] == 0 ||
        AiImproved_ManhattanQ12(worldX,worldY,memory->bridgeFailedX[i],memory->bridgeFailedY[i]) >=
        3 * (int64_t)Q12_ONE) {
      continue;
    }
    if (memory->bridgeFailedForGood[i] != 0 || AiImproved_TicksSince(memory->bridgeFailedUntilTick[i],now) > 0) {
      return true;
    }
  }
  return false;
}

static void AiImproved_BridgeGiveUp(int factionIndex,AiImprovedFactionMemory *memory)
{
  uint32_t now = AiImproved_GameTicks();
  /* the entry of this deposit, else a free one, else the one that runs out first (never one given up for good) */
  int chosen = -1;
  for (int i = 0; i < AI_IMPROVED_BRIDGE_FAILED_SITES; i++) {
    if (memory->bridgeFailedUntilTick[i] != 0 &&
        AiImproved_ManhattanQ12(memory->bridgeX,memory->bridgeY,memory->bridgeFailedX[i],memory->bridgeFailedY[i]) <
        3 * (int64_t)Q12_ONE) {
      chosen = i;
      break;
    }
  }
  if (chosen < 0) {
    for (int i = 0; i < AI_IMPROVED_BRIDGE_FAILED_SITES; i++) {
      if (memory->bridgeFailedUntilTick[i] == 0) {
        chosen = i;
        break;
      }
      if (memory->bridgeFailedForGood[i] != 0) {
        continue;
      }
      if (chosen < 0 || memory->bridgeFailedForGood[chosen] != 0 ||
          AiImproved_TicksSince(memory->bridgeFailedUntilTick[i],memory->bridgeFailedUntilTick[chosen]) < 0) {
        chosen = i;
      }
    }
    if (chosen < 0) {
      chosen = 0; /* every entry is a deposit given up for good: this one replaces the first */
    }
    memory->bridgeFailedCount[chosen] = 0;
    memory->bridgeFailedForGood[chosen] = 0;
  }
  memory->bridgeFailedX[chosen] = memory->bridgeX;
  memory->bridgeFailedY[chosen] = memory->bridgeY;
  memory->bridgeFailedCount[chosen]++;
  uint32_t until = now + AI_IMPROVED_BRIDGE_FAILED_TICKS;
  memory->bridgeFailedUntilTick[chosen] = until == 0 ? 1 : until;
  if (memory->bridgeFailedCount[chosen] >= AI_IMPROVED_BRIDGE_FAILED_FOR_GOOD) {
    memory->bridgeFailedForGood[chosen] = 1;
  }
  AiImproved_Log("Partei %d: Bruecke zur Quelle bei %d/%d erreicht sie nicht, aufgegeben (%u Gebaeude, %u. Versuch)%s",
                 factionIndex,memory->bridgeX / Q12_ONE,memory->bridgeY / Q12_ONE,memory->bridgeSteps,
                 memory->bridgeFailedCount[chosen],
                 memory->bridgeFailedForGood[chosen] != 0 ? " - diese Quelle bleibt liegen" : "");
  memory->bridgeValid = 0;
  memory->bridgeSteps = 0;
  memory->bridgeNoProgressSince = 0;
}

/* Chooses the deposit of workspace 08 to bridge: not placeable now, no hostile in sight within the original's
   minimum distance, at most AI_IMPROVED_BRIDGE_MAX_GAP_Q12 from the nearest base site (workspace 09); Xenite
   counts its gap once, Tritium twice. False (and no target) when none qualifies. */
static bool AiImproved_SelectBridgeTarget(FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime)
{
  AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
  if (g_AiWorkspace08Count == 0 || g_AiWorkspace09Count == 0 || g_AiKnowledgeData == nullptr) {
    memory->bridgeValid = 0;
    return false;
  }
  int64_t bestScore = INT64_MAX;
  AiTerrainFeatureWorkspaceEntry *bestEntry = nullptr;
  AiTerrainFeatureWorkspaceEntry *currentEntry = nullptr;
  AiTerrainFeatureWorkspaceEntry *siteEntry = g_AiWorkspace08TerrainFeatureSites;
  for (uint32_t i = 0; i < g_AiWorkspace08Count; i++, siteEntry++) {
    FieldGridCell *siteCell = siteEntry->cell;
    if (siteCell == nullptr) continue;
    if (!AiPlacement_TestMode4AtWorkspaceRecord(siteEntry->armyAssetId,siteCell,factionIndex,worldRuntime)) {
      continue; /* placeable now: the mine candidate builds it */
    }
    if (AiImproved_BridgeFailed(memory,siteCell->worldX,siteCell->worldY)) continue;
    if (AiHostileWorkspace_GetNearestVisibleHostileDistance(siteCell->worldY,siteCell->worldX) <
        (int)(g_AiKnowledgeData->parameters).specialSiteMinimumWorkspaceDistanceQ12) continue;
    int64_t gap = INT64_MAX;
    FieldGridCell **baseCell = g_AiWorkspace09Cells;
    for (uint32_t k = 0; k < g_AiWorkspace09Count; k++, baseCell++) {
      int64_t distance = AiImproved_ManhattanQ12((*baseCell)->worldX,(*baseCell)->worldY,siteCell->worldX,siteCell->worldY);
      if (distance < gap) gap = distance;
    }
    if (gap > AI_IMPROVED_BRIDGE_MAX_GAP_Q12) continue;
    /* the deposit of the chain under way keeps its place while it qualifies (a change would restart the count) */
    if (memory->bridgeValid != 0 &&
        AiImproved_ManhattanQ12(memory->bridgeX,memory->bridgeY,siteCell->worldX,siteCell->worldY) < (int64_t)Q12_ONE) {
      currentEntry = siteEntry;
    }
    /* Xenite before Tritium, but only while the faction has a Tritium pump: without one it has no energy, and
       its factories build next to nothing (the user saw the AI bridge to Xenite again and again and leave its
       Tritium deposit lying). */
    bool tritium = siteEntry->armyAssetId != ARM_0330_BUILDING_MDL0303;
    bool hasPump = AiPrimaryWorkspace_CountAssignedEntriesById(ARM_0332_BUILDING_MDL0302) != 0;
    int64_t score = (tritium && hasPump) ? gap * 2 : gap;
    if (score < bestScore) {
      bestScore = score;
      bestEntry = siteEntry;
    }
  }
  if (currentEntry != nullptr) {
    bestEntry = currentEntry;
  }
  if (bestEntry == nullptr) {
    memory->bridgeValid = 0;
    return false;
  }
  FieldGridCell *targetCell = bestEntry->cell;
  bool sameTarget = memory->bridgeValid != 0 &&
                    AiImproved_ManhattanQ12(memory->bridgeX,memory->bridgeY,targetCell->worldX,targetCell->worldY) <
                    (int64_t)Q12_ONE;
  if (!sameTarget) {
    memory->bridgeSteps = 0;
    memory->bridgeNoProgressSince = 0;
    AiImproved_Log("Partei %d: baut Bruecke zur Quelle (ARM %u) bei %d/%d",factionIndex,
                   (unsigned)bestEntry->armyAssetId,targetCell->worldX / Q12_ONE,targetCell->worldY / Q12_ONE);
  }
  memory->bridgeValid = 1;
  memory->bridgeX = targetCell->worldX;
  memory->bridgeY = targetCell->worldY;
  memory->bridgeAssetId = (uint32_t)bestEntry->armyAssetId;
  return true;
}

/* The bridge rule runs with the repairs (nearby deposits the AI never used were one of the faults the user saw in
   the campaign) and with the improved AI. */
static bool AiImproved_BridgeRuleActive(FactionRuntimeIndex factionIndex)
{
  return (AiImproved_RepairsActive() || AiImproved_IsActive()) && AiImproved_IsAiFaction(factionIndex);
}

void AiImproved_AddBridgeCandidate(FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime)
{
  if (!AiImproved_BridgeRuleActive(factionIndex)) {
    return;
  }
  if (!AiImproved_SelectBridgeTarget(factionIndex,worldRuntime)) {
    return;
  }
  AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
  if (memory->bridgeSteps >= AI_IMPROVED_BRIDGE_MAX_STEPS) {
    AiImproved_BridgeGiveUp(factionIndex,memory);
    return;
  }
  if (AiPrimaryWorkspace_HasUnassignedEntryById(ARM_0333_BUILDING_MDL0307)) {
    return; /* one storage building waits for its place already */
  }
  uint32_t weight = memory->bridgeAssetId == ARM_0330_BUILDING_MDL0303 ?
                    (g_AiKnowledgeData->parameters).specialSite14aBaseWeight :
                    (g_AiKnowledgeData->parameters).specialSite14cBaseWeight;
  AiCandidateWorkspace_AddOrAccumulateWeightedEntry(ARM_0333_BUILDING_MDL0307,weight / 2,1);
}

bool AiImproved_BridgeTarget(FactionRuntimeIndex factionIndex,Q12 *outWorldX,Q12 *outWorldY)
{
  if (!AiImproved_BridgeRuleActive(factionIndex)) {
    return false;
  }
  const AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
  if (memory->bridgeValid == 0 || memory->bridgeSteps >= AI_IMPROVED_BRIDGE_MAX_STEPS) {
    return false;
  }
  *outWorldX = memory->bridgeX;
  *outWorldY = memory->bridgeY;
  return true;
}

int64_t AiImproved_NearestOwnStructureManhattanQ12(Q12 worldX,Q12 worldY)
{
  int64_t nearest = INT64_MAX;
  for (uint32_t i = 0; i < g_AiImprovedOwnStructureCount; i++) {
    int64_t distance = AiImproved_ManhattanQ12(worldX,worldY,g_AiImprovedOwnStructureX[i],g_AiImprovedOwnStructureY[i]);
    if (distance < nearest) nearest = distance;
  }
  return nearest;
}

void AiImproved_BridgeNoProgress(FactionRuntimeIndex factionIndex)
{
  if (factionIndex < 1 || factionIndex > 7) {
    return;
  }
  AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
  if (memory->bridgeValid == 0) {
    return;
  }
  uint32_t now = AiImproved_GameTicks();
  if (memory->bridgeNoProgressSince == 0) {
    memory->bridgeNoProgressSince = now == 0 ? 1 : now;
    return;
  }
  if (AiImproved_TicksSince(now,memory->bridgeNoProgressSince) >= (int32_t)AI_IMPROVED_BRIDGE_PATIENCE_TICKS) {
    AiImproved_BridgeGiveUp(factionIndex,memory);
  }
}

void AiImproved_OnBridgeStoragePlaced(FactionRuntimeIndex factionIndex,Q12 worldX,Q12 worldY)
{
  if (factionIndex < 1 || factionIndex > 7) {
    return;
  }
  AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
  memory->bridgeSteps++;
  memory->bridgeNoProgressSince = 0;
  AiImproved_Log("Partei %d: Brueckengebaeude %u bei %d/%d Richtung Quelle %d/%d",factionIndex,memory->bridgeSteps,
                 worldX / Q12_ONE,worldY / Q12_ONE,memory->bridgeX / Q12_ONE,memory->bridgeY / Q12_ONE);
}

/* A deposit only the pioneer vehicle can open: the construction planner cannot build there now and it lies beyond
   the reach of the storage chain, so no building of the faction will ever get near it. */
static constexpr uint32_t AI_IMPROVED_PIONEER_BOOST_TICKS = 5 * 60 * AI_IMPROVED_TICKS_PER_SECOND;
static constexpr uint32_t AI_IMPROVED_PIONEER_BOOST_FACTOR = 3;

void AiImproved_BoostPioneerForFarDeposit(FactionRuntimeIndex factionIndex,WorldRuntimeContext *worldRuntime)
{
  if (!AiImproved_IsActive() || factionIndex < 1 || factionIndex > 7 || g_AiWorkspace08Count == 0 ||
      g_AiWorkspace09Count == 0) {
    return;
  }
  /* without a mine at all AiImproved_BoostXeniteRecovery already puts the pioneer first */
  if (AiPrimaryWorkspace_CountAssignedEntriesById(ARM_0330_BUILDING_MDL0303) == 0) {
    return;
  }
  /* one pioneer at a time: one on the map, one queued or one waiting for its place is enough */
  if (AiSecondaryWorkspace_HasEntryById(ARM_0050_UNIT_MDL0103)) {
    return;
  }
  AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
  uint32_t now = AiImproved_GameTicks();
  if (memory->pioneerBoostTick != 0 &&
      AiImproved_TicksSince(now,memory->pioneerBoostTick) < (int32_t)AI_IMPROVED_PIONEER_BOOST_TICKS) {
    return;
  }
  bool farDeposit = false;
  Q12 depositX = 0;
  Q12 depositY = 0;
  AiTerrainFeatureWorkspaceEntry *siteEntry = g_AiWorkspace08TerrainFeatureSites;
  for (uint32_t i = 0; i < g_AiWorkspace08Count && !farDeposit; i++, siteEntry++) {
    FieldGridCell *siteCell = siteEntry->cell;
    if (siteCell == nullptr) continue;
    if (!AiPlacement_TestMode4AtWorkspaceRecord(siteEntry->armyAssetId,siteCell,factionIndex,worldRuntime)) {
      continue; /* placeable now: the mine candidate builds it */
    }
    int64_t gap = INT64_MAX;
    FieldGridCell **baseCell = g_AiWorkspace09Cells;
    for (uint32_t k = 0; k < g_AiWorkspace09Count; k++, baseCell++) {
      int64_t distance = AiImproved_ManhattanQ12((*baseCell)->worldX,(*baseCell)->worldY,siteCell->worldX,
                                                 siteCell->worldY);
      if (distance < gap) gap = distance;
    }
    if (gap <= AI_IMPROVED_BRIDGE_MAX_GAP_Q12) {
      continue; /* the storage chain reaches it */
    }
    farDeposit = true;
    depositX = siteCell->worldX;
    depositY = siteCell->worldY;
  }
  if (!farDeposit) {
    return;
  }
  uint32_t boosted = 0;
  AiCandidateWorkspaceEntry *entry = g_AiWorkspace13Candidates;
  for (uint32_t i = 0; i < g_AiCandidateWorkspaceEntryCount; i++, entry++) {
    uint32_t kind = entry->weightedScoreAndKind & AI_CANDIDATE_KIND_MASK;
    uint32_t id = entry->entityIdAndMultiplicity & AI_CANDIDATE_ID_MASK;
    bool pioneer = (kind == (uint32_t)AI_CANDIDATE_KIND_TECHNOLOGY) ? (id == TEC_011_PIONEER_VEHICLE) :
                   (id == ARM_0050_UNIT_MDL0103);
    if (!pioneer) continue;
    int32_t score = (int32_t)entry->weightedScoreAndKind / AI_CANDIDATE_SCORE_ONE;
    if (score <= 0 || score > INT32_MAX / (int32_t)(AI_IMPROVED_PIONEER_BOOST_FACTOR * AI_CANDIDATE_SCORE_ONE)) continue;
    entry->weightedScoreAndKind =
         (uint32_t)(score * (int32_t)AI_IMPROVED_PIONEER_BOOST_FACTOR * AI_CANDIDATE_SCORE_ONE) | kind;
    boosted++;
  }
  if (boosted != 0) {
    memory->pioneerBoostTick = now == 0 ? 1 : now;
    AiImproved_Log("Partei %d: Quelle bei %d/%d nur mit dem Pionier erreichbar, Pionier vorgezogen",factionIndex,
                   depositX / Q12_ONE,depositY / Q12_ONE);
  }
}

void AiImproved_BoostXeniteRecovery(FactionRuntimeIndex factionIndex)
{
  if (!AiImproved_IsActive()) {
    return;
  }
  AiImprovedFactionMemory *factionMemory = &g_AiImprovedFactionMemory[factionIndex];
  if (AiPrimaryWorkspace_CountAssignedEntriesById(ARM_0330_BUILDING_MDL0303) != 0) {
    factionMemory->xeniteRecoveryLogged = 0;
    return;
  }
  /* no Xenite mine stands: the way to one goes before army and research */
  uint32_t boosted = 0;
  AiCandidateWorkspaceEntry *entry = g_AiWorkspace13Candidates;
  for (uint32_t i = 0; i < g_AiCandidateWorkspaceEntryCount; i++, entry++) {
    uint32_t kind = entry->weightedScoreAndKind & AI_CANDIDATE_KIND_MASK;
    uint32_t id = entry->entityIdAndMultiplicity & AI_CANDIDATE_ID_MASK;
    bool recovery = (kind == (uint32_t)AI_CANDIDATE_KIND_TECHNOLOGY) ? (id == TEC_011_PIONEER_VEHICLE) :
                    (id == ARM_0330_BUILDING_MDL0303 || id == ARM_0333_BUILDING_MDL0307 || id == ARM_0050_UNIT_MDL0103);
    if (!recovery) continue;
    int32_t score = (int32_t)entry->weightedScoreAndKind / AI_CANDIDATE_SCORE_ONE;
    if (score <= 0 || score > INT32_MAX / (4 * AI_CANDIDATE_SCORE_ONE)) continue;
    entry->weightedScoreAndKind = (uint32_t)(score * 4 * AI_CANDIDATE_SCORE_ONE) | kind;
    boosted++;
  }
  if (boosted != 0 && factionMemory->xeniteRecoveryLogged == 0) {
    factionMemory->xeniteRecoveryLogged = 1;
    AiImproved_Log("Partei %d: keine Xenitmine, Mine/Bruecke/Pionier zuerst",factionIndex);
  }
}

void AiImproved_AddReserveMineCandidate(FactionRuntimeIndex factionIndex)
{
  (void)factionIndex;
  if (AiImproved_Level() < 3 || g_AiKnowledgeData == nullptr) {
    return;
  }
  /* only with a running Xenite economy, and one reserve at a time */
  if (!AiPrimaryWorkspace_HasEntryById(ARM_0330_BUILDING_MDL0303) ||
      AiPrimaryWorkspace_HasUnassignedEntryById(ARM_0330_BUILDING_MDL0303)) {
    return;
  }
  AiCandidateWorkspace_AddOrAccumulateWeightedEntry(ARM_0330_BUILDING_MDL0303,
                                                    g_AiKnowledgeData->parameters.specialSite14aBaseWeight / 4,1);
}

/* ---- reserve headquarters and power plant, heavy factory, airport, armour (levels 4..7) ---- */

/* Xenite price of a building (army asset), INT32_MAX when unknown. */
static uint32_t AiImproved_BuildingCostQ4(PckArmyAssetIdCatalog armyAssetId)
{
  AiCandidateWorkspaceEntry entry;
  entry.entityIdAndMultiplicity = (uint32_t)armyAssetId;
  entry.weightedScoreAndKind = 1;
  return (uint32_t)AiCandidateWorkspace_GetEntryXeniteCost(&entry);
}

/* Buildings of this asset the planning faction has bought that wait for their place (workspace 04). Workspace 00
   does not hold them yet, so a rule that only looks there buys the same building again every purchase round until
   the first one stands (seen: four headquarters and eight power plants). */
static uint32_t AiImproved_CountRequestedAssets(PckArmyAssetIdCatalog armyAssetId)
{
  uint32_t count = 0;
  AiRuntimeWorkspaceEntry *entry = g_AiWorkspace04RequestedAssets;
  for (uint32_t i = 0; i < g_AiWorkspace04Count; i++, entry++) {
    if (entry->armyAssetId == armyAssetId) {
      count++;
    }
  }
  return count;
}

/* Standing, planned and bought-but-not-placed buildings of this asset. */
static uint32_t AiImproved_CountOwnBuildings(PckArmyAssetIdCatalog armyAssetId,bool *outWaiting)
{
  const uint32_t requested = AiImproved_CountRequestedAssets(armyAssetId);
  const bool unassigned = AiPrimaryWorkspace_HasUnassignedEntryById(armyAssetId);
  if (outWaiting != nullptr) {
    *outWaiting = requested != 0 || unassigned;
  }
  return (uint32_t)AiPrimaryWorkspace_CountAssignedEntriesById(armyAssetId) + requested + (unassigned ? 1 : 0);
}

/* The mission lets this building be built (ArmyAssetRegistry_FindEnabledById reports false for an enabled one). */
static bool AiImproved_BuildingEnabled(PckArmyAssetIdCatalog armyAssetId)
{
  return !ArmyAssetRegistry_FindEnabledById(armyAssetId);
}

void AiImproved_AddReserveBaseCandidates(FactionRuntimeIndex factionIndex)
{
  if (!AiImproved_IsActive() || g_AiKnowledgeData == nullptr || factionIndex < 1 || factionIndex > 7) {
    return;
  }
  const AiKnowledgeParameters &parameters = g_AiKnowledgeData->parameters;
  if (!g_AiImprovedWeightsLogged) {
    g_AiImprovedWeightsLogged = true;
    AiImproved_Log("KI-Gewichte: Mine %u, Pumpe %u, Silo %u, Tank %u, leichte Fabrik %u, Fabriken %u, Verteidigung %u, "
                   "Angriff %u, Labor %u, Forschung %u",parameters.specialSite14aBaseWeight,
                   parameters.specialSite14cBaseWeight,parameters.structure14bBaseWeight,parameters.structure14dBaseWeight,
                   parameters.strategicClass12dBaseWeight,parameters.strategicClass12fTo132BaseWeight,
                   parameters.armyVariantABaseWeight,parameters.armyVariantCBaseWeight,
                   parameters.strategicClass141To143BaseWeight,parameters.workspace12BestCandidateBaseWeight);
    static const PckArmyAssetIdCatalog shown[] = {ARM_0300_BUILDING_MDL0301,ARM_0310_BUILDING_MDL0305,
                                                  ARM_0302_BUILDING_MDL0300,ARM_0306_BUILDING_MDL0310,
                                                  ARM_0340_BUILDING_MDL0314,ARM_0345_BUILDING_MDL0315,
                                                  ARM_0392_BUILDING_MDL0309,ARM_0395_BUILDING_MDL0323};
    /* sizes of the buildings on the map (class, footprint radius, support radius), once per asset */
    {
      uint32_t seen[4] = {0,0,0,0};
      for (WorldOwnerListNode *node = g_InGameRuntimeRoot->worldRuntime.ownerListHead; node != nullptr;
           node = node->nextNode) {
        if (node->ownerClassId != WORLD_OWNER_RUNTIME_MODEL) continue;
        ModelRuntimeSlot *model = WorldOwnerNode_ModelRuntime(node);
        ArmyRuntimeSlot *army = model->ownerArmyRuntimeOrSavedOffset.armyRuntime;
        if (army == nullptr) continue;
        const uint32_t id = (uint32_t)army->armyAssetId;
        if (id < 300 || id > 399 || (seen[(id - 300) / 32] & (1u << ((id - 300) % 32))) != 0) continue;
        seen[(id - 300) / 32] |= 1u << ((id - 300) % 32);
        ModelDefinition *definition = model->definitionOrSavedId.runtimeDefinition;
        AiImproved_Log("Gebaeude ARM %u: Klasse %d, Radius %.2f, Bauradius %.2f",id,(int)definition->runtimeClassId,
                       (double)definition->footprintRadius / Q12_ONE,(double)definition->supportRadius / Q12_ONE);
      }
    }
    for (PckArmyAssetIdCatalog id : shown) {
      ArmyAssetRecordPrefix *asset;
      if (ArmyAssetRegistry_FindById(id,&asset) == 0) {
        AiImproved_Log("KI-Gebaeude ARM %d: Merkmale %#x, %u Xenit",(int)id,(unsigned)asset[1].selectionDetailTemplateVariantIndex,
                       AiImproved_BuildingCostQ4(id) / 16);
      }
    }
  }
  if (AiImproved_Level() < AI_IMPROVED_RESERVE_BASE_LEVEL) {
    return;
  }
  /* only with a running Xenite economy */
  if (AiPrimaryWorkspace_CountAssignedEntriesById(ARM_0330_BUILDING_MDL0303) == 0) {
    return;
  }
  /* "gerne schon zu Spielbeginn": the power plant with the weight of a factory (above the army), the headquarters
     a quarter above the attack units, so the faction saves up for it once instead of buying units first; both are
     buildings and so share the headquarters' one building order per purchase round with the other buildings */
  const uint32_t powerPlantWeight = parameters.strategicClass12fTo132BaseWeight;
  const uint32_t headquartersWeight = parameters.armyVariantCBaseWeight * 5 / 4;
  AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
  /* a second power plant (one at a time) */
  bool powerPlantWaiting = false;
  if (AiImproved_BuildingEnabled(ARM_0310_BUILDING_MDL0305) &&
      AiImproved_CountOwnBuildings(ARM_0310_BUILDING_MDL0305,&powerPlantWaiting) < 2 && !powerPlantWaiting) {
    AiCandidateWorkspace_AddOrAccumulateWeightedEntry(ARM_0310_BUILDING_MDL0305,powerPlantWeight,1);
    if (memory->reservePowerLogged == 0) {
      memory->reservePowerLogged = 1;
      AiImproved_Log("Partei %d: Reserve-Kraftwerk vorgeschlagen (%d stehen)",factionIndex,
                     AiPrimaryWorkspace_CountAssignedEntriesById(ARM_0310_BUILDING_MDL0305));
    }
  }
  else {
    memory->reservePowerLogged = 0;
  }
  /* a second headquarters, once the storage holds its price (else the purchase would wait for it for good) */
  const uint32_t headquartersCost = AiImproved_BuildingCostQ4(ARM_0300_BUILDING_MDL0301);
  bool headquartersWaiting = false;
  if (AiImproved_BuildingEnabled(ARM_0300_BUILDING_MDL0301) && g_AiWorkspaceOwnedAsset300Runtime != nullptr &&
      AiImproved_CountOwnBuildings(ARM_0300_BUILDING_MDL0301,&headquartersWaiting) < 2 && !headquartersWaiting &&
      g_GameFactionRuntimeImage.records[factionIndex].xeniteStorageLimitQ4 >= headquartersCost) {
    AiCandidateWorkspace_AddOrAccumulateWeightedEntry(ARM_0300_BUILDING_MDL0301,headquartersWeight,1);
    if (memory->reserveHeadquartersLogged == 0) {
      memory->reserveHeadquartersLogged = 1;
      AiImproved_Log("Partei %d: Reserve-Hauptquartier vorgeschlagen (%u Xenit)",factionIndex,headquartersCost / 16);
    }
  }
  else {
    memory->reserveHeadquartersLogged = 0;
  }
}

bool AiImproved_ReserveHeadquartersCellScore(Q12 cellWorldX,Q12 cellWorldY,uint32_t randomBits,int *outScore)
{
  if (!AiImproved_IsActive() || g_AiWorkspaceOwnedAsset300Runtime == nullptr) {
    return false;
  }
  ModelRuntimeNode *node = g_AiWorkspaceOwnedAsset300Runtime->rootModelNodeOrSavedOffset.modelNode;
  const Q12 headquartersX = (node->worldTransform).translation.x;
  const Q12 headquartersY = (node->worldTransform).translation.y;
  int64_t score = (int64_t)(randomBits & 0xffff) -
                  2 * AiImproved_ManhattanQ12(cellWorldX,cellWorldY,headquartersX,headquartersY);
  Q12 threatX;
  Q12 threatY;
  if (AiImproved_NearestThreat(headquartersX,headquartersY,&threatX,&threatY)) {
    score -= AiImproved_ManhattanQ12(cellWorldX,cellWorldY,threatX,threatY);
  }
  if (score < INT32_MIN + 1) score = INT32_MIN + 1;
  if (score > INT32_MAX - 1) score = INT32_MAX - 1;
  *outScore = (int)score;
  return true;
}

void AiImproved_AddHeavyFactoryCandidate(FactionRuntimeIndex factionIndex,RuntimeToken terrainChoice)
{
  if (!AiImproved_IsActive() || AiImproved_Level() < AI_IMPROVED_HEAVY_FACTORY_LEVEL || g_AiKnowledgeData == nullptr ||
      factionIndex < 1 || factionIndex > 7) {
    return;
  }
  /* Technology_IsUnlockedForFaction reports true while the technology is still locked */
  if (Technology_IsUnlockedForFaction(TEC_001_ARMS_FACTORIES,factionIndex)) {
    return;
  }
  static const PckArmyAssetIdCatalog heavyFactories[3] = {ARM_0302_BUILDING_MDL0300,ARM_0303_BUILDING_MDL0316,
                                                          ARM_0304_BUILDING_MDL0324};
  for (int i = 0; i < 3; i++) {
    if (AiPrimaryWorkspace_HasEntryById(heavyFactories[i]) || AiImproved_CountRequestedAssets(heavyFactories[i]) != 0) {
      g_AiImprovedFactionMemory[factionIndex].heavyFactoryLogged = 0;
      return; /* one stands or is on its way */
    }
  }
  PckArmyAssetIdCatalog choice = 0;
  for (int i = 0; i < 3; i++) {
    if ((PckArmyAssetIdCatalog)terrainChoice == heavyFactories[i] && AiImproved_BuildingEnabled(heavyFactories[i])) {
      choice = heavyFactories[i];
    }
  }
  /* the terrain choice was the harbour, the airport or nothing: the heavy factories in turn by faction */
  for (int k = 0; k < 3 && choice == 0; k++) {
    PckArmyAssetIdCatalog candidate = heavyFactories[(factionIndex + k) % 3];
    if (AiImproved_BuildingEnabled(candidate)) {
      choice = candidate;
    }
  }
  if (choice == 0) {
    return;
  }
  AiCandidateWorkspace_AddOrAccumulateWeightedEntry((RuntimeToken)choice,
                                                    g_AiKnowledgeData->parameters.strategicClass12fTo132BaseWeight * 3,1);
  if (g_AiImprovedFactionMemory[factionIndex].heavyFactoryLogged == 0) {
    g_AiImprovedFactionMemory[factionIndex].heavyFactoryLogged = 1;
    AiImproved_Log("Partei %d: keine schwere Fabrik, ARM %d vorgeschlagen",factionIndex,(int)choice);
  }
}

bool AiImproved_ScoresAirport()
{
  return AiImproved_ExtrasActive();
}

/* ---- from level 1: aircraft, laser coils, research (the user's wish of 06.11.2026) ---- */

bool AiImproved_ExtrasActive()
{
  return AiImproved_Level() >= AI_IMPROVED_EXTRAS_LEVEL; /* AiImproved_Level is 0 in a network game */
}

/* Aircraft and laser coils a faction keeps: 2 on level 1, one more on each level above (8 in Hell). */
static uint32_t AiImproved_AirAndCoilQuota()
{
  return AiImproved_ExtrasActive() ? (uint32_t)AiImproved_Level() + 1 : 0;
}

static bool AiImproved_IsAircraftAsset(uint32_t armyAssetId)
{
  return armyAssetId >= AI_IMPROVED_AIRCRAFT_FIRST_ASSET && armyAssetId <= AI_IMPROVED_AIRCRAFT_LAST_ASSET;
}

/* An own aircraft pad (airport, class 22) of workspace 00 as its linked-child view, NULL for anything else. */
static ModelRuntimeLinkedChildSpawnAndBuildView *AiImproved_OwnPad(AiStructureWorkspaceEntry *entry)
{
  ModelRuntimeSlot *slot = entry->runtimeSlotAddressOrZero;
  if (slot == nullptr || slot->definitionOrSavedId.runtimeDefinition->runtimeClassId != MODEL_RUNTIME_CLASS_22 ||
      Any(slot->classState.stateFlags & ARMY_MODEL_STATE_INACTIVE_MASK)) {
    return nullptr;
  }
  return ModelView_Cast<ModelRuntimeLinkedChildSpawnAndBuildView>(slot);
}

/* Hangar slots of a pad (the definition's capacity, at most the 13 slots of the runtime). */
static int AiImproved_PadSlotCount(ModelRuntimeLinkedChildSpawnAndBuildView *pad)
{
  int capacity = (int)pad->modelDefinition->linkedChildSlotCapacity;
  const int slots = (int)(sizeof pad->completedSecondaryArmyAssetIds / sizeof pad->completedSecondaryArmyAssetIds[0]);
  return capacity < 0 ? 0 : (capacity > slots ? slots : capacity);
}

/* Own aircraft. A finished aircraft has no model while it stands in its airport's hangar: it is an asset id in a
   hangar slot of the pad, also while it is out on a mission (the slot is only marked as flying). So the hangar
   slots count, and of workspace 01 only the aircraft queued or in production (no model yet); a flying aircraft's
   model in workspace 01 is already counted by its slot. Without the hangar the quota never filled and the AI bought
   aircraft without end (seen in the first test: 16 Kraehen for a quota of 2). */
static uint32_t AiImproved_CountOwnAircraft()
{
  uint32_t count = 0;
  AiRuntimeWorkspaceEntry *entry = g_AiWorkspace01Units;
  for (uint32_t i = 0; i < g_AiWorkspace01Count; i++, entry++) {
    if (entry->modelRuntime == nullptr && AiImproved_IsAircraftAsset((uint32_t)entry->armyAssetId)) {
      count++;
    }
  }
  AiStructureWorkspaceEntry *structure = g_AiWorkspace00Structures;
  for (uint32_t i = 0; i < g_AiWorkspace00Count; i++, structure++) {
    ModelRuntimeLinkedChildSpawnAndBuildView *pad = AiImproved_OwnPad(structure);
    if (pad == nullptr) continue;
    const int slots = AiImproved_PadSlotCount(pad);
    for (int k = 0; k < slots; k++) {
      if (AiImproved_IsAircraftAsset((uint32_t)pad->completedSecondaryArmyAssetIds[k])) {
        count++;
      }
    }
  }
  return count;
}

/* This round's candidate list already holds the entry. */
static bool AiImproved_CandidateListed(uint32_t id,uint32_t kind)
{
  AiCandidateWorkspaceEntry *entry = g_AiWorkspace13Candidates;
  for (uint32_t i = 0; i < g_AiCandidateWorkspaceEntryCount; i++, entry++) {
    if ((entry->entityIdAndMultiplicity & AI_CANDIDATE_ID_MASK) == id &&
        (entry->weightedScoreAndKind & AI_CANDIDATE_KIND_MASK) == kind) {
      return true;
    }
  }
  return false;
}

static uint32_t AiImproved_TechnologyCostQ4(uint32_t technologyId)
{
  AiCandidateWorkspaceEntry entry;
  entry.entityIdAndMultiplicity = technologyId;
  entry.weightedScoreAndKind = (uint32_t)AI_CANDIDATE_KIND_TECHNOLOGY;
  return (uint32_t)AiCandidateWorkspace_GetEntryXeniteCost(&entry);
}

/* A technology an own structure offers (workspace 12) that the faction may research now, its structure not
   switched off and not waiting for research money. */
static bool AiImproved_TechnologyResearchable(const AiTechnologyPlanningCandidate *candidate,FactionRuntimeIndex factionIndex)
{
  ModelRuntimeSlot *source = candidate->sourceModelRuntime04;
  if (source == nullptr || Any(source->classState.stateFlags & (ARMY_MODEL_STATE_INACTIVE_MASK |
                                                                 ARMY_MODEL_STATE_RESEARCH_UNPAID))) {
    return false;
  }
  return Technology_IsAvailableForFaction(candidate->technologyId00,factionIndex);
}

/* The cheapest of these technologies an own structure offers now; 0 when none. */
static uint32_t AiImproved_CheapestOfferedTechnology(const uint32_t *ids,int idCount,FactionRuntimeIndex factionIndex)
{
  uint32_t best = 0;
  uint32_t bestCost = UINT32_MAX;
  AiTechnologyPlanningCandidate *candidate = g_AiWorkspace12TechnologyCandidates;
  for (uint32_t i = 0; i < g_AiWorkspace12Count; i++, candidate++) {
    uint32_t technologyId = (uint32_t)candidate->technologyId00;
    bool wanted = (ids == nullptr);
    for (int k = 0; k < idCount && !wanted; k++) {
      wanted = ids[k] == technologyId;
    }
    if (!wanted || technologyId == TEC_216_WALL || technologyId == TEC_217_HIGH_WALL ||
        AiImproved_CandidateListed(technologyId,(uint32_t)AI_CANDIDATE_KIND_TECHNOLOGY) ||
        !AiImproved_TechnologyResearchable(candidate,factionIndex)) {
      continue;
    }
    uint32_t cost = AiImproved_TechnologyCostQ4(technologyId);
    if (cost < bestCost) {
      bestCost = cost;
      best = technologyId;
    }
  }
  return best;
}

uint32_t AiImproved_AircraftWanted(FactionRuntimeIndex factionIndex)
{
  (void)factionIndex;
  const uint32_t quota = AiImproved_AirAndCoilQuota();
  const uint32_t own = AiImproved_CountOwnAircraft();
  return quota > own ? quota - own : 0;
}

uint32_t AiImproved_LaserCoilsWanted(FactionRuntimeIndex factionIndex)
{
  (void)factionIndex;
  const uint32_t quota = AiImproved_AirAndCoilQuota();
  bool waiting = false;
  const uint32_t own = AiImproved_CountOwnBuildings(ARM_0345_BUILDING_MDL0315,&waiting);
  if (waiting) {
    return 0; /* one at a time */
  }
  return quota > own ? quota - own : 0;
}

bool AiImproved_LaserCoilPlacementWanted(PckArmyAssetIdCatalog armyAssetId)
{
  return armyAssetId == ARM_0345_BUILDING_MDL0315 && AiImproved_ExtrasActive() &&
         (uint32_t)AiPrimaryWorkspace_CountAssignedEntriesById(ARM_0345_BUILDING_MDL0315) < AiImproved_AirAndCoilQuota();
}

void AiImproved_NoteQuotaCandidate(FactionRuntimeIndex factionIndex,uint32_t armyAssetId,uint32_t own,uint32_t quota)
{
  if (factionIndex < 1 || factionIndex > 7) return;
  AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
  uint32_t *logged = AiImproved_IsAircraftAsset(armyAssetId) ? &memory->aircraftLogged : &memory->laserCoilLogged;
  if (*logged != own + 1) {
    *logged = own + 1;
    AiImproved_Log("Partei %d: %s ARM %u vorgeschlagen (%u von %u)",factionIndex,
                   AiImproved_IsAircraftAsset(armyAssetId) ? "Flugzeug" : "Laserspule",armyAssetId,own,quota);
  }
}

void AiImproved_AddQuotaResearch(FactionRuntimeIndex factionIndex,bool needAircraftType,bool needLaserCoil,
                                 bool needRadarUnit)
{
  if (!AiImproved_ExtrasActive() || g_AiKnowledgeData == nullptr || g_AiWorkspace12Count == 0 ||
      !AiPrimaryWorkspace_HasEntryById(ARM_0330_BUILDING_MDL0303)) {
    return;
  }
  const uint32_t weight = g_AiKnowledgeData->parameters.workspace12BestCandidateBaseWeight * 3 / 2;
  if (needLaserCoil) {
    static const uint32_t coil[1] = {TEC_186_LASER_COIL};
    uint32_t technologyId = AiImproved_CheapestOfferedTechnology(coil,1,factionIndex);
    if (technologyId != 0) {
      AiCandidateWorkspace_AddOrAccumulateWeightedEntry(technologyId,weight,AI_CANDIDATE_KIND_TECHNOLOGY);
    }
  }
  if (needRadarUnit) {
    /* the radar Goliath needs the Goliath and the radar */
    static const uint32_t radar[2] = {TEC_021_GOLIATH_TECHNOLOGY,TEC_183_RADAR};
    uint32_t technologyId = AiImproved_CheapestOfferedTechnology(radar,2,factionIndex);
    if (technologyId != 0) {
      AiCandidateWorkspace_AddOrAccumulateWeightedEntry(technologyId,weight,AI_CANDIDATE_KIND_TECHNOLOGY);
    }
  }
  if (needAircraftType) {
    static const uint32_t aircraft[3] = {TEC_075_SP_CROW,TEC_078_B_MINUS_12_HAWK,TEC_081_B_MINUS_18_EAGLE};
    uint32_t technologyId = AiImproved_CheapestOfferedTechnology(aircraft,3,factionIndex);
    if (technologyId != 0) {
      AiCandidateWorkspace_AddOrAccumulateWeightedEntry(technologyId,weight,AI_CANDIDATE_KIND_TECHNOLOGY);
    }
  }
}

/* An anti-aircraft tower (Flakgeschuetz, leichter Turm mit Flak). */
static bool AiImproved_IsAntiAircraftTower(uint32_t armyAssetId)
{
  return armyAssetId == ARM_0362_BUILDING_MDL0314 || armyAssetId == ARM_0371_BUILDING_MDL0312;
}

/* The target of an air strike, among the hostiles the faction sees (workspace 02), neither of a human player while
   the faction's grace period runs. Buildings before units; walls and tank traps never (the first test bombed
   mostly those). Levels 1 and 2: production, economy and research buildings before towers, and every
   anti-aircraft tower within 6 units of a target counts as 30 units of extra distance, so the aircraft go where
   they survive. From level 3 on (the user's wish of 06.11.2026) the nearest building of any kind, also next to
   flak: so the player's flak is worth its research. */
static bool AiImproved_AircraftTarget(FactionRuntimeIndex factionIndex,Q12 fromX,Q12 fromY,Q12 *outX,Q12 *outY,
                                      uint32_t *outAssetId)
{
  const bool grace = AiImproved_GraceActive(factionIndex);
  int64_t bestScore = INT64_MAX;
  bool bestIsStructure = false;
  AiRuntimeWorkspaceEntry *entry = g_AiWorkspace02VisibleHostiles;
  for (uint32_t i = 0; i < g_AiWorkspace02Count; i++, entry++) {
    ModelRuntimeSlot *hostile = entry->modelRuntime;
    if (hostile == nullptr) continue;
    ArmyRuntimeSlot *owner = hostile->ownerArmyRuntimeOrSavedOffset.armyRuntime;
    if (owner == nullptr || (grace && AiImproved_IsHumanFaction(owner->factionIndex))) continue;
    ModelDefinition *definition = hostile->definitionOrSavedId.runtimeDefinition;
    if (definition->runtimeClassId == MODEL_RUNTIME_CLASS_21_AIRCRAFT) continue; /* bombs do not hit aircraft */
    const uint32_t assetId = (uint32_t)entry->armyAssetId;
    if (assetId >= ARM_0380_BUILDING_MDL0329 && assetId < ARM_0390_BUILDING_MDL0321) continue; /* walls, tank traps */
    const bool structure = definition->accelerationPerTick == 0;
    if (bestIsStructure && !structure) continue;
    ModelRuntimeNode *node = hostile->rootModelNodeOrSavedOffset.modelNode;
    const Q12 hx = (node->worldTransform).translation.x;
    const Q12 hy = (node->worldTransform).translation.y;
    int64_t score = AiImproved_ManhattanQ12(fromX,fromY,hx,hy);
    const bool careful = AiImproved_Level() < AI_IMPROVED_AIR_ALL_BUILDINGS_LEVEL;
    if (careful && structure && assetId >= ARM_0340_BUILDING_MDL0314 && assetId < ARM_0380_BUILDING_MDL0329) {
      score += 20 * (int64_t)Q12_ONE; /* a tower */
    }
    AiRuntimeWorkspaceEntry *other = g_AiWorkspace02VisibleHostiles;
    for (uint32_t k = 0; careful && k < g_AiWorkspace02Count; k++, other++) {
      if (other->modelRuntime == nullptr || !AiImproved_IsAntiAircraftTower((uint32_t)other->armyAssetId)) continue;
      ModelRuntimeNode *otherNode = other->modelRuntime->rootModelNodeOrSavedOffset.modelNode;
      if (AiImproved_ManhattanQ12(hx,hy,(otherNode->worldTransform).translation.x,(otherNode->worldTransform).translation.y) <
          6 * (int64_t)Q12_ONE) {
        score += 30 * (int64_t)Q12_ONE;
      }
    }
    if ((structure && !bestIsStructure) || score < bestScore) {
      bestScore = score;
      bestIsStructure = structure;
      *outX = hx;
      *outY = hy;
      *outAssetId = assetId;
    }
  }
  return bestScore != INT64_MAX;
}

void AiImproved_LaunchAircraft(FactionRuntimeIndex factionIndex)
{
  if (!AiImproved_ExtrasActive() || factionIndex < 1 || factionIndex > 7 || !AiImproved_IsAiFaction(factionIndex)) {
    return;
  }
  AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
  const uint32_t now = AiImproved_GameTicks();
  if (memory->aircraftLaunchValid != 0 &&
      AiImproved_TicksSince(now,memory->aircraftLaunchTick) < (int32_t)AI_IMPROVED_AIRCRAFT_LAUNCH_TICKS) {
    return;
  }
  AiStructureWorkspaceEntry *structure = g_AiWorkspace00Structures;
  for (uint32_t i = 0; i < g_AiWorkspace00Count; i++, structure++) {
    ModelRuntimeLinkedChildSpawnAndBuildView *pad = AiImproved_OwnPad(structure);
    if (pad == nullptr) continue;
    ModelRuntimeLinkedChildPendingSpawnCounts &pending = pad->linkedChildPendingSpawnCounts;
    if (pending.slot0 != 0 || pending.slot1 != 0 || pending.slot2 != 0) continue; /* a launch is under way */
    /* parked aircraft per lane (lane 0 Kraehe, 1 Habicht, 2 Adler, as the player's pad command) */
    const uint32_t flyingMask = ModelView_Cast<ArmyRuntimeLinkedChildMaskSlotView>(pad)->articulatedContact.
                                linkedChildSlotMaskState.linkedChildSlotMask;
    uint32_t parked[3] = {0,0,0};
    const int slots = AiImproved_PadSlotCount(pad);
    for (int k = 0; k < slots; k++) {
      const uint32_t id = (uint32_t)pad->completedSecondaryArmyAssetIds[k];
      if (id == 0 || (flyingMask & (1u << k)) != 0) continue;
      for (int lane = 0; lane < 3; lane++) {
        if (id == g_InGamePointerModePreviewArmyIds[lane == 0 ? 1 : (lane == 1 ? 2 : 4)]) parked[lane]++;
      }
    }
    if (parked[0] + parked[1] + parked[2] == 0) continue;
    ModelRuntimeNode *padNode = pad->rootModelNodeOrSavedOffset.modelNode;
    const Q12 padX = (padNode->worldTransform).translation.x;
    const Q12 padY = (padNode->worldTransform).translation.y;
    Q12 targetX;
    Q12 targetY;
    uint32_t targetAsset;
    if (!AiImproved_AircraftTarget(factionIndex,padX,padY,&targetX,&targetY,&targetAsset)) continue;
    /* the attack run heads from the airport toward the target (the aircraft come in from the own side) */
    const AngleTurn32 heading = (AngleTurn32)(FixedMath_Vector2AngleAndLength(targetY - padY,targetX - padX).angle &
                                              FIXED_ANGLE16_MASK);
    for (int lane = 0; lane < 3; lane++) {
      if (parked[lane] == 0) continue;
      pad->linkedChildSpawnInheritedState[lane].inheritedValue70 = (WorldMotionValue70)targetX;
      pad->linkedChildSpawnInheritedState[lane].inheritedValue74 = (WorldMotionValue74)targetY;
      pad->linkedChildSpawnInheritedState[lane].inheritedValue78 = (WorldMotionValue78)heading;
    }
    pending.slot0 = (uint8_t)parked[0];
    pending.slot1 = (uint8_t)parked[1];
    pending.slot2 = (uint8_t)parked[2];
    memory->aircraftLaunchValid = 1;
    memory->aircraftLaunchTick = now;
    AiImproved_Log("Partei %d: Luftangriff mit %u Flugzeugen (Kraehe %u, Habicht %u, Adler %u) von %d/%d auf ARM %u bei %d/%d",
                   factionIndex,parked[0] + parked[1] + parked[2],parked[0],parked[1],parked[2],padX / Q12_ONE,
                   padY / Q12_ONE,targetAsset,targetX / Q12_ONE,targetY / Q12_ONE);
  }
}

bool AiImproved_PurchaseOverQuota(FactionRuntimeIndex factionIndex,uint32_t entityId,uint32_t kind)
{
  if (!AiImproved_ExtrasActive() || kind == (uint32_t)AI_CANDIDATE_KIND_TECHNOLOGY) {
    return false;
  }
  if (AiImproved_IsAircraftAsset(entityId)) {
    if (factionIndex >= 1 && factionIndex <= 7) {
      AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
      if (memory->aircraftPurchaseValid != 0 &&
          AiImproved_TicksSince(AiImproved_GameTicks(),memory->aircraftPurchaseTick) <
            (int32_t)AI_IMPROVED_AIRCRAFT_PURCHASE_TICKS) {
        return true;
      }
    }
    return AiImproved_AircraftWanted(factionIndex) == 0;
  }
  if (entityId == ARM_0345_BUILDING_MDL0315) {
    return AiImproved_LaserCoilsWanted(factionIndex) == 0;
  }
  if (entityId == ARM_0104_UNIT_MDL0113) {
    return AiImproved_RadarUnitsWanted(factionIndex) == 0 && AiImproved_Level() >= AI_IMPROVED_RADAR_LEVEL;
  }
  return false;
}

uint32_t AiImproved_RadarUnitsWanted(FactionRuntimeIndex factionIndex)
{
  (void)factionIndex;
  if (!AiImproved_IsActive() || AiImproved_Level() < AI_IMPROVED_RADAR_LEVEL) {
    return 0;
  }
  const uint32_t quota = (uint32_t)(AiImproved_Level() - 1) / 2; /* 1 on levels 3 and 4, 2 on 5 and 6, 3 in Hell */
  const uint32_t own = AiImproved_CountOwnUnitsOfAsset(ARM_0104_UNIT_MDL0113);
  return quota > own ? quota - own : 0;
}

bool AiImproved_HandleRadarUnit(FactionRuntimeIndex factionIndex,ArmyRuntimeSlot *armyRuntime)
{
  if (!AiImproved_IsActive() || AiImproved_Level() < AI_IMPROVED_RADAR_LEVEL || armyRuntime == nullptr ||
      armyRuntime->modelNodeRuntime == nullptr || (uint32_t)armyRuntime->armyAssetId != ARM_0104_UNIT_MDL0113) {
    return false;
  }
  Q12 homeX;
  Q12 homeY;
  Q12 threatX;
  Q12 threatY;
  /* nothing hostile seen yet: it explores like any scout */
  if (!AiImproved_FindHomePoint(&homeX,&homeY) || !AiImproved_NearestThreat(homeX,homeY,&threatX,&threatY)) {
    return false;
  }
  const int64_t dx = (int64_t)threatX - homeX;
  const int64_t dy = (int64_t)threatY - homeY;
  const int64_t distance = (int64_t)FixedMath_Vector2AngleAndLength((FixedMathVectorComponent32)dy,
                                                                    (FixedMathVectorComponent32)dx).length;
  if (distance <= 0) {
    return false;
  }
  int64_t postDistance = distance * AI_IMPROVED_RADAR_POST_SHARE_PERCENT / 100;
  if (postDistance > AI_IMPROVED_RADAR_POST_MAX_Q12) postDistance = AI_IMPROVED_RADAR_POST_MAX_Q12;
  const Q12 postX = (Q12)(homeX + dx * postDistance / distance);
  const Q12 postY = (Q12)(homeY + dy * postDistance / distance);
  if (Any(armyRuntime->movementStateFlags & ARMY_MOVEMENT_ACTIVE)) {
    return true; /* on its way */
  }
  const Q12 unitX = (armyRuntime->modelNodeRuntime->worldTransform).translation.x;
  const Q12 unitY = (armyRuntime->modelNodeRuntime->worldTransform).translation.y;
  if (AiImproved_ManhattanQ12(unitX,unitY,postX,postY) <= AI_IMPROVED_RADAR_POST_SLACK_Q12) {
    return true; /* at its post */
  }
  ArmyRuntime_ResolveCommandTarget(nullptr,armyRuntime);
  armyRuntime->aiUnitState = AI_UNIT_COMMANDED_STATE;
  armyRuntime->aiUnitFlags = armyRuntime->aiUnitFlags & ~AI_UNIT_STATE94_GROUP_ASSIGNED;
  ArmyRuntime_StartRoutedMoveCommand(postY,postX,reinterpret_cast<ArmyMovementRuntime *>(armyRuntime)); /* the army slot as its movement view, as in units.cpp */
  AiImproved_Log("Partei %d: Radar-Goliath %d faehrt auf Posten %d/%d (Gegner gesehen bei %d/%d)",factionIndex,
                 AiImproved_SlotIndex(armyRuntime),postX / Q12_ONE,postY / Q12_ONE,threatX / Q12_ONE,threatY / Q12_ONE);
  return true;
}

void AiImproved_OnPurchase(FactionRuntimeIndex factionIndex,uint32_t entityId,uint32_t kind,uint32_t costQ4)
{
  if (kind != (uint32_t)AI_CANDIDATE_KIND_TECHNOLOGY && AiImproved_IsAircraftAsset(entityId) && factionIndex >= 1 &&
      factionIndex <= 7) {
    g_AiImprovedFactionMemory[factionIndex].aircraftPurchaseValid = 1;
    g_AiImprovedFactionMemory[factionIndex].aircraftPurchaseTick = AiImproved_GameTicks();
  }
  if (!AiImproved_LogEnabled()) {
    return;
  }
  if (kind == (uint32_t)AI_CANDIDATE_KIND_TECHNOLOGY) {
    AiImproved_Log("Partei %d: forscht TEC %u (%u Xenit)",factionIndex,entityId,costQ4 / 16);
  }
  else if (AiImproved_IsAircraftAsset(entityId) || entityId == ARM_0345_BUILDING_MDL0315 ||
           entityId == ARM_0306_BUILDING_MDL0310 || entityId == ARM_0300_BUILDING_MDL0301 ||
           entityId == ARM_0104_UNIT_MDL0113) {
    AiImproved_Log("Partei %d: kauft ARM %u (%u Xenit)",factionIndex,entityId,costQ4 / 16);
  }
}

void AiImproved_AddLevelExtrasCandidates(FactionRuntimeIndex factionIndex)
{
  if (!AiImproved_ExtrasActive() || g_AiKnowledgeData == nullptr || factionIndex < 1 || factionIndex > 7) {
    return;
  }
  const AiKnowledgeParameters &parameters = g_AiKnowledgeData->parameters;
  AiImprovedFactionMemory *memory = &g_AiImprovedFactionMemory[factionIndex];
  /* the airport, once Arms Factories is researched and another factory stands; a hangar holds six aircraft, so
     levels 6 and 7 (7 and 8 aircraft) keep a second one */
  bool airportWaiting = false;
  const uint32_t airportsWanted = (AiImproved_AirAndCoilQuota() + AI_IMPROVED_HANGAR_AIRCRAFT - 1) / AI_IMPROVED_HANGAR_AIRCRAFT;
  if (!Technology_IsUnlockedForFaction(TEC_001_ARMS_FACTORIES,factionIndex) &&
      AiImproved_BuildingEnabled(ARM_0306_BUILDING_MDL0310) &&
      AiImproved_CountOwnBuildings(ARM_0306_BUILDING_MDL0310,&airportWaiting) < airportsWanted && !airportWaiting) {
    int factories = 0;
    for (uint32_t id = ARM_0301_BUILDING_MDL0318; id <= ARM_0305_BUILDING_MDL0317; id++) {
      factories += AiPrimaryWorkspace_CountAssignedEntriesById((PckArmyAssetIdCatalog)id);
    }
    if (factories != 0) {
      AiCandidateWorkspace_AddOrAccumulateWeightedEntry(ARM_0306_BUILDING_MDL0310,
                                                        parameters.strategicClass12fTo132BaseWeight,1);
      if (memory->airportLogged == 0) {
        memory->airportLogged = 1;
        AiImproved_Log("Partei %d: Flughafen vorgeschlagen",factionIndex);
      }
    }
  }
  else {
    memory->airportLogged = 0;
  }
  /* research: besides the best one of the round the cheapest one an own structure offers now, below the army
     (x1/2 on level 1 up to x5/4 in Hell), so over the game the AI researches everything it can */
  if (g_AiWorkspace12Count != 0 && AiPrimaryWorkspace_HasEntryById(ARM_0330_BUILDING_MDL0303)) {
    uint32_t technologyId = AiImproved_CheapestOfferedTechnology(nullptr,0,factionIndex);
    if (technologyId != 0) {
      AiCandidateWorkspace_AddOrAccumulateWeightedEntry
        (technologyId,parameters.workspace12BestCandidateBaseWeight * (uint32_t)(AiImproved_Level() + 3) / 8,
         AI_CANDIDATE_KIND_TECHNOLOGY);
    }
  }
}

/* A faction a human player plays; in "zuschauen" the local faction still counts, so the KI-gegen-KI tests show
   the armour. */
static bool AiImproved_IsPlayerFaction(int factionIndex)
{
  if (factionIndex <= 0 || factionIndex > 7) {
    return false;
  }
  /* the local player (also when a test loads a savegame without the frontend's player blocks) */
  if (g_LocalPlayerRuntimeId < 256 && g_SelectionPlayerRuntimeBlockPointers[g_LocalPlayerRuntimeId] != nullptr &&
      (int)g_SelectionPlayerRuntimeBlockPointers[g_LocalPlayerRuntimeId]->factionIndex == factionIndex) {
    return true;
  }
  if (g_FrontendPlayerRuntimeBlocks == nullptr) {
    return false;
  }
  uint32_t count = g_FrontendPlayerRuntimeBlockCount;
  if (count > FRONTEND_PLAYER_RUNTIME_RECORD_ALLOC_COUNT) count = FRONTEND_PLAYER_RUNTIME_RECORD_ALLOC_COUNT;
  for (uint32_t i = 0; i < count; i++) {
    if ((int)g_FrontendPlayerRuntimeBlocks[i].factionAssignment.factionAssignmentIndex == factionIndex) {
      return true;
    }
  }
  return false;
}

/* The placement rules are for the human player's own building; under "zuschauen" the computer builds for the
   local faction and keeps the original's spacing like every other AI faction. */
bool AiImproved_WallChainFor(uint32_t ownerFactionIndex)
{
  return AiImproved_SinglePlayer() && !AiImproved_Settings().spectate &&
         AiImproved_IsPlayerFaction((int)ownerFactionIndex);
}

bool AiImproved_TightBuildingFor(uint32_t ownerFactionIndex)
{
  return AiImproved_WallChainFor(ownerFactionIndex) && AiImproved_Settings().tightBuilding;
}

int32_t AiImproved_ScaleDamageFromPlayer(FactionRuntimeIndex sourceFactionIndex,ModelRuntimeSlot *targetModelRuntime,
                                         int32_t impactValue)
{
  if (impactValue <= 0 || targetModelRuntime == nullptr || !AiImproved_IsActive()) {
    return impactValue;
  }
  const uint32_t armorPercent = g_AiImprovedLevelParameters[AiImproved_Level()].armorPercent;
  if (armorPercent <= 100) {
    return impactValue;
  }
  ArmyRuntimeSlot *owner = targetModelRuntime->ownerArmyRuntimeOrSavedOffset.armyRuntime;
  if (owner == nullptr) {
    return impactValue;
  }
  const int targetFactionIndex = owner->factionIndex;
  if (targetFactionIndex < 1 || targetFactionIndex > 7 || targetFactionIndex == (int)sourceFactionIndex ||
      !AiImproved_IsPlayerFaction((int)sourceFactionIndex) || AiImproved_IsPlayerFaction(targetFactionIndex)) {
    return impactValue;
  }
  int64_t scaled = ((int64_t)impactValue * 100 + armorPercent / 2) / armorPercent;
  if (scaled < 1) scaled = 1;
  if (!g_AiImprovedArmorLogged) {
    g_AiImprovedArmorLogged = true;
    AiImproved_Log("Panzerung %u %%: erster Treffer von Partei %d auf Partei %d, Schaden %d statt %d",armorPercent,
                   (int)sourceFactionIndex,targetFactionIndex,(int)scaled,impactValue);
  }
  return (int32_t)scaled;
}

uint32_t AiImproved_CountOwnUnitsOfAsset(PckArmyAssetIdCatalog armyAssetId)
{
  uint32_t count = 0;
  AiRuntimeWorkspaceEntry *entry = g_AiWorkspace01Units;
  for (uint32_t i = 0; i < g_AiWorkspace01Count; i++, entry++) {
    if (entry->armyAssetId == armyAssetId) {
      count++;
    }
  }
  return count;
}

uint32_t AiImproved_OtherTechnologyScore(uint32_t baseCandidateScore)
{
  /* half the score a fully compatible unit technology gets (relation scale 1.0) */
  return ((uint32_t)(AI_TECHNOLOGY_RELATION_SCORE_FACTOR / 2) * baseCandidateScore) >> 8;
}
