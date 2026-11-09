#include "thandor/simulation_lab.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
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

thandor::SimulationLabSeedEntity producer(std::uint32_t asset, std::uint32_t runtime_class,
                                           float x, float y) {
    thandor::SimulationLabSeedEntity value;
    value.source_placement_index = asset;
    value.army_asset_id = asset;
    value.faction_index = 1U;
    value.runtime_model_class_id = runtime_class;
    value.building = true;
    // V569: real producer buildings participate in the recovered construction workspace through
    // root MDL +0x19C. A zero synthetic fixture is not a valid retail build-area source.
    value.runtime_class_specific_19c_raw = 0x20000;
    value.actor.position = {x, y, 0.0F};
    return value;
}

thandor::SimulationLabProductionAsset product(std::uint32_t asset, std::uint32_t flags,
                                               std::uint32_t cost, std::uint32_t duration,
                                               std::int32_t energy, bool building,
                                               std::uint32_t runtime_class = 2U) {
    thandor::SimulationLabProductionAsset value;
    value.army_asset_id = asset;
    value.army_runtime_flags14_raw = flags;
    value.xenite_cost_q4 = cost;
    value.build_duration_ticks = duration;
    value.energy_demand_q4 = energy;
    value.entity_seed.army_asset_id = asset;
    value.entity_seed.runtime_model_class_id = runtime_class;
    value.entity_seed.building = building;
    if (building) value.entity_seed.runtime_class_specific_19c_raw = 0x20000;
    if (!building) {
        value.entity_seed.motion.speed_q12_per_tick = thandor::float_to_q12(1.0F);
        value.entity_seed.motion.yaw_max_velocity_turn16 = 0x4000;
        value.entity_seed.motion.yaw_acceleration_turn16 = 0x4000;
    }
    return value;
}
}

int main() try {
    auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
    std::array<thandor::SimulationLabFactionEconomySeed, 8> economy{};
    economy[1].xenite_current_q4 = 1000U;
    economy[1].xenite_storage_limit_q4 = 2000U;

    auto class11 = producer(0x501U, 11U, 8.0F, 8.0F);
    auto class13 = producer(0x502U, 13U, 20.0F, 8.0F);
    class13.production_compatibility_mask = 0x04U;
    class13.production_spawn_world = thandor::Vec3f{20.0F, 10.0F, 0.0F};
    class13.production_exit_world = thandor::Vec3f{35.0F, 10.0F, 0.0F};
    auto class22 = producer(0x503U, 22U, 32.0F, 8.0F);
    class22.production_storage_capacity = 2U;
    std::array<thandor::SimulationLabSeedEntity, 3> producers{class11, class13, class22};

    // Deliberately order the products opposite the producer classes. Each producer must scan the
    // shared faction secondary queue and skip incompatible entries rather than consuming by index.
    auto child22 = product(0x603U, 0x08U, 50U, 1U, 4, false);
    auto unit13 = product(0x602U, 0x04U, 200U, 2U, 9, false);
    auto building11 = product(0x601U, 0x10U, 100U, 3U, 7, true, 11U);
    std::array<thandor::SimulationLabProductionAsset, 3> products{child22, unit13, building11};

    thandor::SimulationLabExperiment experiment;
    experiment.reset(config(), parameters, producers, economy, products);
    auto& run = experiment.runs().front();
    run.secondary_purchase_requests.push_back({0x603U, 1U, 0U, 0U});
    run.secondary_purchase_requests.push_back({0x602U, 1U, 0U, 0U});
    run.secondary_purchase_requests.push_back({0x601U, 1U, 0U, 0U});

    require(run.faction_economy[1].xenite_current_q4 == 1000U,
            "enqueueing secondary purchases spent Xenite before producer acceptance");

    experiment.step(field, 1U);
    require(run.secondary_purchase_requests.empty(),
            "eligible producers did not consume their compatible secondary-queue products");
    require(run.faction_economy[1].xenite_current_q4 == 650U,
            "producer acceptance did not deduct ARM +0x28 exactly once");
    require(run.faction_active_production_energy_q4[1] == 20,
            "temporary ARM +0x2C Energy loads were not summed while jobs were active");
    require(run.metrics.production_jobs_started == 3U &&
            run.metrics.secondary_purchase_requests_consumed == 3U,
            "producer-start diagnostics disagree with accepted jobs");
    require(run.pending_asset_requests.empty(),
            "class 11 product reached the primary queue before build duration elapsed");

    experiment.step(field, 1U); // class 22 duration 1 completes here
    require(run.metrics.class22_assets_stored == 1U,
            "class 22 completion did not enter linked-child storage");
    require(run.entities[2].production_linked_child_count == 1U &&
            run.entities[2].production_linked_child_asset_ids[1] == 0x603U,
            "class 22 did not fill the highest free internal slot");
    require(run.faction_active_production_energy_q4[1] == 16,
            "class 22 completion did not remove its temporary Energy load");
    require(run.entities.size() == 3U && run.metrics.class22_aircraft_spawned == 0U,
            "class-22 completion must retain inventory without creating an uncommanded aircraft");

    experiment.step(field, 1U); // class 13 duration 2 completes here
    require(run.metrics.class13_units_spawned == 1U,
            "class 13 completion did not create a physical produced unit");
    require(run.entities.size() == 4U,
            "class 13 completion did not append a mobile entity after stored aircraft completion");
    const auto& produced_unit = run.entities.back();
    require(produced_unit.army_asset_id == 0x602U && !produced_unit.building && produced_unit.synthetic,
            "class 13 produced runtime has the wrong asset/type state");
    require(produced_unit.production_parent_entity_index == 1U,
            "class 13 produced runtime was not linked to its producer");
    require(std::abs(produced_unit.actor.position.x - 20.0F) < 0.01F &&
            produced_unit.production_linked_to_parent && !produced_unit.actor.move_target &&
            run.entities[1].production_behavior_state == 2U,
            "class 13 spawn must await factory-door OPENING before its exit command");
    require(run.entities[2].production_linked_child_count == 1U && run.entities[2].production_linked_child_slot_mask == 0U,
            "unlaunched aircraft storage became occupied while factory production advanced");
    require(run.faction_active_production_energy_q4[1] == 7,
            "class 13 completion did not remove its temporary Energy load");

    experiment.step(field, 1U); // class 11 duration 3 completes here
    require(run.metrics.class11_primary_completions == 1U && run.pending_asset_requests.size() == 1U,
            "class 11 completion did not move the product to the faction primary queue");
    require(run.pending_asset_requests.front().army_asset_id == 0x601U,
            "class 11 primary queue contains the wrong produced ARM");
    require(run.entities.size() == 4U,
            "class 11 incorrectly spawned its completed building directly");
    require(run.faction_active_production_energy_q4[1] == 0,
            "class 11 completion did not remove its temporary Energy load");
    require(run.faction_economy[1].xenite_current_q4 == 650U,
            "completion charged Xenite a second time");

    // Faction 1's first retail economy turn is tick 74. A primary request must
    // remain queued until that turn; the old tick-64 all-faction test was obsolete.
    run.pending_asset_requests.push_back({0x601U, 1U, run.tick, 0U});
    const std::size_t before_placement_entities = run.entities.size();
    experiment.step(field, 70U);
    require(run.tick == 74U, "test did not reach faction 1's retail economy wheel slot");
    require(run.entities.size() == before_placement_entities + 1U,
            "class 11 primary product was not materialized by the construction planner");
    require(run.pending_asset_requests.size() == 1U,
            "planner did not stop after one committed primary placement");
    require(run.metrics.completed_builds >= 3U,
            "Built/completion diagnostics omitted stored aircraft, mobile spawn, or primary placement");
    const auto& placed_building = run.entities.back();
    require(placed_building.army_asset_id == 0x601U && placed_building.building &&
            placed_building.synthetic && placed_building.construction_complete,
            "class 11 planner did not create a temporary run-local synthetic building runtime");
    require(run.entities[3U].production_linked_to_parent &&
            run.entities[1U].production_behavior_state == 2U,
            "class 13 child did not remain protected while the factory door was opening");
    // The default synthetic factory has no archived door speed. Its documented
    // Lab fallback is 0x1000 UV per tick, taking 128 ticks to open fully.
    // Keep this as an actual exit-state test instead of expecting departure by tick 74.
    experiment.step(field, 96U); // 74 -> 170: door open, child exited
    const auto& exited_unit = run.entities[3U];
    require(!exited_unit.production_linked_to_parent && !exited_unit.production_parent_entity_index &&
            !exited_unit.actor.move_target && std::abs(exited_unit.actor.position.x - 35.0F) <= 1.01F,
            "class 13 produced unit did not finish its protected factory-exit movement before normal AI control");
    require(run.faction_economy[1].xenite_current_q4 == 650U,
            "construction placement incorrectly charged Xenite");

    // Fill class 22's second slot, then prove the full producer leaves the next compatible request
    // in the secondary queue rather than accepting a job it cannot store.
    run.secondary_purchase_requests.push_back({0x603U, 1U, run.tick, 0U});
    experiment.step(field, 1U); // accept second child
    require(run.faction_economy[1].xenite_current_q4 == 600U,
            "second class 22 job was not charged on acceptance");
    experiment.step(field, 1U); // complete second child
    require(run.entities[2].production_linked_child_count == 2U &&
            run.entities[2].production_linked_child_asset_ids[0] == 0x603U,
            "class 22 did not use the remaining highest free storage slot");
    run.secondary_purchase_requests.push_back({0x603U, 1U, run.tick, 0U});
    experiment.step(field, 1U);
    require(run.secondary_purchase_requests.size() == 1U &&
            run.faction_economy[1].xenite_current_q4 == 600U,
            "full class 22 producer consumed/charged a product despite exhausted capacity");

    // Failed primary placement must leave the completed class-11 product pending for a later pass.
    // ARM 0x14A uses the recovered resource-site placement domain; an empty FLD has no class-0
    // support cells, so this gives a deterministic failed placement without inventing blockers.
    {
        std::array<thandor::SimulationLabFactionEconomySeed, 8> retry_economy{};
        retry_economy[1].xenite_current_q4 = 100U;
        auto retry_producer = producer(0x701U, 11U, 8.0F, 8.0F);
        auto retry_product = product(0x14AU, 0x10U, 10U, 1U, 3, true, 11U);
        thandor::SimulationLabExperiment retry_experiment;
        retry_experiment.reset(config(), parameters,
            std::span<const thandor::SimulationLabSeedEntity>(&retry_producer, 1U), retry_economy,
            std::span<const thandor::SimulationLabProductionAsset>(&retry_product, 1U));
        auto& retry_run = retry_experiment.runs().front();
        retry_run.secondary_purchase_requests.push_back({0x14AU, 1U, 0U, 0U});
        retry_experiment.step(field, 2U); // accept, then complete into primary queue
        require(retry_run.pending_asset_requests.size() == 1U,
                "class 11 retry product did not reach the primary queue");
        const std::size_t entities_before_retry = retry_run.entities.size();
        retry_experiment.step(field, 72U); // tick 74 faction-1 placement attempt
        require(retry_run.pending_asset_requests.size() == 1U &&
                retry_run.entities.size() == entities_before_retry,
                "failed primary placement consumed the pending ARM or created a runtime");
        require(retry_run.metrics.pending_placement_retries != 0U,
                "failed primary placement was not retained for a later planner pass");
        require(retry_run.faction_economy[1].xenite_current_q4 == 90U,
                "failed placement changed Xenite after the producer-start charge");
    }

    std::cout << "V562_SIMULATION_LAB_PRODUCER_PIPELINE PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V562_SIMULATION_LAB_PRODUCER_PIPELINE FAIL: " << error.what() << '\n';
    return 1;
}
