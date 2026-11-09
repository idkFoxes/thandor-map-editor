#pragma once

#include <cstdint>

namespace thandor {
// InGameTick_RunWorldJob case 7 calls FieldGrid_ClearOccupancyMaskBits0To6AllCells
// before rebuilding the occupancy callbacks. That clear retains the high bit
// of EVERY faction byte already present in the field image. Current projected
// presence, independent hex-sector bit 1 and exploration history are merged
// after the clear; sector bit 1 does not qualify for the scratch-grid presence predicate,
// but historic bit 7 *does*: the recovered predicate is 0xF9.
inline std::uint64_t simulation_lab_phase7_merge_occupancy_mask(
    std::uint64_t previous_mask, std::uint64_t projected_mask,
    std::uint8_t hex_bit1_factions, std::uint8_t fallback_bit0_factions,
    std::uint8_t explored_factions) noexcept {
    constexpr std::uint64_t persistent_bits = 0x8080808080808080ULL;
    // The native projected-occlusion callback ORs the WHOLE depth-class
    // marker. Its high bit is not stripped: class 11 can emit 0x90, and
    // several stock runtime classes emit 0x88 or 0xC0.
    std::uint64_t mask = (previous_mask & persistent_bits) | projected_mask;
    for (std::uint32_t faction=1U; faction<8U; ++faction) {
        const auto shift = faction*8U;
        std::uint64_t additions = 0U;
        if ((hex_bit1_factions & (1U<<faction)) != 0U) additions |= 0x02U;
        if ((fallback_bit0_factions & (1U<<faction)) != 0U) additions |= 0x01U;
        if ((explored_factions & (1U<<faction)) != 0U) additions |= 0x80U;
        mask |= additions<<shift;
    }
    return mask;
}

// Tick-wheel case 7 sets bit 0 across the active view's occupancy byte
// ONLY when UI_COMMAND_RUNTIME_FLAG_LOCAL_FACTION_ENDED is set. There is no
// synthetic automatic inference from a defeated faction's health or owners.
inline std::uint8_t simulation_lab_phase7_ended_faction_mask(
    std::uint32_t active_faction, bool local_faction_ended) noexcept {
    return local_faction_ended && active_faction>0U && active_faction<8U
        ? static_cast<std::uint8_t>(1U<<active_faction) : 0U;
}

inline std::uint8_t simulation_lab_phase7_present_factions(
    std::uint64_t occupancy_mask) noexcept {
    std::uint8_t present = 0U;
    for (std::uint32_t faction=1U; faction<8U; ++faction)
        if (((occupancy_mask >> (faction*8U)) & 0xF9U) != 0U)
            present |= static_cast<std::uint8_t>(1U<<faction);
    return present;
}
} // namespace thandor
