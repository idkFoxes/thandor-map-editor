#pragma once
#include "thandor/field.hpp"
#include <span>

namespace thandor {
struct TerrainRegionPoint { float x{},y{}; }; // Fractional FLD grid coordinates, independent of the camera.
enum class TerrainRegionCombine { Replace, Add, Subtract };
enum class TerrainGeneratorPreset { Hills, Mountains, Lake, River, Depression, Plateau, Plains };
enum class TerrainNoisePattern { Classic, DomainWarped, Cellular };
struct TerrainGeneratorSettings {
    TerrainGeneratorPreset preset{TerrainGeneratorPreset::Hills};
    TerrainNoisePattern pattern{TerrainNoisePattern::Classic};
    uint32_t seed{20261027U};
    float height{2.0F};
    float base_height{};
    float feature_size_cells{14.0F};
    float warp_strength{0.75F}; // Displacement in feature-size units.
    unsigned terrace_steps{}; // Zero disables terraces; otherwise 2..32 height steps.
    float blend_cells{5.0F};
    bool build_on_existing{true};
    bool outside{};
    bool keep_water{true};
    bool connected_building_areas{true}; // Whole-map land generation: reserve pads and connecting ground routes.
    bool paint_material{};
    uint8_t material{};
    float water_level{};
    float river_width_cells{6.0F};
    float river_bend{0.4F};
    float river_angle_degrees{}; // 0 follows grid X; 90 follows grid Y.
    bool drivable_river{true};
    float shallow_depth{0.08F}; // Native ground bands 4..6 reject water delta >= 500 Q12.
    unsigned ford_count{};
    bool use_river_path{};
    std::vector<TerrainRegionPoint> river_path; // Open centerline in FLD grid coordinates.
};
struct TerrainGenerationResult {
    FieldGrid field;
    std::vector<float> weights;
    std::vector<uint8_t> blocked_river_cells;
    std::size_t changed_cells{};
    std::size_t blocked_river_count{};
    std::size_t river_cells{};
    std::size_t river_water_cells{};
    float applied_water_level{};
    bool water_level_lowered{};
    bool playability_checked{};
    bool playability_ok{};
    std::size_t traversable_cells{};
    std::size_t reachable_cells{};
    std::size_t connected_building_sites{};
    std::vector<TerrainRegionPoint> building_sites;
    std::vector<std::vector<TerrainRegionPoint>> protected_approaches;
    std::size_t connected_protected_areas{};
};
std::vector<uint8_t> terrain_polygon_mask(uint32_t width,uint32_t height,std::span<const TerrainRegionPoint> polygon);
void combine_terrain_region(std::vector<uint8_t>& selection,std::span<const uint8_t> shape,TerrainRegionCombine combine);
std::vector<float> terrain_region_weights(const FieldGrid& source,std::span<const uint8_t> selection,
    const TerrainGeneratorSettings& settings,std::span<const uint8_t> protected_cells={});
TerrainGenerationResult generate_terrain_region(const FieldGrid& source,std::span<const uint8_t> selection,
    const TerrainGeneratorSettings& settings,std::span<const uint8_t> protected_cells={});
uint32_t next_terrain_seed(uint32_t seed) noexcept;
} // namespace thandor
