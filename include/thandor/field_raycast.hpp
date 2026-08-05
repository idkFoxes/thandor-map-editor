#pragma once

#include "thandor/field.hpp"

#include <cstdint>
#include <optional>

namespace thandor {

enum class FieldRaycastSurface {
    Terrain,
    Secondary,
    Topmost,
};

struct FieldRaycastHit {
    uint32_t quad_x{};
    uint32_t quad_y{};
    uint32_t nearest_vertex_x{};
    uint32_t nearest_vertex_y{};
    Vec3f position{};
    float distance{};
    bool secondary_surface{};
    uint32_t visited_cells{};
};

// V338 confirms Q12 field-ray contracts and the game's 0x400 traversal ceiling.  The editor uses
// the same bounded cell walk with Q12/Q28-quantized inputs while retaining its affine FLD basis.
std::optional<FieldRaycastHit> raycast_field_surface_compatibility(
    const FieldGrid& field, Vec3f origin, Vec3f direction, float maximum_distance,
    FieldRaycastSurface surface = FieldRaycastSurface::Topmost,
    uint32_t maximum_cell_steps = 0x400U) noexcept;

} // namespace thandor
