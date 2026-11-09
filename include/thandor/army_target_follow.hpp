#pragma once
#include "thandor/pioneer_command_chain.hpp"
namespace thandor {
// ArmyRuntime_StartClampedMoveCommand. Clamp is the existing geometry adapter;
// the flags, guard, final-target preservation and retry are native operations.
template<class Dependencies>
bool army_start_clamped_follow(PioneerCommandState& s,MovementPoint target,Dependencies& deps) {
    if((s.flags&3U)||s.retry)return false;
    target=deps.clamp(s.primary,target,0x2000);
    s.flags=(s.flags&~0x458U)|0xA1U;
    const auto route=deps.resolve(s,target);
    s.fallback=route.fallback;s.stored=route.point;
    s.retry=0x40;s.last_checked=s.model;return true;
}
// The native forward overlap copy repeats fallback into all eight slots.
// A memmove/shift here would change the recovered queue behavior.
template<class Dependencies>
bool army_start_fallback_follow(PioneerCommandState& s,MovementPoint target,Dependencies& deps) {
    if((s.flags&0x202U)||s.retry)return false;
    s.flags&=~0x498U;
    if(s.flags&1U){
        for(auto& point:s.queue)point=s.fallback;
        s.flags|=8U;if(s.queue_count<8U)++s.queue_count;
    }
    s.flags|=0x61U;
    const auto route=deps.resolve(s,target);
    s.fallback=route.fallback;s.primary=route.fallback;s.stored=route.point;
    s.retry=0x40;s.last_checked=s.model;return true;
}
inline void army_reset_follow(PioneerCommandState& s,bool ai) noexcept {
    if(!(s.flags&8U))s.flags&=~1U;
    if(ai){if(s.flags&8U)s.fallback=s.stored=s.model;}
    else if(!(s.flags&0x200U))s.fallback=s.primary=s.stored=s.model;
    s.flags&=~0xF0U;
}
}
