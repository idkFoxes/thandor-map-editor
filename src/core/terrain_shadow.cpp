#include "thandor/terrain_shadow.hpp"
#include "thandor/field_raycast.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace thandor {
namespace {

Vec3f add(Vec3f a, Vec3f b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3f sub(Vec3f a, Vec3f b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3f mul(Vec3f value, float scale) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}
float dot(Vec3f a, Vec3f b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3f cross(Vec3f a, Vec3f b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}
std::optional<Vec3f> normalized(Vec3f value) noexcept {
    const float length = std::sqrt(dot(value, value));
    if (!std::isfinite(length) || length <= 1.0e-6F) return std::nullopt;
    return mul(value, 1.0F / length);
}

float edge(float ax, float ay, float bx, float by, float px, float py) noexcept {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

void rasterize_triangle(std::vector<uint8_t>& mask, uint32_t dimension,
                        const std::array<std::array<float, 2>, 3>& points) {
    float minimum_x = points[0][0];
    float maximum_x = points[0][0];
    float minimum_y = points[0][1];
    float maximum_y = points[0][1];
    for (const auto& point : points) {
        minimum_x = std::min(minimum_x, point[0]);
        maximum_x = std::max(maximum_x, point[0]);
        minimum_y = std::min(minimum_y, point[1]);
        maximum_y = std::max(maximum_y, point[1]);
    }
    const int32_t x0 = std::max<int32_t>(0, static_cast<int32_t>(std::floor(minimum_x)));
    const int32_t x1 = std::min<int32_t>(static_cast<int32_t>(dimension) - 1,
                                        static_cast<int32_t>(std::ceil(maximum_x)));
    const int32_t y0 = std::max<int32_t>(0, static_cast<int32_t>(std::floor(minimum_y)));
    const int32_t y1 = std::min<int32_t>(static_cast<int32_t>(dimension) - 1,
                                        static_cast<int32_t>(std::ceil(maximum_y)));
    const float area = edge(points[0][0], points[0][1], points[1][0], points[1][1],
                            points[2][0], points[2][1]);
    if (std::abs(area) <= 1.0e-7F) return;
    for (int32_t y = y0; y <= y1; ++y) {
        for (int32_t x = x0; x <= x1; ++x) {
            const float px = static_cast<float>(x) + 0.5F;
            const float py = static_cast<float>(y) + 0.5F;
            const float e0 = edge(points[0][0], points[0][1], points[1][0], points[1][1], px, py);
            const float e1 = edge(points[1][0], points[1][1], points[2][0], points[2][1], px, py);
            const float e2 = edge(points[2][0], points[2][1], points[0][0], points[0][1], px, py);
            constexpr float epsilon = 1.0e-4F;
            if ((e0 >= -epsilon && e1 >= -epsilon && e2 >= -epsilon) ||
                (e0 <= epsilon && e1 <= epsilon && e2 <= epsilon)) {
                mask[static_cast<std::size_t>(y) * dimension + static_cast<uint32_t>(x)] = 0xFFU;
            }
        }
    }
}

std::vector<uint8_t> blur_game_soft_group(std::span<const uint8_t> source,
                                          uint32_t dimension) {
    // The MMX path first converts each byte to (source >> 5) & 7. The recovered
    // center-times-four and saturating-neighbour additions form this 32-weight kernel.
    static constexpr std::array<std::array<uint8_t, 5>, 5> kernel{{
        {{0, 1, 1, 1, 0}},
        {{1, 1, 3, 1, 1}},
        {{1, 3, 4, 3, 1}},
        {{1, 1, 3, 1, 1}},
        {{0, 1, 1, 1, 0}},
    }};
    std::vector<uint8_t> result(static_cast<std::size_t>(dimension) * dimension, 0U);
    for (uint32_t y = 0; y < dimension; ++y) {
        for (uint32_t x = 0; x < dimension; ++x) {
            uint32_t sum = 0U;
            for (int32_t ky = -2; ky <= 2; ++ky) {
                const int32_t sy = static_cast<int32_t>(y) + ky;
                if (sy < 0 || sy >= static_cast<int32_t>(dimension)) continue;
                for (int32_t kx = -2; kx <= 2; ++kx) {
                    const int32_t sx = static_cast<int32_t>(x) + kx;
                    if (sx < 0 || sx >= static_cast<int32_t>(dimension)) continue;
                    const uint8_t weight = kernel[static_cast<std::size_t>(ky + 2)]
                                                  [static_cast<std::size_t>(kx + 2)];
                    const uint8_t quantized = static_cast<uint8_t>(
                        (source[static_cast<std::size_t>(sy) * dimension +
                                static_cast<uint32_t>(sx)] >> 5U) & 7U);
                    sum += static_cast<uint32_t>(weight) * quantized;
                }
            }
            result[static_cast<std::size_t>(y) * dimension + x] =
                static_cast<uint8_t>(std::min(sum, 255U));
        }
    }
    return result;
}

std::optional<Vec3f> raycast_receiver(const FieldGrid& field, Vec3f start, Vec3f direction,
                                      float maximum_distance, float surface_bias) noexcept {
    const auto hit = raycast_field_surface_compatibility(field, start, direction,
        maximum_distance, FieldRaycastSurface::Topmost, 0x400U);
    if (!hit) return std::nullopt;
    Vec3f result = hit->position;
    result.z += std::max(0.0F, surface_bias);
    return result;
}

} // namespace

std::optional<GeneratedObjectShadow> build_game_generated_object_shadow(
    const FieldGrid& field, std::span<const GeneratedShadowTriangle> triangles,
    Vec3f shadow_projection_direction, GeneratedShadowSettings settings) {
    if (triangles.empty() || settings.texture_dimension < 8U ||
        settings.texture_dimension > 512U || settings.receiver_grid_dimension < 2U ||
        settings.receiver_grid_dimension > 32U) return std::nullopt;
    const auto projection_value = normalized(shadow_projection_direction);
    if (!projection_value || projection_value->z >= -0.005F) return std::nullopt;
    const Vec3f projection = *projection_value;
    auto right_value = normalized(cross(projection, Vec3f{0.0F, 0.0F, 1.0F}));
    if (!right_value) right_value = normalized(Vec3f{1.0F, 0.0F, 0.0F});
    if (!right_value) return std::nullopt;
    const Vec3f right = *right_value;
    const auto up_value = normalized(cross(right, projection));
    if (!up_value) return std::nullopt;
    const Vec3f up = *up_value;

    Vec3f minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                  std::numeric_limits<float>::max()};
    Vec3f maximum{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                  -std::numeric_limits<float>::max()};
    for (const auto& triangle : triangles) {
        for (const Vec3f vertex : triangle.world) {
            if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z))
                return std::nullopt;
            minimum.x = std::min(minimum.x, vertex.x);
            minimum.y = std::min(minimum.y, vertex.y);
            minimum.z = std::min(minimum.z, vertex.z);
            maximum.x = std::max(maximum.x, vertex.x);
            maximum.y = std::max(maximum.y, vertex.y);
            maximum.z = std::max(maximum.z, vertex.z);
        }
    }
    const Vec3f center = mul(add(minimum, maximum), 0.5F);
    float radius = 0.0F;
    float projection_min_x = std::numeric_limits<float>::max();
    float projection_max_x = -std::numeric_limits<float>::max();
    float projection_min_y = std::numeric_limits<float>::max();
    float projection_max_y = -std::numeric_limits<float>::max();
    for (const auto& triangle : triangles) {
        for (const Vec3f vertex : triangle.world) {
            const Vec3f relative = sub(vertex, center);
            radius = std::max(radius, std::sqrt(dot(relative, relative)));
            const float x = dot(relative, right);
            const float y = dot(relative, up);
            projection_min_x = std::min(projection_min_x, x);
            projection_max_x = std::max(projection_max_x, x);
            projection_min_y = std::min(projection_min_y, y);
            projection_max_y = std::max(projection_max_y, y);
        }
    }
    if (!std::isfinite(radius) || radius <= 1.0e-4F) return std::nullopt;
    float width = projection_max_x - projection_min_x;
    float height = projection_max_y - projection_min_y;
    if (width <= 1.0e-5F || height <= 1.0e-5F) return std::nullopt;
    const float padding = std::max(radius * 0.025F,
        std::max(width, height) * (2.0F / static_cast<float>(settings.texture_dimension)));
    projection_min_x -= padding;
    projection_max_x += padding;
    projection_min_y -= padding;
    projection_max_y += padding;
    width = projection_max_x - projection_min_x;
    height = projection_max_y - projection_min_y;

    std::vector<uint8_t> soft(static_cast<std::size_t>(settings.texture_dimension) *
                              settings.texture_dimension, 0U);
    std::vector<uint8_t> hard(soft.size(), 0U);
    std::size_t soft_count = 0U;
    std::size_t hard_count = 0U;
    for (const auto& triangle : triangles) {
        std::array<std::array<float, 2>, 3> points{};
        for (std::size_t corner = 0; corner < 3U; ++corner) {
            const Vec3f relative = sub(triangle.world[corner], center);
            const float x = dot(relative, right);
            const float y = dot(relative, up);
            points[corner][0] = (x - projection_min_x) / width *
                                static_cast<float>(settings.texture_dimension - 1U);
            points[corner][1] = (projection_max_y - y) / height *
                                static_cast<float>(settings.texture_dimension - 1U);
        }
        if (triangle.blurred_group) {
            rasterize_triangle(soft, settings.texture_dimension, points);
            ++soft_count;
        } else {
            rasterize_triangle(hard, settings.texture_dimension, points);
            ++hard_count;
        }
    }
    auto alpha = blur_game_soft_group(soft, settings.texture_dimension);
    for (std::size_t index = 0; index < alpha.size(); ++index)
        alpha[index] = std::max(alpha[index], hard[index]);
    if (std::none_of(alpha.begin(), alpha.end(), [](uint8_t value) { return value != 0U; }))
        return std::nullopt;

    GeneratedObjectShadow result;
    result.mask.width = settings.texture_dimension;
    result.mask.height = settings.texture_dimension;
    result.mask.pixels.resize(alpha.size() * 4U);
    for (std::size_t index = 0; index < alpha.size(); ++index) {
        result.mask.pixels[index * 4U + 0U] = 255U;
        result.mask.pixels[index * 4U + 1U] = 255U;
        result.mask.pixels[index * 4U + 2U] = 255U;
        result.mask.pixels[index * 4U + 3U] = alpha[index];
    }
    result.projection_direction = projection;
    result.bounding_radius = radius;
    result.blurred_triangle_count = soft_count;
    result.hard_triangle_count = hard_count;

    const uint32_t grid = settings.receiver_grid_dimension;
    result.receiver_vertices.reserve(static_cast<std::size_t>(grid) * grid);
    std::vector<uint8_t> valid(static_cast<std::size_t>(grid) * grid, 0U);
    const float start_distance = std::max(radius * 1.5F, 0.5F);
    const float ray_distance = std::max(radius * 4.0F, 2.0F);
    for (uint32_t gy = 0; gy < grid; ++gy) {
        const float v = static_cast<float>(gy) / static_cast<float>(grid - 1U);
        const float plane_y = projection_max_y - v * height;
        for (uint32_t gx = 0; gx < grid; ++gx) {
            const float u = static_cast<float>(gx) / static_cast<float>(grid - 1U);
            const float plane_x = projection_min_x + u * width;
            const Vec3f plane_point = add(center, add(mul(right, plane_x), mul(up, plane_y)));
            const Vec3f start = sub(plane_point, mul(projection, start_distance));
            const auto hit = raycast_receiver(field, start, projection, ray_distance,
                                               settings.surface_bias);
            const std::size_t index = static_cast<std::size_t>(gy) * grid + gx;
            result.receiver_vertices.push_back({hit.value_or(plane_point), u, v});
            valid[index] = hit.has_value() ? 1U : 0U;
        }
    }
    for (uint32_t gy = 0; gy + 1U < grid; ++gy) {
        for (uint32_t gx = 0; gx + 1U < grid; ++gx) {
            const uint32_t a = gy * grid + gx;
            const uint32_t b = a + 1U;
            const uint32_t c = a + grid + 1U;
            const uint32_t d = a + grid;
            if (valid[a] && valid[b] && valid[c])
                result.receiver_indices.insert(result.receiver_indices.end(), {a, b, c});
            if (valid[a] && valid[c] && valid[d])
                result.receiver_indices.insert(result.receiver_indices.end(), {a, c, d});
        }
    }
    if (result.receiver_indices.empty()) return std::nullopt;
    return result;
}

std::optional<Vec3f> project_directional_shadow_to_field(
    const FieldGrid& field, Vec3f world_point, Vec3f direction_to_light,
    float surface_bias, unsigned refinement_iterations, float maximum_projection_distance) {
    if (!std::isfinite(world_point.x) || !std::isfinite(world_point.y) ||
        !std::isfinite(world_point.z) || !std::isfinite(direction_to_light.x) ||
        !std::isfinite(direction_to_light.y) || !std::isfinite(direction_to_light.z) ||
        !std::isfinite(surface_bias) || !std::isfinite(maximum_projection_distance) ||
        direction_to_light.z <= 0.02F || maximum_projection_distance <= 0.0F) {
        return std::nullopt;
    }

    const float length = std::sqrt(direction_to_light.x * direction_to_light.x +
                                   direction_to_light.y * direction_to_light.y +
                                   direction_to_light.z * direction_to_light.z);
    if (length <= 1.0e-6F) return std::nullopt;
    direction_to_light.x /= length;
    direction_to_light.y /= length;
    direction_to_light.z /= length;

    float projected_x = world_point.x;
    float projected_y = world_point.y;
    float ground = 0.0F;
    const unsigned iterations = std::clamp(refinement_iterations, 1U, 8U);
    for (unsigned iteration = 0; iteration < iterations; ++iteration) {
        const auto sample = field.sample_terrain(projected_x, projected_y);
        if (!sample.inside) return std::nullopt;
        ground = sample.height;
        const float vertical_distance = std::max(0.0F, world_point.z - ground);
        const float ray_distance = std::min(maximum_projection_distance,
                                            vertical_distance / direction_to_light.z);
        projected_x = world_point.x - direction_to_light.x * ray_distance;
        projected_y = world_point.y - direction_to_light.y * ray_distance;
    }

    const auto final_sample = field.sample_terrain(projected_x, projected_y);
    if (!final_sample.inside) return std::nullopt;
    return Vec3f{projected_x, projected_y, final_sample.height + std::max(0.0F, surface_bias)};
}

bool triangle_lies_on_field_receiver(
    const FieldGrid& field, const std::array<Vec3f, 3>& world_triangle,
    float maximum_height_delta) noexcept {
    if (!std::isfinite(maximum_height_delta) || maximum_height_delta < 0.0F) return false;
    for (const Vec3f vertex : world_triangle) {
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z))
            return false;
        const auto sample = field.sample_terrain(vertex.x, vertex.y);
        if (!sample.inside || std::abs(vertex.z - sample.height) > maximum_height_delta)
            return false;
    }
    return true;
}

} // namespace thandor
