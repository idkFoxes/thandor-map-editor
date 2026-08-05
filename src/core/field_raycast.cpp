#include "thandor/field_raycast.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace thandor {
namespace {

constexpr float kEpsilon = 1.0e-6F;

Vec3f add(Vec3f a, Vec3f b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3f sub(Vec3f a, Vec3f b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3f mul(Vec3f a, float scale) noexcept { return {a.x * scale, a.y * scale, a.z * scale}; }
float dot(Vec3f a, Vec3f b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3f cross(Vec3f a, Vec3f b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

float quantize_q12(float value) noexcept {
    if (!std::isfinite(value)) return value;
    const double scaled = std::round(static_cast<double>(value) * 4096.0);
    const double bounded = std::clamp(scaled,
        static_cast<double>(std::numeric_limits<int32_t>::min()),
        static_cast<double>(std::numeric_limits<int32_t>::max()));
    return static_cast<float>(bounded / 4096.0);
}

float quantize_q28(float value) noexcept {
    if (!std::isfinite(value)) return value;
    constexpr double scale = 268435456.0;
    const double scaled = std::round(static_cast<double>(value) * scale);
    const double bounded = std::clamp(scaled,
        static_cast<double>(std::numeric_limits<int32_t>::min()),
        static_cast<double>(std::numeric_limits<int32_t>::max()));
    return static_cast<float>(bounded / scale);
}

std::optional<float> ray_triangle_distance(Vec3f origin, Vec3f direction,
                                           Vec3f a, Vec3f b, Vec3f c) noexcept {
    const Vec3f edge1 = sub(b, a);
    const Vec3f edge2 = sub(c, a);
    const Vec3f p = cross(direction, edge2);
    const float determinant = dot(edge1, p);
    if (std::abs(determinant) <= 1.0e-8F) return std::nullopt;
    const float inverse = 1.0F / determinant;
    const Vec3f offset = sub(origin, a);
    const float u = dot(offset, p) * inverse;
    if (u < -kEpsilon || u > 1.0F + kEpsilon) return std::nullopt;
    const Vec3f q = cross(offset, edge1);
    const float v = dot(direction, q) * inverse;
    if (v < -kEpsilon || u + v > 1.0F + kEpsilon) return std::nullopt;
    const float distance = dot(edge2, q) * inverse;
    if (distance < -kEpsilon || !std::isfinite(distance)) return std::nullopt;
    return std::max(0.0F, distance);
}

struct GridBasis {
    Vec3f origin{};
    float inverse00{};
    float inverse01{};
    float inverse10{};
    float inverse11{};
    bool valid{};
};

GridBasis grid_basis(const FieldGrid& field) noexcept {
    GridBasis result;
    if (field.width() < 2U || field.height() < 2U) return result;
    const auto point = [](const FieldCell& cell) {
        return Vec3f{q12_to_float(cell.world_x_q12), q12_to_float(cell.world_y_q12), 0.0F};
    };
    result.origin = point(field.at(0U, 0U));
    const Vec3f axis_x = sub(point(field.at(1U, 0U)), result.origin);
    const Vec3f axis_y = sub(point(field.at(0U, 1U)), result.origin);
    const float determinant = axis_x.x * axis_y.y - axis_x.y * axis_y.x;
    if (std::abs(determinant) <= 1.0e-10F) return result;
    const float inverse = 1.0F / determinant;
    result.inverse00 = axis_y.y * inverse;
    result.inverse01 = -axis_y.x * inverse;
    result.inverse10 = -axis_x.y * inverse;
    result.inverse11 = axis_x.x * inverse;
    result.valid = true;
    return result;
}

std::array<float, 2> to_grid_point(const GridBasis& basis, Vec3f point) noexcept {
    const float x = point.x - basis.origin.x;
    const float y = point.y - basis.origin.y;
    return {basis.inverse00 * x + basis.inverse01 * y,
            basis.inverse10 * x + basis.inverse11 * y};
}

std::array<float, 2> to_grid_direction(const GridBasis& basis, Vec3f direction) noexcept {
    return {basis.inverse00 * direction.x + basis.inverse01 * direction.y,
            basis.inverse10 * direction.x + basis.inverse11 * direction.y};
}

bool update_slab(float origin, float direction, float minimum, float maximum,
                 float& enter, float& exit) noexcept {
    if (std::abs(direction) <= 1.0e-10F)
        return origin >= minimum - kEpsilon && origin <= maximum + kEpsilon;
    float first = (minimum - origin) / direction;
    float second = (maximum - origin) / direction;
    if (first > second) std::swap(first, second);
    enter = std::max(enter, first);
    exit = std::min(exit, second);
    return enter <= exit + kEpsilon;
}

float vertex_height(const FieldCell& cell, bool secondary) noexcept {
    if (!secondary) return cell.terrain_height();
    // The binary's secondary-surface ray adds the serialized signed delta directly, including
    // the dry sentinel.  Callers decide whether that result is relevant.
    return q12_to_float(cell.terrain_height_q12 + cell.water_delta_q12);
}

struct CellHit {
    float distance{std::numeric_limits<float>::max()};
    Vec3f position{};
    bool secondary{};
};

std::optional<CellHit> test_cell(const FieldGrid& field, uint32_t x, uint32_t y,
                                 Vec3f origin, Vec3f direction,
                                 FieldRaycastSurface surface) noexcept {
    const auto make_point = [&](uint32_t px, uint32_t py, bool secondary) {
        const auto& cell = field.at(px, py);
        return Vec3f{q12_to_float(cell.world_x_q12), q12_to_float(cell.world_y_q12),
                     vertex_height(cell, secondary)};
    };
    const auto test_surface = [&](bool secondary) -> std::optional<CellHit> {
        const Vec3f p00 = make_point(x, y, secondary);
        const Vec3f p01 = make_point(x, y + 1U, secondary);
        const Vec3f p10 = make_point(x + 1U, y, secondary);
        const Vec3f p11 = make_point(x + 1U, y + 1U, secondary);
        const std::array<std::array<Vec3f, 3>, 2> triangles{{
            {{p00, p01, p10}}, {{p01, p11, p10}}}};
        std::optional<CellHit> nearest;
        for (const auto& triangle : triangles) {
            const auto distance = ray_triangle_distance(origin, direction,
                triangle[0], triangle[1], triangle[2]);
            if (!distance || (nearest && *distance >= nearest->distance)) continue;
            nearest = CellHit{*distance, add(origin, mul(direction, *distance)), secondary};
        }
        return nearest;
    };

    if (surface == FieldRaycastSurface::Terrain) return test_surface(false);
    if (surface == FieldRaycastSurface::Secondary) return test_surface(true);
    const auto terrain = test_surface(false);
    const auto secondary = test_surface(true);
    if (!terrain) return secondary;
    if (!secondary) return terrain;
    return secondary->distance < terrain->distance ? secondary : terrain;
}

} // namespace

std::optional<FieldRaycastHit> raycast_field_surface_compatibility(
    const FieldGrid& field, Vec3f origin, Vec3f direction, float maximum_distance,
    FieldRaycastSurface surface, uint32_t maximum_cell_steps) noexcept {
    if (field.width() < 2U || field.height() < 2U || maximum_cell_steps == 0U ||
        !std::isfinite(maximum_distance) || maximum_distance <= 0.0F) return std::nullopt;
    const float length = std::sqrt(dot(direction, direction));
    if (!std::isfinite(length) || length <= 1.0e-10F) return std::nullopt;
    direction = mul(direction, 1.0F / length);
    origin = {quantize_q12(origin.x), quantize_q12(origin.y), quantize_q12(origin.z)};
    direction = {quantize_q28(direction.x), quantize_q28(direction.y), quantize_q28(direction.z)};
    const float quantized_length = std::sqrt(dot(direction, direction));
    if (!std::isfinite(quantized_length) || quantized_length <= 1.0e-10F) return std::nullopt;
    direction = mul(direction, 1.0F / quantized_length);

    const GridBasis basis = grid_basis(field);
    if (!basis.valid) return std::nullopt;
    const auto grid_origin = to_grid_point(basis, origin);
    const auto grid_direction = to_grid_direction(basis, direction);
    float enter = 0.0F;
    float exit = maximum_distance;
    if (!update_slab(grid_origin[0], grid_direction[0], 0.0F,
                     static_cast<float>(field.width() - 1U), enter, exit) ||
        !update_slab(grid_origin[1], grid_direction[1], 0.0F,
                     static_cast<float>(field.height() - 1U), enter, exit)) return std::nullopt;
    enter = std::max(0.0F, enter);
    exit = std::min(maximum_distance, exit);
    if (enter > exit + kEpsilon) return std::nullopt;

    const float start_t = std::min(exit, enter + 1.0e-5F);
    const float start_x = grid_origin[0] + grid_direction[0] * start_t;
    const float start_y = grid_origin[1] + grid_direction[1] * start_t;
    int32_t cell_x = std::clamp(static_cast<int32_t>(std::floor(start_x)), 0,
        static_cast<int32_t>(field.width()) - 2);
    int32_t cell_y = std::clamp(static_cast<int32_t>(std::floor(start_y)), 0,
        static_cast<int32_t>(field.height()) - 2);

    const int32_t step_x = grid_direction[0] > 1.0e-10F ? 1 :
                           (grid_direction[0] < -1.0e-10F ? -1 : 0);
    const int32_t step_y = grid_direction[1] > 1.0e-10F ? 1 :
                           (grid_direction[1] < -1.0e-10F ? -1 : 0);
    const float infinity = std::numeric_limits<float>::infinity();
    const float delta_x = step_x == 0 ? infinity : std::abs(1.0F / grid_direction[0]);
    const float delta_y = step_y == 0 ? infinity : std::abs(1.0F / grid_direction[1]);
    const float boundary_x = step_x > 0 ? static_cast<float>(cell_x + 1) : static_cast<float>(cell_x);
    const float boundary_y = step_y > 0 ? static_cast<float>(cell_y + 1) : static_cast<float>(cell_y);
    float next_x = step_x == 0 ? infinity : (boundary_x - grid_origin[0]) / grid_direction[0];
    float next_y = step_y == 0 ? infinity : (boundary_y - grid_origin[1]) / grid_direction[1];
    next_x = std::max(next_x, enter);
    next_y = std::max(next_y, enter);

    for (uint32_t visited = 1U; visited <= maximum_cell_steps; ++visited) {
        if (cell_x < 0 || cell_y < 0 || cell_x + 1 >= static_cast<int32_t>(field.width()) ||
            cell_y + 1 >= static_cast<int32_t>(field.height())) break;
        const float cell_exit = std::min({next_x, next_y, exit});
        const auto hit = test_cell(field, static_cast<uint32_t>(cell_x),
                                   static_cast<uint32_t>(cell_y), origin, direction, surface);
        if (hit && hit->distance + 1.0e-4F >= enter && hit->distance <= cell_exit + 1.0e-4F) {
            std::array<std::pair<uint32_t, uint32_t>, 4> corners{{
                {static_cast<uint32_t>(cell_x), static_cast<uint32_t>(cell_y)},
                {static_cast<uint32_t>(cell_x), static_cast<uint32_t>(cell_y + 1)},
                {static_cast<uint32_t>(cell_x + 1), static_cast<uint32_t>(cell_y)},
                {static_cast<uint32_t>(cell_x + 1), static_cast<uint32_t>(cell_y + 1)}}};
            float nearest_squared = std::numeric_limits<float>::max();
            std::pair<uint32_t, uint32_t> nearest = corners[0];
            for (const auto& corner : corners) {
                const auto& cell = field.at(corner.first, corner.second);
                const float dx = q12_to_float(cell.world_x_q12) - hit->position.x;
                const float dy = q12_to_float(cell.world_y_q12) - hit->position.y;
                const float squared = dx * dx + dy * dy;
                if (squared < nearest_squared) {
                    nearest_squared = squared;
                    nearest = corner;
                }
            }
            return FieldRaycastHit{static_cast<uint32_t>(cell_x), static_cast<uint32_t>(cell_y),
                nearest.first, nearest.second, hit->position, hit->distance, hit->secondary, visited};
        }
        if (cell_exit >= exit - kEpsilon) break;
        if (next_x < next_y - kEpsilon) {
            cell_x += step_x;
            next_x += delta_x;
        } else if (next_y < next_x - kEpsilon) {
            cell_y += step_y;
            next_y += delta_y;
        } else {
            cell_x += step_x;
            cell_y += step_y;
            next_x += delta_x;
            next_y += delta_y;
        }
    }
    return std::nullopt;
}

} // namespace thandor
