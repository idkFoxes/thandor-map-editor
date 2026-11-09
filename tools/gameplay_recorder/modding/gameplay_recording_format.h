#ifndef THANDOR_GAMEPLAY_RECORDING_FORMAT_H
#define THANDOR_GAMEPLAY_RECORDING_FORMAT_H
#include "../common/win32_min.h"

#define TGR_VERSION 2u
/* V1 ACTR used the model payload as an army: actor ownership/commands invalid.
   V2 preserves record sizes, fixes the owner indirection and normalizes +0x98.
   Header flag 0x4 marks this corrected owner contract. */
#define TGR_TAG_TICK 0x4b434954u /* TICK */
#define TGR_TAG_UCMD 0x444d4355u /* UCMD */
#define TGR_TAG_AIPH 0x48504941u /* AIPH */
#define TGR_TAG_FACT 0x54434146u /* FACT */
#define TGR_TAG_ACTR 0x52544341u /* ACTR */
#define TGR_TAG_AIAA 0x41414941u /* AIAA */
#define TGR_TAG_AIFA 0x41464941u /* AIFA */
#define TGR_TAG_RMOV 0x564f4d52u /* RMOV */
#define TGR_TAG_ENDT 0x54444e45u /* ENDT */

#define TGR_USER_LOCAL_SINGLEPLAYER 1u
#define TGR_USER_NETWORK_APPLIED 2u
#define TGR_AI_ACTOR_SPAWN 1u
#define TGR_AI_ACTOR_COMMAND_OR_STATE 2u
#define TGR_AI_ACTOR_DESPAWN 3u

#pragma pack(push,1)
typedef struct TgrRecordHeaderV1 { u32 tag; u32 payloadBytes; } TgrRecordHeaderV1;

typedef struct TgrFileHeaderV1 {
    u8 magic[8];
    u32 version;
    u32 headerBytes;
    u32 flags;
    u32 initialSimulationTick;
    u32 initialPrimarySeed;
    u32 initialSecondarySeed;
    u16 mapName[64];
    u32 reserved[8];
} TgrFileHeaderV1;

typedef struct TgrTickV1 {
    u32 simulationTick;
    u32 presentationTick;
    u32 periodicClockTick;
    u32 gameSpeedQ8;
    u32 relationUiFlags;
    u32 networkTick;
    u32 primarySeed;
    u32 secondarySeed;
    u32 rngSelector;       /* 0 primary, 1 secondary, 2 unknown/custom */
    u32 humanFactionMask;
    u32 playerCount;
    u32 pendingOverflowCount;
} TgrTickV1;

typedef struct TgrUserCommandV1 {
    u32 source;
    u32 capturedSimulationTick;
    u32 playerRuntimeId;
    u32 factionIndex;
    u32 commandCode;
    u32 payloadDword0C;
    u32 payloadDword08;
    u32 payloadDword04;
} TgrUserCommandV1;

typedef struct TgrAiPlannerV1 {
    u32 capturedSimulationTick;
    u32 factionIndex;
    u32 schedulePhase;
    u32 reserved;
} TgrAiPlannerV1;

typedef struct TgrFactionStateV1 {
    u32 factionIndex;
    u32 lifecycleState;
    u8 scalarState00_BF[0xc0];
    u32 technologyMasks256Bits[8];
    u32 relationStateTicks[8];
    s32 aiPressureValues[8];
} TgrFactionStateV1;

typedef struct TgrActorStateV1 {
    u32 actorId;
    u32 factionIndex;
    u32 armyAssetId;
    u32 nodeRuntimeFlags;
    s32 positionXQ12;
    s32 positionYQ12;
    s32 positionZQ12;
    u32 worldRotationAngle0;
    u32 worldRotationAngle1;
    u32 worldRotationAngle2;
    s32 modelScaleQ12;
    u32 commandTargetActorId;
    s32 commandCoordinate0;
    s32 commandCoordinate1;
    s32 commandCoordinate2;
    u32 commandModeFlags;
    u32 commandGeneration;
    s32 actionVector0;
    s32 actionVector1;
    s32 actionVector2;
    u32 runtimeState40;
    u32 runtimeState44;
    u32 runtimeState48;
    u32 runtimeState4C;
    s32 movementPosition0;
    s32 movementPosition1;
    u32 classState60;
    u32 ownerValue64;
    u32 ownerValue68;
    s32 fallbackWorldY;
    s32 fallbackWorldX;
    s32 movementTarget0;
    s32 movementTarget1;
    u32 runtimeState8C;
    u32 runtimeState90;
    u32 runtimeState94;
    u32 runtimeState98; /* V2 assignedTargetActorId, normalized from army +0x98 */
    u32 depthBinClass;
    u32 runtimeFlags;
    u32 runtimeTimer;
    u32 stateOrTechnologyId;
} TgrActorStateV1;

typedef struct TgrAiActorActionV1 {
    u32 factionIndex;
    u32 actorId;
    u32 actionKind;
    u32 changedDwordMask[2];
} TgrAiActorActionV1;

typedef struct TgrAiFactionActionV1 {
    u32 factionIndex;
    u32 changedDwordMask[3];
} TgrAiFactionActionV1;

typedef struct TgrActorRemoveV1 { u32 actorId; u32 factionIndex; } TgrActorRemoveV1;

typedef struct TgrTickEndV1 {
    u32 simulationTick;
    u32 recordCount;
    u32 actorCount;
    u32 factionCount;
} TgrTickEndV1;
#pragma pack(pop)

typedef char TgrRecordHeaderV1_size_check[(sizeof(TgrRecordHeaderV1)==8)?1:-1];
typedef char TgrFileHeaderV1_size_check[(sizeof(TgrFileHeaderV1)==192)?1:-1];
typedef char TgrTickV1_size_check[(sizeof(TgrTickV1)==48)?1:-1];
typedef char TgrUserCommandV1_size_check[(sizeof(TgrUserCommandV1)==32)?1:-1];
typedef char TgrAiPlannerV1_size_check[(sizeof(TgrAiPlannerV1)==16)?1:-1];
typedef char TgrFactionStateV1_size_check[(sizeof(TgrFactionStateV1)==296)?1:-1];
typedef char TgrActorStateV1_size_check[(sizeof(TgrActorStateV1)==164)?1:-1];
typedef char TgrAiActorActionV1_size_check[(sizeof(TgrAiActorActionV1)==20)?1:-1];
typedef char TgrAiFactionActionV1_size_check[(sizeof(TgrAiFactionActionV1)==16)?1:-1];
typedef char TgrActorRemoveV1_size_check[(sizeof(TgrActorRemoveV1)==8)?1:-1];
typedef char TgrTickEndV1_size_check[(sizeof(TgrTickEndV1)==16)?1:-1];

#endif
