#pragma once
// Native AiSiteCandidate_AddGeneralCellIfSeparated workspace 05 scoring.
// Keep fixed-width modular arithmetic instead of signed-overflow UB or
// a wider accumulated score: those change site ranking/order admission.
#include <algorithm>
#include <span>
#include <bit>
#include <cstdint>

namespace thandor {
inline std::int32_t simulation_lab_workspace05_native_score(
    std::int64_t primary_distance_q12, std::int64_t secondary_distance_q12,
    std::int64_t hostile_distance_q12,
    std::span<const std::uint32_t> parameters) noexcept {
    const auto i32=[](std::uint32_t v) noexcept {return std::bit_cast<std::int32_t>(v);};
    // The recovered callback narrows Q12 distances/parameters to a dword.
    const auto primary=i32(std::uint32_t(parameters[0x21U]) -
        std::uint32_t(primary_distance_q12));
    const std::uint32_t primary_term=std::uint32_t(std::max(primary,0));
    const auto capped=[](std::int64_t distance,std::uint32_t cap) noexcept {
        const auto native_distance=std::bit_cast<std::int32_t>(std::uint32_t(distance));
        const auto native_cap=std::bit_cast<std::int32_t>(cap);
        return std::uint32_t(native_distance>native_cap?native_cap:native_distance);
    };
    const std::uint32_t hostile=capped(hostile_distance_q12,parameters[0x24U]);
    const std::uint32_t secondary=capped(secondary_distance_q12,parameters[0x22U]);
    const auto score=primary_term*parameters[0x25U] +
        hostile*parameters[0x28U] + secondary*parameters[0x26U];
    return i32(score);
}
} // namespace thandor
