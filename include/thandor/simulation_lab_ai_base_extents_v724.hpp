#pragma once
// Open Thandor: AiBaseSiteWorkspace_AddCellInsideBase / AddLargeCellInsideBase
// call AiPrimaryWorkspace_IsPointOutsideAllEntryExtents. Only already admitted,
// assigned Workspace 00 model owners may supply extents (capacity 128).
#include "thandor/pioneer_movement_update.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace thandor {
struct LabAiPrimaryExtentV724 {
    MovementPoint root_position_q12{};
    std::int32_t support_radius_q12{};
};

template<class GetExtent>
bool lab_ai_inside_primary_extents_v724(MovementPoint point,
    std::span<const std::size_t> admitted_structure_indices, GetExtent&& get_extent) {
    for (const auto entity_index : admitted_structure_indices) {
        // Unassigned pending assets (null ModelRuntimeSlot) do not provide a
        // footprint. The caller supplies only world-owner slots in Workspace 00.
        const std::optional<LabAiPrimaryExtentV724> entry = get_extent(entity_index);
        if (!entry || entry->support_radius_q12 <= 0) continue;
        const auto dx = std::int64_t(point.x)-entry->root_position_q12.x;
        const auto dy = std::int64_t(point.y)-entry->root_position_q12.y;
        const auto extent = std::int64_t(entry->support_radius_q12);
        // Strict square bound, not an Euclidean radius, just as the source.
        if (dx > -extent && dx < extent && dy > -extent && dy < extent)
            return true;
    }
    return false;
}
} // namespace thandor
