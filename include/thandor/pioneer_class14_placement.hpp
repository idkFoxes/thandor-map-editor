#pragma once

#include "thandor/pioneer_movement_update.hpp"
#include "thandor/retail_integer_sqrt.hpp"

#include <cstdint>
#include <limits>
#include <span>

namespace thandor {
// The class-14 post-terrain and owner-list decisions of
// ArmyPlacement_CanPlaceResourceExtractor -> ArmyPlacement_CanPlaceBuilding
// (open-thandor-main/src/gameplay/army/placement.cpp). Coordinates are world
// X/Y Q12. The supplied owner list must retain original iteration order.
// The collision, contact-kind terrain test, field blocker and field cell lookup
// are explicitly required inputs; no 'reach-only' result is gameplay-authoritative.
struct PioneerPlacementModel {
    std::int32_t footprint_radius_q12{};
    std::int32_t placement_margin_q12{}; // ModelDefinition.placementFlags
    std::uint8_t class_parameter_c0{};   // class-14 deposit selector
};
struct PioneerPlacementOwner {
    MovementPoint position{};
    std::uint32_t faction{};
    std::uint32_t runtime_class{};
    std::uint32_t class_state_flags{}; // class-18 destroyed/dismantling mask 0x18
    std::int32_t support_radius_q12{};
    bool is_runtime_model{true};
};
enum class PioneerPlacementReason : std::uint8_t {
    Accepted, RuntimeCollision, TerrainRejected, FieldBlocked,
    NoSupportingModel, MissingResourceDeposit, FactoryExitRuntimeCollision, FactoryExitTerrainRejected, MissingFactoryAnchor
};
struct PioneerPlacementResult {
    bool accepted{};
    std::uint32_t placement_value{};
    PioneerPlacementReason reason{PioneerPlacementReason::NoSupportingModel};
};

// Exact floor-sqrt retained for callers that require it. Native measured
// support clearance below uses retail_integer_sqrt's three Newton updates.
constexpr std::uint32_t pioneer_uint64_sqrt(std::uint64_t n) noexcept {
    std::uint64_t bit = std::uint64_t{1} << 62;
    while (bit > n) bit >>= 2;
    std::uint64_t root = 0;
    while (bit != 0) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else root >>= 1;
        bit >>= 2;
    }
    return static_cast<std::uint32_t>(root);
}

// Both source modes require a valid same-faction support owner, unless the
// interaction subsystem is active. Mode 3 measures nearest clearance, skipping
// class 18; mode 0 returns immediately with the source's intermediate y^2.
template<class Owners,class Projection>
inline PioneerPlacementResult pioneer_class14_support_projected(
    std::uint32_t mode, std::int32_t padding_q12,
    MovementPoint candidate, PioneerPlacementModel model, std::uint32_t faction,
    const Owners& owners,Projection&& projection) noexcept {
    constexpr std::int64_t kNoSupportThreshold = INT64_MAX - (std::int64_t{1} << 32);
    std::int64_t nearest_sq = INT64_MAX;
    std::int32_t nearest_radius = 0;
    for (const auto& value : owners) {
        const auto& owner=projection(value);
        if (!owner.is_runtime_model || owner.support_radius_q12 == 0 ||
            owner.faction != faction) continue;
        if (owner.runtime_class == 18U &&
            (((mode & 1U) != 0U) || ((owner.class_state_flags & 0x18U) != 0U))) continue;
        // x86 calculations are dword arithmetic. This conversion reproduces
        // wrapping before squaring; typical model/radius values never overflow.
        const auto reach_u = static_cast<std::uint32_t>(owner.support_radius_q12) +
            static_cast<std::uint32_t>(padding_q12) +
            static_cast<std::uint32_t>(model.placement_margin_q12);
        const auto reach = static_cast<std::int64_t>(static_cast<std::int32_t>(reach_u));
        const auto dx = static_cast<std::int64_t>(owner.position.x) - candidate.x;
        const auto dy = static_cast<std::int64_t>(owner.position.y) - candidate.y;
        // Comparisons avoid overflow if fixtures include extreme Q12 positions.
        const auto abs_x = dx < 0 ? static_cast<std::uint64_t>(-dx) : static_cast<std::uint64_t>(dx);
        const auto abs_y = dy < 0 ? static_cast<std::uint64_t>(-dy) : static_cast<std::uint64_t>(dy);
        const auto abs_reach = reach < 0 ? static_cast<std::uint64_t>(-reach) : static_cast<std::uint64_t>(reach);
        if (abs_x > abs_reach || abs_y > abs_reach) continue;
        const std::uint64_t reach_sq = abs_reach * abs_reach;
        const std::uint64_t distance_sq = abs_x * abs_x + abs_y * abs_y;
        if (distance_sq > reach_sq) continue;
        if ((mode & 2U) == 0U) {
            // Retail's no-measure branch writes the low dword of deltaY^2.
            return {true,static_cast<std::uint32_t>(abs_y * abs_y),PioneerPlacementReason::Accepted};
        }
        if (distance_sq < static_cast<std::uint64_t>(nearest_sq)) {
            nearest_sq = static_cast<std::int64_t>(distance_sq);
            nearest_radius = owner.support_radius_q12;
        }
    }
    if ((mode & 2U) == 0U || nearest_sq > kNoSupportThreshold)
        return {false,0U,PioneerPlacementReason::NoSupportingModel};
    const auto distance_q12 = static_cast<std::int64_t>(retail_integer_sqrt(
        static_cast<std::uint64_t>(nearest_sq)));
    const auto free_q12 = (distance_q12 > nearest_radius)
        ? distance_q12 - nearest_radius : 0;
    const auto margin_value = free_q12 - model.placement_margin_q12;
    return {true,static_cast<std::uint32_t>(margin_value > 0 ? margin_value : 0),
            PioneerPlacementReason::Accepted};
}

inline PioneerPlacementResult pioneer_class14_support(
    std::uint32_t mode,std::int32_t padding,MovementPoint candidate,PioneerPlacementModel model,
    std::uint32_t faction,std::span<const PioneerPlacementOwner> owners) noexcept {
    return pioneer_class14_support_projected(mode,padding,candidate,model,faction,owners,
        [](const auto& owner)->const auto& {return owner;});
}

// Collision and terrain callbacks should execute their actual contact-kind
// dispatches. The flags input is from the actual cell under this query point.
// An interaction-mode shortcut mirrors the original common test but still
// requires class-14 deposit support after the shortcut.
template<class CollisionBlocked,class TerrainTest,class FieldBlocked>
PioneerPlacementResult pioneer_class14_placement(
    std::uint32_t mode, std::int32_t padding_q12,
    MovementPoint candidate, PioneerPlacementModel model, std::uint32_t faction,
    std::span<const PioneerPlacementOwner> owners, std::uint32_t cell_flags,
    bool interaction_active, CollisionBlocked&& collision_blocked,
    TerrainTest&& terrain_test, FieldBlocked&& field_blocked) {
    if (collision_blocked(mode,model.footprint_radius_q12,candidate))
        return {false,0U,PioneerPlacementReason::RuntimeCollision};
    const auto terrain = terrain_test(model.footprint_radius_q12,candidate);
    if (terrain.rejected) return {false,0U,PioneerPlacementReason::TerrainRejected};
    PioneerPlacementResult common;
    if (interaction_active) {
        common = {true,terrain.value,PioneerPlacementReason::Accepted};
    } else {
        if (field_blocked(faction,candidate))
            return {false,0U,PioneerPlacementReason::FieldBlocked};
        common = pioneer_class14_support(mode,padding_q12,candidate,model,faction,owners);
        if (!common.accepted) return common;
    }
    const auto resource_mask = std::uint32_t{0x800U} << (model.class_parameter_c0 & 31U);
    if ((cell_flags & resource_mask) == 0U)
        return {false,0U,PioneerPlacementReason::MissingResourceDeposit};
    return common;
}
struct PioneerTerrainPlacementResult {
    bool rejected{};
    std::uint32_t value{};
};
} // namespace thandor
