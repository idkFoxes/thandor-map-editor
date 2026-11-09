#pragma once
// Open Thandor AiPlanningRebuild_CollectWorldEntities and CollectTargets:
// owner-list traversal, bounded 00/01/02/03 and first 64 visible entries in 07.
// The runtime wrapper decides which entities actually have a live world owner.
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace thandor {
// AiPlanningRebuild_CollectWorldEntities traverses world MODEL owners. It
// does not exclude a model just because its construction is unfinished or its
// terminal effect has started: removal from the world-owner list is the gate.
inline bool lab_ai_has_world_model_owner_v722(bool runtime_removed,
                                               std::uint32_t faction_index) noexcept {
    return !runtime_removed && faction_index != 0U;
}
enum class LabAiOwnerKindV718 : std::uint8_t {
    Skip, Structure00, Unit01, Visible02, Unseen03
};
struct LabAiOwnerWorkspaceV718 {
    std::vector<std::size_t> structures00;
    std::vector<std::size_t> units01;
    std::vector<std::size_t> visible02;
    std::vector<std::size_t> unseen03;
    std::vector<std::size_t> targets07;
    const std::vector<std::size_t>& workspace(unsigned int id) const noexcept {
        switch (id) {
            case 0: return structures00;
            case 1: return units01;
            case 2: return visible02;
            case 3: return unseen03;
            default: return targets07;
        }
    }
};
// Latest linked world-owner model is visited first. In the compact Lab the
// runtime's append-only entity indices correspond to the opposite order.
template<class Classify>
LabAiOwnerWorkspaceV718 lab_ai_admit_owner_list_v718(
        std::size_t entity_count, Classify&& classify) {
    LabAiOwnerWorkspaceV718 ws;
    ws.structures00.reserve(128); ws.units01.reserve(64);
    ws.visible02.reserve(128); ws.unseen03.reserve(512); ws.targets07.reserve(64);
    for (std::size_t visit=0; visit<entity_count; ++visit) {
        const auto index=entity_count-1U-visit;
        switch (classify(index)) {
            case LabAiOwnerKindV718::Structure00:
                if (ws.structures00.size()<128U) ws.structures00.push_back(index);
                break;
            case LabAiOwnerKindV718::Unit01:
                if (ws.units01.size()<64U) ws.units01.push_back(index);
                break;
            case LabAiOwnerKindV718::Visible02:
                if (ws.visible02.size()<128U) {
                    ws.visible02.push_back(index);
                    if (ws.targets07.size()<64U) ws.targets07.push_back(index);
                }
                break;
            case LabAiOwnerKindV718::Unseen03:
                if (ws.unseen03.size()<512U) ws.unseen03.push_back(index);
                break;
            case LabAiOwnerKindV718::Skip: break;
        }
    }
    return ws;
}
} // namespace thandor
