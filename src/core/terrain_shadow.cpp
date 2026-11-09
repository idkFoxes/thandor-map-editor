#include "thandor/terrain_shadow.hpp"
#include "thandor/field_raycast.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

#if !defined(THANDOR_SHADOW_FORCE_SCALAR) && (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#include <emmintrin.h>
#define THANDOR_SHADOW_SSE2 1
#endif

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
    // Quantize once into a zero border. Like open-thandor's SSE2 replacement
    // for the old byte-emulated MMX filter, process sixteen independent lanes.
    // Keep the editor's existing adjacent-texel tap spacing and output bytes;
    // native gridHalfSize/16 tap spacing is a separate parity issue.
    const std::size_t stride = static_cast<std::size_t>(dimension) + 4U;
    std::vector<uint8_t> quantized(stride * stride, 0U);
    for (uint32_t y = 0; y < dimension; ++y)
        for (uint32_t x = 0; x < dimension; ++x)
            quantized[(y + 2U) * stride + x + 2U] =
                static_cast<uint8_t>((source[static_cast<std::size_t>(y) * dimension + x] >> 5U) & 7U);
    std::vector<uint8_t> result(static_cast<std::size_t>(dimension) * dimension, 0U);
    for (uint32_t y = 0; y < dimension; ++y) {
        const uint8_t* row = quantized.data() + (y + 2U) * stride + 2U;
        uint32_t x = 0;
#if defined(THANDOR_SHADOW_SSE2)
        for (; x + 16U <= dimension; x += 16U) {
            const auto load = [&](std::ptrdiff_t offset) {
                return _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + x + offset));
            };
            const auto pitch = static_cast<std::ptrdiff_t>(stride);
            __m128i sum = _mm_slli_epi16(load(0), 2);
            __m128i cross_sum = _mm_adds_epu8(load(-1), load(1));
            cross_sum = _mm_adds_epu8(cross_sum, load(-pitch));
            cross_sum = _mm_adds_epu8(cross_sum, load(pitch));
            sum = _mm_adds_epu8(sum, cross_sum);
            sum = _mm_adds_epu8(sum, cross_sum);
            sum = _mm_adds_epu8(sum, cross_sum);
            sum = _mm_adds_epu8(sum, load(-2));
            sum = _mm_adds_epu8(sum, load(2));
            for (const auto offset : {-pitch, pitch}) {
                sum = _mm_adds_epu8(sum, load(offset - 2));
                sum = _mm_adds_epu8(sum, load(offset - 1));
                sum = _mm_adds_epu8(sum, load(offset + 1));
                sum = _mm_adds_epu8(sum, load(offset + 2));
            }
            for (const auto offset : {-2 * pitch, 2 * pitch}) {
                sum = _mm_adds_epu8(sum, load(offset - 1));
                sum = _mm_adds_epu8(sum, load(offset));
                sum = _mm_adds_epu8(sum, load(offset + 1));
            }
            _mm_storeu_si128(reinterpret_cast<__m128i*>(result.data() +
                static_cast<std::size_t>(y) * dimension + x), sum);
        }
#endif
        // Portable fallback and a tail for tile sizes not divisible by sixteen.
        for (; x < dimension; ++x) {
            uint32_t sum = 0;
            for (int32_t ky = -2; ky <= 2; ++ky)
                for (int32_t kx = -2; kx <= 2; ++kx)
                    sum += kernel[ky + 2][kx + 2] *
                        row[static_cast<std::ptrdiff_t>(x) + ky * static_cast<std::ptrdiff_t>(stride) + kx];
            result[static_cast<std::size_t>(y) * dimension + x] =
                static_cast<uint8_t>(std::min(sum, 255U));
        }
    }
    return result;
}

constexpr float kTau = 6.28318530717958647692F;
constexpr int32_t kTextureQ20One = 0x100000;

uint16_t radians_to_turn16(float radians) noexcept {
    const double turns = static_cast<double>(radians) / static_cast<double>(kTau);
    const int64_t rounded = static_cast<int64_t>(std::llround(turns * 65536.0));
    return static_cast<uint16_t>(rounded & 0xFFFF);
}

struct ShadowProjectionAngles {
    uint16_t elevation{};
    uint16_t azimuth{};
};

ShadowProjectionAngles shadow_projection_angles(Vec3f direction) noexcept {
    const float horizontal = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    return ShadowProjectionAngles{
        radians_to_turn16(std::atan2(direction.z, horizontal)),
        radians_to_turn16(std::atan2(direction.y, direction.x)),
    };
}

Vec3f direction_from_turn16(uint16_t elevation, uint16_t azimuth) noexcept {
    const float e = static_cast<float>(elevation) * (kTau / 65536.0F);
    const float a = static_cast<float>(azimuth) * (kTau / 65536.0F);
    const float c = std::cos(e);
    return {c * std::cos(a), c * std::sin(a), std::sin(e)};
}

int32_t quantized_cos_q28(uint16_t angle) noexcept {
    constexpr double scale = 268435456.0;
    const double radians = static_cast<double>(angle) *
        (6.28318530717958647692 / 65536.0);
    const double value = std::round(std::cos(radians) * scale);
    return static_cast<int32_t>(std::clamp(value,
        static_cast<double>(std::numeric_limits<int32_t>::min()),
        static_cast<double>(std::numeric_limits<int32_t>::max())));
}

uint8_t receiver_distance_attenuation(int32_t distance_q12) noexcept {
    int32_t value = 0x5000 - std::max(distance_q12, 0);
    if (value < 0) value = 0;
    value >>= 6;
    return static_cast<uint8_t>(std::min(value, 0xFF));
}

struct ReceiverContactResult {
    Vec3f position{};
    int32_t texture_v_q20{};
    int32_t distance_q12{};
    GeneratedShadowReceiverContactPath path{
        GeneratedShadowReceiverContactPath::DirectTerrainRay};
};

std::optional<ReceiverContactResult> solve_receiver_contact(
    const FieldGrid& field, Vec3f point, ShadowProjectionAngles angles,
    int32_t radius_q12, int32_t grid_step_q20,
    int32_t texture_v_q20) noexcept {
    if (radius_q12 <= 0 || grid_step_q20 <= 0) return std::nullopt;

    // The retail branch compares against FieldGrid_InterpolateTopSurfaceHeight, whose
    // positive secondary-surface delta is folded into the interpolated terrain height.
    const auto terrain_sample = field.sample_terrain(point.x, point.y);
    if (!terrain_sample.inside) return std::nullopt;
    const int32_t point_z_q12 = float_to_q12(point.z);
    const int32_t top_height_q12 = float_to_q12(
        field.sample_top_surface_height(point.x, point.y));

    const Vec3f direct = direction_from_turn16(angles.elevation, angles.azimuth);
    const int32_t twice_radius_q12 = radius_q12 > std::numeric_limits<int32_t>::max() / 2
        ? std::numeric_limits<int32_t>::max() : radius_q12 * 2;
    const float twice_radius = q12_to_float(twice_radius_q12);

    if (top_height_q12 < point_z_q12) {
        // Above the interpolated top surface: retail first tries the ordinary terrain
        // triangles along the generated-shadow projection direction.
        const auto direct_hit = raycast_field_surface_compatibility(
            field, point, direct, twice_radius, FieldRaycastSurface::Terrain, 0x400U);
        if (direct_hit) {
            const int32_t distance_q12 = float_to_q12(direct_hit->distance);
            return ReceiverContactResult{direct_hit->position, texture_v_q20,
                distance_q12, GeneratedShadowReceiverContactPath::DirectTerrainRay};
        }

        // The direct miss path advances the point by 2*radius, then casts in the
        // machine's perpendicular/reversed-angle direction. Its available scale is
        // proportional to the still-unused V span.
        if (grid_step_q20 - texture_v_q20 == 0) return std::nullopt;
        const int64_t side_scale_product =
            static_cast<int64_t>(grid_step_q20 - texture_v_q20) * twice_radius_q12;
        const int32_t side_scale_q12 = static_cast<int32_t>(
            side_scale_product / grid_step_q20);
        point = add(point, mul(direct, twice_radius));

        const uint16_t reverse_azimuth = static_cast<uint16_t>(angles.azimuth ^ 0x8000U);
        const uint16_t side_elevation = static_cast<uint16_t>(
            -static_cast<int32_t>(angles.elevation) - 0x4000);
        const Vec3f side_direction = direction_from_turn16(side_elevation, reverse_azimuth);
        const auto side_hit = raycast_field_surface_compatibility(
            field, point, side_direction, q12_to_float(side_scale_q12),
            FieldRaycastSurface::Terrain, 0x400U);
        if (!side_hit) return std::nullopt;

        const int32_t side_distance_q12 = float_to_q12(side_hit->distance);
        // FieldGrid_RaycastTerrainSurfaceDistanceCf returns the low byte of the hit
        // cell's +0x50 flags/material word in EDX. 004CDD40 immediately reuses that
        // byte as the elevation argument of the follow-up direction helper. Preserve
        // that literal call-site behavior without assigning it a stronger semantic name.
        const uint8_t hit_cell_byte =
            field.at(side_hit->quad_x, side_hit->quad_y).material();
        const Vec3f follow_direction = direction_from_turn16(hit_cell_byte, reverse_azimuth);
        point = add(point, mul(follow_direction, q12_to_float(side_distance_q12)));

        const int64_t v_numerator =
            static_cast<int64_t>(side_distance_q12) * grid_step_q20;
        texture_v_q20 += static_cast<int32_t>((v_numerator / radius_q12) >> 1);
        if (texture_v_q20 > grid_step_q20) return std::nullopt;
        return ReceiverContactResult{point, texture_v_q20, twice_radius_q12,
            GeneratedShadowReceiverContactPath::FallbackSideRay};
    }

    // Point is at/below the interpolated top surface. The machine clamps the vertical
    // difference to one radius, projects half of its cosine-scaled amount into the V
    // coordinate, and either lifts directly or recasts after V underflow.
    int32_t height_delta_q12 = top_height_q12 - point_z_q12;
    height_delta_q12 = std::min(height_delta_q12, radius_q12);
    const int32_t cosine_q28 = quantized_cos_q28(angles.elevation);
    const int64_t cosine_product =
        static_cast<int64_t>(height_delta_q12) * cosine_q28;
    const int32_t half_projected_delta_q12 = static_cast<int32_t>(cosine_product >> 29);
    const int32_t v_delta_q20 = static_cast<int32_t>(
        static_cast<int64_t>(grid_step_q20) * half_projected_delta_q12 / radius_q12);
    const int32_t previous_v_q20 = texture_v_q20;
    texture_v_q20 -= v_delta_q20;

    if (previous_v_q20 < v_delta_q20) {
        texture_v_q20 = 0;
        const Vec3f start = sub(point, mul(direct, twice_radius));
        const auto hit = raycast_field_surface_compatibility(
            field, start, direct, twice_radius, FieldRaycastSurface::Terrain, 0x400U);
        if (!hit) return std::nullopt;
        return ReceiverContactResult{hit->position, texture_v_q20, 0,
            GeneratedShadowReceiverContactPath::TopSurfaceRecast};
    }

    point.z = q12_to_float(point_z_q12 + height_delta_q12);
    return ReceiverContactResult{point, texture_v_q20, 0,
        GeneratedShadowReceiverContactPath::TopSurfaceLift};
}

} // namespace

bool retail_generated_shadow_settings_valid(GeneratedShadowSettings settings) noexcept {
    if (settings.grid_half_size < 16U || settings.grid_half_size > 256U ||
        settings.texture_dimension < settings.grid_half_size ||
        settings.texture_dimension > 512U || settings.subresource_count == 0U ||
        settings.subresource_count > 256U) return false;
    if ((settings.texture_dimension % settings.grid_half_size) != 0U) return false;
    const uint32_t tiles_per_axis = settings.texture_dimension / settings.grid_half_size;
    return tiles_per_axis != 0U;
}

uint32_t retail_generated_shadow_tiles_per_subresource(GeneratedShadowSettings settings) noexcept {
    if (!retail_generated_shadow_settings_valid(settings)) return 0U;
    const uint32_t tiles_per_axis = settings.texture_dimension / settings.grid_half_size;
    return tiles_per_axis * tiles_per_axis;
}

uint32_t retail_generated_shadow_tile_capacity(GeneratedShadowSettings settings) noexcept {
    const uint32_t tiles = retail_generated_shadow_tiles_per_subresource(settings);
    if (tiles == 0U || settings.subresource_count >
            std::numeric_limits<uint32_t>::max() / tiles) return 0U;
    return tiles * settings.subresource_count;
}

uint8_t retail_generated_shadow_packet_alpha(uint8_t model_tint_alpha,
                                             uint8_t distance_attenuation) noexcept {
    // 004CDD40 builds the alpha lane from (modelTintAlpha >> 1) duplicated into a word,
    // then shifts that lane by four. The lookup at 0x0041EE80 stores that construction for the 0..255 attenuation
    // index in its high 16-bit (alpha) lane. PMULHW keeps the signed high word; both
    // operands are non-negative and <= 0x0FFF here.
    const uint32_t half_alpha = static_cast<uint32_t>(model_tint_alpha >> 1U);
    const uint32_t alpha_lane = ((half_alpha << 8U) | half_alpha) >> 4U;
    const uint32_t attenuation = distance_attenuation;
    const uint32_t attenuation_lane = ((attenuation << 8U) | attenuation) >> 4U;
    const uint32_t product_high = (alpha_lane * attenuation_lane) >> 16U;
    return static_cast<uint8_t>(std::min(product_high, 255U));
}

std::optional<GeneratedObjectShadow> build_game_generated_object_shadow(
    const FieldGrid& field, std::span<const GeneratedShadowTriangle> triangles,
    Vec3f shadow_projection_direction, GeneratedShadowSettings settings) {
    if (triangles.empty() || !retail_generated_shadow_settings_valid(settings))
        return std::nullopt;
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
    const uint32_t tile_dimension = settings.grid_half_size;
    const float padding = std::max(radius * 0.025F,
        std::max(width, height) * (2.0F / static_cast<float>(tile_dimension)));
    projection_min_x -= padding;
    projection_max_x += padding;
    projection_min_y -= padding;
    projection_max_y += padding;
    width = projection_max_x - projection_min_x;
    height = projection_max_y - projection_min_y;

    std::vector<uint8_t> soft(static_cast<std::size_t>(tile_dimension) * tile_dimension, 0U);
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
                                static_cast<float>(tile_dimension - 1U);
            points[corner][1] = (projection_max_y - y) / height *
                                static_cast<float>(tile_dimension - 1U);
        }
        if (triangle.blurred_group) {
            rasterize_triangle(soft, tile_dimension, points);
            ++soft_count;
        } else {
            rasterize_triangle(hard, tile_dimension, points);
            ++hard_count;
        }
    }
    auto alpha = blur_game_soft_group(soft, tile_dimension);
    for (std::size_t index = 0; index < alpha.size(); ++index)
        alpha[index] = std::max(alpha[index], hard[index]);
    if (std::none_of(alpha.begin(), alpha.end(), [](uint8_t value) { return value != 0U; }))
        return std::nullopt;

    GeneratedObjectShadow result;
    result.mask.width = tile_dimension;
    result.mask.height = tile_dimension;
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

    // Retail reserves fourteen 0x80-byte projected-point blocks and fills them from twelve
    // terrain-contact points. The points form an outer rectangle, four edge midpoints, and a
    // centered quarter/three-quarter rectangle. The packet-copy sequence in 0x004CDD40 yields
    // the exact fourteen-triangle stitching below. Keeping this topology matters on uneven
    // terrain: the old editor-only 5x5 grid invented extra receiver diagonals that retail never
    // emits.
    const int32_t grid_step_q20 = static_cast<int32_t>(
        (static_cast<uint64_t>(settings.grid_half_size) * kTextureQ20One) /
        settings.texture_dimension);
    const int32_t grid_max_q20 = grid_step_q20 - 0x1000;
    const int32_t grid_half_q20 = grid_step_q20 >> 1;
    const int32_t grid_quarter_q20 = grid_step_q20 >> 2;

    struct ReceiverSeed {
        float u;
        float v;
        int32_t retail_v_q20;
    };
    // Public vertices retain the normalized V535 lattice so atlas sampling remains at
    // tile-center endpoints. retail_v_q20 is the corresponding machine scalar used by
    // the contact solver; fallback/lift deltas are translated back into normalized V.
    // Machine record order is I,L,J,K,H,F,E,G,A,C,B,D; these entries are reordered into
    // the stable public A..L topology introduced in V535.
    const std::array<ReceiverSeed, 12> seeds{{
        {0.25F, 0.25F, grid_quarter_q20},                  // A: machine record 9
        {0.75F, 0.25F, grid_quarter_q20},                  // B: machine record 11
        {0.25F, 0.75F, grid_half_q20 + grid_quarter_q20},  // C: machine record 10
        {0.75F, 0.75F, grid_half_q20 + grid_quarter_q20},  // D: machine record 12
        {0.50F, 0.00F, 0},                                 // E: machine record 7
        {1.00F, 0.50F, grid_half_q20},                     // F: machine record 6
        {0.50F, 1.00F, grid_max_q20},                      // G: machine record 8
        {0.00F, 0.50F, grid_half_q20},                     // H: machine record 5
        {0.00F, 0.00F, 0},                                 // I: machine record 1
        {1.00F, 0.00F, 0},                                 // J: machine record 3
        {1.00F, 1.00F, grid_max_q20},                      // K: machine record 4
        {0.00F, 1.00F, grid_max_q20},                      // L: machine record 2
    }};
    static constexpr std::array<std::array<uint32_t, 3>, 14> packet_triangles{{
        {{0U, 1U, 2U}},  // A B C
        {{3U, 1U, 2U}},  // D B C
        {{0U, 1U, 4U}},  // A B E
        {{3U, 1U, 5U}},  // D B F
        {{3U, 6U, 2U}},  // D G C
        {{0U, 7U, 2U}},  // A H C
        {{0U, 8U, 4U}},  // A I E
        {{9U, 1U, 4U}},  // J B E
        {{9U, 1U, 5U}},  // J B F
        {{3U, 10U, 5U}}, // D K F
        {{3U, 6U, 10U}}, // D G K
        {{11U, 6U, 2U}}, // L G C
        {{11U, 7U, 2U}}, // L H C
        {{0U, 7U, 8U}},  // A H I
    }};

    result.receiver_vertices.reserve(seeds.size());
    const ShadowProjectionAngles projection_angles = shadow_projection_angles(projection);
    const int32_t radius_q12 = std::max(1, float_to_q12(radius));
    for (const ReceiverSeed& seed : seeds) {
        const float plane_x = projection_min_x + seed.u * width;
        const float plane_y = projection_max_y - seed.v * height;
        const Vec3f plane_point = add(center, add(mul(right, plane_x), mul(up, plane_y)));
        const auto contact = solve_receiver_contact(field, plane_point, projection_angles,
            radius_q12, grid_step_q20, seed.retail_v_q20);
        // 004CDD40 returns before reserving/filling the fourteen packets when any one
        // of the twelve repeated contact branches fails. Do not invent partial meshes.
        if (!contact) return std::nullopt;

        Vec3f position = contact->position;
        position.z += std::max(0.0F, settings.surface_bias);
        const float v_delta = static_cast<float>(
            contact->texture_v_q20 - seed.retail_v_q20) /
            static_cast<float>(grid_step_q20);
        const float corrected_v = std::clamp(seed.v + v_delta, 0.0F, 1.0F);
        result.receiver_vertices.push_back(GeneratedShadowReceiverVertex{
            position, seed.u, corrected_v,
            receiver_distance_attenuation(contact->distance_q12), contact->path});
    }
    result.receiver_indices.reserve(packet_triangles.size() * 3U);
    for (const auto& triangle : packet_triangles) {
        result.receiver_indices.insert(result.receiver_indices.end(),
            {triangle[0], triangle[1], triangle[2]});
    }
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
