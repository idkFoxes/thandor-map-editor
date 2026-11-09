#pragma once

#include "thandor/simulation_lab_phase7_owner.hpp"
#include <cstddef>
#include <cstdint>

namespace thandor {
// The renderer usually consumes the worker's projected fog, which may be
// newer than the retained field image between world jobs. After the last
// native world owner is released, however, phase 7 clears transient FLD
// occupancy without invoking any projected-occlusion writer. In that case
// the completed retained field is authoritative; a cached projection is not.
inline bool simulation_lab_phase7_preview_uses_retained_field(
    const SimulationLabRun& run, std::size_t cell_count) noexcept {
    return !simulation_lab_phase7_maybe_has_world_owner(
               run,run.static_placement_owner_count) &&
        run.retail_scratch &&
        run.retail_scratch->field_occupancy_masks.size()==cell_count &&
        run.retail_scratch->presence_update_tick>=run.current_visibility_occupancy_tick;
}

inline bool simulation_lab_phase7_preview_has_world_owner(
    const SimulationLabRun& run) noexcept {
    return simulation_lab_phase7_maybe_has_world_owner(
        run,run.static_placement_owner_count);
}
} // namespace thandor
