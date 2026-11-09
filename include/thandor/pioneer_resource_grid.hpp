#pragma once

#include "thandor/field.hpp"
#include "thandor/fog_preview.hpp"

#include <cstdint>

namespace thandor {
// ArmyPlacement_CanPlaceResourceExtractor checks deposit support in the FLD
// cell rounded from physical world Q12 X/Y. This differs from the triangular
// selection used for fog, topology and many terrain lookups. The candidate's
// own common collision/contact-kind terrain test remains a separate gate.
inline bool pioneer_class14_resource_cell_eligible(const FieldGrid& field,
    std::int32_t x_q12,std::int32_t y_q12,std::uint32_t resource_class) {
    const auto cell=retail_nearest_world_to_cell(field,x_q12,y_q12);
    return cell && !field.terrain_rejects_placement_occupancy_class(
        cell->x,cell->y,resource_class);
}
} // namespace thandor
