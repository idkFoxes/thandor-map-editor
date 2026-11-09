#pragma once
#include "thandor/catalog.hpp"
#include "thandor/simulation_lab.hpp"

namespace thandor {
// Native army ownership follows the placed model, independently of how the
// editor groups assets in the placement browser.
inline bool simulation_lab_faction_placement_has_runtime_owner(const ArmyAssetPreset& preset,
                                                                uint32_t faction) noexcept {
    return faction>0U&&faction<8U&&preset.runtime_model_class_id<24U&&
        (!preset.resolved_model_hierarchy.empty()||!preset.model_hierarchy.empty());
}

inline bool simulation_lab_model_class_is_building(uint32_t model_class) noexcept {
    switch(model_class){
        case 4U: case 11U: case 13U: case 14U: case 15U:
        case 16U: case 20U: case 22U: case 23U: return true;
        default: return false;
    }
}
inline bool simulation_lab_crushable_scenery(const ArmyAssetPreset& preset,uint32_t faction) noexcept {
    return faction==0U&&preset.registry_id!=0U&&preset.runtime_model_class_id==0U&&
        // Zero-footprint props can still be shot and released (e.g. crates).
        (preset.simulation_motion_profile.collision_radius_q12>0||preset.root_maximum_integrity>0)&&
        preset.timed_effect_id==0U&&preset.timed_water_effect_id==0U;
}

// These placements own mutable MODEL lifecycles. They must never also
// leave a permanent static navigation collider after their runtime disappears.
inline bool simulation_lab_scenery_has_runtime_owner(const ArmyAssetPreset& preset,uint32_t faction) noexcept {
    return faction==0U&&preset.registry_id!=0U&&preset.runtime_model_class_id<24U&&
        (!preset.resolved_model_hierarchy.empty()||!preset.model_hierarchy.empty());
}

// Copy authored ARM linked lists into asset-I/O-free worker metadata.
void simulation_lab_prepare_model_technologies(const ArmyAssetPreset& preset,
    const AssetCatalog& catalog, SimulationLabSeedEntity& seed);
void simulation_lab_prepare_ai_product(const ArmyAssetPreset& preset,
    const AssetCatalog& catalog, SimulationLabProductionAsset& product);
// Shared by GUI and bounded replay/audit tools so research scoring uses the same metadata.
std::vector<SimulationLabTechnologyAsset> simulation_lab_prepare_technologies(
    const AssetCatalog& catalog, const LevelAsset* level = nullptr);
std::vector<V528PathRuntimeBlocker> simulation_lab_prepare_static_navigation_blockers(
    const AssetCatalog& catalog,const LevelAsset& level);
SimulationLabAiArmyScoreSeed simulation_lab_current_ai_score_seed(
    const SimulationLabProductionAsset& product, const FactionTechnologyMask256& mask,
    std::uint32_t faction);
}
