#pragma once
#include "thandor/simulation_lab_catalog.hpp"

namespace thandor {
// The structure entries in the native MODEL class to army depth class table.
constexpr bool simulation_lab_structure_depth_class(uint32_t model_class) noexcept {
    switch (model_class) {
    case 4: case 11: case 13: case 14: case 15: case 16: case 20: case 22: case 23:
        return true;
    default:
        return false;
    }
}

struct SimulationLabRuntimeSprite {
    std::pair<uint32_t,uint32_t> launch_geometry{};
    std::optional<Vec3f> factory_spawn;
    std::optional<Vec3f> factory_exit;
    uint32_t terminal_attachment_point_mask{};
    float height_offset{};
    int32_t pad_baseline_q12{};
    std::vector<std::pair<uint32_t,Vec3f>> shot_points;
    std::vector<Vec3f> effect_points;
    std::vector<std::pair<uint32_t,Vec3f>> damage_effect_points;
    std::vector<std::pair<uint32_t,Vec3f>> effect_point_records;
    std::shared_ptr<const SimulationLabTriangleSurface> collision_surface;
    Vec3f bounds_min{},bounds_max{};
    bool has_bounds{};
    int32_t bounding_radius_q12{};
    bool has_native_bounding_radius{};
    std::array<std::optional<std::array<int32_t,3>>,6> child_translations_q12{};
};
struct SimulationLabRuntimeEffect {
    EffectDefinitionRecord definition;
    std::vector<LinkedShotEmission> emissions;
};
// Prepared once before workers start; no PCK, catalog or sprite I/O during simulation.
struct SimulationLabRuntimeCatalog {
    std::unordered_map<uint32_t,ModelVisualRecord> definitions;
    std::unordered_map<uint32_t,ArmyAssetPreset> armies;
    std::unordered_map<uint32_t,ShotDefinitionRecord> shots;
    std::unordered_map<uint32_t,Vec3f> shot_launch_effect_points;
    std::unordered_map<uint32_t,Vec3f> shot_trail_effect_points;
    std::unordered_map<uint32_t,SimulationLabRuntimeEffect> effects;
    std::unordered_map<uint32_t,uint32_t> conversion_effect_lifetimes;
    std::unordered_map<uint32_t,uint32_t> effect_lifetimes;
    std::unordered_map<uint32_t,uint32_t> terminal_effect_completion_ticks;
    std::unordered_map<std::string,SimulationLabRuntimeSprite> sprites;
};
std::shared_ptr<const SimulationLabRuntimeCatalog> simulation_lab_prepare_runtime_catalog(
    const AssetCatalog& catalog);
std::vector<uint32_t> simulation_lab_prepare_sound_asset_indices(
    const AssetCatalog& catalog,const LevelAsset& level);
bool simulation_lab_initialize_runtime_entity(SimulationLabEntity& entity,
    const SimulationLabSeedEntity& seed,const FactionTechnologyMask256& mask,
    const SimulationLabRuntimeCatalog& catalog,bool creation);
// One replacement per node/pass, retaining the instantiated SPR hierarchy and mutable state.
void simulation_lab_apply_runtime_technology(SimulationLabEntity& entity,
    const FactionTechnologyMask256& mask,const SimulationLabRuntimeCatalog& catalog,
    SimulationLabRunMetrics& metrics);
}
