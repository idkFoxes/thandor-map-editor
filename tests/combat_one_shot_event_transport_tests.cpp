#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_worker.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool same_position(const thandor::Vec3f& a, const thandor::Vec3f& b) {
    return std::abs(a.x - b.x) < 1.0e-6F &&
           std::abs(a.y - b.y) < 1.0e-6F &&
           std::abs(a.z - b.z) < 1.0e-6F;
}

thandor::SimulationLabConfig config(std::uint32_t ticks) {
    thandor::SimulationLabConfig result;
    result.run_count = 1U;
    result.duration_ticks = ticks;
    result.sweep_mode = thandor::SimulationLabSweepMode::Fixed;
    result.parameter_index = 1U;
    return result;
}

thandor::SimulationLabSeedEntity settling_walker(std::size_t placement,
                                                  std::uint32_t asset,
                                                  thandor::Vec3f position,
                                                  std::uint32_t progress) {
    thandor::SimulationLabSeedEntity seed;
    seed.source_placement_index = placement;
    seed.army_asset_id = asset;
    seed.faction_index = 1U;
    seed.runtime_model_class_id = 3U;
    seed.articulated_contact_interpolation_rate_q12 = 0x0900;
    seed.articulated_contact_sound_asset_id = 77U + static_cast<std::uint32_t>(placement);
    seed.actor.position = position;
    seed.actor.articulated_contact_transition_active = true;
    seed.actor.articulated_contact_progress_q12 = progress;
    seed.actor.articulated_last_contact_advance_q12 = 0x0100U;
    seed.actor.articulated_was_moving = true;
    seed.actor.articulated_swing_side = static_cast<std::uint32_t>(placement & 1U);
    return seed;
}

bool wait_for_generations(thandor::SimulationLabWorker& worker,
                          std::uint64_t generations,
                          std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (worker.error()) return false;
        if (worker.completed_generations() >= generations) return true;
        std::this_thread::sleep_for(1ms);
    }
    return false;
}
} // namespace

int main() try {
    const auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};

    // Two independent stop-settle contacts complete on consecutive worker ticks. The event journal
    // must retain both historical poses even if the consumer does not inspect the tick-1 snapshot.
    std::array<thandor::SimulationLabSeedEntity, 2> walkers{
        settling_walker(5U, 0x301U, {6.0F, 7.0F, 1.0F}, 0x0F00U),
        settling_walker(9U, 0x302U, {16.0F, 17.0F, 2.0F}, 0x0E00U),
    };
    thandor::SimulationLabExperiment experiment;
    experiment.reset(config(4U), parameters, walkers);
    experiment.step(field, 2U);
    const auto& run = experiment.runs().front();
    const auto events = thandor::simulation_lab_one_shot_events_after(run, 0U);
    require(events.size() == 2U, "two skipped-tick articulated events were not retained");
    require(events[0].sequence == 1U && events[1].sequence == 2U,
            "one-shot journal sequence is not monotonic");
    require(events[0].tick == 1U && events[1].tick == 2U,
            "one-shot events lost their originating simulation tick");
    require(events[0].source_placement_index == 5U && events[1].source_placement_index == 9U,
            "one-shot events lost source placement identity");
    require(events[0].kind == thandor::SimulationLabOneShotEventKind::ArticulatedContact &&
            events[1].kind == thandor::SimulationLabOneShotEventKind::ArticulatedContact,
            "articulated completions were journaled with the wrong kind");
    require(same_position(events[0].root_world_position, walkers[0].actor.position) &&
            same_position(events[1].root_world_position, walkers[1].actor.position),
            "one-shot journal did not preserve historical root pose");
    require(events[0].actor.articulated_contact_event_serial == 1U &&
            events[1].actor.articulated_contact_event_serial == 1U,
            "journaled actor snapshots are not from the completion tick");
    const auto after_first = thandor::simulation_lab_one_shot_events_after(run, events[0].sequence);
    require(after_first.size() == 1U && after_first.front().sequence == events[1].sequence,
            "sequence cursor replayed or skipped one-shot events");
    require(thandor::simulation_lab_latest_one_shot_event_sequence(run) == 2U,
            "latest one-shot sequence does not match the journal tail");

    // A moving class-2 entity must journal its movement-ground timed effect at the position from
    // the tick that actually triggered the countdown, rather than reconstructing it later.
    std::array<thandor::SimulationLabSeedEntity, 2> moving{};
    moving[0].source_placement_index = 12U;
    moving[0].army_asset_id = 0x401U;
    moving[0].faction_index = 1U;
    moving[0].runtime_model_class_id = 2U;
    moving[0].motion.speed_q12_per_tick = 0x0200;
    moving[0].motion.yaw_max_velocity_turn16 = 0x1000U;
    moving[0].motion.yaw_acceleration_turn16 = 0x0400U;
    moving[0].actor.position = {8.0F, 8.0F, 0.0F};
    moving[0].actor.movement_ground_effect_countdown_ticks = 1;
    moving[0].movement_ground_effect_interval_ticks = 4U;
    moving[0].movement_ground_land_effect_id = 0x55U;
    moving[0].movement_ground_water_effect_id = 0x56U;
    moving[1].source_placement_index = 13U;
    moving[1].army_asset_id = 0x402U;
    moving[1].faction_index = 2U;
    moving[1].actor.position = {20.0F, 8.0F, 0.0F};

    thandor::SimulationLabExperiment movement_experiment;
    movement_experiment.reset(config(4U), parameters, moving);
    movement_experiment.step(field, 1U);
    const auto& movement_run = movement_experiment.runs().front();
    const auto movement_events = thandor::simulation_lab_one_shot_events_after(movement_run, 0U);
    require(movement_events.size() == 1U,
            "movement-ground effect pulse was not journaled on its trigger tick");
    require(movement_events.front().kind == thandor::SimulationLabOneShotEventKind::MovementGroundEffect &&
            movement_events.front().tick == 1U &&
            movement_events.front().source_placement_index == 12U,
            "movement-ground journal metadata is incorrect");
    require(same_position(movement_events.front().root_world_position,
                          movement_run.entities.front().actor.position),
            "movement-ground event did not retain the post-movement root pose");
    require(movement_run.metrics.movement_ground_effect_events == 1U,
            "movement-ground event metric was not incremented");

    // The asynchronous worker publishes once per generation, but a slow UI may only acquire the
    // newest generation. Because snapshots share the immutable chain, both tick-1 and tick-2 events
    // must still be present when no intermediate snapshot was acquired by this consumer.
    thandor::SimulationLabWorker worker;
    worker.start(config(4U), parameters, walkers, field);
    worker.request_advance(2U);
    require(wait_for_generations(worker, 2U, 1000ms),
            "worker did not publish two event-generating generations");
    const auto newest = worker.snapshot();
    require(newest && newest->runs().front().tick >= 2U,
            "newest worker snapshot did not reach the second event tick");
    const auto worker_events = thandor::simulation_lab_one_shot_events_after(newest->runs().front(), 0U);
    require(worker_events.size() == 2U && worker_events[0].tick == 1U && worker_events[1].tick == 2U,
            "newest worker snapshot lost a one-shot event from a skipped generation");
    require(same_position(worker_events[0].root_world_position, walkers[0].actor.position) &&
            same_position(worker_events[1].root_world_position, walkers[1].actor.position),
            "worker snapshot coalescing lost historical one-shot pose");
    worker.clear();

    std::cout << "V544_SIMULATION_LAB_ONE_SHOT_EVENT_TRANSPORT PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V544_SIMULATION_LAB_ONE_SHOT_EVENT_TRANSPORT FAIL: " << error.what() << '\n';
    return 1;
}
