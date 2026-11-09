#pragma once

// Editor-only safety gate for generated projected shadows. The game-compatible
// caster rasterizer / 12-contact receiver in core are intentionally untouched.
// A receiver triangle spanning a steep height discontinuity can become a near-
// vertical dark wall; interpolation gaps can leave a visible floating sheet.

#include "math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace editor {

inline bool shadow_receiver_triangle_is_upright(
    const Vec3f& a, const Vec3f& b, const Vec3f& c) noexcept {
    const auto xy_distance=[](const Vec3f& p,const Vec3f& q) noexcept {
        return std::hypot(p.x-q.x,p.y-q.y);
    };
    const float vertical=std::max({a.z,b.z,c.z})-std::min({a.z,b.z,c.z});
    if(!std::isfinite(vertical)||vertical<0.65F) return false;
    const float horizontal=std::max({xy_distance(a,b),xy_distance(b,c),xy_distance(c,a)});
    // Avoid nearly vertical walls, not genuine terrain-following hillside shadows.
    return horizontal < 0.01F || vertical>horizontal*1.4F;
}

template<typename TerrainHeight>
bool shadow_receiver_triangle_has_unresolved_contact(
    const Vec3f& a,const Vec3f& b,const Vec3f& c,TerrainHeight&& terrain_height,
    float surface_bias,float maximum_contact_error=0.18F) {
    // Sample halfway across the long face, not just at the original receiver
    // contact vertices. Reaching the maximum tessellation depth does not make
    // a suspended triangle safe to render.
    // A small, flat receiver can still hang above a depression or cross an
    // invalid field region. Size and height span do not prove terrain contact.
    const std::array<Vec3f,4> samples{{
        {(a.x+b.x)*0.5F,(a.y+b.y)*0.5F,(a.z+b.z)*0.5F},
        {(b.x+c.x)*0.5F,(b.y+c.y)*0.5F,(b.z+c.z)*0.5F},
        {(c.x+a.x)*0.5F,(c.y+a.y)*0.5F,(c.z+a.z)*0.5F},
        {(a.x+b.x+c.x)/3.0F,(a.y+b.y+c.y)/3.0F,(a.z+b.z+c.z)/3.0F}}};
    for(const auto& sample:samples) {
        const std::optional<float> ground=terrain_height(sample.x,sample.y);
        // Never span holes, invalid FLD regions or map-edge discontinuities.
        if(!ground || !std::isfinite(*ground)) return true;
        if(std::fabs(sample.z-(*ground+std::max(surface_bias,0.0F)))>maximum_contact_error)
            return true;
    }
    return false;
}

} // namespace editor
