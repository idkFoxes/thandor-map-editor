#pragma once

#include "thandor/pioneer_movement_gate.hpp"
#include <array>
#include <cstdint>

namespace thandor {
// V594: observation of the exact movement-gate call, not a replacement for
// retail reset semantics. No gameplay inputs or flags are modified here.
struct PioneerGateTrace {
    std::uint32_t calls{};
    std::uint32_t scheduled_calls{};
    std::uint32_t special_flag_calls{};
    std::uint32_t movement_update_calls{};
    std::uint32_t movement_carry_calls{};
    std::uint32_t special_flag_prevented_update{};
    std::uint32_t reset_preserved_special_flag{};
    // V595: workspace-equality accounting over every gate invocation, not merely
    // the last sampled counts. The structural blocker does NOT imply movement carry.
    std::uint32_t workspace_equal_calls{};
    std::uint32_t workspace_unequal_calls{};
    std::uint32_t unequal_with_update_calls{};
    std::uint32_t unequal_carry_calls{};
    std::uint32_t special_with_unequal_nonempty_sites{};
    std::uint32_t reset_with_equal_workspaces{};
    std::array<std::uint32_t, 5> actions{};
    std::uint32_t last_tick{};
    std::uint32_t last_entry_flags{};
    std::uint32_t last_flags_after_update{};
    std::uint32_t last_gate_flags{};
    std::uint32_t last_workspace00{};
    std::uint32_t last_workspace04{};
    std::uint32_t last_workspace08{};
    bool last_scheduled{};
    bool last_movement_carry{};
    std::uint32_t last_action{}; // 1..5, same numbering as pioneer_last_gate_action

    void record(std::uint32_t tick, bool scheduled, const PioneerGateInput& input,
                PioneerGateResult result) noexcept {
        ++calls;
        const bool equal = input.workspace00_count == input.workspace04_count;
        if (equal) ++workspace_equal_calls;
        else {
            ++workspace_unequal_calls;
            if (result.movement_update_called) {
                ++unequal_with_update_calls;
                if (input.movement_carry) ++unequal_carry_calls;
            }
            if ((input.entry_flags & 0x100U) != 0U && input.workspace08_count != 0U)
                ++special_with_unequal_nonempty_sites;
        }
        if (equal && result.action == PioneerGateAction::ResetAtModel)
            ++reset_with_equal_workspaces;
        if (scheduled) ++scheduled_calls;
        if (input.entry_flags & 0x100U) {
            ++special_flag_calls;
            ++special_flag_prevented_update;
        }
        if (result.movement_update_called) {
            ++movement_update_calls;
            if (input.movement_carry) ++movement_carry_calls;
        }
        const auto action = static_cast<std::uint32_t>(result.action);
        if (action < actions.size()) ++actions[action];
        if (result.action == PioneerGateAction::ResetAtModel &&
            (input.entry_flags & 0x100U) != 0U && (result.flags & 0x100U) != 0U)
            ++reset_preserved_special_flag;
        last_tick = tick;
        last_entry_flags = input.entry_flags;
        last_flags_after_update = input.flags_after_movement;
        last_gate_flags = result.flags;
        last_workspace00 = input.workspace00_count;
        last_workspace04 = input.workspace04_count;
        last_workspace08 = input.workspace08_count;
        last_scheduled = scheduled;
        last_movement_carry = result.movement_update_called && input.movement_carry;
        last_action = action + 1U;
    }
};
} // namespace thandor
