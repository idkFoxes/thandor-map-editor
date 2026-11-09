#include "thandor/catalog.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
SimulationMotionProfile profile(){
 SimulationMotionProfile p;p.speed_q12_per_tick=84;p.ground_speed_control=true;
 p.ground_acceleration_q12=41;p.ground_slope_threshold_turn16=10500;
 p.ground_secondary_slope_threshold_turn16=14500;
 p.yaw_max_velocity_turn16=768;p.yaw_acceleration_turn16=329;
 p.heading_interpolation_distance_q12=390;p.far_heading_error_limit_turn16=121;
 p.near_heading_error_limit_turn16=256;return p;
}
SimulationActorState actor(){SimulationActorState a;a.move_target=Vec3f{100,0,0};a.move_waypoint=a.move_target;return a;}
}
int main(int argc,char**argv)try{
 const auto p=profile();
 for(uint32_t batch:{1U,5U}){
  auto a=actor();
  for(uint32_t expected:{41U,82U,84U}){
   const auto before=float_to_q12(a.position.x);simulation_tick_actor_motion(a,p,batch);
   check(a.ground_advance_q12==expected,"acceleration scaled by callback batch");
   check(float_to_q12(a.position.x)-before==int32_t(expected*batch),"advance not scaled by callback batch");
  }
  a.move_target=Vec3f{a.position.x,100,0};a.move_waypoint=a.move_target;
  check(!simulation_tick_actor_motion(a,p,batch)&&a.ground_advance_q12==0,"heading block retained forward speed");
  a.body_yaw_turn16=0;a.body_turn_velocity_turn16=0;a.move_target=Vec3f{100,0,0};a.move_waypoint=a.move_target;
  simulation_tick_actor_motion(a,p,batch);check(a.ground_advance_q12==41,"restart skipped acceleration");
  a.move_target.reset();simulation_tick_actor_motion(a,p,batch);
  check(a.ground_advance_q12==0&&a.body_turn_velocity_turn16==0,"idle retained ground control state");
 }
 auto a=actor();a.ground_advance_q12=84;a.ground_normal_elevation_turn16=10500;
 for(uint32_t heading:{0x4000U,0x4001U,0xBFFFU,0xC000U}){
  a.body_yaw_turn16=heading;
  check(simulation_ground_advance_q12(a,p)==((heading==0x4000||heading==0xC000)?84U:52U),"slope direction boundary differs");
 }
 a.body_yaw_turn16=0x8000;a.ground_normal_elevation_turn16=10499;
 check(simulation_ground_advance_q12(a,p)==26,"steep terrain cap differs");
 a.ground_normal_elevation_turn16=14500;
 check(simulation_ground_advance_q12(a,p)==84,"secondary slope equality slowed unit");
 a=actor();a.position={99.5F,0,0};a.move_waypoint.reset();a.ground_advance_q12=84;
 simulation_tick_actor_motion(a,p);check(a.move_target&&a.ground_advance_q12!=0,"active ground route ended before reaching its primary point");
 for(int32_t remaining:{0,81,82,83,84}){
  a=actor();a.move_waypoint=Vec3f{q12_to_float(remaining),0,0};
  simulation_tick_actor_motion(a,p);
  check(a.move_waypoint.has_value()==(remaining==84),"native half-distance route point consumption differs");
 }
 a=actor();a.body_yaw_turn16=23456;a.move_waypoint=a.position;
 simulation_tick_actor_motion(a,p);
 check(!a.move_waypoint&&a.body_yaw_turn16==23456,"zero-distance route point changed heading");
 // Captured ARM184 factory exit frames from Asgard. The banked model
 // reaches the 5/8 cap even with level terrain contact; changing the
 // bank heading crosses the uphill boundary while the pitch is unchanged.
 auto banking=p;banking.speed_q12_per_tick=240;banking.ground_acceleration_q12=10;
 banking.ground_secondary_slope_threshold_turn16=15500;
 banking.yaw_acceleration_turn16=658;
 auto b=actor();
 b.position={q12_to_float(47254),q12_to_float(-51873),q12_to_float(2560)};
 b.move_target=b.move_waypoint=Vec3f{q12_to_float(47230),q12_to_float(-60439),b.position.z};
 b.class17_previous_route_point_q12={47230,-60439};
 b.body_yaw_turn16=49122U;
 b.ground_advance_q12=150;
 b.ground_recoil_angles=std::array{49122U,14335U,30719U};
 simulation_tick_class17_actor_motion(b,banking);
 check(b.ground_advance_q12==150&&float_to_q12(b.position.x)==47253&&float_to_q12(b.position.y)==-52023,
       "banking vehicle ignored the speed cap of its published root pose");
 b.position={q12_to_float(47252),q12_to_float(-52173),q12_to_float(2560)};
 b.body_yaw_turn16=49124U;b.body_turn_velocity_turn16=0;
 b.ground_recoil_angles=std::array{49124U,14335U,32768U};
 simulation_tick_class17_actor_motion(b,banking);
 check(b.ground_advance_q12==160&&float_to_q12(b.position.x)==47251&&float_to_q12(b.position.y)==-52333,
       "banking vehicle stayed capped after crossing the uphill heading boundary");
 // The active banking callback still accelerates at its current route point.
 // The following segment changes X only, so it keeps the existing speed.
 {
  const auto field=FieldGrid::create(80,80,0,0);
  const auto& cell=field.at(30,30);
  const MovementPoint point{cell.world_x_q12,cell.world_y_q12};
  SimulationLabSeedEntity seed;seed.faction_index=1;seed.army_asset_id=186;
  seed.runtime_model_class_id=17;seed.root_model_maximum_integrity=3600;
  seed.motion=banking;
  seed.actor.position={q12_to_float(point.x),q12_to_float(point.y),0};
  SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;
  scenario.active_faction_count=scenario.assignable_faction_count=1;
  scenario.runtime_catalog=std::make_shared<SimulationLabRuntimeCatalog>();
  SimulationLabConfig config;config.run_count=1;config.duration_ticks=4;
  std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
  SimulationLabExperiment lab;lab.reset(config,ki,std::span(&seed,1),{},{},{},{},{},scenario);
  auto& glider=lab.runs()[0].entities[0];
  glider.combat_movement_initialized=glider.combat_follow_owned=true;
  glider.combat_movement.flags=1U;
  glider.combat_movement.stored=glider.combat_movement.primary=glider.combat_movement.fallback=point;
  glider.actor.class17_previous_route_point_q12={point.x,point.y};
  glider.actor.ground_advance_q12=160;
  lab.step(field,1);
  check(glider.actor.ground_advance_q12==170,"zero-distance active route reset banking speed");
  glider.combat_movement.flags=1U;glider.combat_follow_owned=true;
  glider.combat_movement.stored={point.x+576,point.y};
  lab.step(field,1);
  check(glider.actor.ground_advance_q12==180,
        "banking route lost retained speed on a single-coordinate waypoint update");
 }
 // Native's newer owner can allocate an effect before the older owner
 // releases its record. The next claim can then recover the released pose.
 {
  using Pool=SimulationLabWorldObjectPool;
  Pool pool;
  const auto old_effect=pool.claim(Pool::Kind::Effect,130);
  const auto newer_owner=pool.claim(Pool::Kind::Model,132);
  pool.begin_tick(355);
  {
   const auto owner=pool.enter_owner(old_effect);
   pool.release(old_effect,{{34659,-23691,4561},16384});
  }
  size_t new_effect;
  {
   const auto owner=pool.enter_owner(newer_owner);
   new_effect=pool.claim(Pool::Kind::Effect,130);
  }
  pool.commit_tick();
  check(pool.retained_pose(new_effect).yaw==0,"newer owner reused an older effect slot before release");
  const auto next_model=pool.claim(Pool::Kind::Model,132);
  const auto& retained=pool.retained_pose(next_model);
  check(retained.yaw==16384&&retained.position==std::array<int32_t,3>{34659,-23691,4561},
        "first free model allocation discarded the reused effect pose");
 }
 // A factory's timed emitter precedes its new unit, while damage smoke
 // follows it, even when the editor stages those callbacks separately.
 {
  using Pool=SimulationLabWorldObjectPool;
  Pool pool;const auto factory=pool.claim(Pool::Kind::Model,13);
  pool.begin_tick(1);
  size_t smoke,unit,timed;
  {const auto owner=pool.enter_owner(factory);
   smoke=pool.claim(Pool::Kind::Effect,118,false,true);
   unit=pool.claim(Pool::Kind::Model,16);
   timed=pool.claim(Pool::Kind::Effect,134,true);
  }
  pool.commit_tick();
  check(pool.slot(timed)<pool.slot(unit)&&pool.slot(unit)<pool.slot(smoke),
        "factory callback phases allocated smoke before its completed unit");
 }
 // Native completes a parent's bookkeeping before dispatching its attached
 // model, independent of how the Lab stages the callbacks.
 {
  using Pool=SimulationLabWorldObjectPool;
  Pool pool;const auto owner_model=pool.claim(Pool::Kind::Model,1);
  pool.begin_tick(2);size_t child_effect,parent_terminal,parent_effect;
  {const auto owner=pool.enter_owner(owner_model);
   {const auto child=pool.enter_component(1);child_effect=pool.claim(Pool::Kind::Effect,130);}
   {const auto terminal=pool.enter_component(0,-2);parent_terminal=pool.claim(Pool::Kind::Effect,54);}
   {const auto callback=pool.enter_component(0);parent_effect=pool.claim(Pool::Kind::Effect,118);}
  }
  pool.commit_tick();
  check(pool.slot(parent_effect)<pool.slot(parent_terminal)&&pool.slot(parent_terminal)<pool.slot(child_effect),
        "attached callback allocation preceded its parent's bookkeeping");
 }
 if(argc>1){
  const auto catalog=AssetCatalog::scan_archive_root(argv[1]);
  const auto runtime=simulation_lab_prepare_runtime_catalog(catalog);
  for(uint32_t arm:{1U,50U}){
   const auto* preset=catalog.army_preset(arm);check(preset,"stock mover absent");
   const auto& m=preset->simulation_motion_profile;
   check(m.ground_speed_control&&m.ground_acceleration_q12==(arm==50?41U:123U),"stock acceleration missing");
   check(m.ground_slope_threshold_turn16==10500&&m.ground_secondary_slope_threshold_turn16==(arm==50?14500:12500),"stock slope table resolution differs");
   SimulationLabSeedEntity seed;seed.army_asset_id=arm;seed.faction_index=1;
   seed.runtime_model_class_id=preset->runtime_model_class_id;
   SimulationLabEntity entity;
   check(simulation_lab_initialize_runtime_entity(entity,seed,{},*runtime,true),"stock runtime creation failed");
   entity.actor.ground_advance_q12=17;
   FactionTechnologyMask256 full;full.fill(UINT32_MAX);SimulationLabRunMetrics metrics;
   simulation_lab_apply_runtime_technology(entity,full,*runtime,metrics);
   const auto& selected=runtime->definitions.at(entity.model_components[entity.root_model_component_index].model_definition_id);
   check(entity.motion.ground_speed_control&&entity.motion.ground_acceleration_q12==uint32_t(selected.simulation.runtime_field_18_raw)&&
         entity.motion.ground_slope_threshold_turn16==selected.simulation.ground_slope_threshold_turn16&&
         entity.motion.ground_secondary_slope_threshold_turn16==selected.simulation.ground_secondary_slope_threshold_turn16,
         "runtime definition selection lost ground speed controls");
   check(entity.actor.ground_advance_q12==17,"technology definition refresh reset live advance");
  }
  check(!catalog.army_preset(111)->simulation_motion_profile.ground_speed_control,"walker routed through ground acceleration");
 }
 std::cout<<"GROUND_SPEED PASS acceleration, callback batches, slope boundaries, stops, stock metadata\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
