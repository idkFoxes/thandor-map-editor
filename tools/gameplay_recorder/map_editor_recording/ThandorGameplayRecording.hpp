#pragma once
#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace thandor::recording {
#pragma pack(push,1)
struct RecordHeader { std::uint32_t tag, payloadBytes; };
struct FileHeader {
    std::uint8_t magic[8];
    std::uint32_t version, headerBytes, flags, initialSimulationTick, initialPrimarySeed, initialSecondarySeed;
    std::uint16_t mapName[64];
    std::uint32_t reserved[8];
};
struct Tick {
    std::uint32_t simulationTick, presentationTick, periodicClockTick, gameSpeedQ8, relationUiFlags, networkTick;
    std::uint32_t primarySeed, secondarySeed, rngSelector, humanFactionMask, playerCount, pendingOverflowCount;
};
struct UserCommand {
    std::uint32_t source, capturedSimulationTick, playerRuntimeId, factionIndex, commandCode;
    std::uint32_t payloadDword0C, payloadDword08, payloadDword04;
};
struct AiPlanner { std::uint32_t capturedSimulationTick, factionIndex, schedulePhase, reserved; };
struct FactionState {
    std::uint32_t factionIndex, lifecycleState;
    std::uint8_t scalarState00_BF[0xc0];
    std::uint32_t technologyMasks256Bits[8], relationStateTicks[8];
    std::int32_t aiPressureValues[8];
};
struct ActorState {
    std::uint32_t actorId, factionIndex, armyAssetId, nodeRuntimeFlags;
    std::int32_t positionXQ12, positionYQ12, positionZQ12;
    std::uint32_t worldRotationAngle0, worldRotationAngle1, worldRotationAngle2;
    std::int32_t modelScaleQ12;
    std::uint32_t commandTargetActorId;
    std::int32_t commandCoordinate0, commandCoordinate1, commandCoordinate2;
    std::uint32_t commandModeFlags, commandGeneration;
    std::int32_t actionVector0, actionVector1, actionVector2;
    std::uint32_t runtimeState40, runtimeState44, runtimeState48, runtimeState4C;
    std::int32_t movementPosition0, movementPosition1;
    std::uint32_t classState60, ownerValue64, ownerValue68;
    std::int32_t fallbackWorldY, fallbackWorldX, movementTarget0, movementTarget1;
    std::uint32_t runtimeState8C, runtimeState90, runtimeState94, runtimeState98;
    std::uint32_t depthBinClass, runtimeFlags, runtimeTimer, stateOrTechnologyId;
};
struct AiActorAction { std::uint32_t factionIndex, actorId, actionKind, changedDwordMask[2]; };
struct AiFactionAction { std::uint32_t factionIndex, changedDwordMask[3]; };
struct ActorRemove { std::uint32_t actorId, factionIndex; };
struct TickEnd { std::uint32_t simulationTick, recordCount, actorCount, factionCount; };
#pragma pack(pop)

static constexpr std::uint32_t TAG_TICK=0x4b434954u;
static constexpr std::uint32_t TAG_UCMD=0x444d4355u;
static constexpr std::uint32_t TAG_AIPH=0x48504941u;
static constexpr std::uint32_t TAG_FACT=0x54434146u;
static constexpr std::uint32_t TAG_ACTR=0x52544341u;
static constexpr std::uint32_t TAG_AIAA=0x41414941u;
static constexpr std::uint32_t TAG_AIFA=0x41464941u;
static constexpr std::uint32_t TAG_RMOV=0x564f4d52u;
static constexpr std::uint32_t TAG_ENDT=0x54444e45u;

struct TickFrame {
    Tick tick{};
    std::vector<UserCommand> userCommands;
    std::vector<AiPlanner> aiPlannerPasses;
    std::vector<FactionState> factions;
    std::vector<ActorState> actors;
    std::vector<AiActorAction> aiActorActions;
    std::vector<AiFactionAction> aiFactionActions;
    std::vector<ActorRemove> removedActors;
    TickEnd end{};
};

class Reader {
public:
    bool open(const std::string& path, std::string* error=nullptr, bool allowLegacyActorState=false);
    bool nextTick(TickFrame& frame, std::string* error=nullptr);
    const FileHeader& header() const { return header_; }
    bool hasCorrectArmyState() const { return header_.version==2 && (header_.flags & 4u)!=0; }
    bool eof() const { return file_.eof(); }
private:
    template<class T> bool readPayload(std::uint32_t bytes, T& out, std::string* error);
    bool readHeader(RecordHeader& h, std::string* error);
    bool fail(std::string* error, const char* message);
    std::ifstream file_;
    FileHeader header_{};
};

std::string utf8MapName(const FileHeader& h);
}
