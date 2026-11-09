#pragma once
#include "thandor/model.hpp"
#include <bit>
#include <cstdint>
#include <limits>
#include <optional>

namespace thandor {
// ArmyRuntimeClass_UpdateTransformAndDamageEffect, original 0x00525130.
// MDL +24/+28 are signed Q12 endpoints for class15, not angular limits.
inline std::optional<int32_t> simulation_storage_child_z_q12(
    const ModelVisualNode& parent,uint32_t child_slot,uint32_t current_q4,
    uint32_t limit_q4) noexcept {
    if(parent.runtime_class_id!=15U||child_slot!=0U||limit_q4==0U)return std::nullopt;
    const auto empty=parent.simulation.pitch_min_turn16;
    const auto full=parent.simulation.pitch_max_turn16;
    const auto delta=std::bit_cast<int32_t>(static_cast<uint32_t>(full)-static_cast<uint32_t>(empty));
    const int64_t product=static_cast<int64_t>(delta)*std::bit_cast<int32_t>(current_q4);
    const int64_t quotient=product/std::bit_cast<int32_t>(limit_q4);
    // Preserve signed IDIV truncation. Malformed inputs that would trap in x86 retain the pose.
    if(quotient<std::numeric_limits<int32_t>::min()||quotient>std::numeric_limits<int32_t>::max())
        return std::nullopt;
    return std::bit_cast<int32_t>(static_cast<uint32_t>(quotient)+static_cast<uint32_t>(empty));
}
} // namespace thandor
