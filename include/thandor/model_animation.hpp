#pragma once

#include "thandor/model.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace thandor {

// ArmyRuntime_UpdateAnimatedModelSubnodes: initial direction is descending, and
// direction changes only after a STRICT boundary crossing. Equality therefore
// keeps the endpoint for an extra callback. Evaluate single-tick callbacks in
// constant time, retaining the authored child translation even at tick zero.
inline int32_t model_runtime_child2_translation_q12(
    int32_t initial, int32_t minimum, int32_t maximum,
    int32_t step, uint32_t ticks) noexcept {
    if (ticks == 0U || step <= 0 || maximum <= minimum) return initial;
    const int64_t start = initial, low = minimum, high = maximum, delta = step;
    const int64_t first_crossing = start >= low ? (start - low) / delta + 1 : 1;
    if (static_cast<int64_t>(ticks) < first_crossing)
        return static_cast<int32_t>(start - static_cast<int64_t>(ticks) * delta);
    const int64_t leg = (high - low) / delta + 1;
    const int64_t phase = (static_cast<int64_t>(ticks) - first_crossing) % (2 * leg);
    return static_cast<int32_t>(phase < leg ? low + phase * delta
                                            : high - (phase - leg) * delta);
}

// Fresh V530 runtime-update table verification: class 10 directly dispatches
// ArmyRuntime_UpdateAnimatedModelSubnodes @ 0x00527E70. Classes 4/11/13/14 reach the same updater
// through their class callbacks. The stock class-10 assets are radar-like, but "radar" is an asset
// description, not a distinct engine animation algorithm. All of these channels consume values from
// the owning selected MDL runtime node; top-level ARM +0x10/+0x14 are unrelated runtime/link fields.
inline constexpr bool runtime_class_uses_generic_model_subnode_animation(uint32_t runtime_class_id) noexcept {
    return runtime_class_id == 4U || runtime_class_id == 11U ||
           runtime_class_id == 13U || runtime_class_id == 14U;
}

inline constexpr bool runtime_class_directly_dispatches_animated_model_subnodes(
    uint32_t runtime_class_id) noexcept {
    return runtime_class_id == 10U;
}

// Compatibility/UI semantic for the stock class-10 radar assets. Engine logic should prefer the
// direct-dispatch predicate above.
inline constexpr bool runtime_class_uses_continuous_radar_subnode_animation(
    uint32_t runtime_class_id) noexcept {
    return runtime_class_directly_dispatches_animated_model_subnodes(runtime_class_id);
}

// Retail class-init callback 0x005251C0 detaches childNodes[3] from the root model node for
// runtime class 14 after the hierarchy has been authored/created. Keep the serialized hierarchy
// intact for asset/editor previews, but suppress that child whenever the hierarchy represents a
// normal world/runtime instance. This is a hierarchy mutation, not a texture alpha rule.
inline constexpr bool model_runtime_world_child_enabled(uint32_t parent_runtime_class_id,
                                                        uint32_t child_slot) noexcept {
    // Class13 removes its placement child3 on every live world update (0x524740).
    // Class15 similarly detaches child1 outside the preview sentinel world.
    return !((parent_runtime_class_id == 13U && child_slot == 3U) ||
             (parent_runtime_class_id == 14U && child_slot == 3U) ||
             (parent_runtime_class_id == 15U && child_slot == 1U));
}

inline constexpr bool runtime_class_uses_animated_model_subnode_channels(
    uint32_t runtime_class_id) noexcept {
    return runtime_class_uses_generic_model_subnode_animation(runtime_class_id) ||
           runtime_class_directly_dispatches_animated_model_subnodes(runtime_class_id);
}

inline constexpr int32_t model_runtime_child_yaw_step_turn16(
    const ModelVisualNode& parent, uint32_t child_slot) noexcept {
    if (!runtime_class_uses_animated_model_subnode_channels(parent.runtime_class_id)) return 0;
    if (child_slot == 0U) return parent.simulation.yaw_max_velocity_turn16;
    if (child_slot == 1U) return parent.simulation.yaw_acceleration_turn16;
    return 0;
}

inline constexpr bool model_runtime_child_uses_bounded_vertical_channel(
    const ModelVisualNode& parent, uint32_t child_slot) noexcept {
    return child_slot == 2U &&
           runtime_class_uses_animated_model_subnode_channels(parent.runtime_class_id) &&
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
        // ArmyRuntimeHierarchy_UpdateProgressAndClassCallbacksRecursive dispatches class 10 to a
        // separate continuous-radar callback. Do not fold that channel into the generic
        // classes-4/11/13/14 authored animation flag merely because both consume +0x10/+0x1C.
        if (!runtime_class_uses_generic_model_subnode_animation(parent.runtime_class_id)) continue;
        if (model_runtime_child_yaw_step_turn16(parent, 0U) != 0 &&
            model_runtime_parent_has_live_child(hierarchy, parent_index, 0U)) return true;
        if (model_runtime_child_yaw_step_turn16(parent, 1U) != 0 &&
            model_runtime_parent_has_live_child(hierarchy, parent_index, 1U)) return true;
        if (model_runtime_child_uses_bounded_vertical_channel(parent, 2U) &&
            model_runtime_parent_has_live_child(hierarchy, parent_index, 2U)) return true;
    }
    return false;
}

inline std::size_t model_runtime_live_direct_child_count(
    std::span<const ModelVisualNode> hierarchy,std::size_t parent_index) noexcept {
    if (parent_index >= hierarchy.size()) return 0U;
    return static_cast<std::size_t>(std::count_if(hierarchy.begin(), hierarchy.end(),
        [parent_index](const ModelVisualNode& child) {
            return child.parent_index == parent_index && child.creates_runtime_node() &&
                   !child.sprite_path.empty();
        }));
}

inline bool hierarchy_root_uses_class17_deployment_tilt(
    std::span<const ModelVisualNode> hierarchy) noexcept {
    for (std::size_t root_index = 0; root_index < hierarchy.size(); ++root_index) {
        const auto& root = hierarchy[root_index];
        if (root.parent_index != std::numeric_limits<uint32_t>::max()) continue;
        // ModelRuntimeSlotClassInit_SeedFieldsFromRootTransform sets the deployment-state bits
        // only when 2 < rootModelNode->childCount. The class-17 update then touches exactly
        // childNodes[0], childNodes[1] and childNodes[2]. This cardinality gate is essential for
        // Dragonfly/Mosquito/Hornet variants whose root owns fewer than three live children.
        return root.runtime_class_id == 17U &&
               model_runtime_live_direct_child_count(hierarchy, root_index) > 2U;
    }
    return false;
}

inline bool hierarchy_has_continuous_radar_subnode_animation(
    std::span<const ModelVisualNode> hierarchy) noexcept {
    for (std::size_t parent_index = 0; parent_index < hierarchy.size(); ++parent_index) {
        const auto& parent = hierarchy[parent_index];
        if (!runtime_class_directly_dispatches_animated_model_subnodes(parent.runtime_class_id)) continue;
        if (model_runtime_child_yaw_step_turn16(parent, 0U) != 0 &&
            model_runtime_parent_has_live_child(hierarchy, parent_index, 0U)) return true;
        if (model_runtime_child_yaw_step_turn16(parent, 1U) != 0 &&
            model_runtime_parent_has_live_child(hierarchy, parent_index, 1U)) return true;
        if (model_runtime_child_uses_bounded_vertical_channel(parent, 2U) &&
            model_runtime_parent_has_live_child(hierarchy, parent_index, 2U)) return true;
    }
    return false;
}

} // namespace thandor
