#pragma once
#include <bit>
#include <cstdint>

namespace thandor {
// Isolated port of the entry/arrival gate at 0x0053B620. The caller must supply
// the real UpdateMovementAndWaypoints result, not derive it from a target optional.
enum class PioneerGateAction : std::uint32_t {
    NoCommand, BootstrapForward, ScoreSites, ResetAtModel, AwaitFallback
};
struct PioneerGateInput {
    std::uint32_t entry_flags{}, flags_after_movement{};
    bool movement_carry{};
    std::uint32_t workspace00_count{}, workspace04_count{}, workspace08_count{};
    std::int32_t model_x{}, model_y{}, fallback_x{}, fallback_y{};
};
struct PioneerGateResult {
    PioneerGateAction action{};
    std::uint32_t flags{};
    bool movement_update_called{};
};
constexpr std::int32_t pioneer_x86_abs_delta(std::int32_t a, std::int32_t b) {
    auto value=static_cast<std::uint32_t>(a)-static_cast<std::uint32_t>(b);
    if ((value & 0x80000000U)!=0U) value=0U-value;
    return std::bit_cast<std::int32_t>(value);
}
constexpr PioneerGateResult pioneer_movement_gate(PioneerGateInput in) {
    const bool update=(in.entry_flags & 0x100U)==0U;
    auto flags=update ? in.flags_after_movement : in.entry_flags;
    if (update && in.movement_carry) {
        if(in.workspace00_count==in.workspace04_count)
            return {PioneerGateAction::BootstrapForward,flags,true};
        if((flags & 0x100U)==0U && in.workspace08_count!=0U)
            return {PioneerGateAction::ScoreSites,flags,true};
        return {PioneerGateAction::NoCommand,flags,true};
    }
    flags|=0x100U;
    // Original SUB/NEG/JGE instructions use wrapped dwords and signed comparisons.
    if(pioneer_x86_abs_delta(in.fallback_x,in.model_x)<0x1B03 &&
       pioneer_x86_abs_delta(in.fallback_y,in.model_y)<0x1B03)
        return {PioneerGateAction::ResetAtModel,flags & 0xFFFFFFC6U,update};
    return {PioneerGateAction::AwaitFallback,flags,update};
}
}
