#pragma once
#include "thandor/simulation_lab_environment.hpp"
namespace thandor {
struct SimulationLabModelRayHit {
    float distance{};
    uint32_t impact_class{};
    std::optional<size_t> entity_index;
    std::optional<size_t> component_index;
};
struct SimulationLabFixedRay {
    std::array<int32_t,3> direction_q28;
    int32_t distance_q12;
};
std::optional<SimulationLabModelRayHit> simulation_lab_raycast_models(
    Vec3f origin,Vec3f direction,float maximum_distance,std::optional<size_t> excluded_entity,
    std::span<const SimulationLabLiveShotHull> live_hulls,
    std::span<const SimulationLabStaticShotHull> static_hulls,
    const SimulationLabFixedRay* fixed_ray=nullptr);
bool simulation_lab_refresh_beam_contact(SimulationProjectileState& projectile,
    const ShotDefinitionRecord& shot,const FieldGrid& field,const SimulationLabRuntimeCatalog& catalog,
    std::span<const SimulationLabLiveShotHull> live_hulls,
    std::span<const SimulationLabStaticShotHull> static_hulls);
// Native test returns blocked; this API returns clear. Geometry remains the Lab envelope representation.
bool simulation_lab_weapon_line_of_fire_clear(const SimulationLabRun& run,size_t source_index,
    std::optional<size_t> target_index,Vec3f origin,Vec3f aim,const SimulationWeaponProfile& weapon,
    const ShotDefinitionRecord& shot,const FieldGrid& field,
    std::span<const SimulationLabLiveShotHull> live_hulls,
    std::span<const SimulationLabStaticShotHull> static_hulls);
}
