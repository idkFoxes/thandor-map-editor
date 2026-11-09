#pragma once

#include "thandor/pioneer_movement_update.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace thandor {
// x86 SUB/NEG/ADD wrapping, matching the original signed-int Manhattan probe.
constexpr std::uint32_t pioneer_abs_wrapped(std::int32_t a, std::int32_t b) {
    const auto delta = static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b);
    return (delta & 0x80000000U) ? 0U - delta : delta;
}

// Ports the control/ordering of AiPlacement_FindNearestPlaceableBaseSite and
// AiPlacement_ReserveSeparatedSpecialSiteChain from the current open-thandor
// reverse-engineered source. The real mode-1 candidate test, ARM333 creation,
// and destruction are injected: this is NOT a site-placement implementation.
// Callback is invoked only for candidates improving the current signed best.
template<class Placeable>
std::optional<MovementPoint> pioneer_nearest_mode1_anchor(
    MovementPoint site, std::span<const MovementPoint> workspace09,
    Placeable&& mode1_placeable) {
    std::uint32_t best = std::uint32_t{INT32_MAX};
    std::optional<MovementPoint> chosen;
    for (const MovementPoint p : workspace09) {
        const std::uint32_t dx = pioneer_abs_wrapped(site.x, p.x);
        const std::uint32_t dy = pioneer_abs_wrapped(site.y, p.y);
        const std::uint32_t distance = dx + dy;
        if (std::bit_cast<std::int32_t>(distance) < std::bit_cast<std::int32_t>(best) &&
            mode1_placeable(p)) {
            best = distance;
            chosen = p;
        }
    }
    return chosen;
}

// Return value is RETAIL 'rejected': false means a near ARM333 anchor was found.
// Probe must return the first currently placeable workspace09 anchor. Creating
// an ARM333 token must affect subsequent probes, as in the real owner list.
// All successfully created tokens are destroyed in reverse creation order,
// including on early acceptance and creation failure. Tokens must be movable.
template<class Token, class NearestAnchor, class Create, class Destroy>
bool pioneer_reserve_separated_site_chain(
    MovementPoint site, std::uint32_t separation_quantum_q12,
    NearestAnchor&& nearest_anchor, Create&& create, Destroy&& destroy) {
    std::array<std::optional<Token>,4U> temporary{};
    unsigned created = 0U;
    bool rejected = true;
    for (unsigned probe = 0; probe < 4U; ++probe) {
        const auto next = nearest_anchor(site);
        if (!next) break;
        auto token = create(*next);
        if (!token) break;
        temporary[created++].emplace(std::move(*token));
        if (pioneer_abs_wrapped(next->x, site.x) +
            pioneer_abs_wrapped(next->y, site.y) < separation_quantum_q12) {
            rejected = false;
            break;
        }
    }
    while (created != 0U) {
        auto& token = temporary[--created];
        destroy(*token);
        token.reset();
    }
    return rejected;
}
} // namespace thandor
