#include "thandor/simulation_lab.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool yes,const char* text){if(!yes)throw std::runtime_error(text);}
void control(bool released,bool traversal){
 auto field=FieldGrid::create(40,40,0,0);const auto& cell=field.at(20,20);
 const Vec3f center{q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
 SimulationLabSeedEntity mover;mover.army_asset_id=1;mover.faction_index=1;mover.runtime_model_class_id=1;mover.root_model_maximum_integrity=100;
 mover.actor.position={center.x-3,center.y,0};mover.motion.speed_q12_per_tick=1024;mover.motion.collision_radius_q12=1000;
 mover.motion.yaw_max_velocity_turn16=mover.motion.yaw_acceleration_turn16=0x4000;
 mover.has_traversal_profile=traversal;mover.traversal_low_band_index=0;mover.traversal_high_band_index=4;
 auto wreck=mover;wreck.army_asset_id=300;wreck.building=true;wreck.runtime_model_class_id=11;wreck.motion.speed_q12_per_tick=0;wreck.motion.collision_radius_q12=4096;wreck.actor.position=center;
 const std::array seeds{mover,wreck};std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=32;config.sweep_mode=SimulationLabSweepMode::Fixed;
 SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=scenario.assignable_faction_count=1;
 SimulationLabExperiment lab;lab.reset(config,ki,seeds,{},{},{},{},{},scenario);auto& run=lab.runs()[0];
 for(auto& entity:run.entities)entity.military_ai_source_dispatch=true;
 run.entities[0].actor.move_target=Vec3f{center.x+4,center.y,0};
 auto& dead=run.entities[1];dead.root_model_terminal=true;dead.root_model_integrity=0;dead.runtime_removed=released;
 // Keep the retained slot's radius deliberately nonzero: owner membership, not stale
 // definition data, must exclude an actually released model from every route path.
 lab.step(field,32);
 const auto p=run.entities[0].actor.position;
 if(released)check(p.x>center.x+2,"released retained slot still blocked the route");
 else {check(std::hypot(p.x-center.x,p.y-center.y)>=q12_to_float(5096)-.001F,"unit crossed a retained wreck");if(!traversal)check(run.metrics.navigation_actual_step_blocks>0,"wreck was not checked by final movement admission");}
 std::cout<<"lifetime released="<<released<<" traversal="<<traversal<<" position="<<p.x-center.x<<","<<p.y-center.y<<" actual_blocks="<<run.metrics.navigation_actual_step_blocks<<'\n';
}
void stationary_control(bool released,uint32_t cls){
 auto field=FieldGrid::create(40,40,0,0);const auto& c=field.at(20,20);
 SimulationLabSeedEntity seed;seed.army_asset_id=1;seed.faction_index=1;seed.runtime_model_class_id=cls;seed.root_model_maximum_integrity=100;
 seed.actor.position={q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),0};seed.motion.speed_q12_per_tick=1024;seed.motion.yaw_max_velocity_turn16=seed.motion.yaw_acceleration_turn16=0x4000;
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=4;config.sweep_mode=SimulationLabSweepMode::Fixed;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=scenario.assignable_faction_count=1;
 SimulationLabExperiment lab;lab.reset(config,ki,std::span(&seed,1),{},{},{},{},{},scenario);auto& run=lab.runs()[0];auto& entity=run.entities[0];
 entity.military_ai_source_dispatch=true;entity.root_model_terminal=true;entity.root_model_integrity=0;entity.runtime_removed=released;
 entity.actor.move_target=Vec3f{seed.actor.position.x+8,seed.actor.position.y,0};entity.actor.move_waypoint=Vec3f{seed.actor.position.x+4,seed.actor.position.y,0};
 lab.step(field,4);
 check(entity.actor.position.x==seed.actor.position.x&&entity.actor.position.y==seed.actor.position.y,"terminal/released unit advanced an old command");
 check(!entity.last_translated&&!entity.last_turned,"terminal/released unit emitted moving presentation state");
 if(released)check(run.metrics.class17_pose_updates==0,"released owner still serviced a pose callback");
 else if(cls==17)check(run.metrics.class17_pose_updates==4,"retained destroyed class17 lost stationary pose servicing");
}
int main()try{
 for(bool traversal:{false,true})for(bool released:{false,true})control(released,traversal);
 for(uint32_t cls:{1U,17U,18U})for(bool released:{false,true})stationary_control(released,cls);
 std::cout<<"V629_COLLISION_LIFETIME PASS\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
