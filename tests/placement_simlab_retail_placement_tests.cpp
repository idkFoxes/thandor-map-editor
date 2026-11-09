#include "thandor/simulation_lab.hpp"
#include "thandor/fog_preview.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

thandor::SimulationLabConfig config() {
    thandor::SimulationLabConfig value;
    value.run_count = 1U;
    value.duration_ticks = 512U;
    value.parameter_index = 100U;
    value.sweep_mode = thandor::SimulationLabSweepMode::Fixed;
    return value;
}

thandor::SimulationLabProductionAsset building(std::uint32_t id, std::uint32_t runtime_class = 14U) {
    thandor::SimulationLabProductionAsset value;
    value.army_asset_id = id;
    value.army_runtime_flags14_raw = 1U;
    value.entity_seed.army_asset_id = id;
    value.entity_seed.runtime_model_class_id = runtime_class;
    value.entity_seed.building = true;
    value.entity_seed.motion.collision_radius_q12 = 0x600;
    return value;
}


thandor::SimulationLabSeedEntity faction_seed(const thandor::FieldGrid& field,
                                               std::uint32_t faction = 1U,
                                               std::uint32_t x = 30U,
                                               std::uint32_t y = 5U,
                                               std::uint32_t runtime_class = 18U,
                                               std::int32_t build_extent_q12 = 0x100000) {
    thandor::SimulationLabSeedEntity value;
    value.source_placement_index = faction - 1U;
    value.army_asset_id = runtime_class == 18U ? 50U : 300U;
    value.faction_index = faction;
    value.runtime_model_class_id = runtime_class;
    value.building = runtime_class != 18U;
    value.runtime_class_specific_19c_raw = build_extent_q12;
    const auto& cell = field.at(x, y);
    value.actor.position = {thandor::q12_to_float(cell.world_x_q12),
                            thandor::q12_to_float(cell.world_y_q12), 0.0F};
    return value;
}

void paint_rect(thandor::FieldGrid& field, std::uint32_t x0, std::uint32_t y0,
                std::uint32_t x1, std::uint32_t y1, std::uint32_t support_class) {
    const std::uint32_t mask = 0x800U << support_class;
    for (std::uint32_t y = y0; y <= y1; ++y)
        for (std::uint32_t x = x0; x <= x1; ++x)
            field.at(x, y).flags_and_material |= mask;
}
}

int main() try {
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
    // Disable autonomous AI candidates; this test drives primary-queue placement directly.
    parameters[1] = parameters[2] = parameters[3] = parameters[4] =
        static_cast<std::uint32_t>(-100000);
    parameters[80] = 0x1000U;

    // A connected Xenite region is an economy collection unit, NOT an
    // exclusive placement reservation. Retail class-14 updates claim their
    // individual support cells, and InGameFactionEconomy_PayCollectedRegion
    // divides visited region cells by the number of registered extractors.
    // Different factions can therefore receive separate shares when placement
    // clearance and the workspace admission tests both permit the structures.
    // Do not turn the region into a global lock just to satisfy an old fixture.
    {
        auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
        paint_rect(field, 10U, 10U, 24U, 24U, 0U);
        const std::array products{building(0x14AU)};
        const std::array seeds{faction_seed(field, 1U, 30U, 5U),
                               faction_seed(field, 2U, 30U, 53U)};
        thandor::SimulationLabExperiment experiment;
        experiment.reset(config(), parameters, seeds, {}, products);
        auto& run = experiment.runs().front();
        run.pending_asset_requests.push_back({0x14AU, 1U, 0U, 0U});
        experiment.step(field, 74U);
        require(run.metrics.planned_builds == 1U && run.pending_asset_requests.empty(),
                "first Xenite extractor was not placed on its support field");
        run.pending_asset_requests.push_back({0x14AU, 2U, run.tick, 0U});
        experiment.step(field, 8U);
        require(run.metrics.planned_builds == 2U && run.pending_asset_requests.empty(),
                "second faction did not place a distinct extractor on the shared region");
        const thandor::SimulationLabEntity* first = nullptr;
        const thandor::SimulationLabEntity* second = nullptr;
        for (const auto& entity : run.entities) {
            if (entity.army_asset_id != 0x14AU || !entity.synthetic) continue;
            if (entity.faction_index == 1U) first = &entity;
            if (entity.faction_index == 2U) second = &entity;
        }
        require(first != nullptr && second != nullptr,
                "shared Xenite region did not materialize both faction extractors");
        const auto first_cell = thandor::retail_fog_world_to_cell(
            field, thandor::float_to_q12(first->actor.position.x),
            thandor::float_to_q12(first->actor.position.y));
        const auto second_cell = thandor::retail_fog_world_to_cell(
            field, thandor::float_to_q12(second->actor.position.x),
            thandor::float_to_q12(second->actor.position.y));
        require(first_cell && second_cell &&
                (first_cell->x != second_cell->x || first_cell->y != second_cell->y),
                "two extractors occupied the same FLD support cell");
        require(field.audit_resource_support_cell(first_cell->x, first_cell->y, 0U).valid() &&
                field.audit_resource_support_cell(second_cell->x, second_cell->y, 0U).valid(),
                "shared deposit placement used a non-Xenite support cell");
    }

    // Retail terrain placement rejects wet terrain. A pending ordinary structure must survive a
    // failed pass and may be consumed only after dry, valid terrain becomes available.
    {
        auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
        for (auto& cell : field.cells()) cell.water_delta_q12 = 0x1000;
        auto ordinary = building(0x500U, 11U);
        ordinary.entity_seed.runtime_class_specific_19c_raw = 0x7000;
        const std::array products{ordinary};
        // Ordinary mode-1 construction cannot use class 18. Seed a live ARM>=300 building with a
        // recovered build extent so this case isolates the terrain rejection/retry contract.
        const std::array seeds{faction_seed(field, 1U, 30U, 5U, 11U, 0x100000)};
        thandor::SimulationLabExperiment experiment;
        experiment.reset(config(), parameters, seeds, {}, products);
        auto& run = experiment.runs().front();
        run.pending_asset_requests.push_back({0x500U, 1U, 0U, 0U});
        experiment.step(field, 74U);
        require(run.metrics.planned_builds == 0U && run.pending_asset_requests.size() == 1U,
                "ordinary building was placed on retail-rejected wet terrain");
        for (auto& cell : field.cells()) cell.water_delta_q12 = thandor::FieldGrid::kDryWaterDeltaQ12;
        experiment.step(field, 128U);
        require(run.metrics.planned_builds == 1U && run.pending_asset_requests.empty(),
                "pending ordinary building was not placed after valid terrain became available");
    }

    // Phase-0 connected-region collector: the same 36-cell Xenite deposit
    // supplies one class-14 model at 72 Q4, or two registered models at 36 Q4
    // each. A disabled/terminal model releases its registration next phase.
    // This fixture gives both factions an explicit Q8 scale; the authored
    // faction-runtime multiplier is not exposed by the LEV editor baseline.
    {
        auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
        paint_rect(field, 20U, 20U, 25U, 25U, 0U);
        const auto extractor = [&](std::uint32_t faction, std::uint32_t x, std::uint32_t y) {
            auto seed = faction_seed(field,faction,x,y,14U);
            seed.army_asset_id = 0x14AU;
            thandor::SimulationLabModelComponentSeed component;
            component.runtime_class_id = 14U;
            component.maximum_integrity = 100;
            component.resource_support_class = 0U;
            component.resource_extraction_weight_u8 = 128U;
            seed.model_components.push_back(component);
            return seed;
        };
        std::array<thandor::SimulationLabFactionEconomySeed,8> economies{};
        for (auto& economy : economies) economy.terrain_contribution_scale_q8=256U;
        const std::array<thandor::SimulationLabProductionAsset,0> products{};
        const std::array single{extractor(1U,20U,20U)};
        thandor::SimulationLabExperiment one;
        one.reset(config(),parameters,single,economies,products);
        one.step(field,8U);
        require(one.runs().front().faction_xenite_extraction_rate_q4_per_tick[1U] == 72U &&
                one.runs().front().entities[0].model_components[0].last_resource_payout_q4 == 72U,
                "single-extractor connected-region payout differs from retail integer share");

        for(uint32_t batch=1;batch<=5;++batch){
            auto batched_config=config();batched_config.runtime_callback_step_ticks=batch;
            thandor::SimulationLabExperiment batched;
            batched.reset(batched_config,parameters,single,economies,products);
            batched.step(field,8U);
            const auto& b=batched.runs().front();
            require(b.faction_xenite_extraction_rate_q4_per_tick[1U]==72U &&
                    b.faction_economy[1U].xenite_current_q4==72U*batch &&
                    b.faction_score[1U].xenite_extracted_total_q4==72U*batch &&
                    b.entities[0].model_components[0].last_resource_payout_q4==72U*batch,
                    "batch changed extraction rate or failed to scale stock, score and model yield");
        }

        const std::array pair{extractor(1U,20U,20U),extractor(2U,25U,25U)};
        thandor::SimulationLabExperiment two;
        two.reset(config(),parameters,pair,economies,products);
        two.step(field,8U);
        auto& run=two.runs().front();
        require(run.faction_xenite_extraction_rate_q4_per_tick[1U]==36U &&
                run.faction_xenite_extraction_rate_q4_per_tick[2U]==36U &&
                run.entities[0].model_components[0].last_resource_payout_q4==36U &&
                run.entities[1].model_components[0].last_resource_payout_q4==36U,
                "two-extractor connected region did not divide its area evenly");
        // An offset linked class-14 MDL must claim the field under its own
        // world-transform translation, not under the ArmyRuntime root. The
        // entity root sits one field cell outside the Xenite deposit.
        auto linked=extractor(1U,19U,20U);
        const auto& support_world=field.at(20U,20U);
        linked.model_components[0].extraction_model_world_offset_at_seed=thandor::Vec3f{
            thandor::q12_to_float(support_world.world_x_q12)-linked.actor.position.x,
            thandor::q12_to_float(support_world.world_y_q12)-linked.actor.position.y,0.0F};
        const std::array linked_seeds{linked};
        thandor::SimulationLabExperiment linked_experiment;
        linked_experiment.reset(config(),parameters,linked_seeds,economies,products);
        linked_experiment.step(field,8U);
        const auto& linked_run=linked_experiment.runs().front();
        require(linked_run.faction_xenite_extraction_rate_q4_per_tick[1U]==72U &&
                linked_run.entities[0].model_components[0].last_resource_payout_q4==72U,
                "linked class-14 model registered at owner root instead of its offset support cell");

        run.entities[1].root_model_terminal=true;
        run.entities[1].runtime_removed=true; // Region membership ends at owner unlink.
        two.step(field,8U);
        require(run.faction_xenite_extraction_rate_q4_per_tick[1U]==72U &&
                run.faction_xenite_extraction_rate_q4_per_tick[2U]==0U &&
                run.entities[1].model_components[0].last_resource_payout_q4==0U,
                "terminated extractor was not released on the next phase-0 region rebuild");
    }

    // Regression: ArmyPlacement_ValidateAssetAtPointAndCellCorners accepts ANY
    // one of five attempts. A singleton deposit need not support all five
    // alternatives: the class-14 deposit check applies only under the first
    // successful world point. The old Lab ALL-of-five check rejected this.
    {
        auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
        paint_rect(field, 20U, 20U, 20U, 20U, 0U);
        const std::array products{building(0x14AU)};
        const std::array seeds{faction_seed(field, 1U, 20U, 18U, 18U, 0x100000)};
        thandor::SimulationLabExperiment experiment;
        experiment.reset(config(), parameters, seeds, {}, products);
        auto& run = experiment.runs().front();
        run.pending_asset_requests.push_back({0x14AU, 1U, 0U, 0U});
        experiment.step(field, 74U);
        require(run.metrics.planned_builds == 1U && run.pending_asset_requests.empty(),
                "single-cell extractor support was wrongly treated as five required deposits");
        const auto& placed = run.entities.back();
        require(placed.army_asset_id == 0x14AU &&
                thandor::float_to_q12(placed.actor.position.x) == field.at(20U, 20U).world_x_q12 &&
                thandor::float_to_q12(placed.actor.position.y) == field.at(20U, 20U).world_y_q12,
                "extractor did not preserve the first accepted placement point");
    }

    std::cout << "V566_SIMULATION_LAB_RETAIL_PLACEMENT PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V566_SIMULATION_LAB_RETAIL_PLACEMENT FAIL: " << error.what() << '\n';
    return 1;
}
