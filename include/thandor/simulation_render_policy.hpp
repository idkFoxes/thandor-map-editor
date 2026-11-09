#pragma once

#include <cstdint>

namespace thandor {

// Static-model vertex control bits packed into ObjectVertex::direct_tint_bypass.
// Bit 0 remains the recovered DirectDraw direct-tint bypass. The remaining bits are
// editor-side replacement categories and never alter serialized map or asset data.
inline constexpr uint32_t kObjectVertexDirectTintBit = 1U << 0U;
inline constexpr uint32_t kObjectVertexAuthoredAnimatedBuildingBit = 1U << 1U;
inline constexpr uint32_t kObjectVertexSimulatedBuildingBit = 1U << 2U;
inline constexpr uint32_t kObjectVertexSimulatedUnitBit = 1U << 3U;
inline constexpr uint32_t kObjectVertexAuthoredAnimatedUnitBit = 1U << 4U;
inline constexpr uint32_t kObjectVertexContinuousRadarBit = 1U << 5U;
// Editor-only static-copy suppression while fog preview routes a placement through the dynamic
// current-visibility path. It applies to scenery as well as units/buildings.
inline constexpr uint32_t kObjectVertexFogDynamicReplacementBit = 1U << 6U;

// The editor exposes timed-effect and authored-building animation as separate preview controls.
// Retail ArmyRuntime_UpdateTimedShotAndEffectEmitters services a static class-4 building such as
// the Special Research Lab even though that ARM has no authored child animation to advance.  Only
// effects owned by a building that actually has authored model-subnode animation are coupled to
// the building-animation preview toggle (the Headquarters smoke/radar case recovered in G368).
inline constexpr bool timed_effect_requires_building_animation(
    bool owner_is_building, bool owner_has_authored_model_animation) noexcept {
    return owner_is_building && owner_has_authored_model_animation;
}

inline constexpr bool timed_effect_preview_enabled(
    bool owner_is_building, bool owner_has_authored_model_animation,
    bool animate_timed_effects, bool animate_buildings, bool runtime_preview_active) noexcept {
    return animate_timed_effects &&
        (!timed_effect_requires_building_animation(owner_is_building,
                                                   owner_has_authored_model_animation) ||
         animate_buildings || runtime_preview_active);
}

struct SimulationRenderPolicy {
    bool is_building{};
    bool is_unit{};
    bool has_authored_model_animation{};
    bool simulation_mode{};
    bool has_live_simulation_state{};
    bool has_continuous_radar_animation{};

    bool authored_animated_building() const noexcept {
        return is_building && has_authored_model_animation;
    }

    bool authored_animated_unit() const noexcept {
        return is_unit && has_authored_model_animation;
    }

    bool simulation_replacement() const noexcept {
        return simulation_mode && has_live_simulation_state && (is_building || is_unit);
    }

    uint32_t packed_static_control_bits(bool direct_tint) const noexcept {
        uint32_t bits = direct_tint ? kObjectVertexDirectTintBit : 0U;
        if (authored_animated_building()) bits |= kObjectVertexAuthoredAnimatedBuildingBit;
        if (simulation_replacement() && is_building) bits |= kObjectVertexSimulatedBuildingBit;
        if (simulation_replacement() && is_unit) bits |= kObjectVertexSimulatedUnitBit;
        if (authored_animated_unit()) bits |= kObjectVertexAuthoredAnimatedUnitBit;
        if (has_continuous_radar_animation) bits |= kObjectVertexContinuousRadarBit;
        return bits;
    }

    bool dynamic_replacement_enabled(bool animate_buildings, bool animate_units) const noexcept {
        if (has_continuous_radar_animation) return true;
        if (!simulation_replacement()) return false;
        return (is_building && animate_buildings) || (is_unit && animate_units);
    }
};

} // namespace thandor
