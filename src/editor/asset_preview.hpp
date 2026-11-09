#pragma once

#include "math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <functional>
#include <span>
#include <vector>

namespace editor {

struct PreviewVec2 {
    float x{};
    float y{};
};

inline PreviewVec2 add(PreviewVec2 a, PreviewVec2 b) noexcept { return {a.x + b.x, a.y + b.y}; }
inline PreviewVec2 sub(PreviewVec2 a, PreviewVec2 b) noexcept { return {a.x - b.x, a.y - b.y}; }
inline PreviewVec2 mul(PreviewVec2 value, float scale) noexcept { return {value.x * scale, value.y * scale}; }
inline float dot(PreviewVec2 a, PreviewVec2 b) noexcept { return a.x * b.x + a.y * b.y; }
inline float length(PreviewVec2 value) noexcept { return std::sqrt(dot(value, value)); }
inline PreviewVec2 normalize(PreviewVec2 value) noexcept {
    const float magnitude = length(value);
    return magnitude > 1.0e-6F ? mul(value, 1.0F / magnitude) : PreviewVec2{};
}

struct AssetPreviewTriangle {
    std::array<Vec3f, 3> positions{};
    uint32_t packed_color_argb{0xFFFFFFFFU};
    std::array<PreviewVec2, 3> texture_uv{};
    uint32_t texture_subresource{std::numeric_limits<uint32_t>::max()};
    float brightness{1.0F};
    float alpha_scale{1.0F};
    bool textured{};
    bool additive{};
};

struct AssetPreviewMesh {
    std::vector<AssetPreviewTriangle> triangles;
    Vec3f minimum{};
    Vec3f maximum{};
    std::size_t source_triangle_count{};
    std::size_t textured_source_triangle_count{};
    std::size_t generated_texture_triangle_count{};
    std::size_t covered_pixel_count{};
    uint32_t texture_subdivision{};
    uint32_t raster_width{};
    uint32_t raster_height{};
    std::vector<uint32_t> raster_argb;
    bool bounds_valid{};
    bool marker_only{};
};

struct AssetPreviewDrawTriangle {
    std::array<PreviewVec2, 3> points{};
    float depth{};
    uint32_t packed_color_argb{0xFFFFFFFFU};
};

inline void include_preview_bounds(AssetPreviewMesh& mesh, Vec3f point) noexcept {
    if (!mesh.bounds_valid) {
        mesh.minimum = point;
        mesh.maximum = point;
        mesh.bounds_valid = true;
        return;
    }
    mesh.minimum.x = std::min(mesh.minimum.x, point.x);
    mesh.minimum.y = std::min(mesh.minimum.y, point.y);
    mesh.minimum.z = std::min(mesh.minimum.z, point.z);
    mesh.maximum.x = std::max(mesh.maximum.x, point.x);
    mesh.maximum.y = std::max(mesh.maximum.y, point.y);
    mesh.maximum.z = std::max(mesh.maximum.z, point.z);
}

using PreviewBarycentric = std::array<float, 3>;

struct PreviewSubdivisionTriangle {
    std::array<PreviewBarycentric, 3> corners{};
};

inline uint32_t asset_preview_texture_subdivision_count(std::size_t source_triangle_count,
                                                         bool element_preview) noexcept {
    if (source_triangle_count == 0U) return 1U;
    const std::size_t budget = element_preview ? 18000U : 12000U;
    const uint32_t maximum = element_preview ? 6U : 5U;
    const double ratio = static_cast<double>(budget) / static_cast<double>(source_triangle_count);
    const auto estimated = static_cast<uint32_t>(std::floor(std::sqrt(std::max(1.0, ratio))));
    return std::clamp(estimated, 1U, maximum);
}

inline PreviewBarycentric preview_barycentric_grid_point(uint32_t along_b, uint32_t along_c,
                                                          uint32_t divisions) noexcept {
    const float inverse = 1.0F / static_cast<float>(std::max(divisions, 1U));
    const float b = static_cast<float>(along_b) * inverse;
    const float c = static_cast<float>(along_c) * inverse;
    return {1.0F - b - c, b, c};
}

inline std::vector<PreviewSubdivisionTriangle> subdivide_asset_preview_triangle(uint32_t divisions) {
    divisions = std::max(divisions, 1U);
    std::vector<PreviewSubdivisionTriangle> result;
    result.reserve(static_cast<std::size_t>(divisions) * divisions);
    for (uint32_t b = 0U; b < divisions; ++b) {
        for (uint32_t c = 0U; c + b < divisions; ++c) {
            const auto p00 = preview_barycentric_grid_point(b, c, divisions);
            const auto p10 = preview_barycentric_grid_point(b + 1U, c, divisions);
            const auto p01 = preview_barycentric_grid_point(b, c + 1U, divisions);
            result.push_back({{{p00, p10, p01}}});
            if (b + c + 1U < divisions) {
                const auto p11 = preview_barycentric_grid_point(b + 1U, c + 1U, divisions);
                result.push_back({{{p10, p11, p01}}});
            }
        }
    }
    return result;
}

inline Vec3f interpolate_preview_position(const std::array<Vec3f, 3>& positions,
                                          PreviewBarycentric weights) noexcept {
    return {positions[0].x * weights[0] + positions[1].x * weights[1] + positions[2].x * weights[2],
            positions[0].y * weights[0] + positions[1].y * weights[1] + positions[2].y * weights[2],
            positions[0].z * weights[0] + positions[1].z * weights[1] + positions[2].z * weights[2]};
}

inline PreviewVec2 interpolate_preview_uv(const std::array<PreviewVec2, 3>& coordinates,
                                          PreviewBarycentric weights) noexcept {
    return {coordinates[0].x * weights[0] + coordinates[1].x * weights[1] + coordinates[2].x * weights[2],
            coordinates[0].y * weights[0] + coordinates[1].y * weights[1] + coordinates[2].y * weights[2]};
}

inline uint32_t modulate_preview_color(uint32_t argb, float brightness, float alpha_scale = 1.0F) noexcept {
    const auto channel = [&](unsigned shift) {
        const float source = static_cast<float>((argb >> shift) & 0xFFU);
        return static_cast<uint32_t>(std::clamp(std::lround(source * brightness), 0L, 255L));
    };
    const uint32_t alpha = static_cast<uint32_t>(std::clamp(
        std::lround(static_cast<float>((argb >> 24U) & 0xFFU) * alpha_scale), 0L, 255L));
    return alpha << 24U | channel(16U) << 16U | channel(8U) << 8U | channel(0U);
}


inline uint32_t blend_preview_argb(uint32_t destination, uint32_t source, bool additive) noexcept {
    const uint32_t sa = (source >> 24U) & 0xFFU;
    const uint32_t sr = (source >> 16U) & 0xFFU;
    const uint32_t sg = (source >> 8U) & 0xFFU;
    const uint32_t sb = source & 0xFFU;
    const uint32_t da = (destination >> 24U) & 0xFFU;
    const uint32_t dr = (destination >> 16U) & 0xFFU;
    const uint32_t dg = (destination >> 8U) & 0xFFU;
    const uint32_t db = destination & 0xFFU;
    if (additive) {
        const auto add_channel = [&](uint32_t d, uint32_t value) {
            return std::min(255U, d + (value * sa + 127U) / 255U);
        };
        return std::max(da, sa) << 24U | add_channel(dr, sr) << 16U |
               add_channel(dg, sg) << 8U | add_channel(db, sb);
    }
    const uint32_t inverse = 255U - sa;
    const auto over = [&](uint32_t d, uint32_t value) {
        return (value * sa + d * inverse + 127U) / 255U;
    };
    const uint32_t out_a = sa + (da * inverse + 127U) / 255U;
    return out_a << 24U | over(dr, sr) << 16U | over(dg, sg) << 8U | over(db, sb);
}

using AssetPreviewTextureSampler = std::function<uint32_t(uint32_t, float, float)>;

inline void rasterize_asset_preview(AssetPreviewMesh& mesh, uint32_t width, uint32_t height,
                                    const AssetPreviewTextureSampler& sample_texture,
                                    float yaw = -0.72F, float pitch = 0.58F,
                                    float padding = 5.0F) {
    mesh.raster_width = width;
    mesh.raster_height = height;
    mesh.raster_argb.assign(static_cast<std::size_t>(width) * height, 0U);
    mesh.covered_pixel_count = 0U;
    if (!mesh.bounds_valid || mesh.triangles.empty() || width < 2U || height < 2U) return;

    const Vec3f center{(mesh.minimum.x + mesh.maximum.x) * 0.5F,
                       (mesh.minimum.y + mesh.maximum.y) * 0.5F,
                       (mesh.minimum.z + mesh.maximum.z) * 0.5F};
    const float cosine_pitch = std::cos(pitch);
    const Vec3f view_direction = normalize(Vec3f{
        cosine_pitch * std::cos(yaw), cosine_pitch * std::sin(yaw), std::sin(pitch)});
    const Vec3f right = normalize(cross(view_direction, Vec3f{0.0F, 0.0F, 1.0F}));
    const Vec3f up = normalize(cross(right, view_direction));

    float minimum_x = std::numeric_limits<float>::max();
    float maximum_x = std::numeric_limits<float>::lowest();
    float minimum_y = std::numeric_limits<float>::max();
    float maximum_y = std::numeric_limits<float>::lowest();
    for (const auto& triangle : mesh.triangles) {
        for (const Vec3f point : triangle.positions) {
            const Vec3f local = sub(point, center);
            const float x = dot(local, right);
            const float y = dot(local, up);
            minimum_x = std::min(minimum_x, x); maximum_x = std::max(maximum_x, x);
            minimum_y = std::min(minimum_y, y); maximum_y = std::max(maximum_y, y);
        }
    }
    const float span_x = std::max(maximum_x - minimum_x, 1.0e-5F);
    const float span_y = std::max(maximum_y - minimum_y, 1.0e-5F);
    const float usable_width = std::max(1.0F, static_cast<float>(width) - 2.0F * padding);
    const float usable_height = std::max(1.0F, static_cast<float>(height) - 2.0F * padding);
    const float scale = std::min(usable_width / span_x, usable_height / span_y);
    const float source_center_x = (minimum_x + maximum_x) * 0.5F;
    const float source_center_y = (minimum_y + maximum_y) * 0.5F;
    std::vector<float> depth(static_cast<std::size_t>(width) * height,
                             std::numeric_limits<float>::lowest());

    for (const auto& triangle : mesh.triangles) {
        std::array<float, 3> sx{}, sy{}, sz{};
        for (std::size_t corner = 0; corner < 3U; ++corner) {
            const Vec3f local = sub(triangle.positions[corner], center);
            sx[corner] = static_cast<float>(width) * 0.5F +
                         (dot(local, right) - source_center_x) * scale;
            sy[corner] = static_cast<float>(height) * 0.5F -
                         (dot(local, up) - source_center_y) * scale;
            sz[corner] = dot(local, view_direction);
        }
        const float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) -
                           (sy[1] - sy[0]) * (sx[2] - sx[0]);
        if (std::abs(area) <= 1.0e-7F) continue;
        const int min_x = std::max(0, static_cast<int>(std::floor(std::min({sx[0], sx[1], sx[2]}))));
        const int max_x = std::min(static_cast<int>(width) - 1,
            static_cast<int>(std::ceil(std::max({sx[0], sx[1], sx[2]}))));
        const int min_y = std::max(0, static_cast<int>(std::floor(std::min({sy[0], sy[1], sy[2]}))));
        const int max_y = std::min(static_cast<int>(height) - 1,
            static_cast<int>(std::ceil(std::max({sy[0], sy[1], sy[2]}))));
        for (int y = min_y; y <= max_y; ++y) {
            for (int x = min_x; x <= max_x; ++x) {
                const float px = static_cast<float>(x) + 0.5F;
                const float py = static_cast<float>(y) + 0.5F;
                const float w0 = ((sx[1] - px) * (sy[2] - py) -
                                  (sy[1] - py) * (sx[2] - px)) / area;
                const float w1 = ((sx[2] - px) * (sy[0] - py) -
                                  (sy[2] - py) * (sx[0] - px)) / area;
                const float w2 = 1.0F - w0 - w1;
                if (w0 < -1.0e-5F || w1 < -1.0e-5F || w2 < -1.0e-5F) continue;
                const std::size_t pixel = static_cast<std::size_t>(y) * width + x;
                const float current_depth = w0 * sz[0] + w1 * sz[1] + w2 * sz[2];
                if (!triangle.additive && current_depth < depth[pixel]) continue;
                uint32_t color = triangle.packed_color_argb;
                if (triangle.textured) {
                    const float u = w0 * triangle.texture_uv[0].x +
                                    w1 * triangle.texture_uv[1].x + w2 * triangle.texture_uv[2].x;
                    const float v = w0 * triangle.texture_uv[0].y +
                                    w1 * triangle.texture_uv[1].y + w2 * triangle.texture_uv[2].y;
                    color = sample_texture(triangle.texture_subresource, u, v);
                }
                color = modulate_preview_color(color, triangle.brightness, triangle.alpha_scale);
                if (((color >> 24U) & 0xFFU) < 4U) continue;
                if (!triangle.additive) depth[pixel] = current_depth;
                mesh.raster_argb[pixel] = blend_preview_argb(mesh.raster_argb[pixel], color,
                                                              triangle.additive);
            }
        }
    }
    mesh.covered_pixel_count = static_cast<std::size_t>(std::count_if(
        mesh.raster_argb.begin(), mesh.raster_argb.end(),
        [](uint32_t color) { return ((color >> 24U) & 0xFFU) != 0U; }));
}

inline std::vector<AssetPreviewDrawTriangle> project_asset_preview(
    const AssetPreviewMesh& mesh, float width, float height,
    float yaw = -0.72F, float pitch = 0.58F, float padding = 8.0F) {
    std::vector<AssetPreviewDrawTriangle> result;
    if (!mesh.bounds_valid || mesh.triangles.empty() || width <= 1.0F || height <= 1.0F) return result;

    const Vec3f center{
        (mesh.minimum.x + mesh.maximum.x) * 0.5F,
        (mesh.minimum.y + mesh.maximum.y) * 0.5F,
        (mesh.minimum.z + mesh.maximum.z) * 0.5F,
    };
    const float cosine_pitch = std::cos(pitch);
    const Vec3f view_direction = normalize(Vec3f{
        cosine_pitch * std::cos(yaw), cosine_pitch * std::sin(yaw), std::sin(pitch)});
    const Vec3f right = normalize(cross(view_direction, Vec3f{0.0F, 0.0F, 1.0F}));
    const Vec3f up = normalize(cross(right, view_direction));

    float minimum_x = std::numeric_limits<float>::max();
    float maximum_x = std::numeric_limits<float>::lowest();
    float minimum_y = std::numeric_limits<float>::max();
    float maximum_y = std::numeric_limits<float>::lowest();
    for (const auto& triangle : mesh.triangles) {
        for (const auto point : triangle.positions) {
            const Vec3f local = sub(point, center);
            const float projected_x = dot(local, right);
            const float projected_y = dot(local, up);
            minimum_x = std::min(minimum_x, projected_x);
            maximum_x = std::max(maximum_x, projected_x);
            minimum_y = std::min(minimum_y, projected_y);
            maximum_y = std::max(maximum_y, projected_y);
        }
    }
    const float span_x = std::max(maximum_x - minimum_x, 1.0e-4F);
    const float span_y = std::max(maximum_y - minimum_y, 1.0e-4F);
    const float usable_width = std::max(1.0F, width - padding * 2.0F);
    const float usable_height = std::max(1.0F, height - padding * 2.0F);
    const float scale = std::min(usable_width / span_x, usable_height / span_y);
    const float source_center_x = (minimum_x + maximum_x) * 0.5F;
    const float source_center_y = (minimum_y + maximum_y) * 0.5F;

    result.reserve(mesh.triangles.size());
    for (const auto& triangle : mesh.triangles) {
        AssetPreviewDrawTriangle output{};
        output.packed_color_argb = triangle.packed_color_argb;
        for (std::size_t corner = 0; corner < 3U; ++corner) {
            const Vec3f local = sub(triangle.positions[corner], center);
            const float projected_x = dot(local, right);
            const float projected_y = dot(local, up);
            output.points[corner] = {
                width * 0.5F + (projected_x - source_center_x) * scale,
                height * 0.5F - (projected_y - source_center_y) * scale,
            };
            output.depth += dot(local, view_direction) / 3.0F;
        }
        result.push_back(output);
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& first, const auto& second) {
        return first.depth < second.depth;
    });
    return result;
}

inline int asset_card_column_count(float available_width, float card_width, float spacing) noexcept {
    if (available_width <= 0.0F || card_width <= 0.0F) return 1;
    return std::max(1, static_cast<int>(std::floor((available_width + spacing) / (card_width + spacing))));
}

inline float point_segment_distance(PreviewVec2 point, PreviewVec2 start, PreviewVec2 end) noexcept {
    const PreviewVec2 delta = sub(end, start);
    const float length_squared = dot(delta, delta);
    if (length_squared <= 1.0e-8F) return length(sub(point, start));
    const float factor = std::clamp(dot(sub(point, start), delta) / length_squared, 0.0F, 1.0F);
    return length(sub(point, add(start, mul(delta, factor))));
}

inline float axis_drag_world_delta(PreviewVec2 drag_start, PreviewVec2 drag_current,
                                   PreviewVec2 axis_start, PreviewVec2 axis_end,
                                   float axis_world_length) noexcept {
    const PreviewVec2 screen_axis = sub(axis_end, axis_start);
    const float screen_length = length(screen_axis);
    if (screen_length <= 1.0e-5F || std::abs(axis_world_length) <= 1.0e-6F) return 0.0F;
    const PreviewVec2 screen_direction = mul(screen_axis, 1.0F / screen_length);
    return dot(sub(drag_current, drag_start), screen_direction) * axis_world_length / screen_length;
}

inline uint32_t add_turn16_delta(uint32_t start_turn16, float radians,
                                 bool snap_to_fifteen_degrees) noexcept {
    if (snap_to_fifteen_degrees) {
        constexpr float snap = std::numbers::pi_v<float> / 12.0F;
        radians = std::round(radians / snap) * snap;
    }
    const auto delta = static_cast<int64_t>(std::llround(
        static_cast<double>(radians) * (65536.0 / (2.0 * std::numbers::pi))));
    return static_cast<uint32_t>((static_cast<int64_t>(start_turn16 & 0xFFFFU) + delta) & 0xFFFF);
}

struct TransformGizmoProjection {
    bool visible{};
    PreviewVec2 origin{};
    PreviewVec2 x_axis{};
    PreviewVec2 y_axis{};
    PreviewVec2 z_axis{};
    std::array<PreviewVec2, 65> rotation_ring{};
    float world_scale{};
};

inline TransformGizmoProjection project_transform_gizmo(Vec3f origin, const Mat4& view_projection,
                                                         float viewport_width, float viewport_height,
                                                         float world_scale) {
    TransformGizmoProjection projection{};
    projection.world_scale = world_scale;
    float depth = 0.0F;
    if (!project_to_screen(origin, view_projection, viewport_width, viewport_height,
                           projection.origin.x, projection.origin.y, depth)) {
        return projection;
    }
    float axis_depth = 0.0F;
    const bool x_visible = project_to_screen(add(origin, Vec3f{world_scale, 0.0F, 0.0F}),
        view_projection, viewport_width, viewport_height,
        projection.x_axis.x, projection.x_axis.y, axis_depth);
    const bool y_visible = project_to_screen(add(origin, Vec3f{0.0F, world_scale, 0.0F}),
        view_projection, viewport_width, viewport_height,
        projection.y_axis.x, projection.y_axis.y, axis_depth);
    const bool z_visible = project_to_screen(add(origin, Vec3f{0.0F, 0.0F, world_scale}),
        view_projection, viewport_width, viewport_height,
        projection.z_axis.x, projection.z_axis.y, axis_depth);
    if (!x_visible || !y_visible || !z_visible) return projection;

    for (std::size_t index = 0; index < projection.rotation_ring.size(); ++index) {
        const float angle = static_cast<float>(index) *
            (2.0F * std::numbers::pi_v<float> /
             static_cast<float>(projection.rotation_ring.size() - 1U));
        const Vec3f point = add(origin, Vec3f{std::cos(angle) * world_scale,
                                             std::sin(angle) * world_scale, 0.0F});
        if (!project_to_screen(point, view_projection, viewport_width, viewport_height,
                               projection.rotation_ring[index].x,
                               projection.rotation_ring[index].y, axis_depth)) {
            return TransformGizmoProjection{};
        }
    }
    projection.visible = true;
    return projection;
}

inline float rotation_ring_distance(PreviewVec2 point,
                                    std::span<const PreviewVec2> ring) noexcept {
    float best = std::numeric_limits<float>::max();
    for (std::size_t index = 1; index < ring.size(); ++index) {
        best = std::min(best, point_segment_distance(point, ring[index - 1U], ring[index]));
    }
    return best;
}

} // namespace editor
