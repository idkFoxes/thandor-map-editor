#include "thandor/model.hpp"
#include "thandor/binary.hpp"
#include "thandor/simulation_lab_geometry.hpp"
#include "thandor/simulation_lab_presentation.hpp"
#include "thandor/simulation_lab_worker.hpp"
#include <thread>
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
ModelVisualRecord decode(uint32_t cls,int32_t raw_radius=0){
 std::vector<std::byte> b(0x480);write_u32(b,0,fourcc("mdl"));write_u32(b,4,b.size());write_u32(b,0xB0,1);
 write_u32(b,0x200,0x280);write_u32(b,0x208,1);write_u32(b,0x24c,1);
 write_u32(b,0x264,0x300);write_u32(b,0x300,0x80);write_u32(b,0x30c,0x4000);
 const std::string sprite="spr/test.spr";for(size_t i=0;i<sprite.size();++i)write_u16(b,0x338+2*i,sprite[i]);
 write_u32(b,0x200+0xdc,static_cast<uint32_t>(raw_radius));write_u32(b,0x200+0x260,cls);
 const auto before=b;const auto result=parse_model_visual_records(b,"test.mdl");
 check(b==before,"definition initialization modified serialized bytes");return result.at(0);
}
void motion_control(bool building,bool traversal){
 auto field=FieldGrid::create(40,40,0,0);const auto& c=field.at(20,20);
 const Vec3f center{q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),0};
 SimulationLabSeedEntity a;a.army_asset_id=1;a.faction_index=1;a.runtime_model_class_id=1;a.root_model_maximum_integrity=10000;
 a.actor.position={center.x-2,center.y,0};a.motion.collision_radius_q12=decode(0).simulation.movement_collision_radius_q12;
 a.motion.speed_q12_per_tick=256;a.motion.yaw_max_velocity_turn16=0x4000;a.motion.yaw_acceleration_turn16=0x4000;
 a.has_traversal_profile=traversal;a.traversal_low_band_index=0;a.traversal_high_band_index=4;
 auto b=a;b.army_asset_id=2;b.actor.position={center.x+2,center.y,0};b.building=building;
 if(building){b.runtime_model_class_id=11;b.motion.collision_radius_q12=decode(UINT32_MAX,4096).simulation.movement_collision_radius_q12;}
 const std::array seeds{a,b};std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=256;config.sweep_mode=SimulationLabSweepMode::Fixed;
 SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=scenario.assignable_faction_count=1;
 SimulationLabExperiment lab;lab.reset(config,ki,seeds,{},{},{},{},{},scenario);auto& run=lab.runs()[0];
 for(auto& e:run.entities)e.military_ai_source_dispatch=true;
 run.entities[0].actor.move_target=Vec3f{center.x+6,center.y,0};
 if(!building)run.entities[1].actor.move_target=Vec3f{center.x-6,center.y,0};
 float closest=100;for(unsigned t=0;t<256;++t){
  const auto previous0=run.entities[0].actor.position,previous1=run.entities[1].actor.position;
  lab.step(field,1);const auto p=run.entities[0].actor.position,q=run.entities[1].actor.position;
  const auto radius0=run.entities[0].motion.collision_radius_q12,radius1=run.entities[1].motion.collision_radius_q12;
  check(!simulation_lab_step_hits_blocker(previous0,p,radius0,{float_to_q12(previous1.x),float_to_q12(previous1.y),radius1}),"moving unit crossed a building/unit during its admitted step");
  check(!simulation_lab_step_hits_blocker(previous1,q,radius1,{float_to_q12(p.x),float_to_q12(p.y),radius0}),"second unit crossed the first unit's admitted position");
  closest=std::min(closest,std::hypot(p.x-q.x,p.y-q.y));
 }
 check(run.entities[0].actor.position.x>a.actor.position.x+.5F,"collision fix prevented all movement");
 check(closest>=q12_to_float(a.motion.collision_radius_q12+b.motion.collision_radius_q12)-.001F,"unit owners overlapped");
 std::cout<<"collision building="<<building<<" traversal="<<traversal<<" closest="<<closest<<" blocks="<<run.metrics.navigation_actual_step_blocks<<'\n';
}
void multi_run_control(uint32_t runs){
 auto field=FieldGrid::create(40,40,0,0);SimulationLabSeedEntity a;
 a.army_asset_id=1;a.faction_index=1;a.runtime_model_class_id=1;a.root_model_maximum_integrity=10000;
 a.actor.position={12,-8,0};a.motion.speed_q12_per_tick=128;a.motion.collision_radius_q12=1000;
 a.motion.yaw_max_velocity_turn16=a.motion.yaw_acceleration_turn16=0x4000;
 auto b=a;b.faction_index=2;b.army_asset_id=2;b.actor.position.x+=4;
 const std::array seeds{a,b};std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
 SimulationLabConfig config;config.run_count=runs;config.duration_ticks=128;config.vary_seed_per_run=true;
 SimulationLabExperiment serial;serial.reset(config,ki,seeds);serial.step(field,128);
 SimulationLabWorker worker;worker.start(config,ki,seeds,field);worker.request_advance(128);
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
 while(worker.completed_generations()<128&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
 check(!worker.error()&&worker.completed_generations()==128,"worker final generation was dropped or failed");
 const auto snapshot=worker.snapshot();check(snapshot&&snapshot->runs().size()==runs,"worker lost represented runs");
 for(size_t i=0;i<runs;++i){const auto& expected=serial.runs()[i];const auto& actual=snapshot->runs()[i];
  check(actual.tick==128&&actual.rng_state==expected.rng_state&&actual.entities.size()==expected.entities.size(),"presentation pacing changed simulation state");
  check(actual.metrics.navigation_actual_step_blocks==expected.metrics.navigation_actual_step_blocks,"parallel collision admission diverged");
  for(size_t j=0;j<actual.entities.size();++j){const auto p=actual.entities[j].actor.position,q=expected.entities[j].actor.position;
   check(p.x==q.x&&p.y==q.y&&actual.entities[j].actor.body_yaw_turn16==expected.entities[j].actor.body_yaw_turn16,"parallel movement diverged");}
 }
 std::cout<<"parallel runs="<<runs<<" ticks=128 publications="<<worker.snapshot_publications()<<" serial_state=identical\n";
 worker.clear();
}
int main()try{
 const std::array radii{1000,1250,1500,1600,1920,2240,2600,4100};
 for(uint32_t i=0;i<radii.size();++i)check(decode(i).simulation.movement_collision_radius_q12==radii[i],"footprint class failed native definition initialization");
 check(decode(UINT32_MAX,5920).simulation.movement_collision_radius_q12==5920,"negative building class lost serialized radius");
 check(decode(8,321).simulation.movement_collision_radius_q12==321,"custom invalid class read past native table");
 check(simulation_lab_step_hits_blocker({-10,0,0},{10,0,0},1000,{0,0,1000}),"fast step tunneled through an owner");
 check(!simulation_lab_step_hits_blocker({-10,0,0},{10,0,0},0,{0,0,1000}),"explicit native zero-radius exemption lost");
 check(!simulation_lab_step_hits_blocker({.1F,0,0},{1,0,0},1000,{0,0,1000}),"outward overlap recovery was blocked");
 for(bool building:{false,true})for(bool traversal:{false,true})motion_control(building,traversal);
 SimulationLabEntity entity;int fallback_calls=0;
 const auto fallback=[&]{++fallback_calls;return std::vector<SimulationWeaponProfile>(1);};
 check(simulation_lab_render_weapon_profiles(&entity,fallback).size()==1&&fallback_calls==1,"unprepared renderer lost fallback");
 entity.runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>();
 check(simulation_lab_render_weapon_profiles(&entity,fallback).empty()&&fallback_calls==1,"empty prepared snapshot decoded sprites");
 entity.combat_weapons.resize(1);entity.combat_weapons[0].profile.shot_definition_id=44;
 check(simulation_lab_render_weapon_profiles(&entity,fallback)[0].shot_definition_id==44&&fallback_calls==1,"renderer did not use snapshot weapon profile");
 using namespace std::chrono_literals;unsigned rebuilds=0;auto last=0ms;
 for(auto now=1ms;now<=1000ms;now+=1ms)if(simulation_lab_scene_update_due(true,false,now-last)){++rebuilds;last=now;}
 check(rebuilds==20,"backlogged snapshot changes exceeded presentation cadence");
 check(simulation_lab_scene_update_due(false,true,0ms),"camera changes were delayed by simulation pacing");
 check(!simulation_lab_scene_update_due(false,false,1s),"clean snapshot caused idle scene rebuild");
 multi_run_control(10);multi_run_control(32);
 std::cout<<"V622_COLLISION_PRESENTATION PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
