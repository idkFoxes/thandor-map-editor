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

thandor::SimulationLabSeedEntity seed_entity(const thandor::FieldGrid& field) {
    thandor::SimulationLabSeedEntity seed;
    seed.source_placement_index = 0U;
    seed.army_asset_id = 300U;
    seed.faction_index = 1U;
    seed.runtime_model_class_id = 11U;
    seed.building = true;
    seed.runtime_class_specific_19c_raw = 0x8000;
    const auto& cell = field.at(14U, 22U);
    seed.actor.position = {thandor::q12_to_float(cell.world_x_q12),
                           thandor::q12_to_float(cell.world_y_q12), 0.0F};
    return seed;
}

void paint_support(thandor::FieldGrid& field, std::uint32_t occupancy_class) {
    thandor::Brush brush;
    brush.radius_cells = 5.0F;
    brush.shape = thandor::BrushShape::Square;
    brush.falloff = thandor::BrushFalloff::Constant;
    field.paint_placement_occupancy_class(22.0F, 22.0F, brush, occupancy_class, true);
}

void check_chain(std::uint32_t support_class, std::uint32_t site_asset,
                 std::uint32_t derived_asset, std::uint32_t competing_site_asset,
                 std::uint32_t competing_derived_asset, std::uint32_t site_ki,
                 std::uint32_t derived_ki) {
    auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    paint_support(field, support_class);

    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
    parameters[site_ki] = 1000U;
    // Give the downstream structure a much larger weight. V531 says it is still ineligible until
    // the matching special-site prerequisite is complete.
    parameters[derived_ki] = 100000U;
    parameters[80U] = 0x1000U; // one-world-unit same-site axis separation for deterministic spacing

    thandor::SimulationLabConfig config;
    config.run_count = 1U;
    config.duration_ticks = 320U;
    config.parameter_index = site_ki;
    config.sweep_mode = thandor::SimulationLabSweepMode::Fixed;
    config.fixed_value = 1000;

    const auto seed = seed_entity(field);
    std::array<thandor::SimulationLabFactionEconomySeed,8> economy{};
    economy[1].xenite_current_q4 = 0U;
    economy[1].tritium_current_q4 = 0U;
    economy[1].xenite_storage_limit_q4 = 100U;
    economy[1].tritium_storage_limit_q4 = 100U;
    parameters[20U] = 1000U; parameters[21U] = 1000U;
    parameters[22U] = 1000U; parameters[23U] = 1000U;
    thandor::SimulationLabExperiment experiment;
    experiment.reset(config, parameters, std::span<const thandor::SimulationLabSeedEntity>(&seed, 1U), economy);

    experiment.step(field, 74U);
    auto& run = experiment.runs().front();
    require(run.tick == 74U, "first construction planning phase did not occur at tick 74");

    require(run.pending_asset_requests.size() == 1U &&
            run.pending_asset_requests.front().army_asset_id == site_asset,
            "special-site candidate was not queued as a pending request at tick 74");
    require(run.metrics.planned_builds == 0U,
            "candidate generation incorrectly placed construction in the same planning phase");
    experiment.step(field, 4U); // next faction-1 economy callback, tick 78
    require(run.tick == 78U && run.metrics.planned_builds == 1U,
            "special-site pending request was not consumed at tick 78");
    std::size_t first_generation = 0U;
    for (const auto& entity : run.entities) {
        if (!entity.synthetic || entity.construction_start_tick != 78U) continue;
        ++first_generation;
        require(entity.army_asset_id == site_asset,
                "special-site planner ignored the V531 FLD support-domain selection");
    }
    require(first_generation == 1U, "expected exactly one first-generation special site");
    for (const auto& entity : run.entities) {
        require(entity.army_asset_id != competing_site_asset,
                "planner created the opposite resource special site without its FLD support kind");
        require(entity.army_asset_id != competing_derived_asset,
                "planner created the opposite downstream structure without its prerequisite");
    }

    // Presence no longer requires completion: the placed-but-in-progress site already satisfies
    // V531 HasEntry. Candidate generation at tick 202 creates a pending downstream request; the
    // next planning phase consumes it into visible construction at tick 206.
    run.ai_parameters[site_ki] = static_cast<std::uint32_t>(-100000);
    run.ai_parameters[derived_ki] = 100000U;
    experiment.step(field, 124U); // next faction-1 economy window begins at tick 202
    require(run.tick == 202U, "derived-structure candidate phase did not land on tick 202");
    require(std::any_of(run.pending_asset_requests.begin(), run.pending_asset_requests.end(),
                [&](const auto& request) { return request.army_asset_id == derived_asset; }),
            "matching V531 downstream structure did not become pending from assigned prerequisite presence");
    experiment.step(field, 4U); // tick 206 completes the next request
    bool found_derived = false;
    for (const auto& entity : run.entities) {
        if (!entity.synthetic || entity.construction_start_tick != 206U) continue;
        if (entity.army_asset_id == derived_asset) found_derived = true;
    }
    require(found_derived,
            "matching V531 downstream pending request did not place on the next planning phase");
    for (const auto& entity : run.entities) {
        require(entity.army_asset_id != competing_site_asset,
                "opposite resource site appeared without matching FLD support");
        require(entity.army_asset_id != competing_derived_asset,
                "opposite downstream structure appeared without matching prerequisite");
    }
}
}

int main() try {
    // V531 AiSiteCandidate_AddTerrainFeatureCellIfSeparated:
    // occupancy/resource support class 0 -> ARM 0x14A, class 1 -> ARM 0x14C.
    // V531 AiStructureCandidate_AddWeightedId14BOr14CCandidate:
    // 0x14B requires 0x14A, 0x14D requires 0x14C.
    check_chain(0U, 0x14AU, 0x14BU, 0x14CU, 0x14DU, 1U, 3U);
    check_chain(1U, 0x14CU, 0x14DU, 0x14AU, 0x14BU, 2U, 4U);

    std::cout << "PASS: V531 construction support domains and prerequisite chains\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
