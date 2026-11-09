#include "thandor/simulation_lab.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::uint32_t next_rng(std::uint32_t& state) {
    const uint32_t first=state*33U+101U;
    state=first*33U+101U;
    return (first<<14U)^(state>>2U);
}

thandor::SimulationLabSeedEntity seed_entity(std::uint32_t asset = 0x100U) {
    thandor::SimulationLabSeedEntity seed;
    seed.source_placement_index = asset;
    seed.army_asset_id = asset;
    seed.faction_index = 1U;
    seed.building = true;
    seed.actor.position = {4.0F, 4.0F, 0.0F};
    return seed;
}

thandor::SimulationLabConfig config(std::uint32_t seed) {
    thandor::SimulationLabConfig value;
    value.run_count = 1U;
    value.duration_ticks = 128U;
    value.base_seed = seed;
    value.parameter_index = 100U; // keep KI slots under test unchanged
    value.sweep_mode = thandor::SimulationLabSweepMode::Fixed;
    value.fixed_value = 0;
    return value;
}

void paint_support(thandor::FieldGrid& field, float x, float y, std::uint32_t support_class) {
    thandor::Brush brush;
    brush.radius_cells = 2.0F;
    brush.shape = thandor::BrushShape::Square;
    brush.falloff = thandor::BrushFalloff::Constant;
    field.paint_placement_occupancy_class(x, y, brush, support_class, true);
}
}

int main() try {
    // V533 AiCandidateWorkspace_AddOrAccumulateWeightedEntry performs unsigned dword arithmetic:
    // (weight * 5 + random % weight) * 16, and rejects weight ranges 0/1 after the RNG draw.
    require(!thandor::simulation_lab_ai_candidate_weighted_delta(0U, 123U),
            "weight range zero should not create a candidate delta");
    require(!thandor::simulation_lab_ai_candidate_weighted_delta(1U, 123U),
            "weight range one should not create a candidate delta");
    require(thandor::simulation_lab_ai_candidate_weighted_delta(2U, 13U) == 176U,
            "weight-2 V533 score transform mismatch"); // (10 + 1) * 16
    require(thandor::simulation_lab_ai_candidate_weighted_delta(100U, 1234U) == 8544U,
            "weight-100 V533 score transform mismatch"); // (500 + 34) * 16
    {
        const std::uint32_t weight = 0xF0000001U;
        const std::uint32_t random = 0xDEADBEEFU;
        const std::uint32_t expected = (weight * 5U + random % weight) * 0x10U;
        require(thandor::simulation_lab_ai_candidate_weighted_delta(weight, random) == expected,
                "V533 candidate score transform lost 32-bit wrap semantics");
    }

    // Close base weights can reverse order under the retail transform. Native seed 9 gives the first
    // two Lab RNG draws mod 10/mod 9 as 0 and 6 respectively: KI1=10 scores 50, KI2=9 scores 51.
    // The old Lab formula (weight*256 + random_byte) could never let 9 beat 10.
    {
        auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
        paint_support(field, 12.0F, 12.0F, 0U);
        paint_support(field, 42.0F, 42.0F, 1U);
        std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
        parameters[1U] = 10U;
        parameters[2U] = 9U;
        parameters[5U] = 0U; // modeled workspace08-derived 14D path still consumes its retail draw
        auto seed = seed_entity();
        thandor::SimulationLabExperiment experiment;
        experiment.reset(config(9U), parameters,
                         std::span<const thandor::SimulationLabSeedEntity>(&seed, 1U));
        experiment.step(field, 74U);
        const auto& run = experiment.runs().front();
        require(run.pending_asset_requests.size() == 1U &&
                run.pending_asset_requests.front().army_asset_id == 0x14CU,
                "retail weighted randomization did not allow KI2=9 to beat KI1=10 for native seed 9");
        std::uint32_t expected_rng = 9U;
        (void)next_rng(expected_rng); // 14A
        (void)next_rng(expected_rng); // 14C
        (void)next_rng(expected_rng); // zero-weight 14D derived contribution
        (void)next_rng(expected_rng); // V574 ARM301 special-site candidate
        require(run.rng_state == expected_rng,
                "weightRange <= 1 stopped consuming the retail candidate RNG draw");
    }

    // V533 calls AddOrAccumulate once for the 14D base branch and again for its workspace08-derived
    // branch. The two contributions must therefore consume distinct draws before accumulating.
    {
        auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
        paint_support(field, 12.0F, 12.0F, 0U);
        std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
        parameters[1U] = 0U;  // 14A source exists but remains an editor-disabled contribution
        parameters[4U] = 10U; // 14D base contribution
        parameters[5U] = 10U; // approximated workspace08-derived 14D contribution
        parameters[21U] = 1000U;
        parameters[23U] = 1000U;
        std::array<thandor::SimulationLabFactionEconomySeed, 8> economy{};
        economy[1].tritium_storage_limit_q4 = 900U;
        economy[1].tritium_current_q4 = 100U;
        auto prerequisite = seed_entity(0x14CU);
        thandor::SimulationLabExperiment experiment;
        experiment.reset(config(0x12345678U), parameters,
                         std::span<const thandor::SimulationLabSeedEntity>(&prerequisite, 1U), economy);
        experiment.step(field, 74U);
        const auto& run = experiment.runs().front();
        require(run.pending_asset_requests.size() == 1U &&
                run.pending_asset_requests.front().army_asset_id == 0x14DU,
                "separate V533 14D contributions did not produce the expected candidate");
        std::uint32_t expected_rng = 0x12345678U;
        (void)next_rng(expected_rng); // zero-weight 14A contribution
        (void)next_rng(expected_rng); // 14D base
        (void)next_rng(expected_rng); // 14D derived
        (void)next_rng(expected_rng); // V574 zero-weight ARM301 special-site candidate
        require(run.rng_state == expected_rng,
                "14D base and derived weights were randomized as one combined contribution");
    }

    std::cout << "V553_AI_CANDIDATE_WEIGHT_RANDOMIZATION PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V553_AI_CANDIDATE_WEIGHT_RANDOMIZATION FAIL: " << error.what() << '\n';
    return 1;
}
