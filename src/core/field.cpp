#include "thandor/retail_placement.hpp"
#include "thandor/field.hpp"
#include "thandor/terrain_texture.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace thandor {
namespace {
constexpr std::size_t kHeaderSize = 0x200;
constexpr std::size_t kCellSize = 0x80;
constexpr uint32_t kFieldMagic = 0x00646C66U; // "fld\0"
constexpr float kPi = 3.14159265358979323846F;

float dot(Vec3f a, Vec3f b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3f normalize(Vec3f value) {
    const float length = std::sqrt(dot(value, value));
    if (length < 1.0e-8F) return {0.0F, 0.0F, 1.0F};
    return {value.x / length, value.y / length, value.z / length};
}
float smoothstep01(float value) { return value * value * (3.0F - 2.0F * value); }



void commit_terrain_height(FieldCell& cell, int32_t new_height_q12,
                           TerrainWaterEditPolicy water_policy) {
    const bool wet = cell.water_delta_q12 > 0;
    if (wet && water_policy == TerrainWaterEditPolicy::IgnoreWetCells) return;

    // Retail TerrainEditBuffer_CommitHeightDeltasAndRefreshLighting performs the paired update
    //   terrainHeight      -= delta
    //   waterSurfaceDelta += delta
    // for every edited cell. Therefore terrainHeight + waterSurfaceDelta remains invariant even
    // when the current water delta is negative ("dry"). G405 incorrectly limited this behavior
    // to wet cells, so dry-cell terrain edits moved the hidden absolute water plane.
    const int64_t absolute_water_q12 = static_cast<int64_t>(cell.terrain_height_q12) +
                                       static_cast<int64_t>(cell.water_delta_q12);
    cell.terrain_height_q12 = new_height_q12;
    if (water_policy == TerrainWaterEditPolicy::PreserveAbsoluteSurface) {
        const int64_t remaining = absolute_water_q12 - static_cast<int64_t>(new_height_q12);
        cell.water_delta_q12 = static_cast<int32_t>(std::clamp<int64_t>(
            remaining, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
    }
}

uint8_t clamp_byte(float value) {
    return static_cast<uint8_t>(std::clamp(std::lround(value), 0L, 255L));
}

struct GridTriangleSample {
    uint32_t cell_x{};
    uint32_t cell_y{};
    float grid_x{};
    float grid_y{};
    float w00{};
    float w10{};
    float w01{};
    float w11{};
    bool inside{};
};

GridTriangleSample grid_triangle_sample(const FieldGrid& field, float world_x, float world_y) {
    if (field.width() < 2U || field.height() < 2U)
        throw FormatError("terrain sampling requires at least a 2x2 grid");
    const auto point = [](const FieldCell& cell) {
        return Vec3f{q12_to_float(cell.world_x_q12), q12_to_float(cell.world_y_q12),
                     cell.terrain_height()};
    };
    const Vec3f origin = point(field.at(0, 0));
    const Vec3f axis_x_point = point(field.at(1, 0));
    const Vec3f axis_y_point = point(field.at(0, 1));
    const float axis_x_x = axis_x_point.x - origin.x;
    const float axis_x_y = axis_x_point.y - origin.y;
    const float axis_y_x = axis_y_point.x - origin.x;
    const float axis_y_y = axis_y_point.y - origin.y;
    const float determinant = axis_x_x * axis_y_y - axis_x_y * axis_y_x;
    if (std::abs(determinant) < 1.0e-8F)
        throw FormatError("FLD world-coordinate basis is singular");
    const float relative_x = world_x - origin.x;
    const float relative_y = world_y - origin.y;
    const float raw_grid_x = (relative_x * axis_y_y - relative_y * axis_y_x) / determinant;
    const float raw_grid_y = (axis_x_x * relative_y - axis_x_y * relative_x) / determinant;
    GridTriangleSample result;
    result.inside = raw_grid_x >= 0.0F && raw_grid_y >= 0.0F &&
                    raw_grid_x <= static_cast<float>(field.width() - 1U) &&
                    raw_grid_y <= static_cast<float>(field.height() - 1U);
    result.grid_x = std::clamp(raw_grid_x, 0.0F, static_cast<float>(field.width() - 1U));
    result.grid_y = std::clamp(raw_grid_y, 0.0F, static_cast<float>(field.height() - 1U));
    result.cell_x = std::min(static_cast<uint32_t>(std::floor(result.grid_x)), field.width() - 2U);
    result.cell_y = std::min(static_cast<uint32_t>(std::floor(result.grid_y)), field.height() - 2U);
    const float local_x = result.grid_x - static_cast<float>(result.cell_x);
    const float local_y = result.grid_y - static_cast<float>(result.cell_y);
    // game.exe 0x00500CE0 splits each quad along BL-to-TR. The first triangle is
    // TL/BL/TR (local_x + local_y <= 1); the second is BL/BR/TR.
    if (local_x + local_y <= 1.0F) {
        result.w00 = 1.0F - local_x - local_y;
        result.w01 = local_y;
        result.w10 = local_x;
    } else {
        result.w01 = 1.0F - local_x;
        result.w11 = local_x + local_y - 1.0F;
        result.w10 = 1.0F - local_y;
    }
    return result;
}

float interpolate_cell_value(float v00, float v10, float v01, float v11,
                             const GridTriangleSample& sample) noexcept {
    return v00 * sample.w00 + v10 * sample.w10 +
           v01 * sample.w01 + v11 * sample.w11;
}

} // namespace

const char* terrain_water_edit_policy_name(TerrainWaterEditPolicy policy) noexcept {
    switch (policy) {
        case TerrainWaterEditPolicy::PreserveAbsoluteSurface: return "Keep absolute water level";
        case TerrainWaterEditPolicy::PreserveWaterDepth: return "Move water with terrain";
        case TerrainWaterEditPolicy::IgnoreWetCells: return "Ignore water-covered cells";
    }
    return "Unknown water policy";
}

Color4f unpack_argb(uint32_t value) {
    constexpr float inv = 1.0F / 255.0F;
    return {
        static_cast<float>((value >> 16U) & 0xFFU) * inv,
        static_cast<float>((value >> 8U) & 0xFFU) * inv,
        static_cast<float>(value & 0xFFU) * inv,
        static_cast<float>((value >> 24U) & 0xFFU) * inv,
    };
}

uint32_t pack_argb(Color4f value) {
    const auto a = clamp_byte(value.a * 255.0F);
    const auto r = clamp_byte(value.r * 255.0F);
    const auto g = clamp_byte(value.g * 255.0F);
    const auto b = clamp_byte(value.b * 255.0F);
    return static_cast<uint32_t>(a) << 24U | static_cast<uint32_t>(r) << 16U |
           static_cast<uint32_t>(g) << 8U | b;
}

uint32_t multiply_rgb_opaque(uint32_t first, uint32_t second) {
    const auto a = unpack_argb(first);
    const auto b = unpack_argb(second);
    return pack_argb({a.r * b.r, a.g * b.g, a.b * b.b, 1.0F});
}

TerrainLighting TerrainLighting::sampled_at_tick(uint32_t simulation_tick) const noexcept {
    if (cycle_duration_ticks == 0U) return *this;
    TerrainLighting result = *this;
    constexpr double tau = 6.283185307179586476925286766559;
    const double phase = static_cast<double>(simulation_tick % cycle_duration_ticks) /
                         static_cast<double>(cycle_duration_ticks);
    const double alternate_weight = (1.0 - std::cos(phase * tau)) * 0.5;
    // V537 uses a triangular 0->alternate->0 interpolation for the low words of the
    // packed origin/dimension pairs, while their high words and the colors use the cosine
    // weight. Keep that asymmetry: it is visible in the recovered update routine.
    const double triangular_weight = phase < 0.5 ? phase * 2.0 : (1.0 - phase) * 2.0;
    const auto lerp_byte = [&](uint32_t a, uint32_t b, unsigned shift) {
        const double av = static_cast<double>((a >> shift) & 0xFFU);
        const double bv = static_cast<double>((b >> shift) & 0xFFU);
        return static_cast<uint32_t>(std::clamp(std::lround(av + (bv - av) * alternate_weight),
                                                0L, 255L));
    };
    const auto lerp_argb = [&](uint32_t a, uint32_t b) {
        return lerp_byte(a,b,24U) << 24U | lerp_byte(a,b,16U) << 16U |
               lerp_byte(a,b,8U) << 8U | lerp_byte(a,b,0U);
    };
    const auto lerp_wrapped_u16 = [&](uint16_t a, uint16_t b, double weight) {
        const int32_t delta = static_cast<int16_t>(static_cast<uint16_t>(b - a));
        return static_cast<uint16_t>(static_cast<uint32_t>(static_cast<int64_t>(a) +
            std::llround(static_cast<double>(delta) * weight)) & 0xFFFFU);
    };
    const auto lerp_u16 = [&](uint16_t a, uint16_t b, double weight) {
        return static_cast<uint16_t>(std::clamp(std::lround(static_cast<double>(a) +
            (static_cast<double>(b) - a) * weight), 0L, 65535L));
    };
    result.base_color = lerp_argb(base_color, alternate_base_color);
    result.ramp_124 = lerp_argb(ramp_124, alternate_ramp_124);
    result.color_128 = lerp_argb(color_128, alternate_color_128);
    result.ramp_12c = lerp_argb(ramp_12c, alternate_ramp_12c);
    result.color_130 = lerp_argb(color_130, alternate_color_130);
    result.color_134 = lerp_argb(color_134, alternate_color_134);
    result.color_138 = lerp_argb(color_138, alternate_color_138);
    result.color_13c = lerp_argb(color_13c, alternate_color_13c);
    result.azimuth = lerp_wrapped_u16(azimuth, alternate_azimuth, triangular_weight);
    result.elevation = lerp_wrapped_u16(elevation, alternate_elevation, alternate_weight);
    result.field_region_width = lerp_u16(field_region_width, alternate_field_region_width, triangular_weight);
    result.field_region_height = lerp_u16(field_region_height, alternate_field_region_height, alternate_weight);
    return result;
}

std::array<uint32_t, 256> TerrainLighting::build_game_ramp() const {
    // WorldRuntime_SetTerrainLightingConfiguration passes LEV +0x124 as the ramp base and
    // LEV +0x120 (base_color in the serialized structure) as the per-step additive color.
    // G336 had these operands reversed. Preserve the original byte-space saturated arithmetic.
    std::array<uint32_t, 256> ramp{};
    for (uint32_t i = 0; i < 256; ++i) {
        const uint32_t scale = 256U - i;
        uint32_t result = ramp_124 & 0xFF000000U;
        const auto accumulate = [&](uint32_t mask, unsigned shift) {
            const uint32_t base = (ramp_124 & mask) >> shift;
            const uint32_t step = (base_color & mask) >> shift;
            return std::min(255U, base + ((step * scale) >> 8U));
        };
        result |= accumulate(0x00FF0000U, 16U) << 16U;
        result |= accumulate(0x0000FF00U, 8U) << 8U;
        result |= accumulate(0x000000FFU, 0U);
        ramp[i] = result;
    }
    return ramp;
}

Vec3f TerrainLighting::shadow_ray_direction() const {
    // FixedMath_DirectionFromAnglesScaledRegs(elevation, azimuth, scale) emits the
    // direction in which light rays travel. Generated shadows require its negative-Z
    // ray directly; surface lighting uses the signed LUT anchor recovered at 0x00501590.
    const float elevation_radians = static_cast<float>(elevation) * (2.0F * kPi / 65536.0F);
    const float azimuth_radians = static_cast<float>(azimuth) * (2.0F * kPi / 65536.0F);
    const float c = std::cos(elevation_radians);
    return normalize({c * std::cos(azimuth_radians), c * std::sin(azimuth_radians),
                      std::sin(elevation_radians)});
}

PreparedDirectionalLight TerrainLighting::prepare_directional_light() const {
    return {shadow_ray_direction(), ramp_124};
}

int32_t PreparedDirectionalLight::signed_lut_index(Vec3f upward_normal) const {
    // game.exe 0x00505690 converts the packed normal to Q28, dots it with the
    // light-ray direction, arithmetic-shifts the signed result by 16, then indexes
    // [signedIndex*4 + 0x00501590]. The 256 generated ramp entries live immediately
    // before that anchor at 0x00501190. Positive/zero indices select the fixed base
    // table; negative indices walk backward through the ramp.
    const Vec3f normal = normalize(upward_normal);
    const float signed_dot = std::clamp(dot(normal, ray_direction), -1.0F, 1.0F);
    return std::clamp(static_cast<int32_t>(std::floor(signed_dot * 256.0F)), -256, 256);
}

uint32_t PreparedDirectionalLight::color_for_upward_normal(
    Vec3f upward_normal, const std::array<uint32_t, 256>& ramp) const {
    const int32_t signed_index = signed_lut_index(upward_normal);
    if (signed_index >= 0) return base_color;
    return ramp[static_cast<std::size_t>(256 + signed_index)];
}

int32_t TerrainLighting::signed_directional_lut_index(Vec3f upward_normal) const {
    return prepare_directional_light().signed_lut_index(upward_normal);
}

uint32_t TerrainLighting::directional_color_for_upward_normal(
    Vec3f upward_normal, const std::array<uint32_t, 256>& ramp) const {
    const int32_t signed_index = signed_directional_lut_index(upward_normal);
    if (signed_index >= 0) return ramp_124;
    return ramp[static_cast<std::size_t>(256 + signed_index)];
}

FieldGrid FieldGrid::parse(std::span<const std::byte> data) {
    if (data.size() < kHeaderSize || read_u32(data, 0) != kFieldMagic) throw FormatError("invalid FLD magic");
    FieldGrid grid;
    grid.width_ = read_u32(data, 0xB8);
    grid.height_ = read_u32(data, 0xBC);
    if (grid.width_ < 2 || grid.height_ < 2 || grid.width_ > 4096 || grid.height_ > 4096) {
        throw FormatError("FLD dimensions are outside editor limits");
    }
    const uint64_t count = static_cast<uint64_t>(grid.width_) * grid.height_;
    const uint64_t expected = kHeaderSize + count * kCellSize;
    if (expected != data.size()) throw FormatError("FLD size does not match width and height");
    grid.header_.assign(data.begin(), data.begin() + kHeaderSize);
    grid.cells_.resize(static_cast<std::size_t>(count));
    for (std::size_t i = 0; i < grid.cells_.size(); ++i) {
        const auto offset = kHeaderSize + i * kCellSize;
        auto& cell = grid.cells_[i];
        cell.world_x_q12 = read_i32(data, offset + 0x40);
        cell.world_y_q12 = read_i32(data, offset + 0x44);
        cell.terrain_height_q12 = read_i32(data, offset + 0x48);
        cell.water_delta_q12 = read_i32(data, offset + 0x4C);
        cell.flags_and_material = read_u32(data, offset + 0x50);
        cell.loaded_terrain_normal_angles = read_u32(data, offset + 0x08);
        cell.loaded_water_normal_angles = read_u32(data, offset + 0x78);
        // Cell +0x54 is rebuilt as a runtime terrain-direction lookup pointer after
        // load and is explicitly zeroed by the retail FLD save path. Do not model it
        // as editor-persistent state.
    }
    return grid;
}

FieldGrid FieldGrid::create(uint32_t width, uint32_t height, int32_t initial_height_q12,
                            uint8_t initial_material) {
    if (width < 2 || height < 2 || width > 4096 || height > 4096) throw FormatError("invalid FLD dimensions");
    FieldGrid grid;
    grid.width_ = width;
    grid.height_ = height;
    grid.header_.resize(kHeaderSize);
    write_u32(grid.header_, 0, kFieldMagic);
    write_u32(grid.header_, 4, static_cast<uint32_t>(kHeaderSize + static_cast<uint64_t>(width) * height * kCellSize));
    write_u32(grid.header_, 8, 1);
    write_u32(grid.header_, 0x0C, FieldGrid::kGameConverterVersion);
    write_u32(grid.header_, 0xB0, initial_material < 26 ? (1U << initial_material) : 0U);
    write_u32(grid.header_, 0xB8, width);
    write_u32(grid.header_, 0xBC, height);
    grid.cells_.resize(static_cast<std::size_t>(width) * height);
    for (auto& cell : grid.cells_) {
        cell.terrain_height_q12 = initial_height_q12;
        cell.water_delta_q12 = FieldGrid::kDryWaterDeltaQ12;
        cell.set_material(initial_material);
    }
    grid.regenerate_coordinates();
    return grid;
}


uint32_t FieldGrid::converter_version() const noexcept {
    return header_.size() >= 0x10U ? read_u32(header_, 0x0CU) : 0U;
}

bool FieldGrid::is_game_compatible_dimension(uint32_t value) noexcept {
    return value >= kGameMinimumDimension && value <= kGameMaximumDimension &&
           (value & 7U) == 3U;
}

bool FieldGrid::is_game_compatible_shape(uint32_t width, uint32_t height) noexcept {
    return is_game_compatible_dimension(width) && is_game_compatible_dimension(height) &&
           static_cast<uint64_t>(width) * height <= kGameMaximumProvenArea;
}

bool FieldGrid::has_game_compatible_dimensions() const noexcept {
    return is_game_compatible_shape(width_, height_);
}

void FieldGrid::normalize_for_game_export() {
    if (header_.size() != kHeaderSize) throw FormatError("FLD header is not exactly 0x200 bytes");
    const uint32_t version = converter_version();
    if (version != kGameConverterVersion && version != kLegacyEditorConverterVersion) {
        throw FormatError("FLD converter version is unsupported by the game; expected 0x00060006");
    }
    if (!has_game_compatible_dimensions()) {
        throw FormatError(
            "FLD dimensions are outside the stock-proven compatibility envelope; each axis must "
            "be 8*n+3 in 59..139 and width*height must not exceed 131*131");
    }
    write_u32(header_, 0x0CU, kGameConverterVersion);
    // Repair G334-era and hand-authored fields. Zero is not dry in game.exe; it enters the
    // nonnegative water branch and can reject construction across the whole map.
    for (auto& cell : cells_) {
        if (cell.water_delta_q12 == 0) cell.water_delta_q12 = kDryWaterDeltaQ12;
    }
}

FieldCell& FieldGrid::at(uint32_t x, uint32_t y) {
    if (x >= width_ || y >= height_) throw std::out_of_range("FLD cell index");
    return cells_[static_cast<std::size_t>(y) * width_ + x];
}
const FieldCell& FieldGrid::at(uint32_t x, uint32_t y) const {
    if (x >= width_ || y >= height_) throw std::out_of_range("FLD cell index");
    return cells_[static_cast<std::size_t>(y) * width_ + x];
}

std::vector<std::byte> FieldGrid::serialize() const {
    const uint64_t size64 = kHeaderSize + static_cast<uint64_t>(cells_.size()) * kCellSize;
    if (size64 > std::numeric_limits<uint32_t>::max()) throw FormatError("FLD exceeds 32-bit allocation size");
    std::vector<std::byte> data(static_cast<std::size_t>(size64));
    if (header_.size() == kHeaderSize) std::copy(header_.begin(), header_.end(), data.begin());
    write_u32(data, 0, kFieldMagic);
    write_u32(data, 4, static_cast<uint32_t>(data.size()));
    write_u32(data, 0xB0, material_mask());
    write_u32(data, 0xB8, width_);
    write_u32(data, 0xBC, height_);
    for (std::size_t i = 0; i < cells_.size(); ++i) {
        const auto offset = kHeaderSize + i * kCellSize;
        const auto& cell = cells_[i];
        // Retail FieldGrid_SaveAssetImageFromRuntimeStateCf (game.exe 0x00532B60) sanitizes
        // runtime-derived cell state before writing the FLD image. Mirror the proven fields that
        // this logical editor owns instead of serializing live-only pointers/classification bits.
        write_u32(data, offset + 0x08, kRetailSerializedTriangle0NormalSeed);
        write_i32(data, offset + 0x40, cell.world_x_q12);
        write_i32(data, offset + 0x44, cell.world_y_q12);
        write_i32(data, offset + 0x48, cell.terrain_height_q12);
        write_i32(data, offset + 0x4C, cell.water_delta_q12);
        write_u32(data, offset + 0x50,
                  cell.flags_and_material & kRetailPersistedFlagsAndMaterialMask);
        write_u32(data, offset + 0x54, 0U);
    }
    return data;
}

void FieldGrid::regenerate_coordinates() {
    int32_t row_base_x = 0;
    int32_t world_y = 0;
    for (uint32_t y = 0; y < height_; ++y) {
        int32_t world_x = row_base_x;
        for (uint32_t x = 0; x < width_; ++x) {
            auto& cell = at(x, y);
            cell.world_x_q12 = world_x;
            cell.world_y_q12 = world_y;
            world_x += 0x901;
        }
        row_base_x += 0x480;
        world_y -= 1999;
    }
}

void FieldGrid::resize(uint32_t new_width, uint32_t new_height, bool center_existing) {
    if (new_width < 2 || new_height < 2 || new_width > 4096 || new_height > 4096) throw FormatError("invalid resized FLD dimensions");
    std::vector<FieldCell> replacement(static_cast<std::size_t>(new_width) * new_height);
    for (auto& cell : replacement) cell.water_delta_q12 = kDryWaterDeltaQ12;
    const int old_x0 = center_existing ? (static_cast<int>(new_width) - static_cast<int>(width_)) / 2 : 0;
    const int old_y0 = center_existing ? (static_cast<int>(new_height) - static_cast<int>(height_)) / 2 : 0;
    // Fill the expanded margin from the closest source edge cell. Zero-initialized padding
    // produced an artificial height-0 cliff around every imported map and lost its terrain
    // material, water and persistent marker flags. The original inner rectangle is copied
    // bit-for-bit (apart from the required regenerated world coordinates below).
    for (uint32_t y = 0; y < new_height; ++y) {
        const auto sy = static_cast<uint32_t>(std::clamp(
            static_cast<int>(y) - old_y0, 0, static_cast<int>(height_) - 1));
        for (uint32_t x = 0; x < new_width; ++x) {
            const auto sx = static_cast<uint32_t>(std::clamp(
                static_cast<int>(x) - old_x0, 0, static_cast<int>(width_) - 1));
            replacement[static_cast<std::size_t>(y) * new_width + x] = at(sx, sy);
        }
    }
    width_ = new_width;
    height_ = new_height;
    cells_ = std::move(replacement);
    regenerate_coordinates();
}

float FieldGrid::brush_weight(float dx, float dy, const Brush& brush) const {
    const float distance = brush.shape == BrushShape::Square ? std::max(std::abs(dx), std::abs(dy)) : std::sqrt(dx * dx + dy * dy);
    if (distance > brush.radius_cells) return 0.0F;
    const float t = brush.radius_cells <= 0.0F ? 1.0F : 1.0F - distance / brush.radius_cells;
    switch (brush.falloff) {
        case BrushFalloff::Constant: return 1.0F;
        case BrushFalloff::Linear: return t;
        case BrushFalloff::Smooth: return smoothstep01(t);
    }
    return t;
}

void FieldGrid::raise_lower(float center_x, float center_y, const Brush& brush, float delta_world,
                            TerrainWaterEditPolicy water_policy) {
    for (uint32_t y = 0; y < height_; ++y) for (uint32_t x = 0; x < width_; ++x) {
        const float weight = brush_weight(static_cast<float>(x) - center_x, static_cast<float>(y) - center_y, brush);
        if (weight <= 0.0F) continue;
        auto& cell = at(x, y);
        if (cell.water_delta_q12 > 0 && water_policy == TerrainWaterEditPolicy::IgnoreWetCells) continue;
        const int32_t next = cell.terrain_height_q12 +
            float_to_q12(delta_world * brush.strength * weight);
        commit_terrain_height(cell, next, water_policy);
    }
}

void FieldGrid::flatten(float center_x, float center_y, const Brush& brush, float target_world,
                        TerrainWaterEditPolicy water_policy) {
    const auto target = float_to_q12(target_world);
    for (uint32_t y = 0; y < height_; ++y) for (uint32_t x = 0; x < width_; ++x) {
        const float weight = brush_weight(static_cast<float>(x) - center_x, static_cast<float>(y) - center_y, brush) * brush.strength;
        if (weight <= 0.0F) continue;
        auto& cell = at(x, y);
        if (cell.water_delta_q12 > 0 && water_policy == TerrainWaterEditPolicy::IgnoreWetCells) continue;
        const int32_t next = static_cast<int32_t>(std::lround(
            static_cast<float>(cell.terrain_height_q12) +
            static_cast<float>(target - cell.terrain_height_q12) * std::clamp(weight, 0.0F, 1.0F)));
        commit_terrain_height(cell, next, water_policy);
    }
}

void FieldGrid::smooth(float center_x, float center_y, const Brush& brush, unsigned iterations,
                       TerrainWaterEditPolicy water_policy) {
    std::vector<int32_t> source(cells_.size());
    for (unsigned iteration = 0; iteration < std::max(1U, iterations); ++iteration) {
        for (std::size_t i = 0; i < cells_.size(); ++i) source[i] = cells_[i].terrain_height_q12;
        for (uint32_t y = 0; y < height_; ++y) for (uint32_t x = 0; x < width_; ++x) {
            const float weight = brush_weight(static_cast<float>(x) - center_x, static_cast<float>(y) - center_y, brush) * brush.strength;
            if (weight <= 0.0F) continue;
            auto& cell = at(x, y);
            if (cell.water_delta_q12 > 0 && water_policy == TerrainWaterEditPolicy::IgnoreWetCells) continue;
            int64_t sum = 0; int count = 0;
            for (int oy = -1; oy <= 1; ++oy) for (int ox = -1; ox <= 1; ++ox) {
                const int sx = static_cast<int>(x) + ox, sy = static_cast<int>(y) + oy;
                if (sx >= 0 && sy >= 0 && sx < static_cast<int>(width_) && sy < static_cast<int>(height_)) {
                    sum += source[static_cast<std::size_t>(sy) * width_ + static_cast<uint32_t>(sx)]; ++count;
                }
            }
            const auto average = static_cast<int32_t>(sum / count);
            const int32_t next = static_cast<int32_t>(std::lround(
                static_cast<float>(cell.terrain_height_q12) +
                static_cast<float>(average - cell.terrain_height_q12) * std::clamp(weight, 0.0F, 1.0F)));
            commit_terrain_height(cell, next, water_policy);
        }
    }
}

void FieldGrid::paint_material(float center_x, float center_y, const Brush& brush, uint8_t material) {
    for (uint32_t y = 0; y < height_; ++y) for (uint32_t x = 0; x < width_; ++x) {
        if (brush_weight(static_cast<float>(x) - center_x, static_cast<float>(y) - center_y, brush) > 0.0F) at(x, y).set_material(material);
    }
}

void FieldGrid::set_water(float center_x, float center_y, const Brush& brush,
                          float absolute_water_world, bool enabled) {
    const auto absolute = float_to_q12(absolute_water_world);
    for (uint32_t y = 0; y < height_; ++y) for (uint32_t x = 0; x < width_; ++x) {
        if (brush_weight(static_cast<float>(x) - center_x, static_cast<float>(y) - center_y, brush) > 0.0F) {
            auto& cell = at(x, y);
            const int64_t delta = static_cast<int64_t>(absolute) - cell.terrain_height_q12;
            cell.water_delta_q12 = enabled && delta > 0
                ? static_cast<int32_t>(std::min<int64_t>(delta, std::numeric_limits<int32_t>::max()))
                : kDryWaterDeltaQ12;
        }
    }
}

std::size_t FieldGrid::set_water_selection(std::span<const uint8_t> selection,
                                           float absolute_water_world, bool enabled) {
    if (selection.size() != cells_.size()) {
        throw std::invalid_argument("water selection size does not match the FLD grid");
    }
    const auto absolute = float_to_q12(absolute_water_world);
    std::size_t changed = 0U;
    for (std::size_t index = 0; index < cells_.size(); ++index) {
        if (selection[index] == 0U) continue;
        auto& cell = cells_[index];
        const int64_t delta = static_cast<int64_t>(absolute) - cell.terrain_height_q12;
        const int32_t next = enabled && delta > 0
            ? static_cast<int32_t>(std::min<int64_t>(delta, std::numeric_limits<int32_t>::max()))
            : kDryWaterDeltaQ12;
        if (cell.water_delta_q12 != next) {
            cell.water_delta_q12 = next;
            ++changed;
        }
    }
    return changed;
}

std::size_t FieldGrid::paint_placement_occupancy_class(float center_x, float center_y,
                                                        const Brush& brush,
                                                        uint32_t occupancy_class,
                                                        bool enabled) {
    if (occupancy_class >= 21U) throw std::out_of_range("placement occupancy class");
    const uint32_t mask = 0x00000800U << occupancy_class;
    std::size_t changed = 0U;
    for (uint32_t y = 0; y < height_; ++y) for (uint32_t x = 0; x < width_; ++x) {
        if (brush_weight(static_cast<float>(x) - center_x,
                         static_cast<float>(y) - center_y, brush) <= 0.0F) continue;
        auto& cell = at(x, y);
        const uint32_t before = cell.flags_and_material;
        if (enabled) cell.flags_and_material |= mask;
        else cell.flags_and_material &= ~mask;
        if (cell.flags_and_material != before) ++changed;
    }
    return changed;
}

void FieldGrid::offset_terrain_anchor(float world_x, float world_y, float delta_world,
                                      TerrainWaterEditPolicy water_policy) {
    if (!std::isfinite(delta_world) || std::abs(delta_world) <= 1.0e-8F) return;
    const auto sample = sample_terrain(world_x, world_y);
    const uint32_t cell_x = std::min(static_cast<uint32_t>(std::floor(sample.grid_x)), width_ - 2U);
    const uint32_t cell_y = std::min(static_cast<uint32_t>(std::floor(sample.grid_y)), height_ - 2U);
    const int32_t delta_q12 = float_to_q12(delta_world);
    const std::array<std::pair<uint32_t, uint32_t>, 4> support{{
        {cell_x, cell_y}, {cell_x + 1U, cell_y},
        {cell_x, cell_y + 1U}, {cell_x + 1U, cell_y + 1U},
    }};
    for (const auto& [x, y] : support) {
        auto& cell = at(x, y);
        const int64_t next = static_cast<int64_t>(cell.terrain_height_q12) + delta_q12;
        commit_terrain_height(cell, static_cast<int32_t>(std::clamp<int64_t>(
            next, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max())),
            water_policy);
    }
}

Vec3f FieldGrid::normal_at(uint32_t x, uint32_t y, bool water) const {
    if (x >= width_ || y >= height_) throw std::out_of_range("terrain normal cell");
    const auto& center = at(x, y);
    const int64_t center_height_q12 = static_cast<int64_t>(center.terrain_height_q12) +
        (water ? static_cast<int64_t>(center.water_delta_q12) : 0LL);

    // game.exe FieldGridCell_RecomputeTriangleNormalAngles uses the six neighbours of the
    // triangular/axial FLD lattice, not a four-neighbour rectangular central difference. Its
    // terrain normal input is {-sum(dx*dz), -sum(dy*dz), 0x00C00000} in fixed-point space.
    // Boundary cells are not recomputed by the retail interior pass; clamping a missing neighbour
    // to the center gives it zero contribution and keeps preview/export queries well-defined.
    constexpr std::array<std::pair<int, int>, 6> kHexNeighbours{{
        {1, 0}, {0, 1}, {0, -1}, {-1, 0}, {-1, 1}, {1, -1},
    }};
    long double normal_x = 0.0L;
    long double normal_y = 0.0L;
    for (const auto& [dx_cell, dy_cell] : kHexNeighbours) {
        const int sx = static_cast<int>(x) + dx_cell;
        const int sy = static_cast<int>(y) + dy_cell;
        if (sx < 0 || sy < 0 || sx >= static_cast<int>(width_) || sy >= static_cast<int>(height_)) {
            continue;
        }
        const auto& neighbour = at(static_cast<uint32_t>(sx), static_cast<uint32_t>(sy));
        const int64_t neighbour_height_q12 = static_cast<int64_t>(neighbour.terrain_height_q12) +
            (water ? static_cast<int64_t>(neighbour.water_delta_q12) : 0LL);
        const long double dz = static_cast<long double>(neighbour_height_q12 - center_height_q12);
        const long double dx = static_cast<long double>(
            static_cast<int64_t>(neighbour.world_x_q12) - center.world_x_q12);
        const long double dy = static_cast<long double>(
            static_cast<int64_t>(neighbour.world_y_q12) - center.world_y_q12);
        normal_x -= dx * dz;
        normal_y -= dy * dz;
    }
    Vec3f normal{static_cast<float>(normal_x), static_cast<float>(normal_y),
                 static_cast<float>(0x00C00000)};
    return normalize(normal);
}

bool FieldGrid::runtime_interpolation_allowed(uint32_t cell_x, uint32_t cell_y) const noexcept {
    if (cell_x + 1U >= width_ || cell_y + 1U >= height_) return false;
    constexpr uint32_t kRuntimeInterpolationRejectMask = 0x88006000U;
    return (at(cell_x, cell_y).flags_and_material & kRuntimeInterpolationRejectMask) == 0U &&
           (at(cell_x + 1U, cell_y + 1U).flags_and_material &
            kRuntimeInterpolationRejectMask) == 0U;
}

bool FieldGrid::terrain_allows_traversal_at_world(float world_x, float world_y,
                                                    uint32_t high_band_index) const {
    if (high_band_index >= 7U) return false;
    const auto sample = grid_triangle_sample(*this, world_x, world_y);
    if (!sample.inside || !runtime_interpolation_allowed(sample.cell_x, sample.cell_y)) return false;
    return !terrain_forbidden_for_traversal_band(sample.cell_x, sample.cell_y, high_band_index);
}

TerrainSample FieldGrid::sample_terrain(float world_x, float world_y) const {
    const auto sample = grid_triangle_sample(*this, world_x, world_y);
    const auto& c00 = at(sample.cell_x, sample.cell_y);
    const auto& c10 = at(sample.cell_x + 1U, sample.cell_y);
    const auto& c01 = at(sample.cell_x, sample.cell_y + 1U);
    const auto& c11 = at(sample.cell_x + 1U, sample.cell_y + 1U);
    const float height = interpolate_cell_value(c00.terrain_height(), c10.terrain_height(),
                                                c01.terrain_height(), c11.terrain_height(), sample);
    const Vec3f n00 = normal_at(sample.cell_x, sample.cell_y, false);
    const Vec3f n10 = normal_at(sample.cell_x + 1U, sample.cell_y, false);
    const Vec3f n01 = normal_at(sample.cell_x, sample.cell_y + 1U, false);
    const Vec3f n11 = normal_at(sample.cell_x + 1U, sample.cell_y + 1U, false);
    const Vec3f normal = normalize({
        n00.x * sample.w00 + n10.x * sample.w10 + n01.x * sample.w01 + n11.x * sample.w11,
        n00.y * sample.w00 + n10.y * sample.w10 + n01.y * sample.w01 + n11.y * sample.w11,
        n00.z * sample.w00 + n10.z * sample.w10 + n01.z * sample.w01 + n11.z * sample.w11});
    return {height, normal, sample.grid_x, sample.grid_y,
            sample.inside && runtime_interpolation_allowed(sample.cell_x, sample.cell_y)};
}

float FieldGrid::sample_water_surface_height(float world_x, float world_y) const {
    const auto sample = grid_triangle_sample(*this, world_x, world_y);
    const auto& c00 = at(sample.cell_x, sample.cell_y);
    const auto& c10 = at(sample.cell_x + 1U, sample.cell_y);
    const auto& c01 = at(sample.cell_x, sample.cell_y + 1U);
    const auto& c11 = at(sample.cell_x + 1U, sample.cell_y + 1U);
    return interpolate_cell_value(c00.water_height(), c10.water_height(),
                                  c01.water_height(), c11.water_height(), sample);
}

RetailSurfaceHeightsQ12 FieldGrid::sample_retail_surface_heights_q12(int32_t world_x_q12,int32_t world_y_q12) const {
    const uint32_t half_row = static_cast<uint32_t>(
        (static_cast<int64_t>(world_y_q12) * -0x20C8CC) >> 21);
    const uint32_t column = static_cast<uint32_t>(
        (static_cast<int64_t>(world_x_q12) * 0x1C6E9C) >> 20) - half_row;
    const uint32_t row = half_row * 2U;
    const int32_t x = std::bit_cast<int32_t>(column) >> 12;
    const int32_t y = std::bit_cast<int32_t>(row) >> 12;
    // Valid retail FLD edges carry flags; also bound the four reads for editable fixtures.
    if (x < 0 || y < 0 || static_cast<uint32_t>(x) + 1U >= width_ ||
        static_cast<uint32_t>(y) + 1U >= height_) return {};
    const auto& c00 = at(static_cast<uint32_t>(x),static_cast<uint32_t>(y));
    const auto& c11 = at(static_cast<uint32_t>(x) + 1U,static_cast<uint32_t>(y) + 1U);
    if (((c00.flags_and_material | c11.flags_and_material) & 0x88006000U) != 0U) return {};
    const int64_t fx = column & 0xFFFU, fy = row & 0xFFFU;
    const int64_t diagonal = fx + fy - 0x1000;
    const auto& c10=at(static_cast<uint32_t>(x)+1U,static_cast<uint32_t>(y));
    const auto& c01=at(static_cast<uint32_t>(x),static_cast<uint32_t>(y)+1U);
    const auto interpolate=[&](int32_t a,int32_t b,int32_t c,int32_t d) {
        const int64_t sum=diagonal<0 ? int64_t(b)*fx+int64_t(c)*fy-int64_t(a)*diagonal
            : int64_t(d)*diagonal-int64_t(c)*(fx-4096)-int64_t(b)*(fy-4096);
        return std::bit_cast<int32_t>(static_cast<uint32_t>(sum>>12));
    };
    const auto add=[](int32_t a,int32_t b){return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));};
    RetailSurfaceHeightsQ12 out;out.valid=true;
    out.terrain=interpolate(c00.terrain_height_q12,c10.terrain_height_q12,c01.terrain_height_q12,c11.terrain_height_q12);
    out.water_delta=interpolate(c00.water_delta_q12,c10.water_delta_q12,c01.water_delta_q12,c11.water_delta_q12);
    out.water_surface=interpolate(add(c00.terrain_height_q12,c00.water_delta_q12),add(c10.terrain_height_q12,c10.water_delta_q12),add(c01.terrain_height_q12,c01.water_delta_q12),add(c11.terrain_height_q12,c11.water_delta_q12));
    out.top=add(out.terrain,std::max(0,out.water_delta));
    return out;
}
int32_t FieldGrid::sample_retail_water_delta_q12(int32_t x,int32_t y) const {
    return sample_retail_surface_heights_q12(x,y).water_delta;
}

int32_t FieldGrid::sample_water_delta_q12(float world_x, float world_y) const {
    const auto sample = grid_triangle_sample(*this, world_x, world_y);
    const auto& c00 = at(sample.cell_x, sample.cell_y);
    const auto& c10 = at(sample.cell_x + 1U, sample.cell_y);
    const auto& c01 = at(sample.cell_x, sample.cell_y + 1U);
    const auto& c11 = at(sample.cell_x + 1U, sample.cell_y + 1U);
    const float delta = interpolate_cell_value(q12_to_float(c00.water_delta_q12),
                                               q12_to_float(c10.water_delta_q12),
                                               q12_to_float(c01.water_delta_q12),
                                               q12_to_float(c11.water_delta_q12), sample);
    return float_to_q12(delta);
}

float FieldGrid::sample_top_surface_height(float world_x, float world_y) const {
    const auto sample = grid_triangle_sample(*this, world_x, world_y);
    const auto& c00 = at(sample.cell_x, sample.cell_y);
    const auto& c10 = at(sample.cell_x + 1U, sample.cell_y);
    const auto& c01 = at(sample.cell_x, sample.cell_y + 1U);
    const auto& c11 = at(sample.cell_x + 1U, sample.cell_y + 1U);
    const float terrain = interpolate_cell_value(c00.terrain_height(), c10.terrain_height(),
                                                 c01.terrain_height(), c11.terrain_height(), sample);
    const float water_delta = interpolate_cell_value(q12_to_float(c00.water_delta_q12),
        q12_to_float(c10.water_delta_q12), q12_to_float(c01.water_delta_q12),
        q12_to_float(c11.water_delta_q12), sample);
    return terrain + std::max(water_delta, 0.0F);
}

uint32_t FieldGrid::material_mask() const noexcept {
    uint32_t mask = 0;
    for (const auto& cell : cells_) {
        const uint8_t material = cell.material();
        if (material < 26) mask |= 1U << material;
    }
    return mask;
}

TerrainCellCompatibility FieldGrid::terrain_compatibility(uint32_t x, uint32_t y) const {
    if (x >= width_ || y >= height_) throw std::out_of_range("terrain compatibility cell");
    const auto& cell = at(x, y);
    TerrainCellCompatibility result;
    result.terrain_normal_elevation_turn16 = static_cast<uint16_t>(retail_placement_normal_angles(*this, x, y, false) >> 16);
    result.water_normal_elevation_turn16 = static_cast<uint16_t>(retail_placement_normal_angles(*this, x, y, true) >> 16);
    result.hard_boundary_or_blocker = (cell.flags_and_material & 0x88006000U) != 0U;
    result.water_covered = cell.water_delta_q12 > 0;

    // Immutable game.exe thresholds at 0x00536F10..0x00536F44. The original routine ORs
    // each class bit when either half of its paired condition is true.
    uint32_t bits = 0U;
    if (cell.water_delta_q12 <= 0 || result.water_normal_elevation_turn16 <= 11500U)
        bits |= 0x01000000U;
    const uint16_t selected_slope = cell.water_delta_q12 < 0
        ? result.terrain_normal_elevation_turn16 : result.water_normal_elevation_turn16;
    if (selected_slope <= 10500U) bits |= 0x02000000U | 0x04000000U | 0x08000000U;
    if (cell.water_delta_q12 >= 500 || result.terrain_normal_elevation_turn16 <= 10500U)
        bits |= 0x10000000U | 0x20000000U | 0x40000000U;
    if (result.hard_boundary_or_blocker) bits |= 0x80000000U;
    result.classification_bits = bits;
    return result;
}

bool FieldGrid::terrain_forbidden_for_traversal_band(uint32_t x, uint32_t y,
                                                      uint32_t high_band_index) const {
    if (high_band_index >= 7U) return true;
    const auto compatibility = terrain_compatibility(x, y);
    const uint32_t mask = 0x01000000U << high_band_index;
    return compatibility.hard_boundary_or_blocker ||
           (compatibility.classification_bits & mask) != 0U;
}

bool FieldGrid::placement_occupancy_class_present(uint32_t x, uint32_t y,
                                                   uint32_t occupancy_class) const {
    if (occupancy_class >= 21U) return false;
    const uint32_t mask = 0x00000800U << occupancy_class;
    return (at(x, y).flags_and_material & mask) != 0U;
}

RetailFieldMarkerAudit retail_field_marker_audit(
    uint32_t flags_and_material, uint32_t resource_bit_index) noexcept {
    RetailFieldMarkerAudit result;
    result.flags_and_material = flags_and_material;
    const uint32_t selected = 0x00000800U << (resource_bit_index & 1U);
    const uint32_t alternate = selected ^ 0x00001800U;
    result.selected_resource_support_mask = selected;
    result.alternate_resource_support_mask = alternate;
    result.projected_vertex_visible = (flags_and_material & 0x00200000U) == 0U;
    result.fluid_marker_gate = (flags_and_material & 0x04000000U) == 0U;
    result.fluid_receiver_excluded = result.fluid_marker_gate &&
        (flags_and_material & 0x20000000U) != 0U;
    result.fluid_source_excluded = result.fluid_marker_gate &&
        (flags_and_material & 0x40000000U) != 0U;
    if (result.projected_vertex_visible && (flags_and_material & 0x00001800U) != 0U) {
        result.selected_resource_support = (flags_and_material & selected) != 0U;
        result.alternate_resource_support = (flags_and_material & alternate) != 0U;
    }
    result.flag_8000_marker = result.projected_vertex_visible &&
        (flags_and_material & 0x00008000U) != 0U;
    return result;
}

ResourceSupportCellAudit FieldGrid::audit_resource_support_cell(
    uint32_t x, uint32_t y, uint32_t occupancy_class) const noexcept {
    ResourceSupportCellAudit result;
    result.required_support_mask = occupancy_class < 21U ? (0x00000800U << occupancy_class) : 0U;
    if (x >= width_ || y >= height_ || result.required_support_mask == 0U) return result;
    result.inside = true;
    result.flags_and_material = cells_[static_cast<std::size_t>(y) * width_ + x].flags_and_material;
    result.support_present = (result.flags_and_material & result.required_support_mask) != 0U;
    result.hard_blocked = (result.flags_and_material & 0x88006000U) != 0U;
    return result;
}

bool FieldGrid::terrain_rejects_placement_occupancy_class(uint32_t x, uint32_t y,
                                                          uint32_t occupancy_class) const {
    if (occupancy_class >= 21U || x >= width_ || y >= height_) return true;
    // ArmyPlacementCandidate_TestFieldOccupancyCf rejects the class-14 candidate unless its
    // center lies strictly inside the one-cell field border: 0 < x/y and x+1 < width/height.
    // This happens before the required 0x0800/0x1000 support-bit test.
    if (x == 0U || y == 0U || x + 1U >= width_ || y + 1U >= height_) return true;
    const auto& cell = at(x, y);
    // The class-14 resource support bit is a positive predicate: Xenite selector 0 requires
    // FLD 0x0800, Tritium selector 1 requires FLD 0x1000. Hard blockers still reject.
    return (cell.flags_and_material & 0x88006000U) != 0U ||
           !placement_occupancy_class_present(x, y, occupancy_class);
}

uint32_t FieldGrid::random_next_primary(uint32_t& state) noexcept {
    const uint32_t first = state * 33U + 101U;
    state = first * 33U + 101U;
    return (first << 14U) ^ (state >> 2U);
}

std::vector<uint8_t> FieldGrid::build_soil_random_groups(uint32_t seed) const {
    std::vector<uint8_t> groups(cells_.size(), 0U);
    uint32_t state = seed;
    for (std::size_t index = 0; index < groups.size(); ++index) {
        (void)random_next_primary(state);
        const uint32_t second = random_next_primary(state);
        groups[index] = static_cast<uint8_t>((second >> 8U) & 7U);
    }
    return groups;
}

std::vector<uint32_t> FieldGrid::build_surface_record_indices(uint32_t selector_mask,
                                                               uint32_t seed) const {
    std::vector<uint32_t> indices(cells_.size(), 0U);
    uint32_t state = seed;
    for (auto& index : indices) {
        index = random_next_primary(state) & selector_mask;
        (void)random_next_primary(state);
    }
    return indices;
}

std::array<TerrainDirectionRecordSeed, FieldGrid::kWaterWaveRecordCount>
FieldGrid::build_water_wave_records(uint32_t seed) noexcept {
    std::array<TerrainDirectionRecordSeed, kWaterWaveRecordCount> records{};
    uint32_t state = seed;
    for (auto& record : records) {
        const uint32_t xy_shape = random_next_primary(state);
        record.scale_a = static_cast<uint16_t>((xy_shape & 0x1FU) + 0x80U);
        int32_t xy_step = static_cast<int32_t>(((xy_shape >> 16U) & 0x7FU) + 0x200U);
        const uint32_t xy_phase_and_sign = random_next_primary(state);
        if (static_cast<int32_t>(xy_phase_and_sign) < 0) xy_step = -xy_step;
        record.rate_a = static_cast<int16_t>(xy_step);
        record.angle_a = static_cast<uint16_t>(xy_phase_and_sign);

        const uint32_t z_shape = random_next_primary(state);
        record.scale_b = static_cast<uint16_t>((z_shape & 0x1FU) + 0x80U);
        int32_t z_step = static_cast<int32_t>(((z_shape >> 16U) & 0x7FU) + 0x200U);
        const uint32_t z_phase_and_sign = random_next_primary(state);
        if (static_cast<int32_t>(z_phase_and_sign) < 0) z_step = -z_step;
        record.rate_b = static_cast<int16_t>(z_step);
        record.angle_b = static_cast<uint16_t>(z_phase_and_sign);
    }
    return records;
}

uint32_t FieldGrid::water_wave_record_index(int32_t world_x_q12,
                                             int32_t world_y_q12) noexcept {
    return (static_cast<uint32_t>(world_x_q12) & 0x0FU) * 16U +
           (static_cast<uint32_t>(world_y_q12) & 0x0FU);
}

std::vector<TerrainVertex> FieldGrid::build_vertices(
    const TerrainLighting& lighting, float texture_repeat_cells,
    TerrainMaterialPreviewMode preview_mode, const TerrainTextureLayout* texture_layout,
    uint32_t soil_random_seed) const {
    if (!std::isfinite(texture_repeat_cells) || texture_repeat_cells <= 0.0F) {
        throw FormatError("terrain preview texture repeat must be positive");
    }
    const auto ramp = lighting.build_game_ramp();
    std::vector<Vec3f> normals(cells_.size());
    std::vector<uint32_t> packed_lights(cells_.size());
    for (uint32_t y = 0; y < height_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * width_ + x;
            normals[index] = normal_at(x, y, false);
            packed_lights[index] =
                lighting.directional_color_for_upward_normal(normals[index], ramp);
        }
    }

    const auto random_groups = build_soil_random_groups(soil_random_seed);
    std::vector<TerrainVertex> vertices;
    // The exact selector queues one to three packets per terrain triangle.
    vertices.reserve(static_cast<std::size_t>(width_ - 1U) * (height_ - 1U) * 18U);
    constexpr float kUploadedTerrainTextureExtent = 256.0F;
    const float inverse_repeat = 1.0F / texture_repeat_cells;

    const auto append_packet = [&](const std::array<std::pair<uint32_t, uint32_t>, 3>& coordinates,
                                   const std::array<uint8_t, 3>& materials,
                                   const std::array<uint8_t, 3>& groups,
                                   uint8_t source_vertex, uint8_t variant) {
        const uint8_t material = materials[source_vertex];
        const uint8_t group = groups[source_vertex];
        const TerrainTextureRecord* record = nullptr;
        if (texture_layout != nullptr && texture_layout->has_material(material)) {
            record = &texture_layout->record(material, group, variant);
        }
        for (std::size_t vertex = 0; vertex < coordinates.size(); ++vertex) {
            const uint32_t x = coordinates[vertex].first;
            const uint32_t y = coordinates[vertex].second;
            const auto& cell = at(x, y);
            const std::size_t cell_index = static_cast<std::size_t>(y) * width_ + x;
            float u = static_cast<float>(x) * inverse_repeat;
            float v = static_cast<float>(y) * inverse_repeat;
            if (record != nullptr) {
                u = q12_to_float(record->uv_q12[vertex * 2U]) /
                    kUploadedTerrainTextureExtent;
                v = q12_to_float(record->uv_q12[vertex * 2U + 1U]) /
                    kUploadedTerrainTextureExtent;
            }
            vertices.push_back({
                {q12_to_float(cell.world_x_q12), q12_to_float(cell.world_y_q12),
                 cell.terrain_height()},
                normals[cell_index], u, v, material, packed_lights[cell_index],
                {0U, 0U, 0U}, {1.0F, 0.0F, 0.0F, 0.0F},
                static_cast<uint32_t>(cell_index)});
        }
    };

    const auto append_triangle = [&](const std::array<std::pair<uint32_t, uint32_t>, 3>& coordinates) {
        std::array<uint8_t, 3> materials{};
        std::array<uint8_t, 3> groups{};
        for (std::size_t vertex = 0; vertex < coordinates.size(); ++vertex) {
            const auto [x, y] = coordinates[vertex];
            const std::size_t index = static_cast<std::size_t>(y) * width_ + x;
            materials[vertex] = at(x, y).material();
            groups[vertex] = random_groups[index];
        }
        const auto selection = select_terrain_soil_packets(materials);
        const uint8_t packet_count =
            preview_mode == TerrainMaterialPreviewMode::DirectCellMaterial || texture_layout == nullptr
            ? 1U : selection.count;
        for (uint8_t packet = 0U; packet < packet_count; ++packet) {
            append_packet(coordinates, materials, groups,
                          selection.choices[packet].source_vertex,
                          selection.choices[packet].variant);
        }
    };

    // game.exe 0x00500CE0 uses the BL-to-TR diagonal and this exact m0/m1/m2 order.
    for (uint32_t y = 0; y + 1U < height_; ++y) {
        for (uint32_t x = 0; x + 1U < width_; ++x) {
            append_triangle({{{x, y}, {x, y + 1U}, {x + 1U, y}}});
            append_triangle({{{x, y + 1U}, {x + 1U, y + 1U}, {x + 1U, y}}});
        }
    }
    return vertices;
}

std::vector<uint32_t> FieldGrid::build_triangle_indices() const {
    const std::size_t vertex_count = static_cast<std::size_t>(width_ - 1) * (height_ - 1) * 6;
    if (vertex_count > std::numeric_limits<uint32_t>::max()) {
        throw FormatError("terrain preview mesh exceeds 32-bit index range");
    }
    std::vector<uint32_t> indices(vertex_count);
    for (std::size_t index = 0; index < vertex_count; ++index) {
        indices[index] = static_cast<uint32_t>(index);
    }
    return indices;
}

std::vector<TerrainVertex> FieldGrid::build_water_vertices(
    const TerrainSurfaceTextureLayout* surface_layout, const TerrainLighting* lighting,
    uint32_t water_wave_seed,
    uint32_t surface_record_seed) const {
    constexpr float kUploadedSurfaceTextureExtent = 256.0F;
    const uint32_t selector_mask = surface_layout != nullptr
        ? surface_layout->selector_mask() : 0U;
    const auto surface_indices = build_surface_record_indices(selector_mask, surface_record_seed);
    const auto wave_records = build_water_wave_records(water_wave_seed);

    std::vector<TerrainVertex> vertices;
    vertices.reserve(static_cast<std::size_t>(width_ - 1U) * (height_ - 1U) * 6U);

    const auto append_triangle = [&](
        uint32_t anchor_x, uint32_t anchor_y,
        const std::array<std::pair<uint32_t, uint32_t>, 3>& coordinates) {
        bool has_positive_secondary_depth = false;
        for (const auto& [x, y] : coordinates) {
            if (at(x, y).water_delta_q12 > 0) {
                has_positive_secondary_depth = true;
                break;
            }
        }
        if (!has_positive_secondary_depth) return;

        // Retail projects the secondary surface from each source FLD vertex independently.
        // Mixed wet/dry triangles therefore retain every vertex's signed secondary depth; dry
        // corners are not replaced by a mean wet plane. The packet is emitted when any of the
        // triangle's secondary depths is positive, matching the recovered queue boundary.

        const std::size_t anchor_index = static_cast<std::size_t>(anchor_y) * width_ + anchor_x;
        const TerrainTextureRecord* record = nullptr;
        if (surface_layout != nullptr) {
            record = &surface_layout->record(surface_indices[anchor_index]);
        }
        for (std::size_t vertex_index = 0; vertex_index < coordinates.size(); ++vertex_index) {
            const auto [x, y] = coordinates[vertex_index];
            const auto& cell = at(x, y);
            const auto& wave = wave_records[water_wave_record_index(
                cell.world_x_q12, cell.world_y_q12)];
            float u = static_cast<float>(x);
            float v = static_cast<float>(y);
            uint32_t texture_layer = 0U;
            if (record != nullptr) {
                u = q12_to_float(record->uv_q12[vertex_index * 2U]) /
                    kUploadedSurfaceTextureExtent;
                v = q12_to_float(record->uv_q12[vertex_index * 2U + 1U]) /
                    kUploadedSurfaceTextureExtent;
                texture_layer = record->material_index;
            }
            // WaterVertex_ProjectDisplacedSurface copies the secondary LEV lighting colour
            // (runtime visual state +0x12c, LEV +0x2f8) into the projected water vertex. Its
            // alpha is the stock water opacity. G347 instead forced opaque terrain-ramp colours,
            // which removed the transparency visible in game.exe.
            uint32_t vertex_tint = lighting != nullptr ? lighting->ramp_12c : 0xFFFFFFFFU;
            const uint32_t packed_phases = static_cast<uint32_t>(wave.angle_a) |
                                           (static_cast<uint32_t>(wave.angle_b) << 16U);
            const uint32_t packed_steps = static_cast<uint32_t>(
                static_cast<uint16_t>(wave.rate_a)) |
                (static_cast<uint32_t>(static_cast<uint16_t>(wave.rate_b)) << 16U);
            const uint32_t packed_amplitudes = static_cast<uint32_t>(wave.scale_a) |
                                               (static_cast<uint32_t>(wave.scale_b) << 16U);
            const Vec3f water_normal = normal_at(x, y, true);
            std::array<float, 4> encoded_lighting{1.0F, 1.0F, 1.0F, 1.0F};
            if (lighting != nullptr) {
                const auto light_color = unpack_argb(vertex_tint);
                encoded_lighting = {light_color.r, light_color.g, light_color.b, light_color.a};
            }
            const int32_t rendered_surface_q12 =
                cell.terrain_height_q12 + cell.water_delta_q12;
            vertices.push_back({
                {q12_to_float(cell.world_x_q12), q12_to_float(cell.world_y_q12),
                 q12_to_float(rendered_surface_q12)},
                water_normal, u, v, texture_layer, vertex_tint,
                {packed_phases, packed_steps, packed_amplitudes}, encoded_lighting,
                static_cast<uint32_t>(static_cast<std::size_t>(y) * width_ + x)});
        }
    };

    // The immutable field-grid draw owner at 0x00500CE0 uses the same BL-to-TR split as terrain.
    // The current semantic export does not expose a callable named FieldGrid_DrawCellTriangles,
    // so the address and triangle order are cited without inventing a function identity.
    // Both triangles use the top-left cell's primary-surface DAT selector.
    for (uint32_t y = 0; y + 1U < height_; ++y) {
        for (uint32_t x = 0; x + 1U < width_; ++x) {
            append_triangle(x, y, {{{x, y}, {x, y + 1U}, {x + 1U, y}}});
            append_triangle(x, y, {{{x, y + 1U}, {x + 1U, y + 1U}, {x + 1U, y}}});
        }
    }
    return vertices;
}

std::array<TerrainDirectionRecordSeed, FieldGrid::kSurfaceDirectionRecordCount>
FieldGrid::build_surface_wave_records(uint32_t seed) noexcept {
    return build_water_wave_records(seed);
}

uint32_t FieldGrid::surface_wave_record_index(int32_t world_x_q12,
                                               int32_t world_y_q12) noexcept {
    return water_wave_record_index(world_x_q12, world_y_q12);
}

std::vector<TerrainVertex> FieldGrid::build_animated_surface_vertices(
    const TerrainSurfaceTextureLayout* surface_layout, const TerrainLighting* lighting,
    uint32_t surface_wave_seed, uint32_t surface_record_seed) const {
    return build_water_vertices(surface_layout, lighting, surface_wave_seed, surface_record_seed);
}

std::vector<uint32_t> FieldGrid::build_animated_surface_indices(
    const TerrainSurfaceTextureLayout* surface_layout, uint32_t surface_record_seed) const {
    return build_water_indices(surface_layout, surface_record_seed);
}

std::vector<uint32_t> FieldGrid::build_water_indices(
    const TerrainSurfaceTextureLayout* surface_layout, uint32_t surface_record_seed) const {
    const auto vertices = build_water_vertices(surface_layout, nullptr,
                                                kDefaultWaterWaveRandomSeed,
                                                surface_record_seed);
    if (vertices.size() > std::numeric_limits<uint32_t>::max()) {
        throw FormatError("water preview mesh exceeds 32-bit index range");
    }
    std::vector<uint32_t> indices(vertices.size());
    for (std::size_t index = 0; index < indices.size(); ++index) {
        indices[index] = static_cast<uint32_t>(index);
    }
    return indices;
}

void FieldGrid::set_source_path(std::string_view path) {
    if (header_.size() != kHeaderSize) header_.resize(kHeaderSize);
    write_utf16le_fixed(header_, 0x100, 0x100, path);
}

} // namespace thandor
