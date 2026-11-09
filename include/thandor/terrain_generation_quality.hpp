#pragma once
#include "thandor/terrain_generator.hpp"

namespace thandor {
// Align hidden dry-corner water heights with adjoining wet planes. The renderer interpolates
// all three signed secondary heights, including dry corners; arbitrary -1 depths make water walls.
void align_generated_shores(FieldGrid& field,std::span<const float> editable_weights);
// One spill limit per connected editable basin. Never raises surrounding terrain to contain water.
std::vector<float> contained_lake_levels(const FieldGrid& source,std::span<const float> weights,float requested);
std::vector<float> terrain_basin_distances(const FieldGrid& field,std::span<const float> weights);
// Whole-map land generator finishing. Preserves water, protected cells, flags and materials.
void reserve_connected_building_areas(const FieldGrid& source,TerrainGenerationResult& result,
    std::span<const uint8_t> protected_cells={});
void erode_generated_slopes(TerrainGenerationResult& result,unsigned iterations=12);
void grade_drivable_river_banks(TerrainGenerationResult& result,std::vector<uint8_t>& footprint,float surface);
void assess_generated_ground(TerrainGenerationResult& result);
} // namespace thandor
