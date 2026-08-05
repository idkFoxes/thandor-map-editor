#pragma once

#include <cstdint>
#include <optional>

namespace thandor {

enum class ContextOrderKind : uint8_t {
    None,
    MoveToTerrain,
    AttackTerrainPoint,
    AttackPlacement,
    SetProductionRallyPoint,
};

struct ContextOrderActorCapabilities {
    bool movable{};
    bool armed{};
    bool building{};
    bool production_rally{};
    uint32_t faction_index{};
};

// The retail pointer-command path resolves an army runtime under the pointer before its
// terrain fallback. Preserve that precedence: a different-faction placement is an entity
// attack target; otherwise a movable unit receives a terrain movement order, while an armed
// immobile/building actor receives a point-target order.
constexpr ContextOrderKind resolve_context_order(
    const ContextOrderActorCapabilities actor,
    const std::optional<uint32_t> hovered_faction,
    const bool terrain_hit) noexcept {
    if (actor.armed && hovered_faction && *hovered_faction != actor.faction_index)
        return ContextOrderKind::AttackPlacement;
    if (!terrain_hit) return ContextOrderKind::None;
    if (actor.movable) return ContextOrderKind::MoveToTerrain;
    if (actor.armed) return ContextOrderKind::AttackTerrainPoint;
    if (actor.production_rally) return ContextOrderKind::SetProductionRallyPoint;
    return ContextOrderKind::None;
}

constexpr bool context_order_is_attack(const ContextOrderKind order) noexcept {
    return order == ContextOrderKind::AttackTerrainPoint ||
           order == ContextOrderKind::AttackPlacement;
}

} // namespace thandor
