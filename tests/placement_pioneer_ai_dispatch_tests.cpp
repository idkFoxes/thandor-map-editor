#include "thandor/pioneer_ai_dispatch.hpp"
#include "thandor/simulation_lab.hpp"
#include <array>
#include <climits>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
}

int main() try {
    using namespace thandor;
    int32_t cooldown = 8;
    check(pioneer_ai_dispatch_ready(0U, cooldown) && cooldown == 8, "idle unit consumed cooldown");
    check(!pioneer_ai_dispatch_ready(3U, cooldown) && cooldown == 8, "locked unit consumed cooldown");
    for (int i = 7; i > 0; --i)
        check(!pioneer_ai_dispatch_ready(1U, cooldown) && cooldown == i, "busy unit dispatched early");
    check(pioneer_ai_dispatch_ready(0x10U, cooldown) && cooldown == 0, "zero cooldown did not dispatch");
    check(pioneer_ai_dispatch_ready(1U, cooldown) && cooldown == 0, "negative decrement was not undone");
    cooldown = -7;
    check(pioneer_ai_dispatch_ready(1U, cooldown) && cooldown == -7, "negative seeded state changed");
    cooldown = INT_MIN;
    check(!pioneer_ai_dispatch_ready(1U, cooldown) && cooldown == INT_MAX, "x86 DEC wrap changed");

    // Match the observed slow Pioneer (84 Q12/tick). The old Lab entered the
    // gate at tick 14 after its tick-10 bootstrap and latched special behavior.
    auto field = FieldGrid::create(59U, 59U, 0, 0U);
    SimulationLabSeedEntity seed;
    seed.army_asset_id = 50U;
    seed.faction_index = 1U;
    seed.runtime_model_class_id = 18U;
    seed.motion.speed_q12_per_tick = 84;
    seed.motion.yaw_max_velocity_turn16 = 0x1000;
    seed.motion.yaw_acceleration_turn16 = 0x400;
    const auto& cell = field.at(20U, 20U);
    seed.actor.position = {q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
    SimulationLabConfig config;
    config.run_count = 1U;
    config.duration_ticks = 400U;
    config.sweep_mode = SimulationLabSweepMode::Fixed;
    config.parameter_index = 100U;
    std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    SimulationLabExperiment experiment;
    experiment.reset(config,parameters,std::span(&seed,1));
    experiment.step(field,14U);
    const auto& first = experiment.runs()[0].entities[0];
    check(first.pioneer_ai_command_count == 1U && first.pioneer_ai_command_cooldown_ticks == 7,
          "tick-14 callback did not retain first order through cooldown");
    check(first.pioneer_ai_busy_skipped_calls == 1U && first.pioneer_gate_trace.calls == 1U &&
          (first.pioneer_movement.flags & 0x100U) == 0U,
          "busy callback prematurely entered special behavior");
    experiment.step(field,286U);
    const auto& later = experiment.runs()[0].entities[0];
    check(later.pioneer_ai_command_count >= 2U && later.pioneer_gate_trace.special_flag_calls == 0U,
          "slow Pioneer stalled instead of accepting a later roaming order");
    // A runtime special callback is independent of the scheduled busy-unit gate.
    auto& live = const_cast<SimulationLabRun&>(experiment.runs()[0]).entities[0];
    live.pioneer_movement.flags |= 0x100U;
    const auto old_calls = live.pioneer_gate_trace.calls;
    experiment.step(field,1U);
    check(live.pioneer_gate_trace.calls == old_calls + 1U,
          "special class-18 runtime callback was incorrectly suppressed");
    std::cout << "V596 Pioneer cooldown and live slow-move regression PASS\n";
    return 0;
} catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
