#pragma once
#include "thandor/simulation_lab_environment.hpp"
namespace thandor {
bool simulation_lab_refresh_projectile_interval_contact(SimulationProjectileState& projectile,
    const ShotDefinitionRecord& shot,uint32_t tick,const FieldGrid& field,
    const SimulationLabRuntimeCatalog& catalog,std::span<const SimulationLabLiveShotHull> live_hulls,
    std::span<const SimulationLabStaticShotHull> static_hulls);
}
