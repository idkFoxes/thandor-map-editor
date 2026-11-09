#include "shadow_receiver_surface_guard.hpp"
#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}
int main() try {
    using editor::Vec3f;
    const auto plane=[](float,float)->std::optional<float> { return 0.0F; };
    const Vec3f a{0,0,0.035F},b{4,0,0.035F},c{0,4,0.035F};
    check(!editor::shadow_receiver_triangle_is_upright(a,b,c),"valid planar shadow was rejected");
    check(!editor::shadow_receiver_triangle_has_unresolved_contact(a,b,c,plane,0.035F),
          "valid planar contact was rejected");
    // The diagonal across a depression hangs a shadow in mid-air, even if the
    // twelve original retail contact points were individually grounded.
    const auto depression=[](float x,float y)->std::optional<float> {
        return x>1.1F&&y>1.1F ? -1.0F : 0.0F;
    };
    check(editor::shadow_receiver_triangle_has_unresolved_contact(a,b,c,depression,0.035F),
          "hovering midpoint/centroid contact was accepted");
    check(editor::shadow_receiver_triangle_has_unresolved_contact(a,b,c,
          [](float x,float)->std::optional<float> {if(x>1.9F) return std::nullopt; return 0.0F;},0.035F),
          "triangle crossing an invalid region was accepted");
    const Vec3f tiny_a{0,0,1.035F},tiny_b{0.2F,0,1.035F},tiny_c{0,0.2F,1.035F};
    check(editor::shadow_receiver_triangle_has_unresolved_contact(tiny_a,tiny_b,tiny_c,plane,0.035F),
          "small flat floating receiver bypassed terrain contact checks");
    check(editor::shadow_receiver_triangle_has_unresolved_contact(tiny_a,tiny_b,tiny_c,
          [](float,float)->std::optional<float>{return std::nullopt;},0.035F),
          "small receiver outside the field bypassed terrain contact checks");
    check(!editor::shadow_receiver_triangle_has_unresolved_contact(
          {0,0,0.035F},{0.2F,0,0.035F},{0,0.2F,0.035F},plane,0.035F),
          "small grounded receiver was rejected");
    const Vec3f wall_a{0,0,0},wall_b{0.05F,0,4},wall_c{0.1F,0.1F,2};
    check(editor::shadow_receiver_triangle_is_upright(wall_a,wall_b,wall_c),
          "almost vertical dark wall was accepted");
    const Vec3f slope_a{0,0,0},slope_b{3,0,1.5F},slope_c{0,3,1.0F};
    check(!editor::shadow_receiver_triangle_is_upright(slope_a,slope_b,slope_c),
          "ordinary hillside was mistaken for a vertical wall");
    std::cout<<"RENDERING_SHADOW_RECEIVER_SURFACE_GUARD_PASS\n";
} catch(const std::exception& e) {
    std::cerr<<"RENDERING_SHADOW_RECEIVER_SURFACE_GUARD_FAIL "<<e.what()<<'\n';return 1;
}
