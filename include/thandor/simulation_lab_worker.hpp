#pragma once

#include "thandor/simulation_lab.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>

namespace thandor {

// Zero requested threads selects auto, reserving two reported hardware threads
// for presentation/audio/logging where possible. Explicit limits allow tuning.
std::uint32_t simulation_lab_worker_thread_count(std::uint32_t runs,
    std::uint32_t requested, std::uint32_t hardware_threads) noexcept;

// Observational timings, in microseconds. Independent atomic samples can straddle generations;
// they never participate in stepping/RNG. Run sum adds overlapping per-run elapsed times
// (including scheduling delays), not CPU time; step wall includes dispatch/wait.
struct SimulationLabWorkerPerformance {
    std::uint64_t step_wall_us{}, run_sum_us{}, slowest_run_us{}, slowest_run_index{};
    std::uint64_t snapshot_copy_us{}, peak_snapshot_copy_us{}, snapshot_copies{};
    std::uint64_t retired_snapshot_reclaim_us{}, peak_pending_ticks{};
    std::uint64_t accepted_ticks{}, deferred_ticks{}, backpressure_events{};
};

// Runs SimulationLabExperiment stepping off the editor/render thread and publishes immutable
// snapshots. The worker owns a copy of the FLD used at experiment start, so terrain edits cannot
// race pathing/collision reads while the experiment is running.
class SimulationLabWorker {
public:
    SimulationLabWorker() = default;
    ~SimulationLabWorker();

    SimulationLabWorker(const SimulationLabWorker&) = delete;
    SimulationLabWorker& operator=(const SimulationLabWorker&) = delete;

    void start(const SimulationLabConfig& config,
               std::span<const std::uint32_t, AiKnowledgeData::kParameterCount> base_parameters,
               std::span<const SimulationLabSeedEntity> seed_entities,
               const FieldGrid& field);
    void start(const SimulationLabConfig& config,
               std::span<const std::uint32_t, AiKnowledgeData::kParameterCount> base_parameters,
               std::span<const SimulationLabSeedEntity> seed_entities,
               std::span<const SimulationLabFactionEconomySeed> faction_economy,
               const FieldGrid& field);
    void start(const SimulationLabConfig& config,
               std::span<const std::uint32_t, AiKnowledgeData::kParameterCount> base_parameters,
               std::span<const SimulationLabSeedEntity> seed_entities,
               std::span<const SimulationLabFactionEconomySeed> faction_economy,
               std::span<const SimulationLabProductionAsset> production_assets,
               const FieldGrid& field);
    void start(const SimulationLabConfig& config,
               std::span<const std::uint32_t, AiKnowledgeData::kParameterCount> base_parameters,
               std::span<const SimulationLabSeedEntity> seed_entities,
               std::span<const SimulationLabFactionEconomySeed> faction_economy,
               std::span<const SimulationLabProductionAsset> production_assets,
               std::span<const SimulationLabTechnologyAsset> technology_assets,
               std::span<const FactionTechnologyMask256> faction_technology_masks,
               std::span<const SimulationLabFactionScoreSeed> faction_score_seeds,
               const FieldGrid& field,
               const SimulationLabScenarioRuntimeSeed& scenario_runtime = {});
    void clear();
    void set_paused(bool paused) noexcept;
    // Returns the number of ticks actually queued (0 while paused or saturated).
    // The caller must retain any unaccepted tick budget for later submission.
    std::uint32_t request_advance(std::uint32_t ticks) noexcept;
    void set_active_view_faction(std::uint32_t faction) noexcept;
    void retire_snapshot(std::shared_ptr<const SimulationLabExperiment> snapshot) noexcept;

    std::shared_ptr<const SimulationLabExperiment> snapshot() const noexcept;
    std::shared_ptr<const std::string> error() const noexcept;
    std::uint32_t pending_ticks() const noexcept;
    // Includes the background coordinator when it executes a one-worker pool.
    std::uint32_t run_worker_count() const noexcept;
    std::uint64_t completed_generations() const noexcept;
    std::uint64_t snapshot_publications() const noexcept;
    std::uint64_t last_generation_duration_us() const noexcept;
    std::uint64_t maximum_generation_duration_us() const noexcept;
    SimulationLabWorkerPerformance performance() const noexcept;
    bool paused() const noexcept;

private:
    struct State;
    void thread_main(std::stop_token stop_token, const std::shared_ptr<State>& state);
    void stop_thread() noexcept;

    std::shared_ptr<State> state_;
    std::jthread thread_;
    std::atomic<std::shared_ptr<const SimulationLabExperiment>> published_{};
    std::atomic<std::shared_ptr<const std::string>> error_{};
};

} // namespace thandor
