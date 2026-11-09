#include "thandor/simulation_lab.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

thandor::SimulationLabSeedEntity entity(std::size_t placement, std::uint32_t asset,
                                         std::uint32_t faction, std::uint32_t runtime_class,
                                         thandor::Vec3f position) {
    thandor::SimulationLabSeedEntity value;
    value.source_placement_index = placement;
    value.army_asset_id = asset;
    value.faction_index = faction;
    value.runtime_model_class_id = runtime_class;
    value.actor.position = position;
    value.root_model_maximum_integrity = 1000;
    return value;
}
} // namespace

int main() try {
    const auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};

    auto shooter = entity(0U, 0x801U, 1U, 10U, {8.0F, 8.0F, 10.0F});
    auto target = entity(1U, 0x802U, 2U, 11U, {12.0F, 8.0F, 10.0F});
    target.shot_impact_class_index = 3U;
    target.root_model_maximum_integrity = 5000;

    thandor::SimulationWeaponProfile profile;
    profile.runtime_class_id = 10U;
    profile.shot_definition_id = 0x608U;
    profile.reload_ticks = 1000U; // isolate one beam for the complete service lifetime
    profile.maximum_range_q12 = thandor::float_to_q12(100.0F);
    profile.yaw_max_velocity_turn16 = 0x7FFF;
    profile.yaw_acceleration_turn16 = 0x7FFF;
    profile.pitch_max_velocity_turn16 = 0x7FFF;
    profile.pitch_acceleration_turn16 = 0x7FFF;
    profile.pitch_min_turn16 = -0x4000;
    profile.pitch_max_turn16 = 0x4000;
    profile.launch_attachment_count = 1U;

    thandor::ShotDefinitionRecord shot;
    shot.definition_id = profile.shot_definition_id;
    shot.trajectory_mode = static_cast<std::int32_t>(thandor::ShotTrajectoryMode::DirectLine);
    shot.launch_speed_q12 = thandor::float_to_q12(4.0F);
    shot.projectile_lifetime_ticks = 20U;
    shot.launch_effect_id = 0xB01U;
    shot.target_impact_effect_ids[3] = 0xB02U;
    shot.target_impact_damage_q12[3] = 2000;

    shooter.combat_weapons.push_back({profile, shot});
    shooter.actor.weapons.resize(1U);
    shooter.actor.weapons[0].pitch.angle_turn16 = 0U;
    std::array<thandor::SimulationLabSeedEntity, 2> seeds{shooter, target};

    thandor::SimulationLabConfig config;
    config.run_count = 1U;
    config.duration_ticks = 64U;
    config.parameter_index = 100U;
    config.sweep_mode = thandor::SimulationLabSweepMode::Fixed;

    thandor::SimulationLabExperiment experiment;
    experiment.reset(config, parameters, seeds);
    auto& run = experiment.runs().front();

    // Fire once. Beam maintenance starts on the next simulation tick, matching the runtime's
    // create-then-maintain ordering.
    experiment.step(field, 1U);
    require(run.metrics.weapon_shots_fired == 1U && run.projectiles.size() == 1U,
            "direct-line weapon did not create exactly one beam");
    require(run.entities[1].root_model_integrity == 5000,
            "beam damaged its target on the creation tick");

    experiment.step(field, 1U);
    require(run.entities[1].root_model_integrity == 4900,
            "first beam maintenance tick was not damage/lifetime");
    require(run.metrics.projectile_damage_applied == 100U,
            "first beam maintenance tick did not account exactly 100 damage");
    require(run.metrics.projectile_impacts == 1U,
            "beam logical impact was not counted once");
    require(run.metrics.weapon_impact_effect_events == 1U,
            "beam target impact effect was not emitted exactly once at first contact");

    experiment.step(field, 4U);
    require(run.entities[1].root_model_integrity == 4500,
            "beam did not accumulate one lifetime-scaled damage application per maintenance tick");
    require(run.metrics.projectile_damage_applied == 500U &&
            run.metrics.weapon_impact_effect_events == 1U && run.metrics.projectile_impacts == 1U,
            "beam repeated one-shot impact accounting while applying repeated damage");

    // Finish ages 6..19. Retail decrements lifetime before servicing the beam, so a serialized
    // lifetime of 20 yields exactly 19 damage ticks at 2000/20 = 100 each.
    experiment.step(field, 14U);
    require(!run.entities[1].root_model_terminal && run.entities[1].root_model_integrity == 3100,
            "beam did not stop after the retail N-1 maintenance service ticks");
    require(run.metrics.projectile_damage_applied == 1900U,
            "beam damage metric does not reflect 19 lifetime-scaled applications");
    require(run.projectiles.size() == 1U,
            "beam expired before the pre-decrement lifetime reached zero");

    // Age 20 decrements the runtime lifetime to zero and releases before another beam update.
    experiment.step(field, 1U);
    require(run.projectiles.empty(), "direct-line beam did not expire at its serialized lifetime");
    require(run.entities[1].root_model_integrity == 3100 &&
            run.metrics.projectile_damage_applied == 1900U,
            "beam applied a forbidden final damage tick on the release edge");
    require(run.metrics.weapon_shots_fired == 1U && run.metrics.projectile_impacts == 1U &&
            run.metrics.weapon_impact_effect_events == 1U,
            "beam lifetime service changed one-shot accounting");
    require(run.faction_score[2].relation_counter_d == 0 &&
            run.faction_score[1].relation_counter_f == 0,
            "non-terminal beam incorrectly changed terminal relation counters");

    const auto events = thandor::simulation_lab_one_shot_events_after(run, 0U);
    std::uint32_t launch_count = 0U;
    std::uint32_t impact_count = 0U;
    for (const auto& event : events) {
        if (event.kind == thandor::SimulationLabOneShotEventKind::WeaponLaunchEffect &&
            event.effect_id == 0xB01U) ++launch_count;
        if (event.kind == thandor::SimulationLabOneShotEventKind::WeaponImpactEffect &&
            event.effect_id == 0xB02U) ++impact_count;
    }
    require(launch_count == 1U && impact_count == 1U,
            "beam launch/impact event journal is not one-shot across repeated damage ticks");

    std::cout << "V608_DIRECT_BEAM_LIFETIME_DAMAGE PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V608_DIRECT_BEAM_LIFETIME_DAMAGE FAIL: " << error.what() << '\n';
    return 1;
}
