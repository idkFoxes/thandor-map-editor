#include "thandor/simulation_lab.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
}
int main() try {
    using namespace thandor;
    SimulationLabModelComponentState c;
    c.maximum_integrity = 100;
    c.integrity = 50;
    c.health_regeneration_per_step = 10;
    simulation_lab_advance_component_health(c);
    check(c.integrity == 60 && c.health_regeneration_delay_ticks == 4,
          "initial component callback did not heal and reset timer");
    for (int i = 0; i < 4; ++i) simulation_lab_advance_component_health(c);
    check(c.integrity == 60 && c.health_regeneration_delay_ticks == 0,
          "zero countdown was treated as negative");
    simulation_lab_advance_component_health(c);
    check(c.integrity == 70,"negative countdown did not heal");
    simulation_lab_advance_component_health(c,5);
    check(c.integrity == 75,"regeneration exceeded 3/4 health");
    c.integrity = 100;
    simulation_lab_advance_component_health(c,5);
    check(c.integrity == 100,"powered full-health model decayed");
    c.runtime_flags_ec = 1;
    simulation_lab_advance_component_health(c,5);
    check(c.integrity == 90,"switched-off health did not decay");
    for (int i = 0; i < 3; ++i) simulation_lab_advance_component_health(c,5);
    check(c.integrity == 75,"switched-off health crossed 3/4 floor");
    c.integrity = 50; c.runtime_flags_ec = 0x400;
    simulation_lab_advance_component_health(c,5);
    check(c.integrity == 50,"no-regeneration flag was ignored");
    c.runtime_flags_ec = 0x10; const auto timer = c.health_regeneration_delay_ticks;
    simulation_lab_advance_component_health(c,10);
    check(c.integrity == 50 && c.health_regeneration_delay_ticks == timer,
          "dismantling should freeze regeneration timer");
    c.runtime_flags_ec = 0; c.health_regeneration_delay_ticks = 0x200;
    for (int i = 0; i < 512; ++i) simulation_lab_advance_component_health(c);
    check(c.integrity == 50 && c.health_regeneration_delay_ticks == 0,
          "impact delay ended before its signed countdown crossed zero");
    simulation_lab_advance_component_health(c);
    check(c.integrity == 60,"impact delay did not release regeneration");
    c.integrity = 0;
    simulation_lab_advance_component_health(c,5);
    check(c.integrity == 0,"dead component was resurrected");

    auto field = FieldGrid::create(8,8,0,0);
    // Known first-triangle sample at world (3,-1). Retail projection gives
    // cell (4,2), fractions (1255,200): the signed weighted depth rounds to -1.
    field.at(4,2).water_delta_q12 = -7;
    field.at(5,2).water_delta_q12 = 11;
    field.at(4,3).water_delta_q12 = 19;
    field.at(5,3).water_delta_q12 = 1000;
    check(field.sample_retail_water_delta_q12(12288,-4096) == -1,
          "retail first triangle lost signed floor rounding");
    field.at(5,3).flags_and_material |= 0x2000U;
    check(field.sample_retail_water_delta_q12(12288,-4096) == 0,
          "diagonal edge flag did not reject water sample");
    field.at(5,3).flags_and_material &= ~0x2000U;
    for (uint32_t y = 0; y < field.height(); ++y)
        for (uint32_t x = 0; x < field.width(); ++x) field.at(x,y).water_delta_q12 = 128;
    check(field.sample_retail_water_delta_q12(12288,-4096) == 128,
          "constant interior water depth changed");
    check(field.sample_retail_water_delta_q12(-4096,4096) == 0 &&
          field.sample_retail_water_delta_q12(1000000,-1000000) == 0,
          "outside coordinates were clamped onto wet terrain");
    // Integration: damage seeds model +0xF8 and the real hierarchy callback
    // decrements it after the class's water-damage callback on the same tick.
    SimulationLabSeedEntity seed;
    seed.army_asset_id = 10; seed.faction_index = 1; seed.runtime_model_class_id = 2;
    seed.water_damage_multiplier = 1;
    seed.actor.position = {3,-1,0};
    SimulationLabModelComponentSeed component_seed;
    component_seed.runtime_class_id = 2;
    component_seed.maximum_integrity = 100;
    component_seed.health_regeneration_per_step = 10;
    seed.model_components.push_back(component_seed);
    std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    SimulationLabConfig config; config.run_count=1; config.duration_ticks=600;
    SimulationLabExperiment experiment;
    experiment.reset(config,parameters,std::span(&seed,1));
    experiment.step(field);
    const auto& component = experiment.runs()[0].entities[0].model_components[0];
    check(component.integrity == 99 && component.health_regeneration_delay_ticks == 511,
          "water damage did not seed and service actual regeneration delay");
    // The collider persists after death. Its water callback still resets the
    // native health timer before regeneration bookkeeping, without reviving it.
    {
        SimulationLabExperiment terminal;terminal.reset(config,parameters,std::span(&seed,1));
        auto& entity=terminal.runs()[0].entities[0];
        entity.model_components[0].integrity=0;
        entity.model_components[0].terminal_runtime_flag8=true;
        entity.root_model_integrity=0;entity.root_model_terminal=true;
        entity.model_components[0].health_regeneration_delay_ticks=511;
        terminal.step(field);
        check(entity.model_components[0].integrity==0&&
              entity.model_components[0].health_regeneration_delay_ticks==511,
              "terminal water callback stopped resetting the regeneration delay");
    }
    for (uint32_t y = 0; y < field.height(); ++y)
        for (uint32_t x = 0; x < field.width(); ++x) field.at(x,y).water_delta_q12 = -1;
    auto& live = const_cast<SimulationLabRun&>(experiment.runs()[0]).entities[0].model_components[0];
    live.integrity = 50;
    experiment.step(field,511);
    check(live.integrity == 50 && live.health_regeneration_delay_ticks == 0,
          "component regenerated before post-water delay expired");
    experiment.step(field);
    check(live.integrity == 60 && experiment.runs()[0].entities[0].root_model_integrity == 60,
          "root health summary did not follow regeneration");
    std::cout << "V596 model health and water regression PASS\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
