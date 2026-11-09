#pragma once

#include "thandor/simulation_lab.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace thandor {
// InGameTick_RunWorldJob case 7 iterates linked world owners, while
// ArmyRuntimeNode_AccumulateTerrainOcclusionAndOccupancyCallback writes the
// owner's occupancy mark only if its relation with the ACTIVE view faction
// has FACTION_RELATION_STATE_ALLIED (bit 3). A dying root remains a world
// owner until the terminal effect releases its model runtime.
inline bool simulation_lab_phase7_sector_owner_eligible(
    const SimulationLabRun& run, const SimulationLabEntity& owner) noexcept {
    if (owner.faction_index == 0U || owner.faction_index >= 8U ||
        owner.runtime_removed) return false;
    if (owner.root_model_component_index < owner.model_components.size() &&
        owner.model_components[owner.root_model_component_index].runtime_removed)
        return false;
    if (run.level_runtime_enabled) {
        if (run.active_view_faction_index >= 8U) return false;
        const auto relation = (run.faction_packed_relation_states[owner.faction_index] >>
            (run.active_view_faction_index * 4U)) & 0xFU;
        return (relation & 0x8U) != 0U;
    }
    // Metadata-only Lab fixtures do not have native relation initialization.
    // Preserve their existing synthetic occupancy behavior rather than
    // interpreting their zero-initialized relation rows as hostile.
    return !owner.root_model_terminal && owner.construction_complete;
}
// The terrain-occlusion writer is a separate pass from active-view sector
// occupancy. Native ArmyRuntimeNode_AccumulateTerrainOcclusionAndOccupancyCallback
// runs for every linked, nonneutral MODEL owner, even while construction or a
// terminal effect is active. Faction relations control the emitted marker bytes,
// not whether this owner is visited. Direct synthetic fixtures retain their
// previous preview-only gating (readiness, disabled bit, positive sight radius).
inline bool simulation_lab_phase7_occlusion_owner_eligible(
    const SimulationLabRun& run, const SimulationLabEntity& owner) noexcept {
    if (owner.faction_index == 0U || owner.faction_index >= 8U ||
        owner.runtime_removed) return false;
    const bool has_root = owner.root_model_component_index < owner.model_components.size();
    if (has_root && owner.model_components[owner.root_model_component_index].runtime_removed)
        return false;
    if (run.level_runtime_enabled) return true;
    return owner.construction_complete && !owner.root_model_terminal &&
        (!has_root || (owner.model_components[owner.root_model_component_index].runtime_flags_ec & 0x10U) == 0U) &&
        owner.fog_uncover_radius_q12 > 0;
}

// A zero native MDL +0x48 scan radius is NOT an absent owner: native
// TerrainProjectedScan_SetStepLimitFromRadius clamps it up to one step.
// The generic preview helper deliberately treats radius zero as no observer,
// so pass a one-unit radius for that native case, without changing preview API.
inline std::uint32_t simulation_lab_phase7_occlusion_preview_radius(
    const SimulationLabRun& run, const SimulationLabEntity& owner) noexcept {
    if (run.level_runtime_enabled && owner.fog_uncover_radius_q12 == 0) return 1U;
    return owner.fog_uncover_radius_q12 > 0
        ? static_cast<std::uint32_t>(owner.fog_uncover_radius_q12) : 0U;
}

// Zero is a meaningful native MDL +0x27C radius. The older Stage10 field
// +0x19C is a compatibility fallback for direct synthetic fixtures ONLY.
inline std::uint32_t simulation_lab_phase7_owner_occupancy_radius(
    const SimulationLabRun& run, const SimulationLabEntity& owner) noexcept {
    if (run.level_runtime_enabled || owner.occupancy_mark_radius_units != 0U)
        return owner.occupancy_mark_radius_units;
    return static_cast<std::uint32_t>(std::max<std::int32_t>(
        0, owner.runtime_class_specific_19c_raw));
}
// World-job callbacks are dispatched only when a linked world owner exists.
// Cached projected fog can outlive the last owner, but must not be replayed
// as current case-7 occupancy after the native clear operation.
// Startup classification remains independent of this world-job rule.
inline bool simulation_lab_phase7_projection_available(
    bool is_world_job, bool has_world_owner) noexcept {
    return !is_world_job || has_world_owner;
}

// A phase-7 world job skips terrain callbacks AND scratch presence
// propagation when the world-owner list is null (tick.cpp). The Lab has
// no full world-owner chain, so only a definitely empty image can skip it;
// static placements may be linked world models outside run.entities.
inline bool simulation_lab_phase7_maybe_has_world_owner(
    const SimulationLabRun& run, std::size_t static_placement_count) noexcept {
    if (static_placement_count!=0U) return true;
    for (const auto& owner:run.entities)
        if (!owner.runtime_removed &&
            (owner.root_model_component_index>=owner.model_components.size() ||
             !owner.model_components[owner.root_model_component_index].runtime_removed))
            return true;
    return false;
}
} // namespace thandor
