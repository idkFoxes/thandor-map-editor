#include "thandor/simulation_lab.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() try {
    auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    // Give the FLD a shallow deterministic X slope. Retail AI placement takes its heading from
    // triangle0NormalAngles.low16, so the placed runtime must not inherit the template yaw.
    for (std::uint32_t y = 0; y < field.height(); ++y)
        for (std::uint32_t x = 0; x < field.width(); ++x)
            field.at(x, y).terrain_height_q12 = static_cast<std::int32_t>(x * 8U);
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
    parameters[1] = parameters[2] = parameters[3] = parameters[4] =
        static_cast<std::uint32_t>(-100000);

    thandor::SimulationLabSeedEntity builder;
    builder.source_placement_index = 0U;
    builder.army_asset_id = 300U;
    builder.faction_index = 1U;
    builder.runtime_model_class_id = 11U;
    builder.building = true;
    builder.runtime_class_specific_19c_raw = 0x100000;
    const auto& origin = field.at(30U, 30U);
    builder.actor.position = {thandor::q12_to_float(origin.world_x_q12),
                              thandor::q12_to_float(origin.world_y_q12), 0.0F};

    thandor::SimulationLabProductionAsset product;
    product.army_asset_id = 0x500U;
    product.army_runtime_flags14_raw = 1U;
    product.placement_effect_id = 88U;
    product.placement_effect_ids_prepared = true;
    product.placement_effect_ids_by_faction[1] = 0x143U;
    product.entity_seed.army_asset_id = product.army_asset_id;
    product.entity_seed.runtime_model_class_id = 14U;
    product.entity_seed.building = true;
    product.entity_seed.motion.collision_radius_q12 = 0x600;
    product.entity_seed.actor.body_yaw_turn16 = 0x1234U;

    thandor::SimulationLabConfig config;
    config.run_count = 1U;
    config.duration_ticks = 128U;
    config.parameter_index = 100U;
    config.sweep_mode = thandor::SimulationLabSweepMode::Fixed;

    const std::array seeds{builder};
    const std::array products{product};
    thandor::SimulationLabExperiment experiment;
    experiment.reset(config, parameters, seeds, {}, products);
    auto& run = experiment.runs().front();
    run.pending_asset_requests.push_back({product.army_asset_id, 1U, 0U, 0U});
    experiment.step(field, 74U); // faction 1 first economy/purchase turn on retail wheel

    require(run.metrics.planned_builds == 1U, "retail placement request did not commit");
    const auto placed = std::find_if(run.entities.begin(), run.entities.end(), [&](const auto& entity) {
        return entity.army_asset_id == product.army_asset_id;
    });
    require(placed != run.entities.end(), "committed building entity is missing");
    require(placed->construction_complete && placed->construction_start_tick == 74U &&
                placed->construction_complete_tick == 74U,
            "catalog-backed retail placement must be complete on its creation tick");
    require(placed->actor.body_yaw_turn16 != 0x1234U,
            "AI placement inherited the template yaw instead of FLD triangle0 normal azimuth");

    const auto events = thandor::simulation_lab_one_shot_events_after(run, 0U);
    const auto effect = std::find_if(events.begin(), events.end(), [](const auto& event) {
        return event.kind == thandor::SimulationLabOneShotEventKind::ConstructionPlacementEffect;
    });
    require(effect != events.end(), "MDL +0x190 placement effect was not journaled");
    require(effect->effect_id == 0x143U && effect->tick == 74U,
            "placement effect did not use the faction technology-selected root MDL +0x190");
    require(effect->army_asset_id == product.army_asset_id && effect->faction_index == 1U,
            "placement effect lost owning army/faction identity");
    require(effect->actor.body_yaw_turn16 == placed->actor.body_yaw_turn16,
            "placement effect actor yaw diverged from the newly created retail runtime");

    std::cout << "V573_BUILDING_PLACEMENT_EFFECT PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V573_BUILDING_PLACEMENT_EFFECT FAIL: " << error.what() << '\n';
    return 1;
}
