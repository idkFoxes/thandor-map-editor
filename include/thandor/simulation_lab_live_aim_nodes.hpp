#pragma once

#include "thandor/simulation_lab.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace thandor {

// An aimed-at attachment is not a valid live target after its owning model
// component is released.  Class 21 uses direct child zero while present, but
// an absent/detached child must not leave a ghost aimpoint at its final pose.
inline std::optional<std::size_t> simulation_lab_live_target_aim_node_index(
    const SimulationLabEntity& entity,
    std::span<const ModelVisualNode> nodes) noexcept {
    std::size_t root_index=nodes.size();
    for (std::size_t i=0;i<nodes.size();++i) {
        if (nodes[i].parent_index==std::numeric_limits<std::uint32_t>::max() &&
            nodes[i].creates_runtime_node() && simulation_lab_model_node_present(entity,i)) {
            root_index=i;
            break;
        }
    }
    if (root_index==nodes.size()) return std::nullopt;
    if (entity.runtime_model_class_id==21U) {
        for (std::size_t i=0;i<nodes.size();++i) {
            if (nodes[i].parent_index==root_index && nodes[i].child_slot==0U &&
                nodes[i].creates_runtime_node() && simulation_lab_model_node_present(entity,i))
                return i;
        }
    }
    return root_index;
}

inline bool simulation_lab_live_weapon_pose_node(
    const SimulationLabEntity& entity,std::span<const ModelVisualNode> nodes,
    std::size_t node_index,std::size_t pose_count) noexcept {
    return node_index<nodes.size() && node_index<pose_count &&
        nodes[node_index].creates_runtime_node() &&
        simulation_lab_model_node_present(entity,node_index);
}

} // namespace thandor
