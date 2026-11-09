#include "thandor/simulation_lab.hpp"
#include "thandor/fog_preview.hpp"

#include <array>
#include <cmath>
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
    thandor::Brush support_brush;
    support_brush.radius_cells = 4.0F;
    support_brush.shape = thandor::BrushShape::Square;
    support_brush.falloff = thandor::BrushFalloff::Constant;
    field.paint_placement_occupancy_class(10.0F, 10.0F, support_brush, 0U, true);
    field.paint_placement_occupancy_class(36.0F, 36.0F, support_brush, 0U, true);
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};

    thandor::SimulationMotionProfile motion;
    motion.speed_q12_per_tick = 0x0200;
    motion.yaw_max_velocity_turn16 = 0x1000U;
    motion.yaw_acceleration_turn16 = 0x0400U;
    motion.collision_radius_q12 = 0x0200;

    std::array<thandor::SimulationLabSeedEntity, 2> seeds{};
    seeds[0].source_placement_index = 0U;
    seeds[0].army_asset_id = 0x100U;
    seeds[0].faction_index = 1U;
    seeds[0].motion = motion;
    const auto& mover0_cell = field.at(8U, 10U);
    seeds[0].actor.position = {thandor::q12_to_float(mover0_cell.world_x_q12),
                               thandor::q12_to_float(mover0_cell.world_y_q12), 0.0F};

    seeds[1].source_placement_index = 1U;
    seeds[1].army_asset_id = 0x101U;
    seeds[1].faction_index = 2U;
    seeds[1].motion = motion;
    const auto& mover1_cell = field.at(34U, 36U);
    seeds[1].actor.position = {thandor::q12_to_float(mover1_cell.world_x_q12),
                               thandor::q12_to_float(mover1_cell.world_y_q12), 0.0F};

    std::array<thandor::SimulationLabSeedEntity, 2> build_areas{};
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 2> anchor_cells{{{10U,10U},{36U,36U}}};
    for (std::size_t i = 0; i < build_areas.size(); ++i) {
        const auto& cell = field.at(anchor_cells[i].first, anchor_cells[i].second);
        auto& anchor = build_areas[i];
        anchor.source_placement_index = 100U + i;
        anchor.army_asset_id = 301U + static_cast<std::uint32_t>(i);
        anchor.faction_index = static_cast<std::uint32_t>(i + 1U);
        anchor.building = true;
        anchor.runtime_class_specific_19c_raw = 0x5000;
        anchor.actor.position = {thandor::q12_to_float(cell.world_x_q12),
                                 thandor::q12_to_float(cell.world_y_q12), 0.0F};
    }
    std::array<thandor::SimulationLabSeedEntity, 4> all_seeds{
        seeds[0], seeds[1], build_areas[0], build_areas[1]};

    thandor::SimulationLabConfig config;
    config.run_count = 1U;
    config.duration_ticks = 256U;
    config.parameter_index = 1U;
    config.sweep_mode = thandor::SimulationLabSweepMode::Fixed;
    // V553/V533 rejects construction candidate weight ranges <= 1. This test owns timing and
    // visibility, not candidate-score rejection, so keep its resource-site candidate positive.
    config.fixed_value = 1000;

    thandor::SimulationLabExperiment experiment;
    experiment.reset(config, parameters, all_seeds);
    experiment.step(field, 1U);
    auto& run = experiment.runs().front();
    require(run.entities[0].actor.move_target.has_value() && run.entities[1].actor.move_target.has_value(),
            "opposing movable seeds did not expose drive targets on the first simulation tick");
    require(run.metrics.movement_distance_world > 0.0,
            "opposing movable seeds did not visibly advance in the simulation state");

    // Faction selection is (tick >> 3) & 7, with economy callbacks on 2/6
    // when bit 6 is set. Faction 1 plans at 74/78; faction 2 at 82/86.
    experiment.step(field, 73U); // 1 -> 74: faction 1 queues a request
    require(run.tick == 74U && run.metrics.pending_requests_created == 1U &&
            run.metrics.planned_builds == 0U,
            "faction 1 did not queue a pending request on its first retail economy callback");
    experiment.step(field, 4U); // tick 78: faction 1 places its prior request
    require(run.tick == 78U && run.metrics.planned_builds == 1U,
            "faction 1 did not place its request on the next retail economy callback");
    experiment.step(field, 4U); // tick 82: faction 2 queues its request
    require(run.tick == 82U && run.metrics.pending_requests_created == 2U &&
            run.metrics.planned_builds == 1U,
            "faction 2 did not queue independently on its retail slot");
    experiment.step(field, 4U); // tick 86: faction 2 commits its request
    require(run.tick == 86U && run.metrics.planned_builds == 2U,
            "both factions did not place exactly once on their own economy turns");

    std::size_t immediate = 0U;
    for (const auto& entity : run.entities) {
        if (!entity.synthetic) continue;
        ++immediate;
        require(entity.construction_complete,
                "retail placement must create the complete runtime hierarchy immediately");
        require((entity.construction_start_tick == 78U || entity.construction_start_tick == 86U) &&
                entity.construction_complete_tick == entity.construction_start_tick,
                "construction compatibility metadata must collapse to each faction's placement tick");
        require(thandor::simulation_lab_construction_progress(run, entity) == 1.0F,
                "immediate retail placement must report full visual construction progress");
    }
    require(immediate == 2U, "expected one immediate proxy placement per faction");

    experiment.step(field, 120U);
    for (const auto& entity : run.entities) {
        if (!entity.synthetic || (entity.construction_start_tick != 78U &&
                                  entity.construction_start_tick != 86U)) continue;
        require(entity.construction_complete,
                "immediate retail placement regressed into a delayed construction lifecycle");
        require(entity.construction_complete_tick == entity.construction_start_tick,
                "immediate placement completion tick drifted after later simulation updates");
    }

    // Stage 5 regression: the worker owns both current occupancy and persistent exploration.
    // A live mobile unit moving between visibility phases must update the current map on the
    // next retail callback without erasing its previous explored cell. This deliberately does
    // not depend on the editor's faction selector or on shader rendering.
    {
        auto visibility_field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
        auto mover = thandor::SimulationLabSeedEntity{};
        mover.source_placement_index = 77U;
        mover.army_asset_id = 0x155U;
        mover.faction_index = 1U;
        mover.runtime_model_class_id = 2U;
        mover.fog_uncover_radius_q12 = 0x240;
        const auto& start_cell = visibility_field.at(12U, 14U);
        const auto& destination_cell = visibility_field.at(42U, 40U);
        mover.actor.position = {thandor::q12_to_float(start_cell.world_x_q12),
                                thandor::q12_to_float(start_cell.world_y_q12), 0.0F};
        thandor::SimulationLabExperiment visibility_experiment;
        visibility_experiment.reset(config, parameters,
            std::span<const thandor::SimulationLabSeedEntity>(&mover, 1U));
        visibility_experiment.step(visibility_field, 1U);
        auto& visibility_run = visibility_experiment.runs().front();
        const std::size_t old_index = 14U * visibility_field.width() + 12U;
        const std::size_t new_index = 40U * visibility_field.width() + 42U;
        require(visibility_run.current_visibility_occupancy_tick == 1U &&
                visibility_run.current_visibility_occupancy.size() ==
                    static_cast<std::size_t>(visibility_field.width()) * visibility_field.height(),
                "first-tick mobile visibility snapshot absent");
        require(thandor::retail_fog_currently_visible(thandor::fog_occupancy_byte(
                    visibility_run.current_visibility_occupancy[old_index], 1U)),
                "mobile observer did not reveal its initial field cell");
        visibility_run.entities.front().actor.position = {
            thandor::q12_to_float(destination_cell.world_x_q12),
            thandor::q12_to_float(destination_cell.world_y_q12), 0.0F};
        visibility_experiment.step(visibility_field, 6U);
        require(visibility_run.current_visibility_occupancy_tick == 7U &&
                thandor::retail_fog_currently_visible(thandor::fog_occupancy_byte(
                    visibility_run.current_visibility_occupancy[new_index], 1U)),
                "mobile observer did not reveal its new cell on phase seven");
        const auto& old_words = visibility_run.faction_explored_cell_bits[1U];
        require((old_words[old_index >> 6U] & (uint64_t{1} << (old_index & 63U))) != 0U &&
                (old_words[new_index >> 6U] & (uint64_t{1} << (new_index & 63U))) != 0U,
                "mobile exploration did not preserve both positions");
    }

    std::cout << "PASS: Simulation Lab drive state and immediate retail placement metadata\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
