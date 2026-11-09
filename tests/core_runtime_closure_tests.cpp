#include "thandor/field.hpp"
#include "thandor/simulation_lab.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

thandor::SimulationLabConfig config_for(std::uint32_t duration = 32U) {
    thandor::SimulationLabConfig config;
    config.run_count = 1U;
    config.duration_ticks = duration;
    config.vary_seed_per_run = false;
    return config;
}

void test_retail_energy_priority_and_phase() {
    thandor::SimulationLabSeedEntity seed;
    seed.army_asset_id = 10U;
    seed.faction_index = 1U;
    seed.runtime_model_class_id = 11U;
    seed.model_components = {
        thandor::SimulationLabModelComponentSeed{.runtime_class_id = 11U,
                                                 .maximum_integrity = 1000,
                                                 .retail_energy_demand_q4 = 100U},
        thandor::SimulationLabModelComponentSeed{.runtime_class_id = 7U,
                                                 .maximum_integrity = 1000,
                                                 .retail_energy_demand_q4 = 100U,
                                                 .parent_component_index = 0U},
        thandor::SimulationLabModelComponentSeed{.runtime_class_id = 16U,
                                                 .maximum_integrity = 1000,
                                                 .energy_generation_capacity_q4 = 150U,
                                                 .parent_component_index = 0U},
    };

    std::array<thandor::SimulationLabFactionEconomySeed, 8> economy{};
    economy[1].tritium_current_q4 = 100U;
    economy[1].derive_energy_allocation_from_components = true;
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> ai{};
    thandor::SimulationLabExperiment experiment;
    experiment.reset(config_for(), ai, std::span<const thandor::SimulationLabSeedEntity>(&seed, 1U), economy);
    thandor::FieldGrid field = thandor::FieldGrid::create(4U, 4U, 0, 0U);

    experiment.step(field, 7U);
    auto& before = experiment.runs().front();
    require(before.faction_energy_generation_capacity_q4[1] == 150U,
            "initial generation capacity was not registered before phase 0");
    require(before.faction_supplied_energy_demand_q4[1] == 0U &&
            before.faction_unpowered_energy_demand_q4[1] == 0U,
            "energy allocation ran before retail scheduler phase 0");
    require(before.faction_economy[1].tritium_current_q4 == 100U,
            "Tritium burned before the phase-0 allocation pass");

    experiment.step(field, 1U);
    auto& run = experiment.runs().front();
    require(run.faction_energy_generation_capacity_q4[1] == 150U,
            "class-16 generation capacity was not derived from live model components");
    require(run.faction_supplied_energy_demand_q4[1] == 100U &&
            run.faction_unpowered_energy_demand_q4[1] == 100U,
            "retail priority allocation did not split the 150-Q4 supply at the expected boundary");
    require((run.entities.front().model_components[0].runtime_flags_ec & 1U) == 0U,
            "class 11 (priority 0x1200) should remain powered");
    require((run.entities.front().model_components[1].runtime_flags_ec & 1U) != 0U,
            "class 7 (priority 0x500) should brown out after class 11");
    require(run.faction_last_tritium_energy_burn_q4[1] == 6U &&
            run.faction_economy[1].tritium_current_q4 == 94U,
            "retail Tritium burn (supplied-baseline)>>4 was not applied on phase 0");
}

void test_class23_collision_handshake_endpoint() {
    thandor::SimulationLabSeedEntity mover;
    mover.army_asset_id = 20U;
    mover.faction_index = 1U;
    mover.runtime_model_class_id = 2U;
    mover.motion.speed_q12_per_tick = 1;
    mover.motion.collision_radius_q12 = 0x1000;
    mover.motion.depth_bin_broad_phase_radius_q12 = 0x1000;
    mover.actor.position = {0.0F, 0.0F, 0.0F};

    thandor::SimulationLabSeedEntity deployment;
    deployment.army_asset_id = 21U;
    deployment.faction_index = 1U;
    deployment.runtime_model_class_id = 23U;
    deployment.motion.collision_radius_q12 = 0x1000;
    deployment.motion.depth_bin_broad_phase_radius_q12 = 0x1000;
    deployment.vertical_deployment_step_q12_per_tick = 100;
    deployment.vertical_deployment_travel_limit_q12 = -200;
    deployment.actor.position = {0.0F, 0.0F, 0.0F};

    const std::array seeds{mover, deployment};
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> ai{};
    thandor::SimulationLabExperiment experiment;
    experiment.reset(config_for(), ai, seeds);
    thandor::FieldGrid field = thandor::FieldGrid::create(4U, 4U, 0, 0U);

    experiment.step(field, 1U);
    auto& deployed = experiment.runs().front().entities[1];
    require((deployed.collision_state_bits & 1U) != 0U && deployed.collision_owner_timer == 0x20U,
            "first class-23 contact did not arm the 0x20 retry countdown");
    require(!deployed.linked_entity_index.has_value(),
            "class-23 contact linked before reaching the deployment endpoint");

    experiment.step(field, 2U);
    require(deployed.vertical_deployment_travel_q12 == -200,
            "class-23 deployment did not travel from zero to the signed endpoint");
    require((deployed.collision_state_bits & 2U) == 0U,
            "class-23 endpoint-ready bit became visible during the deployment step");
    require(!deployed.linked_entity_index.has_value(),
            "class-23 linked on the same update that reached the endpoint");

    experiment.step(field, 1U);
    require((deployed.collision_state_bits & 2U) != 0U && deployed.linked_entity_index == 0U,
            "class-23 did not link on the subsequent contact after endpoint readiness");
    require(experiment.runs().front().entities[0].linked_entity_index == 1U,
            "class-23 collision handshake did not create the reciprocal link");
}
void test_class23_batched_step_and_positioned_sound() {
    // V583: ArmyRuntimeClass_UpdateVerticalDeploymentAndCollisionState (0x005283D0)
    // multiplies MDL+0x0C by g_InGameSimulationStepTicks and emits MDL+0x26C at
    // the first outbound stroke and at the full-travel return threshold.
    thandor::SimulationLabSeedEntity mover;
    mover.army_asset_id = 42U;
    mover.faction_index = 1U;
    mover.runtime_model_class_id = 2U;
    mover.motion.speed_q12_per_tick = 1;
    mover.motion.collision_radius_q12 = 0x1000;
    mover.motion.depth_bin_broad_phase_radius_q12 = 0x1000;
    thandor::SimulationLabSeedEntity deployment;
    deployment.source_placement_index = 1U;
    deployment.army_asset_id = 423U;
    deployment.faction_index = 1U;
    deployment.runtime_model_class_id = 23U;
    deployment.motion.collision_radius_q12 = 0x1000;
    deployment.motion.depth_bin_broad_phase_radius_q12 = 0x1000;
    deployment.vertical_deployment_step_q12_per_tick = 100;
    deployment.vertical_deployment_travel_limit_q12 = -300;
    deployment.class23_deployment_sound_asset_id = 77U;
    const std::array seeds{mover, deployment};
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> ai{};
    auto config = config_for();
    config.class23_callback_step_ticks = 3U;
    thandor::SimulationLabExperiment experiment;
    experiment.reset(config, ai, seeds);
    auto field = thandor::FieldGrid::create(4U, 4U, 0, 0U);
    experiment.step(field, 1U);
    require((experiment.runs().front().entities[1].collision_state_bits & 1U) != 0U,
            "batch test did not enter the class-23 collision hold");
    experiment.step(field, 1U);
    const auto& deployed = experiment.runs().front().entities[1];
    require(deployed.vertical_deployment_travel_q12 == -300 &&
                deployed.vertical_deployment_child_local_z_q12 == -300,
            "class-23 step did not use the original signed batch multiplier");
    auto events = thandor::simulation_lab_one_shot_events_after(experiment.runs().front(), 0U);
    std::size_t sound_count = 0U;
    for (const auto& event : events)
        if (event.kind == thandor::SimulationLabOneShotEventKind::Class23DeploymentSound) {
            ++sound_count;
            require(event.tick == 2U && event.sound_asset_id == 77U,
                    "class-23 outbound sound did not preserve sound index and historical tick");
        }
    require(sound_count == 1U, "class-23 outbound travel emitted the wrong number of sounds");
    experiment.step(field, 1U); // endpoint-ready bit, then collision link
    auto& release = experiment.runs().front().entities[1];
    release.collision_state_bits &= ~1U; // user-level unhold / release callback
    experiment.step(field, 1U);
    require(release.vertical_deployment_travel_q12 == 0 &&
                release.vertical_deployment_child_local_z_q12 == 0,
            "class-23 release did not return the child by the batched signed step");
    events = thandor::simulation_lab_one_shot_events_after(experiment.runs().front(), 0U);
    sound_count = 0U;
    for (const auto& event : events)
        if (event.kind == thandor::SimulationLabOneShotEventKind::Class23DeploymentSound) {
            ++sound_count;
            require((event.tick == 2U || event.tick == 4U) && event.sound_asset_id == 77U,
                    "class-23 return sound tick/index did not match recovered branch");
        }
    require(sound_count == 2U, "class-23 return must emit exactly one additional sound");
}

} // namespace

int main() {
    try {
        test_retail_energy_priority_and_phase();
        test_class23_collision_handshake_endpoint();
        test_class23_batched_step_and_positioned_sound();
        std::cout << "V571 V537 runtime closure tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V571 runtime closure failure: " << error.what() << '\n';
        return 1;
    }
}
