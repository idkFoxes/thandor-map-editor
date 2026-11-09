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

thandor::SimulationLabSeedEntity runtime_entity(std::uint32_t asset, std::uint32_t faction,
                                                 std::uint32_t runtime_class, float x) {
    thandor::SimulationLabSeedEntity value;
    value.source_placement_index = asset;
    value.army_asset_id = asset;
    value.faction_index = faction;
    value.runtime_model_class_id = runtime_class;
    value.building = runtime_class != 18U;
    value.actor.position = {x, 8.0F, 0.0F};
    return value;
}

thandor::SimulationLabProductionAsset building_asset(std::uint32_t asset,
                                                      std::uint32_t runtime_class,
                                                      std::int32_t c0,
                                                      std::uint32_t flags14 = 1U) {
    thandor::SimulationLabProductionAsset value;
    value.army_asset_id = asset;
    value.army_runtime_flags14_raw = flags14;
    value.runtime_class_specific_c0_raw = c0;
    value.entity_seed.army_asset_id = asset;
    value.entity_seed.runtime_model_class_id = runtime_class;
    value.entity_seed.building = true;
    value.entity_seed.root_model_maximum_integrity = 1000;
    return value;
}
}

int main() try {
    auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
    std::array<thandor::SimulationLabFactionEconomySeed, 8> economy{};
    economy[1].xenite_current_q4 = 777U;
    economy[2].xenite_current_q4 = 888U;

    // Faction 1 is the retail empty-base case: it owns class 18 but no Group-A runtime.
    // Faction 2 has class 18 plus class 11 and must therefore NOT receive the startup queue.
    std::array<thandor::SimulationLabSeedEntity, 3> seeds{
        runtime_entity(0x700U, 1U, 18U, 8.0F),
        runtime_entity(0x701U, 2U, 18U, 20.0F),
        runtime_entity(0x702U, 2U, 11U, 24.0F),
    };

    // Retail scans registry slots in ascending order and retains the LAST build-enabled match.
    // Deliberately scramble vector order: selection must follow registry id, not vector order.
    std::vector<thandor::SimulationLabProductionAsset> products;
    products.push_back(building_asset(250U, 11U, 0));
    products.push_back(building_asset(270U, 14U, 0));
    products.push_back(building_asset(200U, 14U, 1));
    products.push_back(building_asset(280U, 16U, 0));
    products.push_back(building_asset(260U, 14U, 7));
    products.push_back(building_asset(100U, 11U, 0));
    // A later but non-build-enabled class-11 definition must be ignored by ARM +0x14 bit 0.
    products.push_back(building_asset(299U, 11U, 0, 0U));

    thandor::SimulationLabExperiment experiment;
    experiment.reset(config(), parameters, seeds, economy, products);
    auto& run = experiment.runs().front();

    require(run.entities.size() == seeds.size(),
            "retail initialization invented runtime starter buildings");
    require(run.metrics.retail_initial_primary_queue_factions == 1U &&
                run.metrics.retail_initial_primary_queue_assets == 4U,
            "retail class-18 initialization diagnostics do not report exactly one four-entry queue");
    require(run.retail_initial_primary_queue_faction_mask == (1U << 1U),
            "retail initialization fired for the wrong faction set");
    require(run.pending_asset_requests.size() == 4U,
            "retail class-18 branch did not seed exactly four primary-queue entries");
    const std::array<std::uint32_t, 4> expected{250U, 270U, 260U, 280U};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        require(run.pending_asset_requests[i].faction_index == 1U,
                "startup queue entry belongs to the wrong faction");
        require(run.pending_asset_requests[i].army_asset_id == expected[i],
                "startup primary queue does not match retail class11/class14zero/class14nonzero/class16 order");
    }
    require(run.secondary_purchase_requests.empty(),
            "retail startup incorrectly used the paid secondary purchase queue");
    require(run.faction_economy[1].xenite_current_q4 == 777U,
            "retail startup queue incorrectly spent Xenite");

    // Queue reconstruction is independent of placement legality. These deliberately arbitrary ARM ids
    // prove the registry-selection order above, but none is one of retail's special 330/332 mode-0
    // resource structures and there is no live ARM>=300 build extent. V569 must therefore keep all four
    // requests pending instead of reviving the old whole-map placement shortcut.
    experiment.step(field, 64U);
    require(run.pending_asset_requests.size() == 4U && run.entities.size() == seeds.size(),
            "arbitrary startup ARM ids escaped the recovered retail placement-area gates");
    require(run.faction_economy[1].xenite_current_q4 == 777U,
            "pending primary startup stock charged Xenite");
    require(run.faction_score[1].relation_counter_b == 0,
            "failed startup placement changed relation counter B");

    // Separately prove that the real startup shape can bootstrap: ARM330 is the canonical Xenite
    // special structure, so a nearby support cell may be committed through class-18 mode 0 even before
    // any ordinary building extent exists. ARM300 remains earlier in the queue but is skipped until a
    // live build area exists, exactly as AiConstructionPlanner_ProcessPendingAssetRequests does.
    auto bootstrap_field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    thandor::Brush support_brush;
    support_brush.radius_cells = 1.0F;
    support_brush.shape = thandor::BrushShape::Square;
    support_brush.falloff = thandor::BrushFalloff::Constant;
    bootstrap_field.paint_placement_occupancy_class(10.0F, 8.0F, support_brush, 0U, true);
    auto bootstrap_pioneer = runtime_entity(50U, 1U, 18U, 0.0F);
    const auto& bootstrap_pioneer_cell = bootstrap_field.at(4U, 8U);
    bootstrap_pioneer.actor.position = {
        thandor::q12_to_float(bootstrap_pioneer_cell.world_x_q12),
        thandor::q12_to_float(bootstrap_pioneer_cell.world_y_q12), 0.0F};
    bootstrap_pioneer.runtime_class_specific_19c_raw = 0x5000;
    bootstrap_pioneer.motion.speed_q12_per_tick = 0x100;
    const std::array bootstrap_seeds{bootstrap_pioneer};
    std::vector<thandor::SimulationLabProductionAsset> canonical_products;
    canonical_products.push_back(building_asset(300U, 11U, 0));
    canonical_products.push_back(building_asset(330U, 14U, 0));
    canonical_products.push_back(building_asset(332U, 14U, 7));
    canonical_products.push_back(building_asset(333U, 16U, 0));
    for (auto& product : canonical_products)
        product.entity_seed.runtime_class_specific_19c_raw = 0x7000;
    thandor::SimulationLabExperiment bootstrap_experiment;
    bootstrap_experiment.reset(config(), parameters, bootstrap_seeds, economy, canonical_products);
    bootstrap_experiment.step(bootstrap_field, 74U);
    const auto& bootstrap_run = bootstrap_experiment.runs().front();
    require(std::any_of(bootstrap_run.entities.begin(), bootstrap_run.entities.end(), [](const auto& entity) {
                return entity.synthetic && entity.building && entity.army_asset_id == 330U &&
                       entity.runtime_model_class_id == 14U && entity.faction_index == 1U;
            }),
            "canonical Pioneer startup did not commit ARM330 through the special mode-0 path");
    require(bootstrap_run.faction_economy[1].xenite_current_q4 == 777U,
            "primary startup placement charged Xenite despite bypassing purchase/production");

    // Retail relies on the stock registry invariant that all four startup definitions exist.
    // The two ungated stack locals are undefined if that invariant is broken, so the editor must
    // reject the branch rather than synthesize a zero/guessed ARM id.
    auto incomplete_products = products;
    incomplete_products.erase(std::remove_if(incomplete_products.begin(), incomplete_products.end(),
        [](const auto& product) { return product.entity_seed.runtime_model_class_id == 16U; }),
        incomplete_products.end());
    thandor::SimulationLabExperiment incomplete_experiment;
    std::array<thandor::SimulationLabSeedEntity, 1> class18_only{
        runtime_entity(0x710U, 1U, 18U, 8.0F),
    };
    incomplete_experiment.reset(config(), parameters, class18_only, economy, incomplete_products);
    const auto& incomplete_run = incomplete_experiment.runs().front();
    require(incomplete_run.pending_asset_requests.empty() &&
                incomplete_run.retail_initial_primary_queue_faction_mask == 0U,
            "malformed catalog invented a retail startup ARM id");

    // A truly empty faction does not satisfy retail's class-18 gate. Do not replace that with an
    // editor-only synthetic base: the user can place the constructor if they want retail behavior.
    thandor::SimulationLabExperiment empty_experiment;
    std::array<thandor::SimulationLabSeedEntity, 0> no_seeds{};
    empty_experiment.reset(config(), parameters, no_seeds, economy, products);
    const auto& empty_run = empty_experiment.runs().front();
    require(empty_run.entities.empty() && empty_run.pending_asset_requests.empty() &&
                empty_run.retail_initial_primary_queue_faction_mask == 0U,
            "constructor-less faction received a non-retail synthetic base");

    std::cout << "V564_SIMULATION_LAB_RETAIL_INITIALIZATION PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V564_SIMULATION_LAB_RETAIL_INITIALIZATION FAIL: " << error.what() << '\n';
    return 1;
}
