#pragma once

#include "thandor/fog_preview.hpp"
#include "thandor/effect.hpp"
#include "thandor/simulation.hpp"
#include <cstdint>
#include <span>

namespace thandor {
// Recovered ShotModelRuntimeMaintenance_RefreshTerrainClassAndTint:
// ordinary shots classify around their model origin at Q12_ONE radius.
// A nonzero direct-line beam additionally classifies its midpoint and end;
// the three masks are ORed before the runtime visibility/tint resolution.
// This helper returns the *raw neighborhood mask*, not the final state tint.
inline uint32_t simulation_lab_shot_neighborhood_mask(
    const FieldGrid& field, std::span<const uint64_t> retained_occupancy,
    Vec3f origin, bool direct_line, Vec3f beam_end, bool beam_has_length) {
    const auto classify=[&](Vec3f point) {
        return retail_model_neighborhood_mask(field,retained_occupancy,
            float_to_q12(point.x),float_to_q12(point.y),0x1000);
    };
    uint32_t mask=classify(origin);
    if(direct_line && beam_has_length) {
        const Vec3f middle{(origin.x+beam_end.x)*0.5F,
                           (origin.y+beam_end.y)*0.5F,
                           (origin.z+beam_end.z)*0.5F};
        mask|=classify(middle);
        mask|=classify(beam_end);
    }
    return mask;
}
// Recovered shot terrainStateRefresh: resolve the active faction's two
// occupancy bits with NOT_REMEMBERED (0x10) set, then apply the definition's
// SHT state tint using the game's byte-lane modulation. Kept separate from
// rendering until the selected-run tint handoff can be verified.
struct SimulationLabShotTerrainClassState {
    uint32_t resolved_mask{};
    uint32_t runtime_flags{};
    uint32_t tint_argb{};
};
inline SimulationLabShotTerrainClassState simulation_lab_resolve_shot_terrain_state(
    uint32_t neighborhood_mask,uint32_t active_faction_index,uint32_t definition_tint_argb) noexcept {
    constexpr uint32_t kNotRemembered=0x10U;
    constexpr uint32_t kSeenBefore=0x08U;
    constexpr uint32_t kPresent=0x04U;
    // The native active index is a faction runtime byte; the bit shift is
    // masked to 31 by x86 (all normal runtime factions are within 0..7).
    const uint32_t seen_bit=1U<<((active_faction_index*2U)&31U);
    const auto occupancy=retail_resolve_model_occupancy(neighborhood_mask,0U,true);
    uint32_t flags=kNotRemembered;
    if((occupancy.primary&seen_bit)!=0U)flags|=kSeenBefore;
    if((neighborhood_mask&(seen_bit<<1U))!=0U)flags|=kPresent;
    return {occupancy.primary,flags,
        retail_modulate_argb_mmx(retail_ui_state_tint_argb(flags),definition_tint_argb)};
}
} // namespace thandor
