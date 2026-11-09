#include "thandor/simulation_lab_worker.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

namespace {
using namespace std::chrono_literals;

int fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

bool wait_for_generation(thandor::SimulationLabWorker& worker, std::uint64_t generation,
                         std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (worker.completed_generations() >= generation) return true;
        std::this_thread::sleep_for(1ms);
    }
    return false;
}
} // namespace

int main() {
    constexpr std::uint32_t kRuns = 10U;
    thandor::SimulationLabConfig config;
    config.run_count = kRuns;
    config.duration_ticks = 100U;
    config.sweep_mode = thandor::SimulationLabSweepMode::Linear;
    config.minimum_value = 0;
    config.maximum_value = 0x1000;

    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
    thandor::SimulationLabSeedEntity a;
    a.source_placement_index = 0U;
    a.army_asset_id = 1U;
    a.faction_index = 1U;
    a.runtime_model_class_id = 0U;
    a.building = true;
    a.root_model_maximum_integrity = 0x100000;
    a.actor.position = {8.0F, 8.0F, 0.0F};

    thandor::SimulationLabSeedEntity b = a;
    b.source_placement_index = 1U;
    b.army_asset_id = 2U;
    b.faction_index = 2U;
    b.actor.position = {48.0F, 48.0F, 0.0F};
    const std::array seeds{a, b};
    const auto field = thandor::FieldGrid::create(59U, 59U);

    thandor::SimulationLabWorker worker;
    worker.start(config, parameters, seeds, field);
    const auto expected_workers=thandor::simulation_lab_worker_thread_count(kRuns,0U,std::thread::hardware_concurrency());
    if (worker.run_worker_count() != expected_workers)
        return fail("worker must use the configured bounded pool");
    if (worker.completed_generations() != 0U)
        return fail("new experiment must begin at generation zero");

    worker.request_advance(7U);
    if (!wait_for_generation(worker, 7U, 2000ms))
        return fail("parallel workers did not complete seven coherent generations");

    const auto snapshot = worker.snapshot();
    if (!snapshot || snapshot->runs().size() != kRuns)
        return fail("parallel worker snapshot must retain all ten runs");
    for (const auto& run : snapshot->runs()) {
        if (run.tick != 7U)
            return fail("every active run must advance exactly once per completed generation");
    }

    // The parallel worker path must remain bit-for-bit compatible with the deterministic serial
    // experiment API at the same generation boundary.
    thandor::SimulationLabExperiment serial;
    serial.reset(config, parameters, seeds);
    serial.step(field, 7U);
    const auto serial_runs = serial.runs();
    const auto parallel_runs = snapshot->runs();
    for (std::size_t i = 0; i < serial_runs.size(); ++i) {
        const auto& expected = serial_runs[i];
        const auto& actual = parallel_runs[i];
        if (actual.tick != expected.tick || actual.rng_state != expected.rng_state ||
            actual.swept_value != expected.swept_value ||
            actual.metrics.planning_cycles != expected.metrics.planning_cycles ||
            actual.metrics.planned_builds != expected.metrics.planned_builds ||
            actual.metrics.completed_builds != expected.metrics.completed_builds ||
            actual.entities.size() != expected.entities.size())
            return fail("parallel generation result diverged from deterministic serial stepping");
    }

    worker.clear();
    if (worker.run_worker_count() != 0U)
        return fail("clear must stop and retire all per-run worker threads");

    std::cout << "PASS: ten Simulation Lab runs use "<<expected_workers<<" workers and advance in lockstep generations\n";
    return 0;
}
