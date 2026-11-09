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
    value.fixed_value = 0;
    return value;
}

thandor::SimulationLabSeedEntity seed(std::uint32_t asset_id, std::uint32_t runtime_class,
                                      std::uint32_t faction, float x, float y, bool building) {
    thandor::SimulationLabSeedEntity value;
    value.source_placement_index = static_cast<std::size_t>(asset_id + faction * 0x1000U);
    value.army_asset_id = asset_id;
    value.faction_index = faction;
    value.runtime_model_class_id = runtime_class;
    value.building = building;
    value.actor.position = {x, y, 0.0F};
    value.motion.collision_radius_q12 = 0;
    value.root_model_maximum_integrity = 1000;
    return value;
}

thandor::SimulationLabProductionAsset product(std::uint32_t asset_id,
                                               std::uint32_t runtime_class,
                                               std::int32_t class_c0,
                                               std::uint32_t flags = 0x11U) {
    thandor::SimulationLabProductionAsset value;
    value.army_asset_id = asset_id;
    value.army_runtime_flags14_raw = flags;
    value.runtime_class_specific_c0_raw = class_c0;
    value.xenite_cost_q4 = 1U;
    value.build_duration_ticks = 2U;
    value.entity_seed = seed(asset_id, runtime_class, 0U, 0.0F, 0.0F, true);
    value.entity_seed.runtime_class_specific_c0_raw = class_c0;
    return value;
}

std::vector<thandor::SimulationLabProductionAsset> startup_catalog() {
    // Minimal catalog satisfying the exact V564 level-start scan: class 11, class 14/C0==0,
    // class 14/C0!=0, class 16. The construction candidates also carry the class-11 producer bit.
    return {
        product(300U, 11U, 0),
        product(0x14AU, 14U, 0),
        product(0x14CU, 14U, 7),
        product(0x14DU, 16U, 0),
    };
}

void paint_xenite_support(thandor::FieldGrid& field) {
    thandor::Brush brush;
    brush.radius_cells = 3.0F;
    brush.shape = thandor::BrushShape::Square;
    brush.falloff = thandor::BrushFalloff::Constant;
    field.paint_placement_occupancy_class(20.0F, 20.0F, brush, 0U, true);
}

std::array<thandor::SimulationLabFactionEconomySeed, 8> rich_economy() {
    std::array<thandor::SimulationLabFactionEconomySeed, 8> economy{};
    for (std::uint32_t faction = 1U; faction <= 4U; ++faction)
        economy[faction].xenite_current_q4 = 10000U;
    return economy;
}
}

int main() try {
    auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    paint_xenite_support(field);
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
    parameters[1U] = 100000U; // admit the 0x14A purchase candidate once a producer exists
    const auto products = startup_catalog();

    // V569 corrected the V568 whole-map placement approximation: with synthetic class-18
    // seeds that expose neither movement nor a +0x19C build radius, the retail four-entry startup
    // queue must remain pending rather than teleporting ARM300 to an arbitrary terrain cell. The
    // V569 placement/range regression supplies real movement/range values and verifies progress.
    {
        std::vector<thandor::SimulationLabSeedEntity> seeds;
        for (std::uint32_t faction = 1U; faction <= 4U; ++faction)
            seeds.push_back(seed(50U, 18U, faction,
                8.0F + static_cast<float>(faction) * 8.0F,
                8.0F + static_cast<float>(faction) * 8.0F, false));

        thandor::SimulationLabExperiment experiment;
        experiment.reset(config(), parameters, seeds, rich_economy(), products);
        auto& run = experiment.runs().front();
        require(run.pending_asset_requests.size() == 16U,
                "four Pioneer-only factions did not receive four retail startup requests each");
        experiment.step(field, 64U);
        require(std::none_of(run.entities.begin(), run.entities.end(), [](const auto& entity) {
                    return entity.army_asset_id >= 300U;
                }),
                "zero-range synthetic Pioneer allowed a startup structure to teleport onto the map");
        require(run.pending_asset_requests.size() == 16U,
                "zero-range synthetic Pioneer unexpectedly consumed a startup request");
    }

    // A pre-existing valid producer base suppresses the startup primary queue in retail. The
    // normal purchase path must therefore remain live: all four class-11 producers accept the
    // scored 0x14A secondary candidate on the first planning cadence. Pre-existing buildings must
    // not replay the Group-A class command and auto-convert the Pioneer.
    {
        std::vector<thandor::SimulationLabSeedEntity> seeds;
        for (std::uint32_t faction = 1U; faction <= 4U; ++faction) {
            seeds.push_back(seed(50U, 18U, faction,
                8.0F + static_cast<float>(faction) * 8.0F,
                8.0F + static_cast<float>(faction) * 8.0F, false));
            seeds.push_back(seed(300U, 11U, faction,
                9.0F + static_cast<float>(faction) * 8.0F,
                8.0F + static_cast<float>(faction) * 8.0F, true));
        }

        thandor::SimulationLabExperiment experiment;
        experiment.reset(config(), parameters, seeds, rich_economy(), products);
        auto& run = experiment.runs().front();
        require(run.pending_asset_requests.empty() &&
                run.retail_initial_primary_queue_faction_mask == 0U,
                "pre-existing Group-A producer base incorrectly received startup primary requests");
        // Retail economy dispatch visits one faction every eight ticks: 74, 82, 90, 98.
        // Observe each acceptance on the following owner callback; a two-tick product finishes before
        // the next faction buys, so four producers cannot all be building at once.
        for (std::uint32_t faction = 1U; faction <= 4U; ++faction) {
            const auto planning_tick=74U+(faction-1U)*8U;
        experiment.step(field,planning_tick-run.tick);
        require(run.metrics.production_jobs_started==faction-1U,
                "world-job purchase ran an already-completed owner callback");
        experiment.step(field,1U); // native producer accepts on the following callback
            require(run.metrics.production_jobs_started >= faction,
                    "a class-11 faction did not accept its default-base purchase on its economy slot");
            require(std::count_if(run.entities.begin(), run.entities.end(), [&](const auto& entity) {
                        return entity.faction_index == faction && entity.runtime_model_class_id == 11U &&
                               entity.production_building;
                    }) == 1,
                    "the currently selected class-11 producer did not start its purchase");
        }
        require(run.metrics.production_jobs_started == 4U,
                "valid class-11 default bases did not start one purchase per faction");
        require(run.secondary_purchase_requests.empty(),
                "accepted default-base purchases remained stuck in the secondary queue");
        require(run.metrics.pioneer_conversion_effects_started == 0U,
                "pre-existing default buildings replayed the Group-A conversion command");
    }

    // An arbitrary Group-A building is not automatically a producer. Retail checks producer
    // eligibility before registering the purchase; the compact Lab must not poison the faction's
    // secondary queue with an ARM that no class 11/13/22 runtime can accept.
    {
        std::array<thandor::SimulationLabSeedEntity, 2> seeds{
            seed(50U, 18U, 1U, 8.0F, 8.0F, false),
            seed(0x401U, 4U, 1U, 10.0F, 8.0F, true)};
        thandor::SimulationLabExperiment experiment;
        experiment.reset(config(), parameters, seeds, rich_economy(), products);
        auto& run = experiment.runs().front();
        experiment.step(field, 64U);
        require(run.secondary_purchase_requests.empty() &&
                run.metrics.secondary_purchase_requests_created == 0U,
                "unsupported candidate poisoned the faction secondary queue");
        require(run.metrics.production_jobs_started == 0U,
                "non-producer Group-A building was treated as a production runtime");
    }

    std::cout << "V568_SIMULATION_LAB_BOOTSTRAP_PRODUCTION PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V568_SIMULATION_LAB_BOOTSTRAP_PRODUCTION FAIL: " << error.what() << '\n';
    return 1;
}
