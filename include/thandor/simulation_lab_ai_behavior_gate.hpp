#pragma once
#include <cstdint>

namespace thandor {
// Open Thandor gameplay/ai/units.cpp::AiUnitBehavior_UpdateOwnUnits.
// Movement command bits 0x1 (active), 0x2 (locked), 0x10 (route point reached).
// The decrement is intentionally only performed when one of these flags is set;
// an idle unit's stale cooldown must not prevent it from selecting a task.
inline bool simulation_lab_ai_unit_behavior_ready(std::uint32_t movement_flags,
                                                   std::int32_t& cooldown) noexcept {
    constexpr std::uint32_t busy=0x1U|0x2U|0x10U;
    if((movement_flags&busy)==0U)return true;
    if((movement_flags&0x2U)!=0U)return false;
    --cooldown;
    if(cooldown!=0){
        if(cooldown>0)return false;
        ++cooldown;
    }
    return true;
}
} // namespace thandor
