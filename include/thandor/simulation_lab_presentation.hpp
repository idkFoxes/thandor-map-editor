#pragma once

#include "thandor/simulation_lab.hpp"
#include <chrono>
#include <utility>

namespace thandor {
// The prepared hierarchy and weapon list belong to the same technology snapshot.
// An empty prepared weapon list is authoritative too. Never decode SPR assets
// merely to discard their result on the rendering thread.
template<class Fallback>
std::vector<SimulationWeaponProfile> simulation_lab_render_weapon_profiles(
    const SimulationLabEntity* entity, Fallback&& fallback) {
    if (!entity || !entity->runtime_model_hierarchy)
        return std::forward<Fallback>(fallback)();
    std::vector<SimulationWeaponProfile> result;
    result.reserve(entity->combat_weapons.size());
    for (const auto& weapon : entity->combat_weapons) result.push_back(weapon.profile);
    return result;
}

// Match the 20 Hz retail presentation cadence under a worker backlog. This
// coalesces only immutable copies/geometry, never simulation ticks or events.
inline constexpr auto simulation_lab_presentation_interval = std::chrono::milliseconds(50);
inline bool simulation_lab_scene_update_due(bool dirty, bool camera_changed,
    std::chrono::steady_clock::duration elapsed) noexcept {
    return camera_changed || (dirty && elapsed >= simulation_lab_presentation_interval);
}
}
