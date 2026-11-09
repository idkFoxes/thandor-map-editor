#pragma once
#include "thandor/simulation_lab_shot_terrain.hpp"
#include "thandor/effect.hpp"
#include <cstdint>

namespace thandor {
// The recovered effect terrainStateRefresh uses NOT_REMEMBERED and no
// accumulated secondary mask. It refreshes flags and modulates the effect
// runtime alpha/tint with the authored definition's stateTintArgb only while
// the model remains visible. Age/lifetime belong to the linked effect slot.
struct SimulationLabEffectTerrainClassState {
    uint32_t resolved_mask{}, runtime_flags{}, tint_argb{};
};
inline SimulationLabEffectTerrainClassState simulation_lab_resolve_effect_terrain_state(
    uint32_t raw_mask,uint32_t active_faction,uint32_t definition_tint,uint8_t effect_alpha) noexcept {
    const auto shot = simulation_lab_resolve_shot_terrain_state(raw_mask,active_faction,definition_tint);
    const uint32_t runtime_alpha_tint=(uint32_t(effect_alpha)<<24U)|0x00FFFFFFU;
    return {shot.resolved_mask,shot.runtime_flags,
        (shot.runtime_flags&0x04U)!=0U
          ?retail_modulate_argb_mmx(runtime_alpha_tint,definition_tint)
          :retail_ui_state_tint_argb(shot.runtime_flags)};
}
}
