#pragma once
#include <bit>
#include <cstdint>

namespace thandor {
// AiUnitBehavior_UpdateOwnUnits, retail 0x0053B0E0. aiUnitState aliases the
// signed cooldown dword at army +0x8C. It counts AI callbacks, not world ticks.
inline bool pioneer_ai_dispatch_ready(std::uint32_t movement_flags,
                                      std::int32_t& cooldown) noexcept {
    if ((movement_flags & 0x13U) == 0U) return true;
    if ((movement_flags & 2U) != 0U) return false;
    cooldown = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(cooldown) - 1U);
    if (cooldown > 0) return false;
    if (cooldown < 0)
        cooldown = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(cooldown) + 1U);
    return true;
}
} // namespace thandor
