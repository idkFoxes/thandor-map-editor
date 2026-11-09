#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <limits>

namespace thandor {
// Port of AiUnitBehavior_ScorePioneerSite and the subsequent x3/(count+3)
// assignment pressure in AiUnitBehavior_UpdatePioneerVehicle. All arithmetic
// is 32-bit wrapped like the retail x86 code, including negative score order.
inline std::int32_t simulation_lab_pioneer_site_weighted_score(
    std::int32_t priority, std::uint32_t army_asset_id,
    std::int64_t secondary_workspace01_distance_q12,
    std::int64_t visible_hostile_workspace02_distance_q12,
    std::int64_t pioneer_manhattan_distance_q12,
    std::uint32_t assigned_runtime_count,
    const std::array<std::uint32_t, 128>& parameters) noexcept {
    const auto i32 = [](std::uint32_t n) { return std::bit_cast<std::int32_t>(n); };
    const auto add = [&](std::int32_t a, std::int32_t b) {
        return i32(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
    };
    const auto mul = [&](std::int32_t a, std::int32_t b) {
        return i32(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
    };
    // The two workspace distance functions return INT32_MAX if no live runtime
    // is present. Clamp distances to the original signed integer range.
    const auto clamp = [](std::int64_t d) {
        return static_cast<std::int32_t>(d >= INT32_MAX ? INT32_MAX : d <= 0 ? 0 : d);
    };
    std::int32_t score = mul(priority, i32(parameters[85]));
    const auto hostile = clamp(visible_hostile_workspace02_distance_q12);
    if (i32(parameters[88]) != 0 && hostile < i32(parameters[84]))
        score = add(score, mul(hostile, i32(parameters[88])));
    const auto secondary = clamp(secondary_workspace01_distance_q12);
    if (i32(parameters[86]) != 0 && secondary < i32(parameters[82]))
        score = add(score, mul(add(i32(parameters[82]), -secondary), i32(parameters[86])));
    const auto distance = clamp(pioneer_manhattan_distance_q12);
    if (i32(parameters[87]) != 0 && distance <= i32(parameters[83]))
        score = add(score, mul(add(i32(parameters[83]), -distance), i32(parameters[87])));
    if (army_asset_id == 330U) score = mul(score, 2);
    const std::uint32_t denominator = assigned_runtime_count + 3U;
    if (denominator == 0U) return 0;
    return i32(static_cast<std::uint32_t>(mul(score, 3)) / denominator);
}
} // namespace thandor
