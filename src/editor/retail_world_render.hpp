#pragma once

#include "math.hpp"
#include "thandor/field.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>

namespace editor {

// V446 RecoveredCallable_0050BC60 filters the first model queue on live runtime flag 0x200.
// ModelNodeRuntime_CreateHierarchyRecursiveCf copies MDL +0x68 bit 0x20 to that runtime bit.
// The remaining normal model roots are drawn by the later post-generated-shading model queue.
enum class RetailWorldModelPass : uint8_t {
    PreTerrain = 0,
    PostGeneratedShading = 1,
    EditorCommandOverlay = 2, // editor intent: always visible after world depth passes
};

constexpr RetailWorldModelPass retail_world_model_pass_from_mdl_flags(uint32_t mdl_render_flags) noexcept {
    return (mdl_render_flags & 0x20U) != 0U
        ? RetailWorldModelPass::PreTerrain
        : RetailWorldModelPass::PostGeneratedShading;
}

inline int16_t retail_surface_signed16(uint32_t packed, uint32_t shift) noexcept {
    return static_cast<int16_t>((packed >> shift) & 0xFFFFU);
}

inline Vec3f retail_surface_position_at_tick(const thandor::TerrainVertex& vertex,
                                             uint32_t presentation_tick,
                                             bool animate_surface) noexcept {
    if (!animate_surface) return vertex.position;
    constexpr float kTurn16ToRadians = 2.0F * std::numbers::pi_v<float> / 65536.0F;
    const uint32_t phases = vertex.blend_materials[0];
    const uint32_t steps = vertex.blend_materials[1];
    const uint32_t amplitudes = vertex.blend_materials[2];
    const uint16_t phase_xy = static_cast<uint16_t>(phases & 0xFFFFU);
    const uint16_t phase_z = static_cast<uint16_t>((phases >> 16U) & 0xFFFFU);
    const int16_t step_xy = retail_surface_signed16(steps, 0U);
    const int16_t step_z = retail_surface_signed16(steps, 16U);
    const float amplitude_xy = thandor::q12_to_float(static_cast<int32_t>(amplitudes & 0xFFFFU));
    const float amplitude_z = thandor::q12_to_float(static_cast<int32_t>((amplitudes >> 16U) & 0xFFFFU));
    const uint16_t angle_xy = static_cast<uint16_t>(
        (static_cast<int64_t>(phase_xy) + static_cast<int64_t>(step_xy) * presentation_tick) & 0xFFFFLL);
    const uint16_t angle_z = static_cast<uint16_t>(
        (static_cast<int64_t>(phase_z) + static_cast<int64_t>(step_z) * presentation_tick) & 0xFFFFLL);
    const float radians_xy = static_cast<float>(angle_xy) * kTurn16ToRadians;
    const float radians_z = static_cast<float>(angle_z) * kTurn16ToRadians;
    return {vertex.position.x + std::cos(radians_xy) * amplitude_xy,
            vertex.position.y + std::sin(radians_xy) * amplitude_xy,
            vertex.position.z + std::cos(radians_z) * amplitude_z};
}

inline std::vector<uint32_t> retail_surface_back_to_front_indices(
    std::span<const thandor::TerrainVertex> vertices, const Camera& camera,
    uint32_t presentation_tick, bool animate_surface) {
    if ((vertices.size() % 3U) != 0U)
        throw std::runtime_error("retail secondary-surface packet stream is not triangle aligned");
    struct Entry { uint32_t first{}; float depth{}; uint32_t source_order{}; };
    std::vector<Entry> order;
    order.reserve(vertices.size() / 3U);
    const Vec3f eye = camera.eye();
    const Vec3f forward = normalize(sub(camera.target, eye));
    for (uint32_t first = 0U, source = 0U; first < vertices.size(); first += 3U, ++source) {
        const Vec3f a = retail_surface_position_at_tick(vertices[first], presentation_tick, animate_surface);
        const Vec3f b = retail_surface_position_at_tick(vertices[first + 1U], presentation_tick, animate_surface);
        const Vec3f c = retail_surface_position_at_tick(vertices[first + 2U], presentation_tick, animate_surface);
        const Vec3f centroid = mul(add(add(a, b), c), 1.0F / 3.0F);
        order.push_back({first, dot(sub(centroid, eye), forward), source});
    }
    // GraphicsPrimitiveQueue_RadixSortForRendering uses summed projected depth for packets whose
    // renderFlags include the secondary-surface 0x6000 class. Stable descending depth is the
    // Vulkan correspondence: far packets first, retaining source order for equal-depth packets.
    std::stable_sort(order.begin(), order.end(), [](const Entry& left, const Entry& right) {
        if (left.depth != right.depth) return left.depth > right.depth;
        return left.source_order < right.source_order;
    });
    std::vector<uint32_t> indices;
    indices.reserve(vertices.size());
    for (const auto& entry : order) {
        indices.push_back(entry.first);
        indices.push_back(entry.first + 1U);
        indices.push_back(entry.first + 2U);
    }
    return indices;
}

} // namespace editor
