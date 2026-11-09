#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/simulation_lab_follow.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace thandor;
static void check(bool yes, const char* msg) {
    if (!yes) throw std::runtime_error(msg);
}

int main() try {
    FieldGrid field=FieldGrid::create(80,80,0,0);
    const auto& cell=field.at(35,35);
    const Vec3f start{q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
    SimulationLabSeedEntity seed{};
    seed.army_asset_id=1U;
    seed.faction_index=1U;
    seed.runtime_model_class_id=1U;
    seed.actor.position=start;
    seed.actor.move_target=Vec3f{start.x+2.F,start.y,0};
    seed.motion.speed_q12_per_tick=2048;
    seed.motion.yaw_max_velocity_turn16=0x7fffU;
    seed.motion.yaw_acceleration_turn16=0x7fffU;
    seed.root_model_maximum_integrity=1000;

    SimulationLabConfig config{};
    config.run_count=1U;
    config.duration_ticks=256U;
    config.sweep_mode=SimulationLabSweepMode::Fixed;
    std::array<std::uint32_t,AiKnowledgeData::kParameterCount> ki{};
    SimulationLabScenarioRuntimeSeed scenario{};
    scenario.enabled=true;
    scenario.active_faction_count=scenario.assignable_faction_count=1U;
    SimulationLabExperiment lab;
    lab.reset(config,ki,std::span(&seed,1),{},{},{},{},{},scenario);
    auto& unit=lab.runs()[0].entities.at(0);
    check(unit.ai_path_coordinate_q12.has_value(),"seed path coordinate missing");
    check(!unit.ai_damage_tracked_coordinate_q12,"unearned initial tracked anchor");
    // Starting a route publishes native fallback/primary/stored aliases in .777.
    simulation_lab_sync_follow(unit);
    const auto original_path=*unit.ai_path_coordinate_q12;
    const auto original_stored=unit.ai_damage_tracked_coordinate_q12;
    check(original_path.x==float_to_q12(seed.actor.move_target->x) &&
          original_path.y==float_to_q12(seed.actor.move_target->y),
          "active route did not publish its fallback coordinate");
    bool arrived=false;
    for(unsigned i=0;i<120;++i) {
        lab.step(field,1);
        if(!unit.actor.move_target && !unit.actor.move_waypoint) {arrived=true;break;}
    }
    check(arrived,"fixture never arrived at final destination");
    check((unit.combat_movement.flags&0xF9U)==0U,
          "arrival left the unit moving during the same-tick AI world job");
    check(unit.actor.position.x>start.x+0.5F,"fixture did not physically move");
    check(unit.ai_path_coordinate_q12 &&
          unit.ai_path_coordinate_q12->x==original_path.x &&
          unit.ai_path_coordinate_q12->y==original_path.y,
          "ordinary arrival overwrote native stored path coordinate");
    check(original_stored && unit.ai_damage_tracked_coordinate_q12 &&
          unit.ai_damage_tracked_coordinate_q12->x==original_stored->x &&
          unit.ai_damage_tracked_coordinate_q12->y==original_stored->y,
          "ordinary arrival replaced the stored route coordinate with a stop anchor");
    std::cout<<"V729_ARRIVAL_COORDINATES PASS moved="
             <<(unit.actor.position.x-start.x)<<"\n";
} catch (const std::exception& error) {
    std::cerr<<error.what()<<"\n";
    return 1;
}
