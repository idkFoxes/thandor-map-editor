#pragma once
#include "thandor/field.hpp"

namespace thandor {
enum class NewMapTerrainType { Land, ShallowWater, DeepWater, Island };
struct NewMapTerrainSettings {
    NewMapTerrainType type{NewMapTerrainType::Land};
    float land_height{2.0F};
    float surface_level{};
    float water_depth{2.0F};
    uint32_t seed{20261027U};
};
NewMapTerrainSettings new_map_terrain_defaults(NewMapTerrainType type);
// Replaces elevations/water only, retaining the FLD geometry, materials and other flags.
void initialize_new_map_terrain(FieldGrid& field,const NewMapTerrainSettings& settings);
} // namespace thandor
