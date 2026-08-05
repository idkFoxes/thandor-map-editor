#pragma once

#include "thandor/field.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace thandor {

// Editor-only current-visibility preview. Retail LEV/FLD data contains terrain,
// placements and player configuration, but no persistent per-player explored bitmap.
struct FogPreviewObserver {
    Vec3f position{};
    float radius{};
};

bool fog_preview_point_visible(std::span<const FogPreviewObserver> observers,
                               Vec3f point) noexcept;

std::vector<uint8_t> build_radial_fog_preview_visibility(
    const FieldGrid& field,
    std::span<const FogPreviewObserver> observers);

} // namespace thandor
