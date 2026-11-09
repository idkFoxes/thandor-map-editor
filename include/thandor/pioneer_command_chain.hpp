#pragma once
#include "thandor/pioneer_move_command.hpp"
namespace thandor {
template<class Dependencies>
void pioneer_command_chain(PioneerCommandState&,MovementPoint,unsigned,Dependencies&);
// mode 0: Variant A; 1: Variant B; 2: waypoint helper at 0x0051CA60.
// Valid state requires queue_count <= 8 and excludes flags 2+0x80 together
// while the definition permits movement: the original mutually recurses there.
template<class Dependencies>
void pioneer_command_chain(PioneerCommandState& s,MovementPoint target,unsigned mode,Dependencies& deps) {
    if(mode==2) {
        if(!(s.flags&0x80U) && (s.flags&3U)) {
            if(!(s.flags&8U)) {s.queue_count=0;s.flags|=8U;}
            auto index=s.queue_count;
            s.flags|=0x40U;
            if(index<8U)++index;
            s.command_mode&=0xFFFFFBEFU;
            s.queue[index-1]=target;
            s.queue_count=index;
        } else pioneer_command_chain(s,target,0,deps);
        return;
    }
    if(!s.definition_allows_movement)return;
    if(s.flags&2U) {
        s.flags&=0xFFFFFFF7U;
        pioneer_command_chain(s,target,2,deps);
        return;
    }
    s.flags=(s.flags|0x241U)&(mode==0 ? 0xFFFFFB47U : 0xFFFFFF4FU);
    s.command_mode&=0xFFFFFFEFU;
    const auto route=deps.resolve(s,target);
    s.fallback=route.fallback;s.primary=route.fallback;s.stored=route.point;
    s.retry=0x40U;s.last_checked=s.model;
}
// Returns the carry observed at 0x0051CD30 exit, not a coordinate result.
template<class Dependencies>
bool pioneer_direct_command(PioneerCommandState& s,MovementPoint target,Dependencies& deps) {
    if(s.flags&2U)return false; // TEST clears CF on the blocked branch.
    s.flags=(s.flags|0x81U)&0xFFFFFF87U;
    const auto route=deps.resolve(s,target);
    s.fallback=route.fallback;s.stored=route.point;
    s.retry=0x40U;s.last_checked=s.model;
    return route.carry;
}
}
