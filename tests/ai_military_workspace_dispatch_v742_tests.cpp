#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_ai_owner_list_v718.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace thandor;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
}
int main() try {
    // The native owner list admits at most 64 entries before the behavior
    // callback visits them. It does NOT back-fill busy/locked slots from the
    // units that overflowed the list.
    const auto admitted = lab_ai_admit_owner_list_v718(70, [](std::size_t) {
        return LabAiOwnerKindV718::Unit01;
    });
    check(admitted.units01.size()==64 && admitted.units01.front()==69 &&
          admitted.units01.back()==6, "workspace01 capacity/order changed");
    const auto field=FieldGrid::create(59,59,0,0);
    const auto& cell=field.at(29,29);
    SimulationLabSeedEntity seed;
    seed.army_asset_id=100;seed.faction_index=1;seed.runtime_model_class_id=2;
    seed.actor.position={q12_to_float(cell.world_x_q12), q12_to_float(cell.world_y_q12), 0};
    seed.root_model_maximum_integrity=1000;
    seed.motion.speed_q12_per_tick=4096;
    seed.motion.yaw_max_velocity_turn16=0x7fff;
    seed.motion.yaw_acceleration_turn16=0x7fff;
    seed.fog_uncover_radius_q12=8*4096;
    seed.occupancy_mark_radius_units=4096;
    seed.has_traversal_profile=true;seed.traversal_low_band_index=0;
    seed.traversal_high_band_index=4;
    SimulationLabProductionAsset product;
    product.army_asset_id=100;product.entity_seed=seed;
    product.ai_definition_class_value70=1;
    std::vector<SimulationLabSeedEntity> seeds(70,seed);
    SimulationLabConfig config;
    config.run_count=1;config.duration_ticks=64;
    config.sweep_mode=SimulationLabSweepMode::Fixed;
    config.parameter_index=100;
    SimulationLabScenarioRuntimeSeed scenario;
    scenario.enabled=true;scenario.active_faction_count=2;
    scenario.assignable_faction_count=2;
    scenario.simulate_all_active_factions_as_ai=true;
    std::array<std::uint32_t,AiKnowledgeData::kParameterCount> ki{};
    ki[0x23]=250000;ki[0x27]=1024;
    SimulationLabExperiment lab;
    lab.reset(config,ki,seeds,{},std::span(&product,1),{},{},{},scenario);
    auto& run=lab.runs()[0];
    // Native lock skips behavior, but must still consume one of the 64 slots.
    run.tick=9; // schedule faction 1 directly; retain the anchored native movement state before its owner callback
    auto& locked=run.entities[69];
    locked.combat_follow_owned=true;locked.combat_movement_initialized=true;
    locked.combat_movement.primary=locked.combat_movement.fallback=locked.combat_movement.stored=
        locked.combat_movement.model={float_to_q12(locked.actor.position.x),float_to_q12(locked.actor.position.y)};
    run.entities[69].combat_movement.flags|=0x2U;
    lab.step(field,1); // faction 1 callback at tick 10
    check(run.metrics.military_ai_selector_calls==63,
          "military behavior scanned beyond the bounded WS01 or refilled locked slot");
    check(!run.entities[69].military_ai_source_dispatch ||
          run.entities[69].military_ai_last_choice.kind==0,
          "locked unit received military site decision");
    for (std::size_t i=0;i<6;++i)
        check(!run.entities[i].military_ai_source_dispatch,
              "overflowed older owner received native military callback");
    for (std::size_t i=6;i<69;++i)
        check(run.entities[i].military_ai_source_dispatch,
              "admitted owner did not receive military callback");
    std::cout << "V742_WS01_DISPATCH PASS admitted=64 active=63 overflow=6 newest_first=1\n";
} catch (const std::exception& e) {
    std::cerr << "V742_WS01_DISPATCH FAIL " << e.what() << '\n';
    return 1;
}
