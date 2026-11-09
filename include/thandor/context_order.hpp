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
    // Recovery/repair SHTs use negative target-class damage and select same-faction runtimes.
    // Keep this distinct from hostile weapon capability so normal weapons cannot target allies.
    bool friendly_support{};
};

// The retail pointer-command path resolves an army runtime under the pointer before its
// terrain fallback. Preserve that precedence: a different-faction placement is an entity
// attack target; otherwise a movable unit receives a terrain movement order, while an armed
// immobile/building actor receives a point-target order.
constexpr ContextOrderKind resolve_context_order(
    const ContextOrderActorCapabilities actor,
    const std::optional<uint32_t> hovered_faction,
    const bool pointer_entity_hit,
    const bool terrain_hit) noexcept {
    // InGameWorldInput_CommitPointerActionCf resolves the entity candidate before its terrain
    // fallback. In the editor a visible placement therefore blocks a drive/rally point beneath
    // it even when it is friendly, neutral scenery, or the selected actor itself. Only a
    // non-neutral different-faction placement is a valid attack entity.
    if ((actor.armed || actor.friendly_support) && hovered_faction && *hovered_faction != 0U) {
        if (actor.armed && *hovered_faction != actor.faction_index)
            return ContextOrderKind::AttackPlacement;
        if (actor.friendly_support && *hovered_faction == actor.faction_index)
            return ContextOrderKind::AttackPlacement;
    }
    if (pointer_entity_hit) return ContextOrderKind::None;
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
