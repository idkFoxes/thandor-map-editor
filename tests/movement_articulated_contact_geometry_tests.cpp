#include "thandor/simulation_lab.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool near(float a, float b, float epsilon = 1.0e-5F) {
    return std::abs(a - b) <= epsilon;
}
}

int main() try {
    const auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};

    thandor::SimulationLabSeedEntity walker;
    walker.source_placement_index = 1U;
    walker.army_asset_id = 0x300U;
    walker.faction_index = 1U;
    walker.runtime_model_class_id = 3U;
    walker.motion.speed_q12_per_tick = 0x0200; // 0.125 world units/tick.
    walker.motion.yaw_max_velocity_turn16 = 0x0800U;
    walker.articulated_contact_interpolation_rate_q12 = 0x0400;
    walker.articulated_contact_lift_q12 = 0x0100;
    walker.articulated_contact_steering_limit_turn16 = 0x0800U;
    walker.articulated_root_height_q12 = 0x1000;
    walker.articulated_contact_geometry_enabled = true;
    walker.articulated_contact_local_offsets[0] = {0.0F, -1.0F, 0.0F};
    walker.articulated_contact_local_offsets[1] = {0.0F, 1.0F, 0.0F};
    walker.actor.position = {10.0F, 10.0F, 1.0F};
    walker.actor.body_yaw_turn16 = 0U;
    walker.actor.articulated_contacts_initialized = true;
    walker.actor.articulated_foot_anchor_world[0] = {10.0F, 9.0F, 0.0F};
    walker.actor.articulated_foot_anchor_world[1] = {10.0F, 11.0F, 0.0F};
    walker.actor.articulated_foot_target_world = walker.actor.articulated_foot_anchor_world;
    walker.actor.articulated_contact_heading_turn16 = {0U, 0U};
    walker.actor.articulated_contact_target_heading_turn16 = {0U, 0U};
    walker.actor.articulated_pose_swing_side = 0U;
    walker.actor.articulated_contact_step_q12 = 0x2000;

    thandor::SimulationLabSeedEntity enemy;
    enemy.source_placement_index = 2U;
    enemy.army_asset_id = 0x301U;
    enemy.faction_index = 2U;
    enemy.actor.position = {20.0F, 10.0F, 0.0F};

    std::array<thandor::SimulationLabSeedEntity, 2> seeds{walker, enemy};
    thandor::SimulationLabConfig config;
    config.run_count = 1U;
    config.duration_ticks = 8U;
    config.sweep_mode = thandor::SimulationLabSweepMode::Fixed;
    config.parameter_index = 1U;

    thandor::SimulationLabExperiment experiment;
    experiment.reset(config, parameters, seeds);
    experiment.step(field, 1U);
    const auto& actor = experiment.runs().front().entities.front().actor;

    require(actor.articulated_contact_transition_active,
            "worker did not start the class-3 contact transition");
    require(actor.articulated_pose_transition_active,
            "worker did not freeze a class-3 contact target");
    require(actor.articulated_contact_progress_q12 == 0x0400U,
            "first contact progress does not match the recovered movement*step domain");
    require(near(actor.articulated_foot_target_world[0].x, 10.25F) &&
            near(actor.articulated_foot_target_world[0].y, 9.0F),
            "worker did not freeze the authored terminal-foot target from the authored full stride");
    const std::int32_t expected_step = thandor::simulation_articulated_contact_step_q12(
        0x0400, 0x0100, 0x0400);
    require(actor.articulated_contact_step_q12 == expected_step && expected_step != 0x2000,
            "worker did not replace the default contact step with distance-derived class-3 timing");
    require(near(actor.position.x, 10.03125F) && near(actor.position.y, 10.0F) &&
            near(actor.position.z, 1.0234375F),
            "worker root was not recentered from the two historical contacts with the lift arc");

    const float frozen_target_x = actor.articulated_foot_target_world[0].x;
    experiment.step(field, 1U);
    const auto& second = experiment.runs().front().entities.front().actor;
    require(near(second.articulated_foot_target_world[0].x, frozen_target_x),
            "active contact target drifted instead of remaining frozen");
    require(second.articulated_contact_progress_q12 > 0x0400U,
            "distance-derived contact step did not advance on the next worker tick");
    require(second.position.x > 10.03125F && second.position.x < 10.25F,
            "worker root did not interpolate between the planted and frozen contacts");

    std::cout << "V544_SIMULATION_LAB_ARTICULATED_CONTACT_GEOMETRY PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V544_SIMULATION_LAB_ARTICULATED_CONTACT_GEOMETRY FAIL: "
              << error.what() << '\n';
    return 1;
}
