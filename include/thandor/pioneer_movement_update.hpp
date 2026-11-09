#pragma once
#include <array>
#include <bit>
#include <cstdint>
namespace thandor {
struct MovementPoint { std::int32_t x{},y{}; };
struct MovementResult { MovementPoint point{}; bool carry{}; };
struct MovementRouteResult { MovementPoint point{},fallback{}; bool carry{}; };
struct PioneerMovementState {
    std::uint32_t flags{},retry{},queue_count{};
    MovementPoint model{},stored{},last_checked{},primary{},fallback{};
    std::array<MovementPoint,8> queue{};
};
constexpr std::int32_t movement_sub(std::int32_t a,std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::uint32_t(a)-std::uint32_t(b));
}
constexpr std::uint32_t movement_abs(std::int32_t a,std::int32_t b) {
    auto d=std::uint32_t(a)-std::uint32_t(b);
    return (d&0x80000000U) ? 0U-d : d;
}
// Port of 0x0051CEE0. Dependencies must implement their original side effects.
// Resolver and queue callbacks may mutate state; model remains the live position.
template<class Dependencies>
MovementResult pioneer_movement_update(PioneerMovementState& s,Dependencies& deps) {
    if(!(s.flags&0x10U)) {
        if(s.flags&1U) return {s.stored,false};
    } else {
        s.flags&=0xFFFFFFEFU;
        const auto dx=movement_sub(s.fallback.x,s.model.x);
        const auto dy=movement_sub(s.fallback.y,s.model.y);
        if(dx<0x40 && dy<0x40 && dx>-0x40 && dy>-0x40) {
            if(s.flags&8U) {
                const auto next=s.queue[0];
                --s.queue_count;
                if(!s.queue_count)s.flags&=0xFFFFFFF7U;
                else for(unsigned i=0;i<7;++i)s.queue[i]=s.queue[i+1];
                deps.queue(s,next);
                return pioneer_movement_update(s,deps);
            }
        } else {
            if(s.retry && s.model.x==s.last_checked.x && s.model.y==s.last_checked.y)
                return {s.stored,false};
            s.retry=0x40;
            s.last_checked=s.model;
            const auto route=deps.resolve(s,s.fallback);
            if(!route.carry) {
                s.stored=route.point;s.fallback=route.fallback;
                return {route.point,false};
            }
        }
    }
    // Original final-distance test is unsigned JA, unlike the signed Pioneer gate.
    if(movement_abs(s.model.x,s.primary.x)<=0x1000U &&
       movement_abs(s.model.y,s.primary.y)<=0x1000U) {
        s.flags&=0xFFFFFF06U;
        return {s.model,true};
    }
    return deps.retarget(s,s.primary);
}
}
