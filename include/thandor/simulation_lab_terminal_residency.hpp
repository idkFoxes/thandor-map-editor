#pragma once

#include "thandor/model.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace thandor {

// Collect only effects that can be emitted by the concrete runtime MDL hierarchy
// currently resident in the selected Simulation Lab run. The terminal +0x80..B8
// channels are independent from the construction/dismantle +0x190 effect.
// Zero IDs are null, not stock effect zero. Source definition roots only: a
// same-MDL child SPR must not widen the selected-run texture atlas.
inline std::vector<std::uint32_t> simulation_lab_terminal_residency_effect_ids(
    std::span<const ModelVisualNode> hierarchy) {
    std::vector<std::uint32_t> ids;
    for (std::size_t index = 0; index < hierarchy.size(); ++index) {
        const auto& node = hierarchy[index];
        if (!node.creates_runtime_node()) continue;
        const bool definition_root = node.parent_index == std::numeric_limits<std::uint32_t>::max() ||
            (node.parent_index < hierarchy.size() &&
             hierarchy[node.parent_index].model_definition_id != node.model_definition_id);
        if (!definition_root) continue;
        for (const auto effect : node.simulation.terminal_attachment_effect_ids)
            if (effect != 0U) ids.push_back(effect);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

} // namespace thandor
