#pragma once

#include "thandor/simulation_lab.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace thandor {

// Resolve an authored MDL damage emitter against the live runtime *component* at the
// definition root. This is not an invented four-stage replacement-model selector:
// the stock MDL rule is one strict integrity-percent threshold plus an EFF.
inline std::optional<uint32_t> simulation_lab_damage_emitter_for_model(
    const SimulationLabEntity& entity, std::span<const ModelVisualNode> hierarchy,
    size_t node_index) noexcept {
    if (entity.runtime_removed || !entity.construction_complete ||
        node_index >= hierarchy.size()) return std::nullopt;
    const auto& node=hierarchy[node_index];
    const bool runtime_definition_root =
        node.parent_index == std::numeric_limits<uint32_t>::max() ||
        (node.parent_index < hierarchy.size() &&
         hierarchy[node.parent_index].model_definition_id != node.model_definition_id);
    if (!runtime_definition_root || node.simulation.damage_effect_id == 0U ||
        node.simulation.damage_effect_health_threshold_percent <= 0 ||
        !simulation_lab_model_node_present(entity,node_index)) return std::nullopt;

    int32_t integrity=0, maximum=0;
    bool found=false;
    for (const auto& component: entity.model_components) {
        if (component.source_model_node_index != node_index) continue;
        if (component.runtime_removed || component.terminal_runtime_flag8 ||
            component.integrity <= 0 || component.maximum_integrity <= 0) return std::nullopt;
        integrity=component.integrity;
        maximum=component.maximum_integrity;
        found=true;
        break;
    }
    // Synthetic fixtures and simple one-node actors may have no concrete component vector;
    // only the actual root is allowed to use the compatibility integrity summary.
    if (!found && entity.model_components.empty() &&
        node.parent_index == std::numeric_limits<uint32_t>::max() &&
        entity.root_model_maximum_integrity > 0) {
        integrity=entity.root_model_integrity;
        maximum=entity.root_model_maximum_integrity;
        found=true;
    }
    if (!found || integrity <= 0 ||
        (entity.root_model_terminal &&
         node.parent_index == std::numeric_limits<uint32_t>::max())) return std::nullopt;
    const int32_t percent=static_cast<int32_t>(std::clamp<int64_t>(
        static_cast<int64_t>(integrity)*100/maximum,0,100));
    if (percent >= node.simulation.damage_effect_health_threshold_percent) return std::nullopt;
    return node.simulation.damage_effect_id;
}

} // namespace thandor
