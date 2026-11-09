#include "thandor/simulation_lab_worker.hpp"
#include "thandor/simulation_lab_presentation.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

namespace thandor {

namespace {
constexpr std::uint32_t kMaximumPendingTicks = 256U;
// One queued Simulation Lab tick is one coherent generation across every active run.
// Snapshot publication may coalesce backlog generations; internal stepping stays one tick/run.
constexpr std::uint32_t kWorkerChunkTicks = 1U;
}

std::uint32_t simulation_lab_worker_thread_count(std::uint32_t runs,
    std::uint32_t requested, std::uint32_t hardware_threads) noexcept {
    if(runs==0U)return 0U;
    const auto automatic=hardware_threads>2U?hardware_threads-2U:1U;
    return std::min({runs,32U,requested?std::min(requested,32U):automatic});
}

struct SimulationLabWorker::State {
    FieldGrid field;
    SimulationLabExperiment experiment;
    std::atomic<std::uint32_t> pending_ticks{};
    std::atomic<bool> paused{};
    std::atomic<std::uint32_t> active_view_faction_index{1U};
    std::atomic<std::uint64_t> completed_generations{};
    std::atomic<std::uint64_t> snapshot_publications{};
    std::atomic<std::uint64_t> last_generation_duration_us{};
    std::atomic<std::uint64_t> maximum_generation_duration_us{};

    std::atomic<std::uint64_t> step_wall_us{}, run_sum_us{}, slowest_run_us{}, slowest_run_index{};
    std::atomic<std::uint64_t> snapshot_copy_us{}, peak_snapshot_copy_us{}, snapshot_copies{};
    std::atomic<std::uint64_t> retired_snapshot_reclaim_us{}, peak_pending_ticks{};
    std::atomic<std::uint64_t> accepted_ticks{}, deferred_ticks{}, backpressure_events{};
    // Each element has one writer; the generation barrier orders coordinator reads.
    std::vector<std::uint64_t> run_duration_us;
    std::uint32_t simulation_worker_count{};
    std::atomic<std::size_t> next_run{};

    // Coordinator wake state.
    std::mutex wake_mutex;
    std::condition_variable_any wake_condition;
    std::vector<std::shared_ptr<const SimulationLabExperiment>> retired_snapshots;

    // Persistent pool and generation barrier. Each claimed run has exactly one
    // writer until the barrier; different runs share only immutable inputs.
    std::mutex dispatch_mutex;
    std::condition_variable_any dispatch_condition;
    std::condition_variable_any dispatch_done_condition;
    std::uint64_t dispatch_epoch{};
    std::uint32_t dispatch_ticks{};
    std::size_t dispatch_completed{};
    std::shared_ptr<const std::string> dispatch_error;
    std::vector<std::jthread> run_workers;
};

SimulationLabWorker::~SimulationLabWorker() {
    stop_thread();
}

void SimulationLabWorker::stop_thread() noexcept {
    if (thread_.joinable()) {
        thread_.request_stop();
        if (state_) {
            state_->wake_condition.notify_all();
            state_->dispatch_condition.notify_all();
            state_->dispatch_done_condition.notify_all();
        }
        thread_.join();
    }
    // Run callables retain State, and State owns their jthreads. Break that
    // ownership cycle explicitly after the coordinator can no longer dispatch.
    // Request every stop before joining any worker, then release the callables.
    if (state_) {
        for (auto& worker : state_->run_workers) worker.request_stop();
        state_->dispatch_condition.notify_all();
        for (auto& worker : state_->run_workers)
            if (worker.joinable()) worker.join();
        state_->run_workers.clear();
    }
    state_.reset();
}

void SimulationLabWorker::start(
    const SimulationLabConfig& config,
    std::span<const std::uint32_t, AiKnowledgeData::kParameterCount> base_parameters,
    std::span<const SimulationLabSeedEntity> seed_entities,
    const FieldGrid& field) {
    start(config, base_parameters, seed_entities, {}, {}, {}, {}, {}, field);
}

void SimulationLabWorker::start(
    const SimulationLabConfig& config,
    std::span<const std::uint32_t, AiKnowledgeData::kParameterCount> base_parameters,
    std::span<const SimulationLabSeedEntity> seed_entities,
    std::span<const SimulationLabFactionEconomySeed> faction_economy,
    const FieldGrid& field) {
    start(config, base_parameters, seed_entities, faction_economy, {}, {}, {}, {}, field);
}

void SimulationLabWorker::start(
    const SimulationLabConfig& config,
    std::span<const std::uint32_t, AiKnowledgeData::kParameterCount> base_parameters,
    std::span<const SimulationLabSeedEntity> seed_entities,
    std::span<const SimulationLabFactionEconomySeed> faction_economy,
    std::span<const SimulationLabProductionAsset> production_assets,
    const FieldGrid& field) {
    start(config, base_parameters, seed_entities, faction_economy, production_assets, {}, {}, {}, field);
}

void SimulationLabWorker::start(
    const SimulationLabConfig& config,
    std::span<const std::uint32_t, AiKnowledgeData::kParameterCount> base_parameters,
    std::span<const SimulationLabSeedEntity> seed_entities,
    std::span<const SimulationLabFactionEconomySeed> faction_economy,
    std::span<const SimulationLabProductionAsset> production_assets,
    std::span<const SimulationLabTechnologyAsset> technology_assets,
    std::span<const FactionTechnologyMask256> faction_technology_masks,
    std::span<const SimulationLabFactionScoreSeed> faction_score_seeds,
    const FieldGrid& field,
    const SimulationLabScenarioRuntimeSeed& scenario_runtime) {
    stop_thread();

    auto state = std::make_shared<State>();
    state->field = field;
    state->experiment.reset(config, base_parameters, seed_entities, faction_economy, production_assets,
                            technology_assets, faction_technology_masks, faction_score_seeds, scenario_runtime);
    state->active_view_faction_index.store(std::min(scenario_runtime.active_view_faction_index,7U));
    // FLD traversal compatibility is immutable for this worker's field snapshot. Build it once
    // before the timed 20 Hz generation loop so every run thread shares read-only classification
    // instead of recomputing terrain normals inside route searches.
    state->experiment.prepare_navigation_terrain(state->field);
    error_.store({}, std::memory_order_release);
    published_.store(std::make_shared<const SimulationLabExperiment>(
                         state->experiment.snapshot_copy()),
                     std::memory_order_release);

    const auto run_count = state->experiment.runs().size();
    state->run_duration_us.resize(run_count);
    state->simulation_worker_count=simulation_lab_worker_thread_count(
        static_cast<std::uint32_t>(run_count),state->experiment.config().worker_thread_limit,
        std::thread::hardware_concurrency());
    // One worker executes directly on the background coordinator: no dispatch
    // thread, condition-variable round trip or cross-core migration per tick.
    const auto pool_size=state->simulation_worker_count>1U?state->simulation_worker_count:0U;
    state->run_workers.reserve(pool_size);
    for (std::uint32_t worker_index = 0; worker_index < pool_size; ++worker_index) {
        state->run_workers.emplace_back([state](std::stop_token stop_token) {
            std::uint64_t observed_epoch = 0U;
            while (!stop_token.stop_requested()) {
                std::uint32_t ticks = 0U;
                {
                    std::unique_lock lock(state->dispatch_mutex);
                    state->dispatch_condition.wait(lock, stop_token, [&] {
                        return stop_token.stop_requested() || state->dispatch_epoch != observed_epoch;
                    });
                    if (stop_token.stop_requested()) break;
                    observed_epoch = state->dispatch_epoch;
                    ticks = state->dispatch_ticks;
                }

                try {
                    for(;;){
                        if(stop_token.stop_requested())break;
                        const auto run_index=state->next_run.fetch_add(1U,std::memory_order_relaxed);
                        if(run_index>=state->experiment.runs().size())break;
                        const auto run_start=std::chrono::steady_clock::now();
                        state->experiment.step_run(state->field, run_index, ticks);
                        state->run_duration_us[run_index]=static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now()-run_start).count());
                    }
                } catch (const std::exception& exception) {
                    std::lock_guard lock(state->dispatch_mutex);
                    if (!state->dispatch_error)
                        state->dispatch_error = std::make_shared<const std::string>(exception.what());
                } catch (...) {
                    std::lock_guard lock(state->dispatch_mutex);
                    if (!state->dispatch_error)
                        state->dispatch_error = std::make_shared<const std::string>(
                            "unknown exception in Simulation Lab run worker");
                }

                bool last_worker = false;
                {
                    std::lock_guard lock(state->dispatch_mutex);
                    last_worker = ++state->dispatch_completed == state->run_workers.size();
                }
                // Only the last arrival can satisfy the coordinator's predicate.
                // Avoid waking/relocking it once for every run in a large experiment.
                if (last_worker) state->dispatch_done_condition.notify_one();
            }
        });
    }

    state_ = state;
    thread_ = std::jthread([this, state](std::stop_token stop_token) {
        thread_main(stop_token, state);
    });
}

void SimulationLabWorker::clear() {
    stop_thread();
    published_.store({}, std::memory_order_release);
    error_.store({}, std::memory_order_release);
}

void SimulationLabWorker::set_paused(bool paused_value) noexcept {
    auto state = state_;
    if (!state) return;
    {
        std::lock_guard lock(state->wake_mutex);
        state->paused.store(paused_value, std::memory_order_release);
        if (paused_value) state->pending_ticks.store(0U, std::memory_order_release);
    }
    state->wake_condition.notify_all();
}

void SimulationLabWorker::set_active_view_faction(std::uint32_t faction) noexcept {
    if(faction>=8U)return;
    const auto state=state_;
    if(state)state->active_view_faction_index.store(faction,std::memory_order_release);
}

std::uint32_t SimulationLabWorker::request_advance(std::uint32_t ticks) noexcept {
    auto state = state_;
    if (!state || ticks == 0U) return 0U;
    std::uint32_t accepted = 0U;

    {
    // Pair predicate mutation with the coordinator's wait mutex. An atomic
    // counter alone does not prevent notify racing between predicate and wait.
    std::lock_guard lock(state->wake_mutex);
    if (state->paused.load(std::memory_order_acquire)) return 0U;
    auto current = state->pending_ticks.load(std::memory_order_relaxed);
    while (true) {
        const auto remaining = kMaximumPendingTicks - std::min(current, kMaximumPendingTicks);
        accepted = std::min(ticks, remaining);
        const auto desired = current + accepted;
        if (state->pending_ticks.compare_exchange_weak(
                current, desired, std::memory_order_release, std::memory_order_relaxed)) {
            auto peak = state->peak_pending_ticks.load(std::memory_order_relaxed);
            while (desired > peak && !state->peak_pending_ticks.compare_exchange_weak(
                peak, desired, std::memory_order_relaxed)) {}
            break;
        }
    }
    state->accepted_ticks.fetch_add(accepted,std::memory_order_relaxed);
    if(accepted<ticks){
        state->deferred_ticks.fetch_add(ticks-accepted,std::memory_order_relaxed);
        state->backpressure_events.fetch_add(1U,std::memory_order_relaxed);
    }
    }
    if (accepted != 0U) state->wake_condition.notify_one();
    return accepted;
}

void SimulationLabWorker::retire_snapshot(
    std::shared_ptr<const SimulationLabExperiment> snapshot_value) noexcept {
    auto state = state_;
    if (!state || !snapshot_value) return;
    {
        std::lock_guard lock(state->wake_mutex);
        state->retired_snapshots.push_back(std::move(snapshot_value));
    }
    state->wake_condition.notify_one();
}

std::shared_ptr<const SimulationLabExperiment> SimulationLabWorker::snapshot() const noexcept {
    return published_.load(std::memory_order_acquire);
}

std::shared_ptr<const std::string> SimulationLabWorker::error() const noexcept {
    return error_.load(std::memory_order_acquire);
}

std::uint32_t SimulationLabWorker::pending_ticks() const noexcept {
    const auto state = state_;
    return state ? state->pending_ticks.load(std::memory_order_acquire) : 0U;
}

std::uint32_t SimulationLabWorker::run_worker_count() const noexcept {
    const auto state = state_;
    return state ? state->simulation_worker_count : 0U;
}

std::uint64_t SimulationLabWorker::completed_generations() const noexcept {
    const auto state = state_;
    return state ? state->completed_generations.load(std::memory_order_acquire) : 0U;
}

std::uint64_t SimulationLabWorker::snapshot_publications() const noexcept {
    const auto state=state_;
    return state?state->snapshot_publications.load(std::memory_order_acquire):0U;
}

std::uint64_t SimulationLabWorker::last_generation_duration_us() const noexcept {
    const auto state = state_;
    return state ? state->last_generation_duration_us.load(std::memory_order_acquire) : 0U;
}

std::uint64_t SimulationLabWorker::maximum_generation_duration_us() const noexcept {
    const auto state = state_;
    return state ? state->maximum_generation_duration_us.load(std::memory_order_acquire) : 0U;
}

SimulationLabWorkerPerformance SimulationLabWorker::performance() const noexcept {
    SimulationLabWorkerPerformance result;
    const auto state = state_;
    if (!state) return result;
#define SAMPLE(name) result.name = state->name.load(std::memory_order_relaxed)
    SAMPLE(step_wall_us); SAMPLE(run_sum_us); SAMPLE(slowest_run_us); SAMPLE(slowest_run_index);
    SAMPLE(snapshot_copy_us); SAMPLE(peak_snapshot_copy_us); SAMPLE(snapshot_copies);
    SAMPLE(retired_snapshot_reclaim_us); SAMPLE(peak_pending_ticks);
    SAMPLE(accepted_ticks); SAMPLE(deferred_ticks); SAMPLE(backpressure_events);
#undef SAMPLE
    return result;
}

bool SimulationLabWorker::paused() const noexcept {
    const auto state = state_;
    return state ? state->paused.load(std::memory_order_acquire) : false;
}

void SimulationLabWorker::thread_main(std::stop_token stop_token,
                                      const std::shared_ptr<State>& state) {
    auto last_publication=std::chrono::steady_clock::now();
    std::uint64_t unpublished_generations=0U;
    while (!stop_token.stop_requested()) {
        std::vector<std::shared_ptr<const SimulationLabExperiment>> retired;
        {
            std::unique_lock lock(state->wake_mutex);
            state->wake_condition.wait(lock, stop_token, [&] {
                return stop_token.stop_requested() || !state->retired_snapshots.empty() ||
                    (!state->paused.load(std::memory_order_acquire) &&
                     state->pending_ticks.load(std::memory_order_acquire) != 0U);
            });
            retired.swap(state->retired_snapshots);
        }
        // Destroy superseded snapshots on the worker rather than making the render/UI thread free
        // potentially large multi-run entity graphs while processing a frame.
        const auto reclaim_start = std::chrono::steady_clock::now();
        retired.clear();
        state->retired_snapshot_reclaim_us.store(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now()-reclaim_start).count()), std::memory_order_relaxed);
        if (stop_token.stop_requested()) break;
        if (state->paused.load(std::memory_order_acquire)) continue;

        auto pending = state->pending_ticks.load(std::memory_order_relaxed);
        std::uint32_t chunk = 0U;
        while (pending != 0U) {
            chunk = std::min(pending, kWorkerChunkTicks);
            if (state->pending_ticks.compare_exchange_weak(
                    pending, pending - chunk, std::memory_order_acq_rel,
                    std::memory_order_relaxed)) break;
        }
        if (chunk == 0U) continue;

        try {
            const auto generation_start = std::chrono::steady_clock::now();
            state->experiment.set_active_view_faction(
                state->active_view_faction_index.load(std::memory_order_acquire));
            if(state->run_workers.empty()){
                for(std::size_t run_index=0;run_index<state->experiment.runs().size();++run_index){
                    if(stop_token.stop_requested())break;
                    const auto run_start=std::chrono::steady_clock::now();
                    state->experiment.step_run(state->field,run_index,chunk);
                    state->run_duration_us[run_index]=static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now()-run_start).count());
                }
                if(stop_token.stop_requested())break;
            }else{
                {
                    std::lock_guard lock(state->dispatch_mutex);
                    // Previous generation has joined at dispatch_done_condition.
                    // Publish the view input once before waking any run workers.
                    state->next_run.store(0U,std::memory_order_relaxed);
                    state->dispatch_ticks = chunk;
                    state->dispatch_completed = 0U;
                    state->dispatch_error.reset();
                    ++state->dispatch_epoch;
                }
                state->dispatch_condition.notify_all();
    
                {
                    std::unique_lock lock(state->dispatch_mutex);
                    state->dispatch_done_condition.wait(lock, stop_token, [&] {
                        return stop_token.stop_requested() ||
                            state->dispatch_completed == state->run_workers.size();
                    });
                    if (stop_token.stop_requested()) break;
                    if (state->dispatch_error) throw std::runtime_error(*state->dispatch_error);
                }
            }

            state->step_wall_us.store(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now()-generation_start).count()), std::memory_order_relaxed);
            std::uint64_t sum_us=0U, slowest_us=0U, slowest_index=0U;
            for (std::size_t i=0; i<state->run_duration_us.size(); ++i) {
                const auto duration=state->run_duration_us[i];
                sum_us+=duration;
                if (duration>slowest_us) { slowest_us=duration; slowest_index=i; }
            }
            state->run_sum_us.store(sum_us,std::memory_order_relaxed);
            state->slowest_run_us.store(slowest_us,std::memory_order_relaxed);
            state->slowest_run_index.store(slowest_index,std::memory_order_relaxed);
            unpublished_generations+=chunk;
            // Under a backlog, the GUI cannot consume a thousand full multi-run
            // entity copies per second. Keep the one-tick parallel barrier, but
            // publish at most every 50 ms until the queue drains or the run ends.
            // The retained one-shot chain transports all intervening events.
            const auto completed_at=std::chrono::steady_clock::now();
            std::uint64_t completed_to_publish=0U;
            if(state->pending_ticks.load(std::memory_order_acquire)==0U ||
               state->experiment.finished() ||
               completed_at-last_publication>=simulation_lab_presentation_interval) {
                const auto copy_start=std::chrono::steady_clock::now();
                published_.store(std::make_shared<const SimulationLabExperiment>(
                                     state->experiment.snapshot_copy()),
                                 std::memory_order_release);
                const auto copy_us=static_cast<std::uint64_t>(std::chrono::duration_cast<
                    std::chrono::microseconds>(std::chrono::steady_clock::now()-copy_start).count());
                state->snapshot_copy_us.store(copy_us,std::memory_order_relaxed);
                state->peak_snapshot_copy_us.store(std::max(copy_us,
                    state->peak_snapshot_copy_us.load(std::memory_order_relaxed)),std::memory_order_relaxed);
                state->snapshot_copies.fetch_add(1U,std::memory_order_relaxed);
                completed_to_publish=unpublished_generations;
                unpublished_generations=0U;
                last_publication=completed_at;
            }
            // Generation cost includes snapshot publication; retired UI reclamation is timed separately.
            const auto generation_us = static_cast<std::uint64_t>(std::chrono::duration_cast<
                std::chrono::microseconds>(std::chrono::steady_clock::now() - generation_start).count());
            state->last_generation_duration_us.store(generation_us, std::memory_order_release);
            auto maximum_us = state->maximum_generation_duration_us.load(std::memory_order_relaxed);
            while (generation_us > maximum_us &&
                   !state->maximum_generation_duration_us.compare_exchange_weak(
                       maximum_us, generation_us, std::memory_order_release,
                       std::memory_order_relaxed)) {}
            if (state->experiment.finished())
                state->pending_ticks.store(0U, std::memory_order_release);
            // Readers waiting on completed_generations must see this generation's
            // snapshot and its duration, rather than the previous timing sample.
            if(completed_to_publish!=0U){
                state->snapshot_publications.fetch_add(1U,std::memory_order_release);
                state->completed_generations.fetch_add(completed_to_publish,std::memory_order_release);
            }
        } catch (const std::exception& exception) {
            error_.store(std::make_shared<const std::string>(exception.what()),
                         std::memory_order_release);
            state->pending_ticks.store(0U, std::memory_order_release);
            state->paused.store(true, std::memory_order_release);
        } catch (...) {
            error_.store(std::make_shared<const std::string>(
                "unknown exception in Simulation Lab coordinator"), std::memory_order_release);
            state->pending_ticks.store(0U, std::memory_order_release);
            state->paused.store(true, std::memory_order_release);
        }
        std::this_thread::yield();
    }
}

} // namespace thandor
