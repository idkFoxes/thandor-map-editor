#pragma once
#include "thandor/pioneer_movement_update.hpp"
#include <cstdint>
#include <optional>

namespace thandor {
// V595: source-derived common portion of ArmyRuntime_ResetMovementStateFromModel
// from the user's V537 Ghidra export. The retail +0x100 stateOrTechnologyId
// and command-generation/target state are independent runtime fields; never
// fabricate their values from movement flags or model-class state +0xEC.
struct PioneerRetailResetInput {
    std::uint32_t movement_flags{};
    std::uint32_t command_mode{};
    std::optional<std::int32_t> state_or_technology_id;
    MovementPoint model{};
};
struct PioneerRetailResetResult {
    std::uint32_t movement_flags{};
    std::uint32_t command_mode{};
    MovementPoint stored{}, primary{}, fallback{};
    bool command_mode_known{}; // false when +0x100 is not modeled
    bool command_generation_reissued{};
    bool special_behavior_retained{};
};
constexpr PioneerRetailResetResult pioneer_retail_reset_common(PioneerRetailResetInput in) {
    PioneerRetailResetResult out;
    out.movement_flags = in.movement_flags & 0xFFFFFFC6U;
    out.command_mode = in.command_mode;
    out.stored = out.primary = out.fallback = in.model;
    out.command_mode_known = in.state_or_technology_id.has_value();
    if (in.state_or_technology_id && *in.state_or_technology_id != 0 &&
        (in.command_mode & 3U) != 0U) {
        out.command_mode = (in.command_mode & 0xFFFFFFFCU) | 4U;
        out.command_generation_reissued = true;
    }
    out.special_behavior_retained = (out.movement_flags & 0x100U) != 0U;
    return out;
}
} // namespace thandor
