#include "thandor/simulation_lab.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

thandor::SimulationMotionProfile motion(int32_t speed = 0x0200) {
    thandor::SimulationMotionProfile result;
    result.speed_q12_per_tick = speed;
    result.yaw_max_velocity_turn16 = 0x1000U;
    result.yaw_acceleration_turn16 = 0x0400U;
    result.collision_radius_q12 = 0x0200;
    return result;
}
}

int main() try {
    auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
    thandor::SimulationLabConfig config;
    config.run_count = 1U;
    config.duration_ticks = 64U;
    config.parameter_index = 1U;

    // Class 2: Simulation Lab must advance exactly the same two signed track channels as the
    // ordinary 20 Hz presentation callback for the same root delta.
    std::array<thandor::SimulationLabSeedEntity, 2> tank{};
    tank[0].source_placement_index = 0U;
    tank[0].army_asset_id = 0x200U;
    tank[0].faction_index = 1U;
    tank[0].runtime_model_class_id = 2U;
    tank[0].motion = motion();
    tank[0].actor.position = {8.0F, 8.0F, 0.0F};
    tank[0].tank_track_animation_enabled = true;
    tank[0].tank_track_local_bounds_y0 = -0.75F;
    tank[0].tank_track_texture_travel_scale = 0x180;
    tank[1].source_placement_index = 1U;
    tank[1].army_asset_id = 0x201U;
    tank[1].faction_index = 2U;
    tank[1].runtime_model_class_id = 0U;
    tank[1].actor.position = {24.0F, 10.0F, 0.0F};

    auto expected_tank = tank[0].actor;
    expected_tank.move_target = tank[1].actor.position;
    const auto before_tank = expected_tank.position;
    const auto before_tank_yaw = expected_tank.body_yaw_turn16;
    const bool expected_tank_moved = thandor::simulation_tick_actor_motion(expected_tank, tank[0].motion);
    thandor::simulation_update_tank_track_offsets(expected_tank, before_tank, before_tank_yaw,
        expected_tank.position, expected_tank.body_yaw_turn16,
        tank[0].tank_track_local_bounds_y0, tank[0].tank_track_texture_travel_scale);
    require(expected_tank_moved, "tank control did not produce a movement/turn delta");

    thandor::SimulationLabExperiment tank_experiment;
    tank_experiment.reset(config, parameters, tank);
    tank_experiment.step(field, 1U);
    const auto& tank_run = tank_experiment.runs().front();
    const auto& lab_tank = tank_run.entities[0];
    require(lab_tank.actor.primary_track_texture_offset_u == expected_tank.primary_track_texture_offset_u &&
            lab_tank.actor.secondary_track_texture_offset_u == expected_tank.secondary_track_texture_offset_u,
            "Simulation Lab class-2 track offsets diverged from the ordinary callback helper");
    require(tank_run.metrics.tank_track_updates == 1U,
            "Simulation Lab did not record the class-2 track presentation update");

    // Class 3: use a contact numerator deliberately different from movement speed. The old Lab
    // incorrectly passed speed into the articulated controller, which can desynchronize stock
    // Orang/Emu/Gorilla feet from ordinary simulation.
    std::array<thandor::SimulationLabSeedEntity, 2> walker{};
    walker[0].source_placement_index = 2U;
    walker[0].army_asset_id = 0x300U;
    walker[0].faction_index = 1U;
    walker[0].runtime_model_class_id = 3U;
    walker[0].motion = motion(0x0100);
    walker[0].articulated_contact_interpolation_rate_q12 = 0x0900;
    walker[0].actor.position = {7.0F, 7.0F, 0.0F};
    walker[1].source_placement_index = 3U;
    walker[1].army_asset_id = 0x301U;
    walker[1].faction_index = 2U;
    walker[1].actor.position = {20.0F, 9.0F, 0.0F};

    auto expected_walker = walker[0].actor;
    expected_walker.move_target = walker[1].actor.position;
    const auto walker_before = expected_walker.position;
    const auto walker_yaw_before = expected_walker.body_yaw_turn16;
    thandor::simulation_tick_articulated_actor_motion(expected_walker, walker[0].motion,
        walker[0].articulated_contact_interpolation_rate_q12);
    thandor::simulation_update_articulated_contact_transition(expected_walker,
        walker_before, walker_yaw_before, expected_walker.position,
        expected_walker.body_yaw_turn16, walker[0].articulated_contact_interpolation_rate_q12,
        walker[0].motion.speed_q12_per_tick);

    thandor::SimulationLabExperiment walker_experiment;
    walker_experiment.reset(config, parameters, walker);
    walker_experiment.step(field, 1U);
    const auto& lab_walker = walker_experiment.runs().front().entities[0].actor;
    require(lab_walker.body_yaw_turn16 == expected_walker.body_yaw_turn16 &&
            lab_walker.articulated_contact_progress_q12 == expected_walker.articulated_contact_progress_q12 &&
            lab_walker.articulated_contact_event_serial == expected_walker.articulated_contact_event_serial,
            "Simulation Lab class-3 contact state diverged from the ordinary movement/contact callback sequence");

    // Explicit stop-settle gate: no target and no planar movement must still finish a partially
    // active articulated contact rather than freezing a foot in mid-air.
    std::array<thandor::SimulationLabSeedEntity, 1> settling{};
    settling[0] = walker[0];
    settling[0].actor.move_target.reset();
    settling[0].actor.articulated_contact_transition_active = true;
    settling[0].actor.articulated_contact_progress_q12 = 0x0F00U;
    settling[0].actor.articulated_last_contact_advance_q12 = 0x0100U;
    settling[0].actor.articulated_was_moving = true;
    thandor::SimulationLabExperiment settle_experiment;
    settle_experiment.reset(config, parameters, settling);
    settle_experiment.step(field, 1U);
    const auto& settled = settle_experiment.runs().front().entities[0].actor;
    require(settled.articulated_contact_progress_q12 == 0U &&
            settled.articulated_contact_event_serial == 1U &&
            !settled.articulated_contact_transition_active,
            "Simulation Lab failed to finish a class-3 stop-settle contact transition");

    // Class 17: deployment is a lifecycle pose, not contingent on root translation. Lab must
    // advance it on idle ticks just like ordinary simulation_tick_class17_pose().
    std::array<thandor::SimulationLabSeedEntity, 1> class17{};
    class17[0].source_placement_index = 4U;
    class17[0].army_asset_id = 0x400U;
    class17[0].faction_index = 1U;
    class17[0].runtime_model_class_id = 17U;
    class17[0].actor.position = {12.0F, 12.0F, 0.0F};
    thandor::simulation_initialize_class17_deployment(class17[0].actor, true, true);
    const auto deploy_before = class17[0].actor.class17_rotor_deploy_ticks_remaining;
    thandor::SimulationLabExperiment class17_experiment;
    class17_experiment.reset(config, parameters, class17);
    class17_experiment.step(field, 1U);
    const auto& class17_run = class17_experiment.runs().front();
    require(class17_run.entities[0].actor.class17_rotor_deploy_ticks_remaining + 1U == deploy_before &&
            class17_run.metrics.class17_pose_updates == 1U,
            "Simulation Lab did not advance the idle class-17 deployment pose");

    // Sound state is a deterministic pure gate. Ensure the per-snapshot movement flags published
    // by Lab produce the same requested class-2 loop slot as ordinary simulation.
    const thandor::SimulationRuntimeSoundProfile sound_profile{2U, 31U, 32U, 0U, 0U};
    const auto sound_indices = thandor::simulation_runtime_looping_sound_indices(sound_profile,
        {true, lab_tank.last_translated, lab_tank.last_turned, false});
    require((sound_indices[0] == 31U || sound_indices[1] == 31U ||
             sound_indices[0] == 32U || sound_indices[1] == 32U),
            "Simulation Lab published movement flags that cannot drive the class-2 spatial-loop gate");

    std::cout << "V543_SIMULATION_LAB_PRESENTATION_PARITY PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V543_SIMULATION_LAB_PRESENTATION_PARITY FAIL: " << error.what() << '\n';
    return 1;
}
