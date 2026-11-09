#include "thandor/simulation.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

float distance_xy(thandor::Vec3f a, thandor::Vec3f b) {
    return std::hypot(a.x-b.x, a.y-b.y);
}
}

int main() try {
    using namespace thandor;

    FieldGrid field = FieldGrid::create(256U, 256U, 0, 0U);
    const auto& center_cell = field.at(128U, 128U);
    const Vec3f root{q12_to_float(center_cell.world_x_q12),
                     q12_to_float(center_cell.world_y_q12), 4.0F};
    const Vec3f attachment{root.x + 2.0F, root.y, root.z};
    const auto exact = simulation_linked_effect_shot_geometry(root, attachment);
    require(distance_xy(exact.launch_position, attachment) < 1.0e-6F &&
            std::abs(exact.launch_position.z-attachment.z) < 1.0e-6F,
            "linked EFF shot no longer launches from the transformed type-2 attachment");
    require(std::abs(exact.aim_position.x-(root.x+4.0F)) < 1.0e-6F &&
            std::abs(exact.aim_position.y-root.y) < 1.0e-6F &&
            std::abs(exact.aim_position.z-root.z) < 1.0e-6F,
            "linked EFF reflected aim point changed");

    ShotDefinitionRecord shot{};
    shot.definition_id = 28U;
    shot.trajectory_mode = 1;
    shot.launch_speed_q12 = 450;
    shot.ballistic_divisor_q12 = 4;
    shot.projectile_lifetime_ticks = 900U;
    shot.terrain_impact_effect_ids[0] = 2U;

    SimulationWeaponProfile weapon{};
    weapon.shot_definition_id = 28U;
    weapon.reload_ticks = 1U;
    weapon.launch_attachment_count = 1U;

    uint64_t serial = 1U;
    const std::array<Vec3f, 1> launch{exact.launch_position};
    auto volley = simulation_create_volley(0U, weapon, launch, exact.aim_position,
                                            shot, 0U, serial);
    require(volley.size() == 1U, "synthetic lava linked shot was not created");
    require(volley.front().travel_ticks == shot.projectile_lifetime_ticks,
            "ballistic linked shot still terminates at the reflected aim point");
    const Vec3f after_aim = simulation_projectile_position(volley.front(), shot, 40U);
    require(after_aim.x > exact.aim_position.x + 0.05F,
            "ballistic linked shot still snaps to its reflected aim point");

    std::vector<Vec3f> impacts;
    impacts.reserve(8U);
    constexpr float kInvSqrt2 = 0.7071067811865475F;
    const std::array<Vec3f, 8> directions{{
        {1.0F,0.0F,0.0F},{kInvSqrt2,kInvSqrt2,0.0F},{0.0F,1.0F,0.0F},
        {-kInvSqrt2,kInvSqrt2,0.0F},{-1.0F,0.0F,0.0F},{-kInvSqrt2,-kInvSqrt2,0.0F},
        {0.0F,-1.0F,0.0F},{kInvSqrt2,-kInvSqrt2,0.0F},
    }};
    for (const Vec3f direction : directions) {
        const Vec3f point{root.x+direction.x*2.0F, root.y+direction.y*2.0F, root.z};
        const auto geometry = simulation_linked_effect_shot_geometry(root, point);
        const std::array<Vec3f, 1> one_launch{geometry.launch_position};
        auto one = simulation_create_volley(0U, weapon, one_launch, geometry.aim_position,
                                            shot, 0U, serial);
        require(one.size() == 1U, "one of the eight lava arcs failed projectile creation");
        simulation_resolve_projectile_surface_impact(one.front(), shot, field);
        require(one.front().impact_kind == SimulationImpactKind::TerrainSurface,
                "lava arc did not resolve a terrain impact");
        require(one.front().travel_ticks < shot.projectile_lifetime_ticks,
                "lava arc never contacted terrain before SHT lifetime expiry");
        impacts.push_back(one.front().impact_position);
    }
    for (std::size_t i=0; i<impacts.size(); ++i) {
        for (std::size_t j=i+1; j<impacts.size(); ++j) {
            require(distance_xy(impacts[i], impacts[j]) > 0.25F,
                    "eight lava linked shots collapsed onto one impact location");
        }
    }

    std::cout << "V558_LAVA_LINKED_SHOT_RUNTIME attachmentLaunch=exact ballisticLifetime=exact "
                 "distinctImpacts=8 PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V558 lava linked-shot runtime failed: " << error.what() << '\n';
    return 1;
}
