#pragma once

#include "thandor/simulation_lab.hpp"
#include <cstddef>
#include <cstdint>
#include <span>

namespace thandor {
// InGameTick_RefreshEntityTerrainStates visits linked MODEL owners. A root
// running a terminal effect is still linked; a released root is not.
inline bool simulation_lab_terrain_refresh_root_linked(
    const SimulationLabEntity& entity) noexcept {
    return !entity.runtime_removed &&
        (entity.root_model_component_index >= entity.model_components.size() ||
         !entity.model_components[entity.root_model_component_index].runtime_removed);
}

// The startup terrain callback can use a projected image before a field
// snapshot exists. Native phase 3/7 callbacks instead read the FLD occupancy
// image retained AFTER the phase's world maintenance, never cached projection.
inline std::span<const std::uint64_t> simulation_lab_terrain_refresh_source(
    const SimulationLabRun& run, std::size_t cell_count,
    bool completed_world_job) noexcept {
    if (completed_world_job) {
        if (run.retail_scratch &&
            run.retail_scratch->field_occupancy_masks.size()==cell_count)
            return run.retail_scratch->field_occupancy_masks;
        return {};
    }
    if (run.current_visibility_occupancy.size()==cell_count)
        return run.current_visibility_occupancy;
    return {};
}
} // namespace thandor
