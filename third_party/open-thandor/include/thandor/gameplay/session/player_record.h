/*
 * Open Thandor
 * File: include/thandor/gameplay/session/player_record.h
 * Record of the player's own actions (not part of the original game).
 */

#ifndef THANDOR_GAMEPLAY_SESSION_PLAYER_RECORD_H
#define THANDOR_GAMEPLAY_SESSION_PLAYER_RECORD_H

#include <stdint.h>

/* With ki.ini "aufzeichnung = an" (single player) the local player's commands are written, readable, to a file
   of their own next to the run's log ("<log>_spieler.log", Thandor_LogPlayer): driving, attacking, firing at a
   point, stopping, buying, placing and selling, research, diplomacy, pause and game speed, each with the game
   time, the units of the selection and the place. Every five minutes a line with the player's stock follows.
   It only reads; the game runs exactly as without it.

   The commands come from the local branch of InGameCommand_Issue / InGameCommand_IssueHandler
   (network/protocol/commands.h), the same handlers a networked game would queue: PlayerRecord_BeforeLocalCommand
   right before the handler runs (to keep what the handler consumes, the building chosen for placement), and
   PlayerRecord_AfterLocalCommand right after it (to read where the units are going now). code is the command's
   code in the in-game command table (INGAME_COMMAND_*), 0xFFFFFFFF for a handler without one. */
void PlayerRecord_BeforeLocalCommand(uint32_t code,uint32_t payload1,uint32_t payload2,uint32_t payload3);
void PlayerRecord_AfterLocalCommand(uint32_t code,uint32_t payload1,uint32_t payload2,uint32_t payload3);

/* A new level or a loaded savegame: a heading line for the record. */
void PlayerRecord_OnSessionStart(bool loadedFromSave);

/* Once per simulation step: the stock line every five minutes of game time. */
void PlayerRecord_OnSimulationStep();

#endif /* THANDOR_GAMEPLAY_SESSION_PLAYER_RECORD_H */
