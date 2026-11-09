#include "thandor/simulation_lab.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

thandor::SimulationLabSeedEntity faction_seed(std::uint32_t asset, thandor::Vec3f position) {
    thandor::SimulationLabSeedEntity seed;
    seed.source_placement_index = asset;
    seed.army_asset_id = asset;
    seed.faction_index = 1U;
    seed.building = true;
    seed.runtime_class_specific_19c_raw = 0x40000;
    seed.actor.position = position;
    return seed;
}

thandor::SimulationLabConfig config(std::uint32_t seed) {
    thandor::SimulationLabConfig value;
    value.run_count = 1U;
    value.duration_ticks = 80U;
    value.base_seed = seed;
    value.parameter_index = 100U;
    value.sweep_mode = thandor::SimulationLabSweepMode::Fixed;
    value.fixed_value = 0;
    return value;
}

thandor::Vec3f place_pending_14b(thandor::FieldGrid& field,
                                 thandor::SimulationLabSeedEntity seed_entity,
                                 std::uint32_t rng_seed) {
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
    thandor::SimulationLabExperiment experiment;
    experiment.reset(config(rng_seed), parameters,
                     std::span<const thandor::SimulationLabSeedEntity>(&seed_entity, 1U));
    auto& run = experiment.runs().front();
    run.pending_asset_requests.push_back({0x14BU, 1U, 0U, 0U});
    experiment.step(field, 74U); // faction 1 first retail economy slot
    require(run.metrics.pending_requests_consumed == 1U,
            "pending 14B request was not consumed at the planning tick");
    for (const auto& entity : run.entities) {
        if (entity.synthetic && entity.army_asset_id == 0x14BU) return entity.actor.position;
    }
    throw std::runtime_error("pending 14B request did not create a synthetic entity");
}
}

int main() try {
    auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    constexpr std::uint32_t kSeed = 0x13579BDFU;

    // Workspace10 admission depends on the recovered persistent scratch image,
    // the phase-7 occupancy mask, own-structure extents and the 256-entry cap.
    // Previous V554 expected the *first 256 cells in the entire field* to be
    // eligible, which was only true of the obsolete Stage-7 placement proxy.
    // Test the exact V533 scoring rule independently from that admission set.
    const auto& candidate_cell = field.at(11U, 17U);
    const auto& anchor_cell = field.at(29U, 29U);
    const thandor::Vec3f candidate{
        thandor::q12_to_float(candidate_cell.world_x_q12),
        thandor::q12_to_float(candidate_cell.world_y_q12), 0.0F};
    const thandor::Vec3f arm300{
        thandor::q12_to_float(anchor_cell.world_x_q12),
        thandor::q12_to_float(anchor_cell.world_y_q12), 0.0F};
    constexpr std::uint32_t random_dword = 0x12345678U;
    const std::int64_t raw_expected =
        static_cast<std::int64_t>(thandor::float_to_q12(candidate.x)) +
        thandor::float_to_q12(candidate.y) + 0x5678;
    require(thandor::simulation_lab_workspace10_candidate_score(
                candidate, std::nullopt, random_dword) == raw_expected,
            "workspace10 no-ARM300 score substituted a synthetic centroid");
    const std::int64_t anchored_expected =
        std::llabs(static_cast<long long>(thandor::float_to_q12(candidate.x)) -
                   thandor::float_to_q12(arm300.x)) +
        std::llabs(static_cast<long long>(thandor::float_to_q12(candidate.y)) -
                   thandor::float_to_q12(arm300.y)) + 0x5678;
    require(thandor::simulation_lab_workspace10_candidate_score(
                candidate, arm300, random_dword) == anchored_expected,
            "workspace10 ARM300 scorer did not use the owned anchor translation");

    // Retain an end-to-end construction assertion for both source variants,
    // but do not confuse workspace population with the scoring function.
    const thandor::Vec3f origin_test = place_pending_14b(
        field, faction_seed(301U, arm300), kSeed);
    const thandor::Vec3f owned_anchor_test = place_pending_14b(
        field, faction_seed(300U, arm300), kSeed);
    require(std::isfinite(origin_test.x) && std::isfinite(origin_test.y) &&
            std::isfinite(owned_anchor_test.x) && std::isfinite(owned_anchor_test.y),
            "workspace10 placement did not return a finite accepted position");

    std::cout << "V554_AI_WORKSPACE10_ANCHOR_SCORING PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V554_AI_WORKSPACE10_ANCHOR_SCORING FAIL: " << error.what() << '\n';
    return 1;
}
