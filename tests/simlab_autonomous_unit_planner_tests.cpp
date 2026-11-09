#include "thandor/simulation_lab.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

thandor::SimulationLabConfig config() {
    thandor::SimulationLabConfig value;
    value.run_count = 1U;
    value.duration_ticks = 256U;
    value.parameter_index = 100U;
    value.sweep_mode = thandor::SimulationLabSweepMode::Fixed;
    return value;
}

thandor::SimulationLabSeedEntity factory(std::uint32_t faction, float x) {
    thandor::SimulationLabSeedEntity value;
    value.source_placement_index = 0x500U + faction;
    value.army_asset_id = 0x500U + faction;
    value.faction_index = faction;
    value.runtime_model_class_id = 13U;
    value.building = true;
    value.actor.position = {x, 8.0F, 0.0F};
    value.production_compatibility_mask = 0x20U;
    value.production_spawn_world = {x, 10.0F, 0.0F};
    value.production_exit_world = {x + 8.0F, 10.0F, 0.0F};
    return value;
}

thandor::SimulationLabProductionAsset unit_product(std::uint32_t id, std::uint32_t flags,
                                                    std::uint32_t class74,
                                                    bool selected_technology_unlocked = true) {
    thandor::SimulationLabProductionAsset value;
    value.army_asset_id = id;
    value.army_runtime_flags14_raw = flags;
    value.xenite_cost_q4 = 10U;
    value.build_duration_ticks = 2U;
    value.entity_seed.army_asset_id = id;
    value.entity_seed.runtime_model_class_id = 2U;
    value.entity_seed.building = false;
    value.entity_seed.motion.speed_q12_per_tick = thandor::float_to_q12(1.0F);
    value.entity_seed.motion.yaw_max_velocity_turn16 = 0x4000;
    value.entity_seed.motion.yaw_acceleration_turn16 = 0x4000;

    value.ai_definition_class_value74 = class74;
    value.ai_faction_score_seeds_prepared = true;
    value.ai_baseline_root_definition_value0c = 64;
    value.ai_baseline_root_definition_value18 = 1;
    value.ai_baseline_root_definition_value60 = 1000;
    value.ai_baseline_root_category_maximum_index5c = 0U;
    for (std::uint32_t faction = 1U; faction <= 4U; ++faction) {
        auto& score = value.ai_faction_score_seeds[faction];
        score.resolved = true;
        score.all_selected_model_technologies_unlocked = selected_technology_unlocked;
        score.root_definition_value0c = 64;
        score.root_definition_value18 = 1;
        score.root_definition_value60 = 1000;
        score.root_category_maximum_index5c = 0U;
    }
    return value;
}
}

int main() try {
    auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
    parameters[0x09U] = 4096U; // Variant-A purchase weight.

    std::array<thandor::SimulationLabFactionEconomySeed, 8> economy{};
    std::array<thandor::FactionTechnologyMask256, 8> technology_masks{};
    std::vector<thandor::SimulationLabSeedEntity> factories;
    for (std::uint32_t faction = 1U; faction <= 4U; ++faction) {
        economy[faction].xenite_current_q4 = 1000U;
        thandor::set_faction_technology_unlocked(technology_masks[faction], 1U, true);
        factories.push_back(factory(faction, 8.0F + static_cast<float>(faction) * 12.0F));
    }

    // Two valid class-13 products plus two decoys. The higher ARM +0x74 score must win for every
    // faction. A numerically huge product with the wrong producer mask and another with a locked
    // selected MDL must never enter workspace11.
    const auto low = unit_product(0x101U, 0x21U, 2U);
    const auto high = unit_product(0x102U, 0x21U, 50U);
    const auto incompatible = unit_product(0x103U, 0x41U, 100000U);
    const auto technology_locked = unit_product(0x104U, 0x21U, 100000U, false);
    std::array<thandor::SimulationLabProductionAsset, 4> products{
        low, high, incompatible, technology_locked};

    thandor::SimulationLabExperiment experiment;
    experiment.reset(config(), parameters, factories, economy, products, {}, technology_masks, {});
    auto& run = experiment.runs().front();

    // Each faction receives its economy slot at ticks 74,82,90,98, NOT simultaneously
    // at tick 64. The purchase queues on that tick; the factory starts on the following callback, before its 2-tick product completes.
    for (std::uint32_t faction = 1U; faction <= 4U; ++faction) {
        const auto planning_tick=74U+(faction-1U)*8U;
        experiment.step(field,planning_tick-run.tick);
        require(run.metrics.production_jobs_started==faction-1U,
                "world-job purchase ran an already-completed owner callback");
        experiment.step(field,1U); // native producer accepts on the following callback
        require(run.metrics.production_jobs_started >= faction,
                "autonomous planner did not start the class-13 purchase on its faction slot");
        const auto factory_it = std::find_if(run.entities.begin(), run.entities.end(), [&](const auto& entity) {
            return entity.faction_index == faction && entity.runtime_model_class_id == 13U;
        });
        require(factory_it != run.entities.end() && factory_it->production_building &&
                factory_it->production_asset_id == 0x102U,
                "variant-A score/producer filtering selected the wrong autonomous unit ARM");
    }
    require(run.metrics.production_jobs_started == 4U,
            "autonomous planner did not start exactly one purchase per faction");
    require(run.secondary_purchase_requests.empty(),
            "autonomous unit requests remained queued after idle compatible factories accepted them");

    experiment.step(field, 2U);
    require(run.metrics.class13_units_spawned == 4U,
            "autonomous class-13 purchases did not complete into physical units for all factions");
    for (std::uint32_t faction = 1U; faction <= 4U; ++faction) {
        require(std::count_if(run.entities.begin(), run.entities.end(), [&](const auto& entity) {
                    return entity.faction_index == faction && entity.synthetic &&
                           entity.army_asset_id == 0x102U;
                }) == 1,
                "one faction failed to receive the highest-scored autonomous unit");
        require(std::none_of(run.entities.begin(), run.entities.end(), [&](const auto& entity) {
                    return entity.faction_index == faction && entity.synthetic &&
                           (entity.army_asset_id == 0x103U || entity.army_asset_id == 0x104U);
                }),
                "workspace11 admitted a producer-incompatible or technology-locked ARM");
    }

    // Variant B must independently generate a unit purchase when TEC 1 is locked (so Variant A
    // is unavailable). This locks the second retail army-candidate path instead of allowing the
    // Variant-A scenario above to mask a dead B implementation.
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> variant_b_parameters{};
    variant_b_parameters[0x0AU] = 4096U;
    std::array<thandor::SimulationLabFactionEconomySeed, 8> variant_b_economy{};
    variant_b_economy[1].xenite_current_q4 = 1000U;
    std::array<thandor::FactionTechnologyMask256, 8> variant_b_technology_masks{};
    std::vector<thandor::SimulationLabSeedEntity> variant_b_factories{factory(1U, 20.0F)};

    thandor::SimulationLabExperiment variant_b_experiment;
    variant_b_experiment.reset(config(), variant_b_parameters, variant_b_factories,
                               variant_b_economy, products, {}, variant_b_technology_masks, {});
    auto& variant_b_run = variant_b_experiment.runs().front();
    variant_b_experiment.step(field, 74U);
    require(variant_b_run.metrics.production_jobs_started==0U,"variant-B purchase executed before its next owner callback");
    variant_b_experiment.step(field,1U);
    const auto variant_b_factory = std::find_if(variant_b_run.entities.begin(),
                                                variant_b_run.entities.end(), [](const auto& entity) {
        return entity.faction_index == 1U && entity.runtime_model_class_id == 13U;
    });
    require(variant_b_factory != variant_b_run.entities.end() &&
            variant_b_factory->production_building &&
            variant_b_factory->production_asset_id == 0x102U,
            "variant-B path failed to autonomously select/start the highest-scored compatible unit");


    // The world-job wheel rebuilds pressure on ticks 2/6, before the compact Lab's first
    // faction-1 purchase dispatch at tick 74. A channel-2 hostile therefore already makes the channel-2
    // weapon win on the FIRST cycle; the older zero-pressure-first-cycle assertion is obsolete.
    // Absent a hostile, equal channel pressures still require insertion-order tie handling.
    auto pressure_low = unit_product(0x105U, 0x21U, 20U);
    auto pressure_high = unit_product(0x106U, 0x21U, 20U);
    pressure_low.build_duration_ticks = 1U;
    pressure_high.build_duration_ticks = 1U;
    for (auto* product : {&pressure_low, &pressure_high}) {
        auto& score = product->ai_faction_score_seeds[1U];
        score.child_list_count = 1U;
        score.child_weapons[0].valid = true;
        score.child_weapons[0].reload_divisor30 = 4096;
    }
    pressure_low.ai_faction_score_seeds[1U].child_weapons[0].target_class_impact_damage_q12[1U] = 4096;
    pressure_high.ai_faction_score_seeds[1U].child_weapons[0].target_class_impact_damage_q12[2U] = 4096;
    std::array<thandor::SimulationLabProductionAsset, 2> pressure_products{pressure_low, pressure_high};

    auto pressure_factory = factory(1U, 20.0F);
    // This test exercises a second economy turn at tick 202. Supply an authored
    // fast door animation so the first child can exit and the factory can close;
    // an unresolved speed uses the Lab's conservative 128-tick opening fallback.
    pressure_factory.motion.speed_q12_per_tick = 0x80000;
    thandor::SimulationLabSeedEntity hostile;
    hostile.source_placement_index = 0x900U;
    hostile.army_asset_id = 0x190U;
    hostile.faction_index = 2U;
    hostile.runtime_model_class_id = 2U;
    hostile.shot_impact_class_index = 2U;
    hostile.actor.position = {44.0F, 20.0F, 0.0F};
    std::array<thandor::SimulationLabSeedEntity, 2> pressure_entities{pressure_factory, hostile};
    std::array<thandor::SimulationLabFactionEconomySeed, 8> pressure_economy{};
    pressure_economy[1U].xenite_current_q4 = 10000U;
    std::array<thandor::FactionTechnologyMask256, 8> pressure_technologies{};
    thandor::set_faction_technology_unlocked(pressure_technologies[1U], 1U, true);

    thandor::SimulationLabExperiment neutral_experiment;
    std::array<thandor::SimulationLabSeedEntity, 1> neutral_entities{pressure_factory};
    neutral_experiment.reset(config(), parameters, neutral_entities, pressure_economy,
                             pressure_products, {}, pressure_technologies, {});
    neutral_experiment.step(field, 75U); // tick-74 world purchase is accepted by the next owner callback
    const auto& neutral_run = neutral_experiment.runs().front();
    require(neutral_run.entities.front().production_building &&
            neutral_run.entities.front().production_asset_id == 0x105U,
            "equal static scores without a hostile no longer retain candidate insertion order");

    thandor::SimulationLabExperiment pressure_experiment;
    pressure_experiment.reset(config(), parameters, pressure_entities, pressure_economy,
                              pressure_products, {}, pressure_technologies, {});
    auto& pressure_run = pressure_experiment.runs().front();
    pressure_experiment.step(field, 2U);
    require(pressure_run.faction_ai_pressure_values[1U][2U] >
            pressure_run.faction_ai_pressure_values[1U][1U],
            "tick-2 world-job pressure rebuild did not credit the channel-2 hostile");
    pressure_experiment.step(field, 73U); // accept the tick-74 purchase on tick 75
    auto pressure_factory_it = std::find_if(pressure_run.entities.begin(), pressure_run.entities.end(),
        [](const auto& entity) {
            return entity.faction_index == 1U && entity.runtime_model_class_id == 13U;
        });
    require(pressure_factory_it != pressure_run.entities.end() && pressure_factory_it->production_building &&
            pressure_factory_it->production_asset_id == 0x106U,
            "first purchase cycle ignored hostile pressure rebuilt before tick 74");
    require(pressure_run.faction_ai_pressure_values[1U][2U] >
            pressure_run.faction_ai_pressure_values[1U][1U] &&
            pressure_run.faction_maximum_ai_pressure[1U] ==
            pressure_run.faction_ai_pressure_values[1U][2U],
            "4-tick-wheel hostile pressure image did not preserve the expected dominant channel");

    pressure_experiment.step(field, 2U); // finish the first one-tick factory product
    pressure_experiment.step(field, 126U); // tick-202 economy purchase is accepted on tick 203
    pressure_factory_it = std::find_if(pressure_run.entities.begin(), pressure_run.entities.end(),
        [](const auto& entity) {
            return entity.faction_index == 1U && entity.runtime_model_class_id == 13U;
        });
    require(pressure_factory_it != pressure_run.entities.end() && pressure_factory_it->production_building &&
            pressure_factory_it->production_asset_id == 0x106U,
            "second pressure cycle did not prefer the weapon matching the dominant hostile channel");

    std::cout << "V568_SIMULATION_LAB_AUTONOMOUS_UNIT_PLANNER PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V568_SIMULATION_LAB_AUTONOMOUS_UNIT_PLANNER FAIL: " << error.what() << '\n';
    return 1;
}
