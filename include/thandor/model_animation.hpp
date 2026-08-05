#pragma once

#include "thandor/model.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace thandor {

// ArmyRuntimeHierarchy_UpdateProgressAndClassCallbacksRecursive dispatches runtime classes 4, 11,
// 13 and 14 through ArmyRuntime_UpdateAnimatedModelSubnodes. Class 10 owns the separate continuous
// radar callback recovered earlier. Both consume animation values from the owning selected MDL
// runtime node; top-level ARM +0x10/+0x14 are unrelated runtime/link fields.
inline constexpr bool runtime_class_uses_generic_model_subnode_animation(uint32_t runtime_class_id) noexcept {
    return runtime_class_id == 4U || runtime_class_id == 11U ||
           runtime_class_id == 13U || runtime_class_id == 14U;
}

inline constexpr bool runtime_class_uses_continuous_radar_subnode_animation(uint32_t runtime_class_id) noexcept {
    return runtime_class_id == 10U;
}

inline constexpr int32_t model_runtime_child_yaw_step_turn16(
    const ModelVisualNode& parent, uint32_t child_slot) noexcept {
    if (!runtime_class_uses_generic_model_subnode_animation(parent.runtime_class_id) &&
        !runtime_class_uses_continuous_radar_subnode_animation(parent.runtime_class_id)) return 0;
    if (child_slot == 0U) return parent.simulation.yaw_max_velocity_turn16;
    if (child_slot == 1U) return parent.simulation.yaw_acceleration_turn16;
    return 0;
}

inline constexpr bool model_runtime_child_uses_bounded_vertical_channel(
    const ModelVisualNode& parent, uint32_t child_slot) noexcept {
    return child_slot == 2U &&
           runtime_class_uses_generic_model_subnode_animation(parent.runtime_class_id) &&
           parent.simulation.pitch_max_velocity_turn16 != 0 &&
           parent.simulation.pitch_max_turn16 > parent.simulation.pitch_min_turn16;
}

inline bool model_runtime_parent_has_live_child(std::span<const ModelVisualNode> hierarchy,
                                                std::size_t parent_index,
                                                uint32_t child_slot) noexcept {
    for (const auto& child : hierarchy) {
        if (child.parent_index == parent_index && child.child_slot == child_slot &&
            child.creates_runtime_node() && !child.sprite_path.empty()) return true;
    }
    return false;
}

inline bool hierarchy_has_authored_model_subnode_animation(
    std::span<const ModelVisualNode> hierarchy) noexcept {
    for (std::size_t parent_index = 0; parent_index < hierarchy.size(); ++parent_index) {
        const auto& parent = hierarchy[parent_index];
        if (model_runtime_child_yaw_step_turn16(parent, 0U) != 0 &&
            model_runtime_parent_has_live_child(hierarchy, parent_index, 0U)) return true;
        if (model_runtime_child_yaw_step_turn16(parent, 1U) != 0 &&
            model_runtime_parent_has_live_child(hierarchy, parent_index, 1U)) return true;
        if (model_runtime_child_uses_bounded_vertical_channel(parent, 2U) &&
            model_runtime_parent_has_live_child(hierarchy, parent_index, 2U)) return true;
    }
    return false;
}

inline bool hierarchy_has_continuous_radar_subnode_animation(
    std::span<const ModelVisualNode> hierarchy) noexcept {
    for (std::size_t parent_index = 0; parent_index < hierarchy.size(); ++parent_index) {
        const auto& parent = hierarchy[parent_index];
        if (!runtime_class_uses_continuous_radar_subnode_animation(parent.runtime_class_id)) continue;
        if (model_runtime_child_yaw_step_turn16(parent, 0U) != 0 &&
            model_runtime_parent_has_live_child(hierarchy, parent_index, 0U)) return true;
        if (model_runtime_child_yaw_step_turn16(parent, 1U) != 0 &&
            model_runtime_parent_has_live_child(hierarchy, parent_index, 1U)) return true;
    }
    return false;
}

} // namespace thandor
