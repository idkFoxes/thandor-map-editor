#include "thandor/simulation_lab_worker.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

namespace {
using namespace std::chrono_literals;

std::uint32_t max_tick(const thandor::SimulationLabExperiment& experiment) {
    std::uint32_t result = 0U;
    for (const auto& run : experiment.runs()) result = std::max(result, run.tick);
    return result;
}

bool wait_until_tick(thandor::SimulationLabWorker& worker, std::uint32_t target,
                     std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto snapshot = worker.snapshot();
        if (snapshot && max_tick(*snapshot) >= target) return true;
        std::this_thread::sleep_for(1ms);
    }
    return false;
}

int fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}
} // namespace

int main() {
    thandor::SimulationLabConfig config;
    config.run_count = 2U;
    config.duration_ticks = 12U;
    config.sweep_mode = thandor::SimulationLabSweepMode::Fixed;
    config.fixed_value = 0;

    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
    thandor::SimulationLabSeedEntity seed;
    seed.source_placement_index = 0U;
    seed.army_asset_id = 1U;
    seed.faction_index = 1U;
    seed.runtime_model_class_id = 0U;
    seed.building = true;
    seed.root_model_maximum_integrity = 0x100000;
    seed.actor.position = {8.0F, 8.0F, 0.0F};
    const std::array seeds{seed};
    const auto field = thandor::FieldGrid::create(59U, 59U);

    thandor::SimulationLabWorker worker;
    worker.start(config, parameters, seeds, field);
    auto snapshot = worker.snapshot();
    if (!snapshot || !snapshot->active()) return fail("start must publish an active immutable snapshot");
    if (max_tick(*snapshot) != 0U) return fail("initial snapshot must be tick zero");

    const auto initial_snapshot = snapshot;
    worker.request_advance(5U);
    if (!wait_until_tick(worker, 5U, 1000ms)) return fail("worker did not advance requested ticks");
    if (!initial_snapshot || max_tick(*initial_snapshot) != 0U)
        return fail("published snapshots must remain immutable after worker advances");

    worker.set_paused(true);
    std::this_thread::sleep_for(10ms); // allow at most the in-flight two-tick chunk to publish
    snapshot = worker.snapshot();
    const auto paused_tick = snapshot ? max_tick(*snapshot) : 0U;
    worker.request_advance(5U);
    std::this_thread::sleep_for(30ms);
    snapshot = worker.snapshot();
    if (!snapshot || max_tick(*snapshot) != paused_tick)
        return fail("paused worker must not accept/advance queued presentation ticks");

    worker.set_paused(false);
    worker.request_advance(32U);
    const auto deadline = std::chrono::steady_clock::now() + 1000ms;
    while (std::chrono::steady_clock::now() < deadline) {
        snapshot = worker.snapshot();
        if (snapshot && snapshot->finished()) break;
        std::this_thread::sleep_for(1ms);
    }
    snapshot = worker.snapshot();
    if (!snapshot || !snapshot->finished()) return fail("worker did not reach experiment completion");
    if (max_tick(*snapshot) != config.duration_ticks)
        return fail("worker must stop exactly at configured experiment duration");

    worker.clear();
    if (worker.snapshot()) return fail("clear must retire the published experiment snapshot");
    std::cout << "PASS: async Simulation Lab worker stepping, pause/resume, completion, and clear\n";
    return 0;
}
