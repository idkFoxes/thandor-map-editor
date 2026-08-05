#include "thandor/fog_preview.hpp"

#include <cstddef>

namespace thandor {

bool fog_preview_point_visible(std::span<const FogPreviewObserver> observers,
                               Vec3f point) noexcept {
    for (const auto& observer : observers) {
        const float dx = point.x - observer.position.x;
        const float dy = point.y - observer.position.y;
        if (dx * dx + dy * dy <= observer.radius * observer.radius) return true;
    }
    return false;
}

std::vector<uint8_t> build_radial_fog_preview_visibility(
    const FieldGrid& field,
    std::span<const FogPreviewObserver> observers) {
    std::vector<uint8_t> visibility(
        static_cast<std::size_t>(field.width()) * field.height(), 0U);
    for (uint32_t y = 0; y < field.height(); ++y) {
        for (uint32_t x = 0; x < field.width(); ++x) {
            const auto& cell = field.at(x, y);
            const Vec3f point{q12_to_float(cell.world_x_q12),
                              q12_to_float(cell.world_y_q12),
                              cell.terrain_height()};
            if (fog_preview_point_visible(observers, point))
                visibility[static_cast<std::size_t>(y) * field.width() + x] = 1U;
        }
    }
    return visibility;
}

} // namespace thandor
