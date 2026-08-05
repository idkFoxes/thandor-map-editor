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
