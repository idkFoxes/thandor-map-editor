#pragma once
#include "thandor/simulation_lab_ai_workspaces.hpp"
#include <algorithm>
namespace thandor {
// Recovered AiPlanning_CollectActiveGridMaskClasses (runtime unit subset).
// Pending workspace01 entries without runtime slots are deliberately ignored,
// just as in retail; no pointer-less asset is promoted to an active class.
inline std::array<std::uint32_t,4> simulation_lab_ai_active_grid_classes(
    const SimulationLabRun& run, std::uint32_t faction) {
    std::array<std::uint32_t,4> masks{};
    masks.fill(0xFFFFFFFFU); // AI_GRID_MASK_CLASS_FREE, not zero.
    std::size_t count=0;
    const auto own=simulation_lab_ai_own_workspaces(run,faction);
    const auto consider=[&](const SimulationLabEntity& unit) {
        // Native MDL +0x18 is accelerationPerTick. Catalog-backed runtime
        // hierarchies carry it as ground_acceleration_q12 even for aircraft;
        // speed alone is not a valid native mobility test. Older synthetic
        // fixtures without authored hierarchy retain their legacy proxy.
        const bool authored=unit.runtime_model_hierarchy &&
            !unit.runtime_model_hierarchy->empty();
        const bool accelerates=authored ? unit.motion.ground_acceleration_q12!=0U
                                       : unit.motion.movable();
        // Native AiPlanning_CollectActiveGridMaskClasses only requires a
        // linked Workspace 01 model, its acceleration and valid traversal
        // classes. Terminal effects and construction flags do not unlink an
        // owner, and an already unlinked root never reaches this callback.
        if (unit.faction_index!=faction || unit.army_asset_id>=300U ||
            unit.runtime_removed || !unit.has_traversal_profile ||
            !accelerates ||
            static_cast<std::int32_t>(unit.traversal_low_band_index)<0 ||
            static_cast<std::int32_t>(unit.traversal_high_band_index)<0)
            return;
        // The recovered model routine accepts any nonnegative class and
        // masks the shift count to five bits; it does not impose 8/7 caps.
        const auto bits=(0x100U << (unit.traversal_low_band_index&31U)) |
                        0x80000000U | (0x01000000U << (unit.traversal_high_band_index&31U));
        if (std::find(masks.begin(),masks.end(),bits)!=masks.end()) return;
        if (count<masks.size()) masks[count++]=bits;
    };
    // Both synthetic and level-backed runs must respect the same native
    // Workspace 01 first-64 admission and linked-owner traversal order.
    // Scanning run.entities directly leaks overflow units into WS05 masks.
    for (const auto& entry : own.units)
        if (entry.entity_index) consider(run.entities[*entry.entity_index]);
    if (count==0U) masks[0]=0x90000100U;
    return masks;
}

}
