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
    // Own WS01 is newest-first, capped at 64 *before* behavior eligibility.
    const auto admitted=lab_ai_admit_owner_list_v718(70,[](std::size_t) {
        return LabAiOwnerKindV718::Unit01;
    });
    check(admitted.units01.size()==64 && admitted.units01.front()==69 &&
          admitted.units01.back()==6,"native Workspace 01 admission changed");
    const auto field=FieldGrid::create(59,59,0,0);
    const auto& cell=field.at(24,24);
    SimulationLabSeedEntity seed;
    seed.army_asset_id=50;seed.faction_index=1;seed.runtime_model_class_id=18;
    seed.actor.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
    seed.motion.speed_q12_per_tick=84;
    seed.motion.yaw_max_velocity_turn16=0x1000;
    seed.motion.yaw_acceleration_turn16=0x400;
    std::vector<SimulationLabSeedEntity> seeds(70,seed);
    SimulationLabConfig config;
    config.run_count=1;config.duration_ticks=64;
    config.sweep_mode=SimulationLabSweepMode::Fixed;config.parameter_index=100;
    SimulationLabScenarioRuntimeSeed scenario;
    scenario.enabled=true;scenario.active_faction_count=2;
    scenario.assignable_faction_count=2;scenario.simulate_all_active_factions_as_ai=true;
    std::array<std::uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    SimulationLabExperiment lab;
    lab.reset(config,parameters,seeds,{},{},{},{},{},scenario);
    auto& run=lab.runs()[0];
    run.tick=9;
    lab.step(field,1); // faction 1's first WS01 selector phase, tick 10
    std::uint32_t admitted_callbacks=0,overflow_callbacks=0,steps=0;
    for(std::size_t i=0;i<run.entities.size();++i) {
        const auto& pioneer=run.entities[i];
        check(pioneer.pioneer_runtime_step_count!=0,
              "overflow pioneer lost independent movement callback");
        ++steps;
        if(i>=6) admitted_callbacks+=pioneer.pioneer_gate_trace.calls;
        else overflow_callbacks+=pioneer.pioneer_gate_trace.calls;
    }
    check(admitted_callbacks==64,"admitted Pioneer selector callbacks missing");
    check(overflow_callbacks==0,"overflow Pioneer ran AI selector outside WS01");
    std::cout<<"V743_PIONEER_WS01 PASS admitted="<<admitted_callbacks
             <<" overflow="<<overflow_callbacks<<" movement="<<steps<<"\n";
} catch(const std::exception& e) {
    std::cerr<<"V743_PIONEER_WS01 FAIL "<<e.what()<<'\n';
    return 1;
}
