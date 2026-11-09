#pragma once
#include "thandor/pioneer_movement_update.hpp"
namespace thandor {
struct PioneerCommandState : PioneerMovementState {
    std::uint32_t command_mode{};
    bool definition_allows_movement{};
};
// Original Variant A, 0x0051C8E0. Queue and route effects remain dependencies.
template<class Dependencies>
void pioneer_move_command_a(PioneerCommandState& s,MovementPoint target,Dependencies& deps) {
    if(!s.definition_allows_movement)return;
    if(s.flags&2U) {
        s.flags&=0xFFFFFFF7U;
        deps.queue(s,target);
        return;
    }
    s.flags=(s.flags|0x241U)&0xFFFFFB47U;
    s.command_mode&=0xFFFFFFEFU;
    const auto route=deps.resolve(s,target);
    // The original writes these fields regardless of resolver carry.
    s.fallback=route.fallback;
    s.primary=route.fallback;
    s.stored=route.point;
    s.retry=0x40U;
    s.last_checked=s.model;
}
}
