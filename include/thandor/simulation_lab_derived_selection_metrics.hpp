#pragma once

#include "thandor/model.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace thandor {

struct SimulationLabDerivedSelectionMetrics {
    std::int32_t fog_uncover_radius_q12{};
    std::uint32_t occupancy_mark_radius_units{};
    std::int32_t fog_reference_height_offset_q12{};
    std::uint32_t runtime_root_count{};
};

constexpr bool simulation_lab_is_model_runtime_root(
    std::span<const ModelVisualNode> hierarchy,
    std::size_t node_index) noexcept {
    if (node_index >= hierarchy.size() || !hierarchy[node_index].creates_runtime_node())
        return false;
    const auto parent = hierarchy[node_index].parent_index;
    return parent == std::numeric_limits<std::uint32_t>::max() ||
        parent >= hierarchy.size() ||
        hierarchy[parent].model_definition_id != hierarchy[node_index].model_definition_id;
}


inline void simulation_lab_accumulate_derived_selection_metrics(
    SimulationLabDerivedSelectionMetrics& result,
    const ModelVisualNode& node,
    std::int32_t runtime_root_height_offset_q12) noexcept {
    result.fog_uncover_radius_q12 = std::max(
        result.fog_uncover_radius_q12, node.simulation.fog_uncover_radius_q12);
    result.occupancy_mark_radius_units = std::max(
        result.occupancy_mark_radius_units, node.simulation.occupancy_mark_radius_units);
    // Native stores this maximum as a dword. A child below its army root
    // can wrap negative, win the unsigned comparison, then lower the final
    // signed occlusion reference after the root Z is added.
    const uint32_t eye_height=uint32_t(runtime_root_height_offset_q12)+
        uint32_t(node.simulation.fog_reference_height_offset_q12);
    if(uint32_t(result.fog_reference_height_offset_q12)<eye_height)
        result.fog_reference_height_offset_q12=std::bit_cast<int32_t>(eye_height);
    ++result.runtime_root_count;
}

// Mirrors ArmyRuntime_RebuildDerivedSelectionMetrics for the fields represented
// by Simulation Lab. Only ModelRuntime roots contribute; visual children inside
// one MDL are not separate model runtimes and must not inflate the eye height.
inline SimulationLabDerivedSelectionMetrics simulation_lab_derived_selection_metrics(
    std::span<const ModelVisualNode> hierarchy,
    std::span<const std::int32_t> runtime_root_height_offsets_q12 = {}) noexcept {
    SimulationLabDerivedSelectionMetrics result;
    for (std::size_t node_index = 0; node_index < hierarchy.size(); ++node_index) {
        if (!simulation_lab_is_model_runtime_root(hierarchy, node_index)) continue;
        const auto height = node_index < runtime_root_height_offsets_q12.size()
            ? runtime_root_height_offsets_q12[node_index] : 0;
        simulation_lab_accumulate_derived_selection_metrics(
            result, hierarchy[node_index], height);
    }
    return result;
}

} // namespace thandor
