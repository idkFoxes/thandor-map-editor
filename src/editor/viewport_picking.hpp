#pragma once

#include "math.hpp"
#include "thandor/field.hpp"
#include "thandor/field_raycast.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace editor {

struct ViewRay {
    Vec3f origin{};
    Vec3f direction{0.0F, 0.0F, -1.0F};
};

struct AxisAlignedBounds {
    Vec3f minimum{std::numeric_limits<float>::max(),
                  std::numeric_limits<float>::max(),
                  std::numeric_limits<float>::max()};
    Vec3f maximum{std::numeric_limits<float>::lowest(),
                  std::numeric_limits<float>::lowest(),
                  std::numeric_limits<float>::lowest()};

    bool valid() const noexcept {
        return minimum.x <= maximum.x && minimum.y <= maximum.y && minimum.z <= maximum.z;
    }

    void include(Vec3f point) noexcept {
        minimum.x = std::min(minimum.x, point.x);
        minimum.y = std::min(minimum.y, point.y);
        minimum.z = std::min(minimum.z, point.z);
        maximum.x = std::max(maximum.x, point.x);
        maximum.y = std::max(maximum.y, point.y);
        maximum.z = std::max(maximum.z, point.z);
    }

    void pad(float amount) noexcept {
        if (!valid()) return;
        const float safe = std::max(0.0F, amount);
        minimum = {minimum.x - safe, minimum.y - safe, minimum.z - safe};
        maximum = {maximum.x + safe, maximum.y + safe, maximum.z + safe};
    }
};


inline std::optional<float> ray_triangle_distance(const ViewRay& ray, Vec3f a, Vec3f b, Vec3f c) noexcept;


struct TransformedBoundsHull {
    std::array<Vec3f, 8> corners{};
    bool valid{};

    static TransformedBoundsHull from_axis_aligned(const AxisAlignedBounds& bounds) noexcept {
        TransformedBoundsHull result;
        if (!bounds.valid()) return result;
        for (uint32_t corner = 0U; corner < 8U; ++corner) {
            result.corners[corner] = {
                (corner & 1U) != 0U ? bounds.maximum.x : bounds.minimum.x,
                (corner & 2U) != 0U ? bounds.maximum.y : bounds.minimum.y,
                (corner & 4U) != 0U ? bounds.maximum.z : bounds.minimum.z};
        }
        result.valid = true;
        return result;
    }

    AxisAlignedBounds axis_aligned_bounds() const noexcept {
        AxisAlignedBounds result;
        if (!valid) return result;
        for (const auto corner : corners) result.include(corner);
        return result;
    }
};

inline std::optional<float> ray_transformed_bounds_distance(
    const ViewRay& ray, const TransformedBoundsHull& hull) noexcept {
    if (!hull.valid) return std::nullopt;
    // Eight transformed SPR header-bound corners, twelve face triangles.  This mirrors the
    // recovered projected-bound face census while retaining a world-ray formulation.
    static constexpr std::array<std::array<uint8_t, 3>, 12> faces{{
        {{0, 2, 3}}, {{0, 3, 1}}, // z-min
        {{4, 5, 7}}, {{4, 7, 6}}, // z-max
        {{0, 1, 5}}, {{0, 5, 4}}, // y-min
        {{2, 6, 7}}, {{2, 7, 3}}, // y-max
        {{0, 4, 6}}, {{0, 6, 2}}, // x-min
        {{1, 3, 7}}, {{1, 7, 5}}, // x-max
    }};
    float nearest = std::numeric_limits<float>::max();
    bool found = false;
    for (const auto& face : faces) {
        const auto distance = ray_triangle_distance(ray, hull.corners[face[0]],
            hull.corners[face[1]], hull.corners[face[2]]);
        if (distance && *distance < nearest) {
            nearest = *distance;
            found = true;
        }
    }
    return found ? std::optional<float>{nearest} : std::nullopt;
}

inline ViewRay make_view_ray(const Camera& camera, float mouse_x, float mouse_y,
                             float viewport_width, float viewport_height) noexcept {
    const float width = std::max(viewport_width, 1.0F);
    const float height = std::max(viewport_height, 1.0F);
    const float normalized_x = mouse_x * (2.0F / width) - 1.0F;
    const float normalized_y = mouse_y * (2.0F / height) - 1.0F;
    const float tangent = std::tan(camera.fov_y * 0.5F);
    const float aspect = width / height;

    const Vec3f origin = camera.eye();
    const Vec3f forward = normalize(sub(camera.target, origin));
    const Vec3f right = normalize(cross(forward, {0.0F, 0.0F, 1.0F}));
    const Vec3f up = normalize(cross(right, forward));
    const Vec3f direction = normalize(add(forward,
        add(mul(right, normalized_x * tangent * aspect),
            mul(up, -normalized_y * tangent))));
    return {origin, direction};
}

inline std::optional<float> ray_triangle_distance(const ViewRay& ray, Vec3f a, Vec3f b,
                                                   Vec3f c) noexcept {
    constexpr float kEpsilon = 1.0e-7F;
    const Vec3f edge1 = sub(b, a);
    const Vec3f edge2 = sub(c, a);
    const Vec3f p = cross(ray.direction, edge2);
    const float determinant = dot(edge1, p);
    if (std::abs(determinant) <= kEpsilon) return std::nullopt;
    const float inverse = 1.0F / determinant;
    const Vec3f offset = sub(ray.origin, a);
    const float u = dot(offset, p) * inverse;
    if (u < -kEpsilon || u > 1.0F + kEpsilon) return std::nullopt;
    const Vec3f q = cross(offset, edge1);
    const float v = dot(ray.direction, q) * inverse;
    if (v < -kEpsilon || u + v > 1.0F + kEpsilon) return std::nullopt;
    const float distance = dot(edge2, q) * inverse;
    if (distance < 0.0F || !std::isfinite(distance)) return std::nullopt;
    return distance;
}

inline std::optional<float> ray_bounds_distance(const ViewRay& ray,
                                                const AxisAlignedBounds& bounds) noexcept {
    if (!bounds.valid()) return std::nullopt;
    float near_distance = 0.0F;
    float far_distance = std::numeric_limits<float>::max();
    const std::array<float, 3> origin{ray.origin.x, ray.origin.y, ray.origin.z};
    const std::array<float, 3> direction{ray.direction.x, ray.direction.y, ray.direction.z};
    const std::array<float, 3> minimum{bounds.minimum.x, bounds.minimum.y, bounds.minimum.z};
    const std::array<float, 3> maximum{bounds.maximum.x, bounds.maximum.y, bounds.maximum.z};
    for (std::size_t axis = 0; axis < 3U; ++axis) {
        if (std::abs(direction[axis]) <= 1.0e-8F) {
            if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis])
                return std::nullopt;
            continue;
        }
        float first = (minimum[axis] - origin[axis]) / direction[axis];
        float second = (maximum[axis] - origin[axis]) / direction[axis];
        if (first > second) std::swap(first, second);
        near_distance = std::max(near_distance, first);
        far_distance = std::min(far_distance, second);
        if (near_distance > far_distance) return std::nullopt;
    }
    return far_distance >= 0.0F ? std::optional<float>{near_distance} : std::nullopt;
}

struct TerrainPickHit {
    uint32_t cell_x{};
    uint32_t cell_y{};
    Vec3f position{};
    float distance{};
};

inline std::optional<TerrainPickHit> pick_terrain_cell(const thandor::FieldGrid& field,
                                                       const ViewRay& ray) noexcept {
    const auto hit = thandor::raycast_field_surface_compatibility(
        field, ray.origin, ray.direction, 100000.0F,
        thandor::FieldRaycastSurface::Topmost, 0x400U);
    if (!hit) return std::nullopt;
    return TerrainPickHit{hit->nearest_vertex_x, hit->nearest_vertex_y,
                          hit->position, hit->distance};
}

} // namespace editor
