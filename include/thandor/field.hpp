#pragma once

#include "thandor/binary.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace thandor {

class TerrainTextureLayout;
class TerrainSurfaceTextureLayout;

struct Vec3f { float x{}, y{}, z{}; };
struct Color4f { float r{}, g{}, b{}, a{1.0F}; };

struct FieldCell {
    int32_t world_x_q12{};
    int32_t world_y_q12{};
    int32_t terrain_height_q12{};
    int32_t water_delta_q12{};
    uint32_t flags_and_material{};
    // Native load recomputes interior normals only. Border records retain these
    // FLD +0x08/+0x78 values; new maps use this serializer's native save seeds.
    uint32_t loaded_terrain_normal_angles{0x40000000U};
    uint32_t loaded_water_normal_angles{};

    uint8_t material() const noexcept { return static_cast<uint8_t>(flags_and_material & 0xFFU); }
    void set_material(uint8_t value) noexcept {
        flags_and_material = (flags_and_material & 0xFFFFFF00U) | value;
    }
    float terrain_height() const noexcept { return q12_to_float(terrain_height_q12); }
    float water_height() const noexcept { return q12_to_float(terrain_height_q12 + water_delta_q12); }
};

enum class TerrainMaterialPreviewMode {
    // Diagnostic mode: render only the exact variant-0 base packet selected from vertex m0.
    DirectCellMaterial,
    // Game-facing mode: queue the exact soil.dat base/transition packets recovered from game.exe.
    ExactSoilDatPackets,
    // Compatibility spelling retained for older callers; G339 supersedes the G338 blend.
    GameTransitionBlend = ExactSoilDatPackets,
    // Historical diagnostic only. The exact packet path is the normal renderer.
    SmoothEditorBlend,
};

struct TerrainVertex {
    Vec3f position;
    Vec3f normal;
    float u{};
    float v{};
    uint32_t material{};
    uint32_t packed_light_argb{};
    std::array<uint32_t, 3> blend_materials{};
    std::array<float, 4> blend_weights{1.0F, 0.0F, 0.0F, 0.0F};
    // Source FLD vertex/cell index. Runtime fog is cell state, not a coplanar overlay mesh;
    // terrain and animated primary-surface shaders use this index to fetch the live visibility byte.
    uint32_t fog_cell_index{};
};


struct TerrainDirectionRecordSeed {
    // Runtime TerrainDirectionRecord +0x0C packs angle A in the low word and angle B in the
    // high word. The former xy/z names were renderer heuristics, not binary field semantics.
    uint16_t angle_a{};
    uint16_t angle_b{};
    int16_t rate_a{};
    int16_t rate_b{};
    uint16_t scale_a{};
    uint16_t scale_b{};
};

struct RetailSurfaceHeightsQ12 {
    bool valid{};
    int32_t terrain{},water_delta{},water_surface{},top{};
};

struct TerrainSample {
    float height{};
    Vec3f normal{0.0F, 0.0F, 1.0F};
    float grid_x{};
    float grid_y{};
    bool inside{};
};

// A sampled light is constant throughout a scene build. Keep its trigonometric
// direction outside the vertex loop; normal normalization and LUT rounding stay
// identical to TerrainLighting's scalar entry points.
struct PreparedDirectionalLight {
    Vec3f ray_direction{};
    uint32_t base_color{};
    int32_t signed_lut_index(Vec3f upward_normal) const;
    uint32_t color_for_upward_normal(Vec3f upward_normal,
        const std::array<uint32_t, 256>& ramp) const;
};

struct TerrainLighting {
    uint32_t color_13c{};
    uint32_t color_138{};
    uint32_t color_134{};
    uint32_t color_130{};
    uint32_t ramp_12c{};
    uint32_t color_128{};
    uint32_t ramp_124{};
    uint32_t base_color{};
    // V537 LevelAssetRuntimeTail90: LEV +0x2E0 is the packed pair consumed by
    // WorldRuntime_RecomputeFieldRegionNormalsAndLighting. Its low/high words become the
    // directional-light azimuth/elevation inputs. LEV +0x2F0 is a separate packed
    // field-region width/height pair and must not be used as light direction.
    uint16_t azimuth{};
    uint16_t elevation{};
    uint16_t field_region_width{};
    uint16_t field_region_height{};
    uint32_t cycle_duration_ticks{};

    // V537 runtime-tail alternate lighting state at LEV +0x328..+0x34C. The executable
    // periodically interpolates between the primary and alternate sets when cycle_duration_ticks
    // is non-zero. The alternate packed origin is consumed in the same angle order.
    uint16_t alternate_azimuth{};
    uint16_t alternate_elevation{};
    uint16_t alternate_field_region_width{};
    uint16_t alternate_field_region_height{};
    uint32_t alternate_color_13c{};
    uint32_t alternate_color_138{};
    uint32_t alternate_color_134{};
    uint32_t alternate_color_130{};
    uint32_t alternate_ramp_12c{};
    uint32_t alternate_color_128{};
    uint32_t alternate_ramp_124{};
    uint32_t alternate_base_color{};

    TerrainLighting sampled_at_tick(uint32_t simulation_tick) const noexcept;

    // A freshly initialized game field stores 0xFFFFFFFF at cell +0x04. That is the
    // fully-visible/no-fog overlay multiplier consumed by the DirectDraw lighting path.
    static constexpr uint32_t kFullyVisibleOverlayArgb = 0xFFFFFFFFU;

    std::array<uint32_t, 256> build_game_ramp() const;
    Vec3f shadow_ray_direction() const;
    PreparedDirectionalLight prepare_directional_light() const;
    int32_t signed_directional_lut_index(Vec3f upward_normal) const;
    uint32_t directional_color_for_upward_normal(
        Vec3f upward_normal, const std::array<uint32_t, 256>& ramp) const;
};

enum class BrushShape { Circle, Square };
enum class BrushFalloff { Constant, Linear, Smooth };

// FLD stores water as a terrain-relative delta. Terrain tools must state whether that
// relative value, the resulting absolute surface, or the wet terrain itself is preserved.
enum class TerrainWaterEditPolicy {
    PreserveAbsoluteSurface,
    PreserveWaterDepth,
    IgnoreWetCells,
};

// game.exe rebuilds seven high terrain-classification bits from water-surface delta and
// packed terrain/water normal elevation angles. Moving-unit MDL records select one of these bits at
// +0x264 as their forbidden terrain band. Structures additionally select one field-cell
// occupancy bit at MDL +0x0C0. The editor exposes these checks as compatibility, not as
// unproven periodic damage.
struct TerrainCellCompatibility {
    uint32_t classification_bits{};
    uint16_t terrain_normal_elevation_turn16{};
    uint16_t water_normal_elevation_turn16{};
    bool hard_boundary_or_blocker{};
    bool water_covered{};
};

// Machine-verified FLD support-state audit for resource-extractor placement. Retail uses
// 0x0800 << occupancy_class as the positive support predicate and treats 0x88006000 as a
// hard placement blocker. Keep this distinct from editor paint/stamp policy: the runtime
// consumes the cell bits themselves, not a particular brush footprint.
struct ResourceSupportCellAudit {
    uint32_t flags_and_material{};
    uint32_t required_support_mask{};
    bool inside{};
    bool support_present{};
    bool hard_blocked{};

    bool valid() const noexcept { return inside && support_present && !hard_blocked; }
};

// Machine-verified predicates used by the retail selection/debug overlay family at
// 0x0052F680..0x0052FA20. These classify FLD state only; the Vulkan editor deliberately uses
// modern diagnostic glyphs instead of claiming pixel-identical AEx/Bx sprite presentation.
struct RetailFieldMarkerAudit {
    uint32_t flags_and_material{};
    uint32_t selected_resource_support_mask{};
    uint32_t alternate_resource_support_mask{};
    bool projected_vertex_visible{};        // AE path: clear when 0x00200000 is set.
    bool fluid_marker_gate{};               // AF/B0 path: clear when 0x04000000 is set.
    bool fluid_receiver_excluded{};         // AF path: 0x20000000; uses projectedPointB.
    bool fluid_source_excluded{};           // B0 path: 0x40000000; uses projectedPointB.
    bool selected_resource_support{};       // B1 path: selected 0x0800/0x1000; projectedPointA.
    bool alternate_resource_support{};      // B2 path: opposite 0x0800/0x1000; projectedPointA.
    bool flag_8000_marker{};                 // AF path at 0x0052FA20; projectedPointA.
};

RetailFieldMarkerAudit retail_field_marker_audit(uint32_t flags_and_material,
                                                  uint32_t resource_bit_index) noexcept;

const char* terrain_water_edit_policy_name(TerrainWaterEditPolicy policy) noexcept;

struct Brush {
    float radius_cells{3.0F};
    float strength{1.0F};
    BrushShape shape{BrushShape::Circle};
    BrushFalloff falloff{BrushFalloff::Smooth};
};

class FieldGrid {
public:
    // The immutable game accepts FLD converter version 0x00060006. The older editor
    // accidentally emitted the LEV converter version 0x00070001 into fresh FLDs.
    static constexpr uint32_t kGameConverterVersion = 0x00060006U;
    static constexpr uint32_t kLegacyEditorConverterVersion = 0x00070001U;
    static constexpr uint32_t kGameMinimumDimension = 59U;
    static constexpr uint32_t kGameMaximumDimension = 139U;
    static constexpr uint32_t kGameMaximumProvenArea = 131U * 131U;
    // game.exe treats waterSurfaceDelta >= 0 as water in construction/traversal tests.
    // A dry FLD cell must therefore use a strictly negative terrain-relative delta.
    static constexpr int32_t kDryWaterDeltaQ12 = -0x1000;
    // Retail FieldGrid_SaveAssetImageFromRuntimeStateCf sanitizes each 0x80-byte
    // cell before writing: only these flags/material bits survive serialization.
    static constexpr uint32_t kRetailPersistedFlagsAndMaterialMask = 0xE80078FFU;
    static constexpr uint32_t kRetailSerializedTriangle0NormalSeed = 0x40000000U;
    static constexpr uint32_t kDefaultSoilRandomSeed = 0x0000198FU;
    static constexpr uint32_t kDefaultWaterWaveRandomSeed = 0x0000198FU;
    static constexpr std::size_t kWaterWaveRecordCount = 256U;
    static constexpr uint32_t kDefaultSurfaceWaveRandomSeed = kDefaultWaterWaveRandomSeed;
    static constexpr std::size_t kSurfaceDirectionRecordCount = kWaterWaveRecordCount;

    static FieldGrid parse(std::span<const std::byte> data);
    static FieldGrid create(uint32_t width, uint32_t height, int32_t initial_height_q12 = 0,
                            uint8_t initial_material = 0);

    uint32_t width() const noexcept { return width_; }
    uint32_t height() const noexcept { return height_; }
    uint32_t converter_version() const noexcept;
    void capture_runtime_normals();
    void refresh_runtime_normals();
    void apply_runtime_terrain_impact(int32_t x_q12, int32_t y_q12, int32_t radius_q12,
                                      int32_t height_amplitude_q12, int32_t material);
    bool has_runtime_normals() const noexcept { return runtime_normals_captured_; }
    static bool is_game_compatible_dimension(uint32_t value) noexcept;
    static bool is_game_compatible_shape(uint32_t width, uint32_t height) noexcept;
    bool has_game_compatible_dimensions() const noexcept;
    void normalize_for_game_export();
    uint32_t material_mask() const noexcept;
    std::span<FieldCell> cells() noexcept { return cells_; }
    std::span<const FieldCell> cells() const noexcept { return cells_; }
    FieldCell& at(uint32_t x, uint32_t y);
    const FieldCell& at(uint32_t x, uint32_t y) const;

    std::vector<std::byte> serialize() const;
    void resize(uint32_t new_width, uint32_t new_height, bool center_existing = true);

    void raise_lower(float center_x, float center_y, const Brush& brush, float delta_world,
                     TerrainWaterEditPolicy water_policy = TerrainWaterEditPolicy::PreserveAbsoluteSurface);
    void flatten(float center_x, float center_y, const Brush& brush, float target_world,
                 TerrainWaterEditPolicy water_policy = TerrainWaterEditPolicy::PreserveAbsoluteSurface);
    void smooth(float center_x, float center_y, const Brush& brush, unsigned iterations = 1,
                TerrainWaterEditPolicy water_policy = TerrainWaterEditPolicy::PreserveAbsoluteSurface);
    void paint_material(float center_x, float center_y, const Brush& brush, uint8_t material);
    void set_water(float center_x, float center_y, const Brush& brush,
                   float absolute_water_world, bool enabled);
    // Applies one absolute water plane to an explicit editor selection. The mask is one byte per
    // persisted FLD vertex. Every enabled cell receives the same terrain+delta result, so painted
    // water cannot inherit the brush falloff and become a hill.
    std::size_t set_water_selection(std::span<const uint8_t> selection,
                                    float absolute_water_world, bool enabled);
    // Structure placement class N requires FLD bit (0x800 << N). This is positive
    // support metadata, not a rejection flag. The stock editor paints these masks
    // independently from the visible resource ARM objects.
    std::size_t paint_placement_occupancy_class(float center_x, float center_y,
                                                const Brush& brush,
                                                uint32_t occupancy_class, bool enabled);

    // LEV placement records persist X, Y, and one yaw value but no independent Z coordinate.
    // The editor's blue Z gizmo therefore translates the four FLD support vertices under the
    // selected terrain-bound object so the height change is visible in the stock game as well.
    void offset_terrain_anchor(float world_x, float world_y, float delta_world,
                               TerrainWaterEditPolicy water_policy = TerrainWaterEditPolicy::PreserveAbsoluteSurface);

    TerrainSample sample_terrain(float world_x, float world_y) const;
    // Retail terrain/water interpolation rejects a quad when either diagonal endpoint used by
    // the runtime wrapper (c00 or c11) carries any 0x88006000 hard-boundary bit.  Keep this
    // separate from the seven traversal bands: it is the admission gate shared by height, water
    // and height+normal queries.
    bool runtime_interpolation_allowed(uint32_t cell_x, uint32_t cell_y) const noexcept;
    bool terrain_allows_traversal_at_world(float world_x, float world_y,
                                           uint32_t high_band_index) const;
    // Placement-contact callbacks interpolate the water plane from terrain+water-delta and
    // the top surface from terrain+max(interpolated water delta, 0).
    float sample_water_surface_height(float world_x, float world_y) const;
    int32_t sample_water_delta_q12(float world_x, float world_y) const;
    // Retail 0x004FED50: fixed projection/triangle arithmetic, zero outside or on edge cells.
    int32_t sample_retail_water_delta_q12(int32_t world_x_q12, int32_t world_y_q12) const;
    RetailSurfaceHeightsQ12 sample_retail_surface_heights_q12(int32_t world_x_q12,int32_t world_y_q12) const;
    float sample_top_surface_height(float world_x, float world_y) const;
    TerrainCellCompatibility terrain_compatibility(uint32_t x, uint32_t y) const;
    bool terrain_forbidden_for_traversal_band(uint32_t x, uint32_t y,
                                              uint32_t high_band_index) const;
    bool placement_occupancy_class_present(uint32_t x, uint32_t y,
                                             uint32_t occupancy_class) const;
    bool terrain_rejects_placement_occupancy_class(uint32_t x, uint32_t y,
                                                    uint32_t occupancy_class) const;
    ResourceSupportCellAudit audit_resource_support_cell(uint32_t x, uint32_t y,
                                                        uint32_t occupancy_class) const noexcept;

    // Reproduces Random_NextPrimary at game.exe 0x004030B0 using unsigned 32-bit wraparound.
    static uint32_t random_next_primary(uint32_t& state) noexcept;
    // Terrain initialization consumes two primary RNG values per FLD vertex and stores bits 8..10
    // of the second result as the soil.dat random-group selector.
    std::vector<uint8_t> build_soil_random_groups(
        uint32_t seed = kDefaultSoilRandomSeed) const;
    // Primary-surface record selection uses the low DAT-header bit count and the first of the
    // same two per-cell RNG values at game.exe 0x00503CEE..0x00503D1C.
    std::vector<uint32_t> build_surface_record_indices(
        uint32_t selector_mask, uint32_t seed = kDefaultSoilRandomSeed) const;
    // game.exe 0x005043A0..0x0050441D creates 256 phase/amplitude/step records.
    static std::array<TerrainDirectionRecordSeed, kWaterWaveRecordCount>
        build_water_wave_records(uint32_t seed = kDefaultWaterWaveRandomSeed) noexcept;
    // Runtime cells select one of those records from the low nibbles of their world X/Y Q12
    // coordinates: ((x & 15) << 4) + (y & 15).
    static uint32_t water_wave_record_index(int32_t world_x_q12,
                                            int32_t world_y_q12) noexcept;
    static std::array<TerrainDirectionRecordSeed, kSurfaceDirectionRecordCount>
        build_surface_wave_records(uint32_t seed = kDefaultSurfaceWaveRandomSeed) noexcept;
    static uint32_t surface_wave_record_index(int32_t world_x_q12,
                                              int32_t world_y_q12) noexcept;

    std::vector<TerrainVertex> build_vertices(
        const TerrainLighting& lighting, float texture_repeat_cells = 2.0F,
        TerrainMaterialPreviewMode preview_mode = TerrainMaterialPreviewMode::ExactSoilDatPackets,
        const TerrainTextureLayout* texture_layout = nullptr,
        uint32_t soil_random_seed = kDefaultSoilRandomSeed) const;
    std::vector<uint32_t> build_triangle_indices() const;
    std::vector<TerrainVertex> build_water_vertices(
        const TerrainSurfaceTextureLayout* surface_layout = nullptr,
        const TerrainLighting* lighting = nullptr,
        uint32_t water_wave_seed = kDefaultWaterWaveRandomSeed,
        uint32_t surface_record_seed = kDefaultSoilRandomSeed) const;
    std::vector<uint32_t> build_water_indices(
        const TerrainSurfaceTextureLayout* surface_layout = nullptr,
        uint32_t surface_record_seed = kDefaultSoilRandomSeed) const;
    std::vector<TerrainVertex> build_animated_surface_vertices(
        const TerrainSurfaceTextureLayout* surface_layout = nullptr,
        const TerrainLighting* lighting = nullptr,
        uint32_t surface_wave_seed = kDefaultSurfaceWaveRandomSeed,
        uint32_t surface_record_seed = kDefaultSoilRandomSeed) const;
    std::vector<uint32_t> build_animated_surface_indices(
        const TerrainSurfaceTextureLayout* surface_layout = nullptr,
        uint32_t surface_record_seed = kDefaultSoilRandomSeed) const;

    std::span<const std::byte> header_template() const noexcept { return header_; }
    void set_source_path(std::string_view path);

private:
    bool runtime_normals_captured_{};
    uint32_t width_{};
    uint32_t height_{};
    std::vector<std::byte> header_;
    std::vector<FieldCell> cells_;

    void regenerate_coordinates();
    float brush_weight(float dx, float dy, const Brush& brush) const;
    Vec3f normal_at(uint32_t x, uint32_t y, bool water) const;
};

Color4f unpack_argb(uint32_t value);
uint32_t pack_argb(Color4f value);
// Terrain lighting colors are RGB values in stock LEV files; their high byte is commonly zero.
// Object preview modulation must therefore force opaque alpha instead of multiplying that zero.
uint32_t multiply_rgb_opaque(uint32_t first, uint32_t second);

} // namespace thandor
