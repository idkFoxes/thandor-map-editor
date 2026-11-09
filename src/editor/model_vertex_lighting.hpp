#pragma once

#include "math.hpp"
#include "model_transform.hpp"

#include <bit>
#include <cstdint>

namespace editor {

struct ModelVertexLighting {
    Vec3f normal{};
    uint32_t directional_color{};
};

// SPR stores a separate corner for each triangle even when its normal is repeated.
// Keep only the previous result, within one posed model node and one scene build.
// Positions, UVs, materials and the emitted corner stream remain independent.
class ModelVertexLightingCache {
public:
    ModelVertexLightingCache(const ModelHierarchyTransform& transform,
                             thandor::PreparedDirectionalLight light,
                             const std::array<uint32_t, 256>& ramp)
        : transform_(transform), light_(light), ramp_(ramp) {}

    ModelVertexLighting sample(Vec3f source_normal) {
        const std::array<uint32_t, 3> key{
            std::bit_cast<uint32_t>(source_normal.x),
            std::bit_cast<uint32_t>(source_normal.y),
            std::bit_cast<uint32_t>(source_normal.z)};
        if (!valid_ || key != key_) {
            value_.normal = normalize(transform_vector(transform_, source_normal));
            value_.directional_color = light_.color_for_upward_normal(value_.normal, ramp_);
            key_ = key;
            valid_ = true;
        }
        return value_;
    }

private:
    const ModelHierarchyTransform transform_;
    const thandor::PreparedDirectionalLight light_;
    const std::array<uint32_t, 256>& ramp_;
    std::array<uint32_t, 3> key_{};
    ModelVertexLighting value_{};
    bool valid_{};
};

} // namespace editor
