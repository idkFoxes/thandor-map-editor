#include "thandor/simulation.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() try {
    using namespace thandor;

    ShotDefinitionRecord shot{};
    shot.definition_id = 28U;
    shot.trajectory_mode = 1;
    shot.launch_speed_q12 = 450;
    shot.ballistic_divisor_q12 = 4;
    shot.projectile_lifetime_ticks = 900U;
    shot.terrain_impact_effect_ids[0] = 2U;

    SimulationWeaponProfile synthetic{};
    synthetic.shot_definition_id = shot.definition_id;
    synthetic.reload_ticks = 1U;
    synthetic.launch_attachment_count = 1U;

    FieldGrid field = FieldGrid::create(128U, 128U, 0, 0U);
    const auto& center = field.at(64U, 64U);
    const Vec3f root{q12_to_float(center.world_x_q12), q12_to_float(center.world_y_q12), 4.0F};
    const auto geometry = simulation_linked_effect_shot_geometry(
        root, Vec3f{root.x + 2.0F, root.y, root.z});
    const std::array<Vec3f, 1> launch{geometry.launch_position};
    uint64_t serial = 77U;
    auto volley = simulation_create_volley(
        std::numeric_limits<std::size_t>::max(), 0U, synthetic, launch,
        geometry.aim_position, shot, 123U, serial, 3U, std::nullopt,
        SimulationProjectileOwnerKind::Environmental, 0U);
    require(volley.size() == 1U, "environmental linked shot was not materialized");
    const auto& projectile = volley.front();
    require(projectile.owner_kind == SimulationProjectileOwnerKind::Environmental,
            "environmental shot lost its ownerless runtime kind");
    require(projectile.owner_placement_index == std::numeric_limits<std::size_t>::max(),
            "environmental shot fabricated an authored placement owner");
    require(projectile.owner_runtime_serial == 0U,
            "environmental shot fabricated a runtime army owner");
    require(projectile.spawn_tick == 123U && projectile.fired_attachment_selector == 3U,
            "environmental shot lost deterministic map-effect spawn identity");

    auto resolved = projectile;
    simulation_resolve_projectile_surface_impact(resolved, shot, field);
    require(resolved.impact_kind == SimulationImpactKind::TerrainSurface,
            "environmental shot did not enter the normal projectile surface resolver");
    require(resolved.travel_ticks < resolved.lifetime_ticks,
            "environmental ballistic shot did not resolve a real contact before lifetime expiry");

    std::cout << "V559_ENVIRONMENTAL_SHOT_OWNER owner=null sharedProjectileRuntime=1 PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V559 environmental shot owner test failed: " << error.what() << '\n';
    return 1;
}
