#include "thandor/simulation.hpp"
#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_runtime.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace thandor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}

SimulationMotionProfile constant_motion(){
    SimulationMotionProfile motion;
    motion.speed_q12_per_tick=4096; // one world unit per callback
    motion.yaw_max_velocity_turn16=0x4000;
    motion.yaw_acceleration_turn16=0x4000;
    return motion;
}

SimulationActorState command(float target_x){
    SimulationActorState actor;
    actor.position={0.0F,0.0F,0.0F};
    actor.body_yaw_turn16=0U;
    actor.move_target=Vec3f{target_x,0.0F,0.0F};
    return actor;
}

void authored_acceleration_and_sliding(){
    auto motion=constant_motion();motion.ground_speed_control=true;
    motion.speed_q12_per_tick=240;motion.ground_acceleration_q12=10;
    auto actor=command(10);actor.move_target=Vec3f{0,-10,0};
    simulation_tick_class17_actor_motion(actor,motion,1);
    require(float_to_q12(actor.position.x)==0&&float_to_q12(actor.position.y)==-10&&actor.ground_advance_q12==10,
            "banking unit did not accelerate and slide independently of its facing");
    simulation_tick_class17_actor_motion(actor,motion,1);
    require(float_to_q12(actor.position.y)==-30&&actor.ground_advance_q12==20,
            "banking acceleration restarted for a retained route point");
    actor.move_target=Vec3f{1,-10,0};simulation_tick_class17_actor_motion(actor,motion,1);
    require(actor.ground_advance_q12==10,"banking route changing both coordinates retained speed");
    actor.move_target=Vec3f{1,-11,0};simulation_tick_class17_actor_motion(actor,motion,1);
    require(actor.ground_advance_q12==20,"banking route changing one coordinate reset speed");
}

void direct_callback_boundary(){
    const auto motion=constant_motion();

    auto generic=command(1.5F);
    require(simulation_tick_actor_motion(generic,motion,1U),
        "generic constant-speed movement did not advance");
    require(generic.move_target.has_value()&&std::abs(generic.position.x-1.0F)<1.0e-6F,
        "generic movement incorrectly inherited the class-17 half-distance rule");

    auto banking=command(1.5F);
    require(simulation_tick_class17_actor_motion(banking,motion,1U),
        "class-17 banking movement did not advance");
    require(!banking.move_target&&std::abs(banking.position.x-1.5F)<1.0e-6F,
        "class-17 callback did not consume a target at the native half-distance threshold");

    auto farther=command(2.5F);
    require(simulation_tick_class17_actor_motion(farther,motion,1U),
        "class-17 far movement did not advance");
    require(farther.move_target.has_value()&&std::abs(farther.position.x-1.0F)<1.0e-6F,
        "class-17 callback consumed a target before travel reached half the distance");

    auto waypoint=command(8.0F);
    waypoint.move_waypoint=Vec3f{1.5F,0.0F,0.0F};
    require(simulation_tick_class17_actor_motion(waypoint,motion,1U),
        "class-17 waypoint movement did not advance");
    require(!waypoint.move_waypoint&&waypoint.move_target.has_value()&&
            std::abs(waypoint.position.x-1.5F)<1.0e-6F,
        "class-17 route-point consumption lost the final target or failed to advance the route");
}

SimulationLabExperiment experiment_for(std::uint32_t runtime_class,float base_x,float base_y){
    SimulationLabSeedEntity seed;
    seed.army_asset_id=200U+runtime_class;
    seed.faction_index=1U;
    seed.runtime_model_class_id=runtime_class;
    seed.root_model_maximum_integrity=1000;
    seed.motion=constant_motion();
    seed.actor=command(base_x+1.5F);
    seed.actor.position={base_x,base_y,0.0F};
    seed.actor.move_target=Vec3f{base_x+1.5F,base_y,0.0F};

    SimulationLabConfig config;
    config.run_count=1U;
    config.duration_ticks=4U;
    config.sweep_mode=SimulationLabSweepMode::Fixed;
    std::array<std::uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    SimulationLabScenarioRuntimeSeed scenario;
    scenario.enabled=true;
    scenario.active_faction_count=1U;
    scenario.assignable_faction_count=1U;
    scenario.human_faction_mask=1U<<1U;
    scenario.ai_planning_enabled=false;
    scenario.runtime_catalog=std::make_shared<SimulationLabRuntimeCatalog>();
    SimulationLabExperiment lab;
    const std::array seeds{seed};
    lab.reset(config,parameters,seeds,{},{},{},{},{},scenario);
    return lab;
}

void simulation_lab_dispatches_banking_callback(){
    const auto field=FieldGrid::create(32U,32U,0,0U);
    const auto& start=field.at(16U,16U);
    const float base_x=q12_to_float(start.world_x_q12);
    const float base_y=q12_to_float(start.world_y_q12);
    auto banking=experiment_for(17U,base_x,base_y);
    banking.step(field,1U);
    const auto& banked=banking.runs().front().entities.front();
    require(!banked.actor.move_target&&std::abs(banked.actor.position.x-(base_x+1.5F))<1.0e-6F,
        "Simulation Lab still routed runtime class 17 through generic full-distance arrival");

    // Class 19 now has its own Pioneer route adapter; use ordinary class 20 here.
    auto water=experiment_for(20U,base_x,base_y);
    water.step(field,1U);
    const auto& generic=water.runs().front().entities.front();
    require(generic.actor.move_target.has_value()&&std::abs(generic.actor.position.x-(base_x+1.0F))<1.0e-6F,
        "ordinary movement was incorrectly changed by the class-17 arrival correction");
}
}

int main()try{
    direct_callback_boundary();
    authored_acceleration_and_sliding();
    simulation_lab_dispatches_banking_callback();
    std::cout<<"V758_CLASS17_BANKING_ARRIVAL PASS half_distance=1 generic_preserved=1 lab_dispatch=1\n";
}catch(const std::exception& error){
    std::cerr<<"V758_CLASS17_BANKING_ARRIVAL FAIL "<<error.what()<<'\n';
    return 1;
}
