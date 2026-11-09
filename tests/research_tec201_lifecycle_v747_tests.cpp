#include "thandor/simulation_lab.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
using namespace thandor;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

SimulationLabSeedEntity structure(std::uint32_t asset_id,
                                  std::uint32_t runtime_class,
                                  std::vector<std::uint32_t> panel) {
    SimulationLabSeedEntity seed;
    seed.army_asset_id = asset_id;
    seed.faction_index = 1U;
    seed.runtime_model_class_id = runtime_class;
    seed.building = true;
    seed.root_model_maximum_integrity = 1000;
    seed.actor.position = {10.0F + static_cast<float>(asset_id & 3U), 10.0F, 0.0F};
    seed.research_technology_ids = std::move(panel);
    return seed;
}
}

int main() try {
    constexpr std::uint32_t kTechnology = 201U;
    auto field = FieldGrid::create(59U, 59U, 0, 0U);

    SimulationLabConfig config;
    config.run_count = 1U;
    config.duration_ticks = 256U;
    config.parameter_index = 100U;
    config.sweep_mode = SimulationLabSweepMode::Fixed;

    std::array<std::uint32_t, AiKnowledgeData::kParameterCount> parameters{};
    parameters[0x0DU] = 4096U; // selected-research candidate weight

    std::array<SimulationLabFactionEconomySeed, 8> economy{};
    economy[1].xenite_current_q4 = 1000U;
    economy[1].baseline_xenite_storage_limit_q4 = 1000U;
    economy[1].baseline_energy_supply_q4 = 1000U;
    economy[1].baseline_energy_generation_capacity_q4 = 1000U;

    std::array<FactionTechnologyMask256, 8> masks{};
    set_faction_technology_unlocked(masks[1], 0U, true);

    SimulationLabTechnologyAsset technology;
    technology.technology_id = kTechnology;
    technology.dependency_technology_id = kTechnology;
    technology.ai_base_candidate_score = 512;
    technology.xenite_cost_q4 = 120U;
    technology.energy_demand_q4 = 16U;
    technology.research_duration_ticks = 4U;

    // ARM330 enables research. The later source is the first represented owner
    // in a level-runtime scan; its panel is walked from the last serialized slot down.
    const std::array seeds{
        structure(330U, 11U, {}),
        structure(321U, 11U, {199U, kTechnology, 202U})
    };

    SimulationLabScenarioRuntimeSeed scenario;
    scenario.enabled = true;
    scenario.active_faction_count = 1U;
    scenario.assignable_faction_count = 1U;
    scenario.simulate_all_active_factions_as_ai = true;

    SimulationLabExperiment experiment;
    experiment.reset(config, parameters, seeds, economy, {},
                     std::span(&technology, 1), masks, {}, scenario);
    auto& run = experiment.runs().front();
    auto& source = run.entities[1];

    // A pending class-11 production job does not prevent research assignment,
    // but the pending-payment branch must wait for production to finish.
    source.production_building = true;
    source.production_asset_id = 399U;
    source.production_duration_ticks = 10000U;

    experiment.step(field, 74U);

    const auto& research = run.ai_research_trace[1];
    require(research.last_rebuild_tick == 74U && research.arms_factory_present,
            "TEC201 trace did not run on faction 1's first economy planning turn");
    require(research.source_count == 2U && research.candidate_count == 1U &&
                research.admitted_candidates == 1U && research.positive_candidates == 1U,
            "TEC201 WS00/WS12 source or candidate counts differ");
    const auto& candidate = research.candidates[0];
    require(candidate.technology_id == kTechnology && candidate.source_entity_index == 1U &&
                candidate.panel_slot == 2U && candidate.score_kind == 2U &&
                candidate.score == 512U,
            "TEC201 source order, panel slot, score kind or score differs");
    require(research.best_technology_id == kTechnology && research.best_score == 512U,
            "TEC201 was not the selected WS12 research candidate");

    const auto& purchase = run.ai_purchase_trace[1];
    require(purchase.tick == 74U && purchase.recorded_count >= 1U &&
                purchase.candidates[0].rank == 0U &&
                purchase.candidates[0].id == kTechnology &&
                purchase.candidates[0].kind == 2U &&
                purchase.candidates[0].decision ==
                    SimulationLabRun::PurchaseDecision::ResearchQueued,
            "TEC201 did not occupy the expected WS13 purchase rank/decision");
    require(research.last_purchase_tick == 74U &&
                research.last_purchase_technology_id == kTechnology &&
                research.last_purchase_queued && source.research_pending_payment &&
                !source.research_active,
            "TEC201 purchase did not create the recovered pending-payment state");
    require(run.faction_economy[1].xenite_current_q4 == 1000U,
            "TEC201 assignment spent Xenite before payment");

    experiment.step(field, 2U);
    require(source.research_pending_payment && !source.research_active &&
                run.faction_economy[1].xenite_current_q4 == 1000U &&
                run.metrics.research_jobs_started == 0U,
            "busy research source did not retain pending payment");

    source.production_building = false;
    experiment.step(field, 1U);
    require(!source.research_pending_payment && source.research_active &&
                source.research_elapsed_ticks == 0U &&
                run.faction_economy[1].xenite_current_q4 == 880U &&
                run.faction_active_research_energy_q4[1] == 16 &&
                run.metrics.research_jobs_started == 1U,
            "TEC201 payment/start state differs after production released the source");

    experiment.step(field, 3U);
    require(source.research_active && source.research_elapsed_ticks == 3U,
            "TEC201 completed before its fourth serviced research tick");
    experiment.step(field, 1U);
    require(!source.research_active &&
                faction_technology_unlocked(run.faction_technology_masks[1], kTechnology) &&
                run.ai_research_trace[1].last_completed_technology_id == kTechnology &&
                run.ai_research_trace[1].last_completion_tick == 81U &&
                run.metrics.research_jobs_completed == 1U &&
                run.faction_active_research_energy_q4[1] == 0 &&
                run.faction_economy[1].xenite_current_q4 == 880U,
            "TEC201 completion tick, unlock, energy release or one-time payment differs");

    std::cout << "V747_TEC201_LIFECYCLE PASS assignment=74 payment=77 completion=81"
                 " source=1 slot=2 rank=0\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V747_TEC201_LIFECYCLE FAIL: " << error.what() << '\n';
    return 1;
}
