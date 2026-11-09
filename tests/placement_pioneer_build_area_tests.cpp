#include "thandor/simulation_lab.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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
    value.duration_ticks = 400U;
    value.parameter_index = 100U;
    value.sweep_mode = thandor::SimulationLabSweepMode::Fixed;
    value.fixed_value = 0;
    return value;
}

thandor::SimulationLabSeedEntity seed(std::uint32_t asset_id, std::uint32_t runtime_class,
                                      std::uint32_t faction, thandor::Vec3f position,
                                      bool building) {
    thandor::SimulationLabSeedEntity value;
    value.source_placement_index = static_cast<std::size_t>(asset_id + faction * 0x1000U);
    value.army_asset_id = asset_id;
    value.faction_index = faction;
    value.runtime_model_class_id = runtime_class;
    value.building = building;
    value.actor.position = position;
    value.root_model_maximum_integrity = 1000;
    return value;
}

thandor::SimulationLabProductionAsset product(std::uint32_t asset_id,
                                               std::uint32_t runtime_class,
                                               std::int32_t class_c0,
                                               std::int32_t build_extent_q12) {
    thandor::SimulationLabProductionAsset value;
    value.army_asset_id = asset_id;
    value.army_runtime_flags14_raw = 0x11U;
    value.runtime_class_specific_c0_raw = class_c0;
    value.xenite_cost_q4 = 1U;
    value.build_duration_ticks = 2U;
    value.entity_seed = seed(asset_id, runtime_class, 0U, {}, true);
    value.entity_seed.runtime_class_specific_c0_raw = class_c0;
    value.entity_seed.runtime_class_specific_19c_raw = build_extent_q12;
    value.entity_seed.runtime_class_specific_1a8_raw = 0;
    return value;
}

std::array<thandor::SimulationLabFactionEconomySeed, 8> rich_economy() {
    std::array<thandor::SimulationLabFactionEconomySeed, 8> economy{};
    economy[1].xenite_current_q4 = 10000U;
    economy[1].tritium_current_q4 = 10000U;
    return economy;
}

float distance2(thandor::Vec3f a, thandor::Vec3f b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}
}

int main() try {
    auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    thandor::Brush brush;
    brush.radius_cells = 1.0F;
    brush.shape = thandor::BrushShape::Square;
    brush.falloff = thandor::BrushFalloff::Constant;
    field.paint_placement_occupancy_class(30.0F, 20.0F, brush, 0U, true);

    const auto& start_cell = field.at(20U, 20U);
    const auto& resource_cell = field.at(30U, 20U);
    const thandor::Vec3f start{
        thandor::q12_to_float(start_cell.world_x_q12),
        thandor::q12_to_float(start_cell.world_y_q12), 0.0F};
    const thandor::Vec3f resource{
        thandor::q12_to_float(resource_cell.world_x_q12),
        thandor::q12_to_float(resource_cell.world_y_q12), 0.0F};

    auto pioneer = seed(50U, 18U, 1U, start, false);
    // The real values come from ARM50's root MDL. This test only needs a nonzero class-18 drive
    // profile and a finite +0x19C mode-0 build radius.
    pioneer.motion.speed_q12_per_tick = 0x300;
    pioneer.motion.yaw_max_velocity_turn16 = 0x800;
    pioneer.motion.yaw_acceleration_turn16 = 0x100;
    pioneer.runtime_class_specific_19c_raw = 0x5000; // 5 world units
    pioneer.actor.body_yaw_turn16 = 0U;              // east, toward the resource strip

    std::vector<thandor::SimulationLabProductionAsset> products{
        product(300U, 11U, 0, 0x7000),
        product(330U, 14U, 0, 0x7000),
        product(332U, 14U, 7, 0x7000),
        product(333U, 16U, 0, 0x7000),
    };
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};

    // Distant resources remain negative placement coverage. Busy-unit AI
    // cooldown permits later bootstrap orders after arrival; the V569 assertion
    // of a permanent fallback latch encoded the missing scheduler cooldown.
    {
        auto remote = pioneer;
        const auto& cell = field.at(4U, 20U);
        remote.actor.position = {thandor::q12_to_float(cell.world_x_q12),
                                 thandor::q12_to_float(cell.world_y_q12), 0.0F};
        thandor::SimulationLabExperiment distant;
        distant.reset(config(), parameters,
            std::span<const thandor::SimulationLabSeedEntity>(&remote, 1U),
            rich_economy(), products);
        distant.step(field, 300U);
        const auto& run = distant.runs().front();
        require(run.entities.size() == 1U && run.entities.front().army_asset_id == 50U,
                "distant startup illegally placed an out-of-range building");
        require(run.entities.front().pioneer_ai_command_count >= 2U &&
                (run.entities.front().pioneer_movement.flags & 0x100U) == 0U,
                "distant Pioneer did not resume roaming after arrival");
    }

    thandor::SimulationLabExperiment experiment;
    experiment.reset(config(), parameters, std::span<const thandor::SimulationLabSeedEntity>(&pioneer, 1U),
                     rich_economy(), products);
    auto& run = experiment.runs().front();
    require(run.pending_asset_requests.size() == 4U,
            "class-18/no-primary startup did not seed the four retail primary requests");

    // First planning cadence: ARM300 has no assigned primary extent, and ARM330 is still initially
    // outside the Pioneer's mode-0 +0x19C radius. Nothing is allowed to teleport to the resource.
    experiment.step(field, 20U);
    require(std::none_of(run.entities.begin(), run.entities.end(), [](const auto& e) {
                return e.army_asset_id >= 300U;
            }), "out-of-range startup structure was placed on the first cadence");
    const auto pioneer_after_first = std::find_if(run.entities.begin(), run.entities.end(), [](const auto& e) {
        return e.runtime_model_class_id == 18U;
    });
    require(pioneer_after_first != run.entities.end(), "Pioneer disappeared during bootstrap");
    require(distance2(pioneer_after_first->actor.position, start) > 0.0001F,
            "class-18 bootstrap roam did not move the Pioneer");

    // Continue until the mode-0 resource placement becomes legal. The special extractor must be
    // near the Xenite support and cannot appear before the mobile +0x19C source reaches it.
    bool extractor_placed = false;
    for (unsigned batch = 0; batch < 12U && !extractor_placed; ++batch) {
        experiment.step(field, 20U);
        extractor_placed = std::any_of(run.entities.begin(), run.entities.end(), [](const auto& e) {
            return e.army_asset_id == 330U;
        });
    }
    require(extractor_placed, "Pioneer never brought Xenite into legal mode-0 build range");
    const auto extractor = std::find_if(run.entities.begin(), run.entities.end(), [](const auto& e) {
        return e.army_asset_id == 330U;
    });
    require(extractor != run.entities.end(), "Xenite extractor runtime missing after placement");
    require(distance2(extractor->actor.position, resource) < 16.0F,
            "Xenite extractor was not placed on the resource-support area");

    const auto extractor_position = extractor->actor.position;
    const auto extractor_extent = extractor->runtime_class_specific_19c_raw;

    // The next ordinary structure uses mode 1/workspace10. Class 18 is excluded there; the point
    // must instead be inside the assigned ARM>=300 building's strict +0x19C workspace extent.
    for (unsigned batch = 0; batch < 8U; ++batch) {
        if (std::any_of(run.entities.begin(), run.entities.end(), [](const auto& e) {
                return e.army_asset_id == 300U;
            })) break;
        experiment.step(field, 20U);
    }
    const auto hq = std::find_if(run.entities.begin(), run.entities.end(), [](const auto& e) {
        return e.army_asset_id == 300U;
    });
    require(hq != run.entities.end(), "ordinary ARM300 never placed after extractor created build area");
    const std::int64_t dx = std::llabs(static_cast<long long>(thandor::float_to_q12(hq->actor.position.x)) -
                                       thandor::float_to_q12(extractor_position.x));
    const std::int64_t dy = std::llabs(static_cast<long long>(thandor::float_to_q12(hq->actor.position.y)) -
                                       thandor::float_to_q12(extractor_position.y));
    require(dx < extractor_extent &&
            dy < extractor_extent,
            "ordinary structure escaped the assigned building +0x19C workspace extent");


    // Four-faction regression for the failure shape reported from the editor. Give each Pioneer a
    // separate resource island in front of its authored heading. No faction may steal a remote
    // global placement; all four must physically advance until their own mode-0 range reaches a
    // free resource component, then grow an ordinary build area from the placed extractor.
    {
        auto multi_field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
        constexpr std::array<std::uint32_t, 4> rows{{8U, 20U, 32U, 44U}};
        std::vector<thandor::SimulationLabSeedEntity> pioneers;
        pioneers.reserve(rows.size());
        for (std::uint32_t i = 0; i < rows.size(); ++i) {
            const auto faction = i + 1U;
            thandor::Brush resource_brush;
            resource_brush.radius_cells = 1.0F;
            resource_brush.shape = thandor::BrushShape::Square;
            resource_brush.falloff = thandor::BrushFalloff::Constant;
            multi_field.paint_placement_occupancy_class(30.0F, static_cast<float>(rows[i]),
                                                        resource_brush, 0U, true);
            const auto& cell = multi_field.at(20U, rows[i]);
            auto p = seed(50U, 18U, faction,
                {thandor::q12_to_float(cell.world_x_q12),
                 thandor::q12_to_float(cell.world_y_q12), 0.0F}, false);
            p.motion.speed_q12_per_tick = 0x300;
            p.motion.yaw_max_velocity_turn16 = 0x800;
            p.motion.yaw_acceleration_turn16 = 0x100;
            p.runtime_class_specific_19c_raw = 0x5000;
            p.actor.body_yaw_turn16 = 0U;
            pioneers.push_back(p);
        }
        auto economy = rich_economy();
        for (std::uint32_t faction = 2U; faction <= 4U; ++faction) {
            economy[faction].xenite_current_q4 = 10000U;
            economy[faction].tritium_current_q4 = 10000U;
        }
        thandor::SimulationLabExperiment multi;
        multi.reset(config(), parameters, pioneers, economy, products);
        multi.step(multi_field, 300U);
        const auto& multi_run = multi.runs().front();
        for (std::uint32_t faction = 1U; faction <= 4U; ++faction) {
            require(std::any_of(multi_run.entities.begin(), multi_run.entities.end(),
                    [&](const auto& e) { return e.faction_index == faction && e.army_asset_id == 330U; }),
                    "one of four Pioneers never reached/placed its Xenite extractor");
            require(std::any_of(multi_run.entities.begin(), multi_run.entities.end(),
                    [&](const auto& e) { return e.faction_index == faction && e.army_asset_id == 300U; }),
                    "one of four factions failed to grow ordinary construction from its extractor area");
        }
    }

    std::cout << "V569_PIONEER_BUILD_AREA PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V569_PIONEER_BUILD_AREA FAIL: " << error.what() << '\n';
    return 1;
}
