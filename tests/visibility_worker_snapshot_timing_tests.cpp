#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_worker.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;

// A 50 ms wall-time target depends on CPU speed and scheduler contention.
// Always test publication/navigation correctness; controlled performance runs
// can explicitly enforce the target, including in a release CI benchmark job.
const bool kEnforceHardWallTime=[] {
    const char* value=std::getenv("THANDOR_ENFORCE_20HZ_BUDGET");
    return value&&std::string_view(value)=="1";
}();

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool wait_for_generation(thandor::SimulationLabWorker& worker, std::uint64_t generation,
                         std::chrono::milliseconds timeout = 2000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (worker.completed_generations() >= generation) return true;
        if (worker.error()) return false;
        std::this_thread::sleep_for(1ms);
    }
    return false;
}

thandor::SimulationLabConfig config(std::uint32_t duration = 512U) {
    thandor::SimulationLabConfig value;
    value.run_count = 1U;
    value.duration_ticks = duration;
    value.parameter_index = 100U;
    value.sweep_mode = thandor::SimulationLabSweepMode::Fixed;
    return value;
}

thandor::SimulationLabProductionAsset startup_asset(std::uint32_t asset,
                                                     std::uint32_t runtime_class,
                                                     std::int32_t c0) {
    thandor::SimulationLabProductionAsset value;
    value.army_asset_id = asset;
    value.army_runtime_flags14_raw = 1U;
    value.runtime_class_specific_c0_raw = c0;
    value.entity_seed.army_asset_id = asset;
    value.entity_seed.runtime_model_class_id = runtime_class;
    // Deliberately false: the editor browser can classify retail startup class-14/class-16 assets
    // outside its Buildings tab. Primary-queue construction origin must make the runtime a building.
    value.entity_seed.building = false;
    value.entity_seed.root_model_maximum_integrity = 1000;
    return value;
}
} // namespace

int main() try {
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};

    // Regression for the user's V564 screenshot: retail startup may consume a catalog record whose
    // editor seed is not browser-classified as Building. Once the primary construction planner
    // commits it, the run-local runtime must still be marked as a synthetic building for rendering.
    {
        auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
        thandor::SimulationLabSeedEntity constructor;
        constructor.source_placement_index = 0U;
        constructor.army_asset_id = 50U;
        constructor.faction_index = 1U;
        constructor.runtime_model_class_id = 18U;
        constructor.building = false;
        constructor.runtime_class_specific_19c_raw = 0x5000;
        constructor.motion.speed_q12_per_tick = 0x100;
        const auto& constructor_cell = field.at(4U, 8U);
        constructor.actor.position = {thandor::q12_to_float(constructor_cell.world_x_q12),
                                      thandor::q12_to_float(constructor_cell.world_y_q12), 0.0F};
        thandor::Brush support_brush;
        support_brush.radius_cells = 1.0F;
        support_brush.shape = thandor::BrushShape::Square;
        support_brush.falloff = thandor::BrushFalloff::Constant;
        field.paint_placement_occupancy_class(10.0F, 8.0F, support_brush, 0U, true);
        const std::array seeds{constructor};
        auto hq = startup_asset(300U, 11U, 0);
        auto xenite = startup_asset(330U, 14U, 0);
        auto tritium = startup_asset(332U, 14U, 7);
        auto energy = startup_asset(333U, 16U, 0);
        hq.entity_seed.runtime_class_specific_19c_raw = 0x7000;
        xenite.entity_seed.runtime_class_specific_19c_raw = 0x7000;
        tritium.entity_seed.runtime_class_specific_19c_raw = 0x7000;
        energy.entity_seed.runtime_class_specific_19c_raw = 0x7000;
        const std::array products{hq, xenite, tritium, energy};
        thandor::SimulationLabExperiment experiment;
        experiment.reset(config(), parameters, seeds, {}, products);
        experiment.step(field, 74U); // faction 1 first retail economy dispatch
        const auto& run = experiment.runs().front();
        require(run.entities.size() == 2U, "retail empty-base planner did not create first legal runtime");
        const auto& placed = run.entities.back();
        require(placed.synthetic && placed.building && placed.army_asset_id == 330U,
                "mode-0 primary placement is not published as a visible synthetic building");
    }

    // 20 Hz correctness workload and opt-in performance contract. This reproduces the expensive part of the screenshot with
    // 24 simultaneously moving traversal actors on a larger 256x128 FLD. Every published worker
    // generation is one logical tick; the controlled benchmark target is 50 ms.
    {
        auto field = thandor::FieldGrid::create(256U, 128U, 0, 0U);
        std::vector<thandor::SimulationLabSeedEntity> seeds;
        seeds.reserve(24U);
        for (std::uint32_t i = 0; i < 24U; ++i) {
            thandor::SimulationLabSeedEntity seed;
            seed.source_placement_index = i;
            seed.army_asset_id = 100U + i;
            seed.faction_index = i < 12U ? 1U : 2U;
            seed.runtime_model_class_id = 2U;
            seed.has_traversal_profile = true;
            seed.traversal_low_band_index = 0U;
            // Fresh flat/dry cells carry retail classification bit 0x01000000, so use the next
            // recovered high-band selector for an admitted ground route.
            seed.traversal_high_band_index = 1U;
            seed.motion.speed_q12_per_tick = 0x100;
            seed.motion.yaw_max_velocity_turn16 = 0x1000;
            seed.motion.yaw_acceleration_turn16 = 0x400;
            seed.motion.collision_radius_q12 = 0x600;
            const std::uint32_t cell_x = i < 12U ? 16U : 220U;
            const std::uint32_t cell_y = 12U + (i % 12U) * 8U;
            const auto& cell = field.at(cell_x, cell_y);
            seed.actor.position = {thandor::q12_to_float(cell.world_x_q12),
                                   thandor::q12_to_float(cell.world_y_q12), 0.0F};
            seeds.push_back(seed);
        }

        thandor::SimulationLabWorker worker;
        worker.start(config(128U), parameters, seeds, field);
        std::uint64_t maximum_observed_us = 0U;
        for (std::uint64_t generation = 1U; generation <= 12U; ++generation) {
            worker.request_advance(1U);
            require(wait_for_generation(worker, generation),
                    "20 Hz worker did not publish the requested generation");
            require(!worker.error(), "20 Hz worker reported a simulation error");
            const auto elapsed_us = worker.last_generation_duration_us();
            require(elapsed_us>0U, "completed generation did not publish its timing metrics");
            require(worker.snapshot_publications()==generation,
                    "completed generation did not publish exactly one requested snapshot");
            maximum_observed_us = std::max(maximum_observed_us, elapsed_us);
            if (kEnforceHardWallTime && elapsed_us >= 50000U) {
                std::cerr << "generation " << generation << " took " << elapsed_us << " us\n";
                require(false, "Simulation Lab generation exceeded the 50 ms wall-time budget");
            }
        }
        const auto snapshot = worker.snapshot();
        require(snapshot && snapshot->runs().front().tick == 12U,
                "worker timing test did not preserve coherent tick publication");
        const auto& metrics = snapshot->runs().front().metrics;
        require(metrics.navigation_route_resolves > 0U && metrics.navigation_cached_ticks > 0U,
                "navigation scheduler did not exercise both route solving and cached movement ticks");
        std::cout << "V565 maximum 24-actor generation: " << maximum_observed_us << " us\n";
        worker.clear();
    }

    // Routed-path budget regression. Force every cross-faction command through a hard wall with a
    // narrow gap on a retail-maximum 139x139 field. The Lab may validate all direct lines in one
    // generation, but it is allowed to execute at most one full detour search per run/tick. This is
    // the workload that previously produced ~200 ms generations in the editor screenshot.
    {
        auto field = thandor::FieldGrid::create(139U, 139U, 0, 0U);
        for (std::uint32_t y = 2U; y + 2U < field.height(); ++y) {
            if (y >= 67U && y <= 71U) continue;
            field.at(70U, y).flags_and_material |= 0x80000000U;
        }
        std::vector<thandor::SimulationLabSeedEntity> seeds;
        seeds.reserve(24U);
        for (std::uint32_t i = 0; i < 24U; ++i) {
            thandor::SimulationLabSeedEntity seed;
            seed.source_placement_index = i;
            seed.army_asset_id = 300U + i;
            seed.faction_index = i < 12U ? 1U : 2U;
            seed.runtime_model_class_id = 2U;
            seed.has_traversal_profile = true;
            seed.traversal_low_band_index = 0U;
            seed.traversal_high_band_index = 1U;
            seed.motion.speed_q12_per_tick = 0x100;
            seed.motion.yaw_max_velocity_turn16 = 0x1000;
            seed.motion.yaw_acceleration_turn16 = 0x400;
            seed.motion.collision_radius_q12 = 0x600;
            const std::uint32_t cell_x = i < 12U ? 10U : 125U;
            const std::uint32_t cell_y = 8U + (i % 12U) * 10U;
            const auto& cell = field.at(cell_x, cell_y);
            seed.actor.position = {thandor::q12_to_float(cell.world_x_q12),
                                   thandor::q12_to_float(cell.world_y_q12), 0.0F};
            seeds.push_back(seed);
        }

        thandor::SimulationLabWorker worker;
        worker.start(config(128U), parameters, seeds, field);
        std::uint64_t maximum_observed_us = 0U;
        for (std::uint64_t generation = 1U; generation <= 24U; ++generation) {
            worker.request_advance(1U);
            require(wait_for_generation(worker, generation),
                    "routed 20 Hz worker did not publish the requested generation");
            require(!worker.error(), "routed 20 Hz worker reported a simulation error");
            const auto elapsed_us = worker.last_generation_duration_us();
            require(elapsed_us>0U, "completed generation did not publish its timing metrics");
            require(worker.snapshot_publications()==generation,
                    "completed generation did not publish exactly one requested snapshot");
            maximum_observed_us = std::max(maximum_observed_us, elapsed_us);
            if (kEnforceHardWallTime && elapsed_us >= 50000U) {
                std::cerr << "routed generation " << generation << " took " << elapsed_us << " us\n";
                require(false, "routed Simulation Lab generation exceeded the 50 ms wall-time budget");
            }
        }
        const auto snapshot = worker.snapshot();
        require(snapshot && snapshot->runs().front().tick == 24U,
                "routed timing test did not preserve coherent tick publication");
        const auto& metrics = snapshot->runs().front().metrics;
        require(metrics.navigation_routed_solves > 0U &&
                    metrics.navigation_routed_solves <= snapshot->runs().front().tick,
                "routed solver budget executed more than one full detour solve per tick");
        require(metrics.navigation_deferred_route_solves > 0U,
                "routed stress did not exercise deterministic deferred detour work");
        std::cout << "V565 maximum routed 24-actor generation: " << maximum_observed_us << " us\n";
        worker.clear();
    }

    std::cout << "20 Hz wall budget: " << (kEnforceHardWallTime?"ENFORCED":"reported only; set THANDOR_ENFORCE_20HZ_BUDGET=1 to enforce") << '\n';
    std::cout << "V565_SIMULATION_LAB_VISIBILITY_TIMING PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V565_SIMULATION_LAB_VISIBILITY_TIMING FAIL: " << error.what() << '\n';
    return 1;
}
