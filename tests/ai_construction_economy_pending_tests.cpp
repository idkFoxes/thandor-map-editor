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

thandor::SimulationLabSeedEntity seed(std::uint32_t asset, float x = 8.0F, float y = 8.0F) {
    thandor::SimulationLabSeedEntity value;
    value.source_placement_index = asset;
    value.army_asset_id = asset;
    value.faction_index = 1U;
    value.building = true;
    value.actor.position = {x, y, 0.0F};
    return value;
}

thandor::SimulationLabConfig config() {
    thandor::SimulationLabConfig value;
    value.run_count = 1U;
    value.duration_ticks = 512U;
    value.parameter_index = 100U; // do not overwrite the KI slots under test
    value.sweep_mode = thandor::SimulationLabSweepMode::Fixed;
    value.fixed_value = 0;
    return value;
}

void paint_support(thandor::FieldGrid& field, std::uint32_t support_class) {
    thandor::Brush brush;
    brush.radius_cells = 3.0F;
    brush.shape = thandor::BrushShape::Square;
    brush.falloff = thandor::BrushFalloff::Constant;
    field.paint_placement_occupancy_class(20.0F, 20.0F, brush, support_class, true);
}
}

int main() try {
    // Strict retail threshold boundaries: equality with KI[20] or KI[22] is blocked because both
    // machine comparisons are strict '<'.
    {
        auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
        std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
        parameters[3U] = 100000U;
        parameters[20U] = 1000U;
        parameters[22U] = 100U;
        std::array<thandor::SimulationLabFactionEconomySeed, 8> economy{};
        economy[1].xenite_storage_limit_q4 = 1000U; // equality: blocked
        economy[1].xenite_current_q4 = 901U;         // free 99, otherwise eligible
        auto prerequisite = seed(0x14AU);
        thandor::SimulationLabExperiment experiment;
        experiment.reset(config(), parameters, std::span<const thandor::SimulationLabSeedEntity>(&prerequisite, 1U), economy);
        experiment.step(field, 74U);
        const auto& run = experiment.runs().front();
        require(run.metrics.candidate_builds[2] == 0U,
                "KI[20] equality incorrectly passed the strict Xenite storage comparison");
    }
    {
        auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
        std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
        parameters[3U] = 100000U;
        parameters[20U] = 1000U;
        parameters[22U] = 100U;
        std::array<thandor::SimulationLabFactionEconomySeed, 8> economy{};
        economy[1].xenite_storage_limit_q4 = 999U;
        economy[1].xenite_current_q4 = 899U; // free == 100: blocked
        auto prerequisite = seed(0x14AU);
        thandor::SimulationLabExperiment experiment;
        experiment.reset(config(), parameters, std::span<const thandor::SimulationLabSeedEntity>(&prerequisite, 1U), economy);
        experiment.step(field, 74U);
        require(experiment.runs().front().metrics.candidate_builds[2] == 0U,
                "KI[22] equality incorrectly passed the strict Xenite free-storage comparison");
    }

    // Known capacity below both thresholds creates 14B; completely full Xenite applies the retail
    // x4 base-weight branch before candidate insertion.
    {
        auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
        std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
        parameters[3U] = 10U;
        parameters[20U] = 1000U;
        parameters[22U] = 1000U;
        std::array<thandor::SimulationLabFactionEconomySeed, 8> economy{};
        economy[1].xenite_storage_limit_q4 = 900U;
        economy[1].xenite_current_q4 = 900U;
        auto prerequisite = seed(0x14AU);
        thandor::SimulationLabExperiment experiment;
        experiment.reset(config(), parameters, std::span<const thandor::SimulationLabSeedEntity>(&prerequisite, 1U), economy);
        experiment.step(field, 74U);
        const auto& run = experiment.runs().front();
        require(run.metrics.candidate_builds[2] == 1U && run.metrics.xenite_full_weight_boosts == 1U,
                "full-Xenite 14B x4 candidate branch was not preserved");
    }

    // Unknown daten.hex storage capacity is explicit: do not guess a capacity and do not admit the
    // KI[20]/KI[22] base branch.
    {
        auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
        std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
        parameters[3U] = 100000U;
        parameters[20U] = 1000U;
        parameters[22U] = 1000U;
        std::array<thandor::SimulationLabFactionEconomySeed, 8> economy{};
        economy[1].xenite_current_q4 = 100U;
        auto prerequisite = seed(0x14AU);
        thandor::SimulationLabExperiment experiment;
        experiment.reset(config(), parameters, std::span<const thandor::SimulationLabSeedEntity>(&prerequisite, 1U), economy);
        experiment.step(field, 74U);
        const auto& run = experiment.runs().front();
        require(run.metrics.candidate_builds[2] == 0U && run.metrics.unknown_storage_capacity_blocks != 0U,
                "unknown Xenite capacity was silently treated as an authoritative storage limit");
    }

    // Pending presence semantics. A huge existing collision radius makes every supported 14A site
    // unplaceable. The pending 14A survives processing at tick 202 and nevertheless satisfies the
    // 14B HasEntry prerequisite in that same planning phase.
    {
        auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
        paint_support(field, 0U);
        std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
        parameters[1U] = 1000U;
        parameters[3U] = 100000U;
        parameters[20U] = 1000U;
        parameters[22U] = 1000U;
        std::array<thandor::SimulationLabFactionEconomySeed, 8> economy{};
        economy[1].xenite_storage_limit_q4 = 900U;
        economy[1].xenite_current_q4 = 100U;
        auto blocker = seed(0x100U, 20.0F, 20.0F);
        blocker.motion.collision_radius_q12 = thandor::float_to_q12(100.0F);
        thandor::SimulationLabExperiment experiment;
        experiment.reset(config(), parameters, std::span<const thandor::SimulationLabSeedEntity>(&blocker, 1U), economy);
        experiment.step(field, 74U);
        auto& run = experiment.runs().front();
        require(run.pending_asset_requests.size() == 1U && run.pending_asset_requests.front().army_asset_id == 0x14AU,
                "tick 74 did not create the expected pending 14A request");
        run.ai_parameters[1U] = static_cast<std::uint32_t>(-100000);
        // Faction 1's next economy phase is 128 ticks later (tick 202).
        experiment.step(field, 128U);
        require(std::any_of(run.pending_asset_requests.begin(), run.pending_asset_requests.end(),
                    [](const auto& request) { return request.army_asset_id == 0x14AU; }),
                "unplaceable pending 14A was incorrectly consumed");
        require(std::any_of(run.pending_asset_requests.begin(), run.pending_asset_requests.end(),
                    [](const auto& request) { return request.army_asset_id == 0x14BU; }),
                "pending 14A did not satisfy the V531 14B presence prerequisite");
        require(run.metrics.pending_placement_retries != 0U,
                "failed pending placement was not exposed by diagnostics");
    }

    // Workspace04 is eight entries. Failed requests remain and prevent a ninth candidate from
    // being generated for the faction.
    {
        auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
        std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
        auto entity = seed(0x100U);
        thandor::SimulationLabExperiment experiment;
        experiment.reset(config(), parameters, std::span<const thandor::SimulationLabSeedEntity>(&entity, 1U));
        auto& run = experiment.runs().front();
        for (std::uint32_t i = 0; i < 8U; ++i)
            run.pending_asset_requests.push_back({0x14AU, 1U, 0U, 0U});
        experiment.step(field, 74U);
        require(run.pending_asset_requests.size() == 8U && run.metrics.pending_capacity_blocks == 1U,
                "workspace04 eight-entry pending capacity was not enforced");
    }

    // 14D has an independent workspace08-derived weighting path. Xenite support alone can provide
    // a KI[5] contribution even with no 14C and no Tritium capacity.
    {
        auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
        paint_support(field, 0U);
        std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
        parameters[1U] = static_cast<std::uint32_t>(-100000);
        parameters[4U] = 0U;
        parameters[5U] = 100000U;
        auto entity = seed(0x100U);
        thandor::SimulationLabExperiment experiment;
        experiment.reset(config(), parameters, std::span<const thandor::SimulationLabSeedEntity>(&entity, 1U));
        experiment.step(field, 74U);
        const auto& run = experiment.runs().front();
        require(run.metrics.candidate_builds[3] == 1U && run.metrics.derived14d_weight_events != 0U,
                "14D workspace08-derived KI[5] path incorrectly depends on 14C/capacity");
    }

    std::cout << "V542_AI_CONSTRUCTION_ECONOMY_PENDING PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V542_AI_CONSTRUCTION_ECONOMY_PENDING FAIL: " << error.what() << '\n';
    return 1;
}
