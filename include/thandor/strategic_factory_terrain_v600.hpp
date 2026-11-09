#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace thandor {

// AiStrategicClass_SelectTerrainSuitedBuilding, recovered planning.cpp:420-536.
// PCK names, in array order: Armour factory (ARM302 / Panzerfabrik), Walker
// factory (303 / Walkerfabrik), Glider factory (304 / Gleiterfabrik), Harbour
// (305 / Hafen), Airport (306 / Flughafen). All require TEC1 "Arms factories".
// Their creation grants are TEC15 "TANK Badger", TEC30 "Emu walker", TEC45
// "GLD-1 Mosquito", TEC60 "Dolphin Class", and TEC75 "SP Crow", respectively.
// These are family unlocks, separate from TEC category labels A/B/C/-; the
// first four base grants are category A, while SP Crow has no category letter.
// Verified against DATEN.PCK ARM -> MDL and ENGINE.PCK help/techno/tech data;
// see docs/AI_PCK_NAME_CROSSWALK.json for all unit/building names and TEC groups.
// Scratch is the completed, retained 4x FLD state image, NOT per-FLD-cell flags.
// All caller-provided availability bits refer to enabled ARM records; present
// means any primary-workspace entry of that ID, including queued/unassigned.
struct StrategicFactoryTerrainDecisionV600 {
    std::uint32_t selected_army_id{};
    std::uint32_t existing_count{};
    std::array<std::uint32_t, 3> free_percent{}; // bit28..30, bit25..27, bit24
    std::int32_t selected_score{};
};

inline StrategicFactoryTerrainDecisionV600 select_strategic_factory_terrain_v600(
    std::span<const std::uint32_t> scratch_state, std::uint32_t rng_bits,
    const std::array<bool, 5>& present,
    const std::array<bool, 5>& enabled) noexcept {
    StrategicFactoryTerrainDecisionV600 result;
    if (scratch_state.empty()) return result;
    std::array<std::uint64_t, 3> free{};
    for (const auto mask : scratch_state) {
        free[0] += (mask & 0x70000000U) == 0U;
        free[1] += (mask & 0x0E000000U) == 0U;
        free[2] += (mask & 0x01000000U) == 0U;
    }
    for (std::size_t i = 0; i < free.size(); ++i)
        result.free_percent[i] = static_cast<std::uint32_t>(
            free[i] * 100U / scratch_state.size());
    constexpr std::array<std::array<std::int32_t, 3>, 4> weights{{
        {{256, 0, 0}}, {{240, 16, 0}}, {{0, 176, 80}}, {{0, 0, 256}}
    }};
    std::int32_t best_score = 0;
    std::uint32_t random = rng_bits;
    for (std::size_t i = 0; i < present.size(); ++i) {
        if (present[i]) { ++result.existing_count; continue; }
        if (!enabled[i]) continue;
        if (i == 4U) {
            // Original Airport (ARM306) branch compares best_score < 0, without
            // assigning its calculated score. Nonnegative weights make it
            // unreachable in the ordinary source contract.
            if (best_score < 0) result.selected_army_id = 306U;
            continue;
        }
        const std::int32_t score =
            static_cast<std::int32_t>(result.free_percent[0]) * weights[i][0] +
            static_cast<std::int32_t>(result.free_percent[1]) * weights[i][1] +
            static_cast<std::int32_t>(result.free_percent[2]) * weights[i][2] +
            static_cast<std::int32_t>(random & 0x3FFFU);
        // The first enabled Armour factory (ARM302) initializes the selected token even with a
        // zero score. Later choices require strictly greater scores.
        if ((i == 0U && result.selected_army_id == 0U) || score > best_score) {
            result.selected_army_id = static_cast<std::uint32_t>(302U+i);
            best_score = score;
        }
        random >>= 5U; // recovered low-14-bit-equivalent rotate for <=4 candidates
    }
    result.selected_score = best_score;
    return result;
}

} // namespace thandor
