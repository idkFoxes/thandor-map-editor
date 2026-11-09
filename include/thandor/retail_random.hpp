#pragma once
#include <cstdint>
namespace thandor {
// Random_NextPrimary/Secondary: two wrapping 33*x+101 steps, then mix their bits.
// Zero is a valid native seed and must never be replaced with an editor fallback.
constexpr uint32_t retail_random_next(uint32_t& seed) noexcept {
    const uint32_t first=seed*33U+101U;
    seed=first*33U+101U;
    return (first<<14U)^(seed>>2U);
}
}
