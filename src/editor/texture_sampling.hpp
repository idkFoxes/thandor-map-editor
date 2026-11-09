#pragma once

#include <algorithm>
#include <cstdint>

namespace editor {

enum class TextureSamplingMode : std::uint8_t {
    MipmappedAnisotropic,
    DirectDrawSingleLevelLinear,
};

struct TextureSamplingPlan {
    std::uint32_t mip_levels{1U};
    bool allow_anisotropy{};
    bool blend_between_mip_levels{};
};

constexpr std::uint32_t complete_mip_level_count(std::uint32_t extent) noexcept {
    std::uint32_t levels = 1U;
    for (std::uint32_t size = std::max(1U, extent); size > 1U; size /= 2U) ++levels;
    return levels;
}

constexpr TextureSamplingPlan texture_sampling_plan(
    std::uint32_t extent, TextureSamplingMode mode) noexcept {
    if (mode == TextureSamplingMode::DirectDrawSingleLevelLinear) {
        return {1U, false, false};
    }
    return {complete_mip_level_count(extent), true, true};
}

} // namespace editor
