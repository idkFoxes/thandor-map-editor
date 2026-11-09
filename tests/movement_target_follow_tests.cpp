#include "thandor/simulation_lab_follow.hpp"
#include "thandor/simulation_lab_targeting.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/army_target_follow.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace thandor;
void check(bool x,const char* why){if(!x)throw std::runtime_error(why);}
struct Fixture {
 FieldGrid field=FieldGrid::create(100,60,0,0);SimulationLabExperiment lab;Vec3f origin{};
 Fixture(bool planner=false){
  const auto& c=field.at(25,25);origin={q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),0};
  SimulationLabSeedEntity a;a.army_asset_id=60;a.faction_index=1;a.runtime_model_class_id=2;a.actor.position=origin;a.root_model_maximum_integrity=10000;a.shot_impact_class_index=1;a.motion.speed_q12_per_tick=512;a.motion.yaw_max_velocity_turn16=a.motion.yaw_acceleration_turn16=0x7fff;a.fog_uncover_radius_q12=30*4096;a.fog_reference_height_offset_q12=4*4096;
  SimulationWeaponProfile w;w.shot_definition_id=88;w.reload_ticks=2;w.maximum_range_q12=10*4096;w.launch_attachment_count=1;w.yaw_max_velocity_turn16=w.yaw_acceleration_turn16=w.pitch_max_velocity_turn16=w.pitch_acceleration_turn16=0x7fff;w.pitch_min_turn16=-0x4000;w.pitch_max_turn16=0x4000;ShotDefinitionRecord shot;shot.definition_id=88;shot.trajectory_mode=3;shot.launch_speed_q12=4096;shot.projectile_lifetime_ticks=11;shot.target_impact_damage_q12[2]=4096;a.combat_weapons={{w,shot}};
  auto b=a;b.army_asset_id=331;b.runtime_model_class_id=15;b.faction_index=2;b.building=true;b.motion={};b.shot_impact_class_index=2;b.actor.position.x+=11;b.combat_weapons.clear();const std::array seeds{a,b};
  SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=512;cfg.sweep_mode=SimulationLabSweepMode::Fixed;std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};std::vector<SimulationLabProductionAsset> products;
  if(planner){SimulationLabProductionAsset product;product.army_asset_id=a.army_asset_id;product.entity_seed=a;product.ai_definition_class_value70=1;products.push_back(product);ki[0x23]=1000000;// V729's 4096 scale overflowed the native signed 32-bit product
  // for the large synthetic site bias. With correct signed SAR12 that
  // produces no positive site order; use a representable test score.
  ki[0x27]=1024;}
  SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=scenario.assignable_faction_count=2;scenario.simulate_all_active_factions_as_ai=true;scenario.runtime_catalog=std::make_shared<SimulationLabRuntimeCatalog>();lab.reset(cfg,ki,seeds,{},products,{},{},{},scenario);
  origin=e().actor.position;e().military_ai_source_dispatch=true;
 }
 SimulationLabRun& run(){return lab.runs()[0];}SimulationLabEntity& e(){return run().entities[0];}
};
int main(int argc,char** argv)try{
 const bool site_only=argc==2&&std::string(argv[1])=="--site-only";
 if(!site_only){
 for(bool movement_callback:{false,true}){
  Fixture f;auto& e=f.e();
  e.runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(1);auto& state=e.combat_movement;V565PathTerrainCache cache;
  const MovementPoint here{float_to_q12(e.actor.position.x),float_to_q12(e.actor.position.y)};
  state.model=state.stored=state.last_checked=here;
  state.primary=state.fallback={here.x+8527,here.y+4159};
  state.flags=0;state.retry=32;e.combat_movement_initialized=true;e.combat_follow_owned=false;
  const bool resumed=simulation_lab_prepare_follow_motion(f.run(),f.field,0,{},cache,movement_callback);
  check(resumed&&e.combat_follow_owned&&(state.flags&1U),
        "inactive native movement callback failed to retarget a retained distant primary");
  check(state.retry==64,"retargeted inactive root did not initialize native retry");
 }

 {
  Fixture f;auto& e=f.e();auto& state=e.combat_movement;V565PathTerrainCache cache;
  const MovementPoint here{float_to_q12(e.actor.position.x),float_to_q12(e.actor.position.y)};
  state.model=here;state.primary=state.fallback=state.stored={here.x+4096,here.y};
  state.last_checked=here;state.retry=59;state.flags=1;e.combat_movement_initialized=true;
  e.group_route_pending_point=state.stored;e.group_route_pending_tick=f.run().tick;
  simulation_lab_prepare_follow_motion(f.run(),f.field,0,{},cache);
  check(state.retry==59&&e.combat_follow_owned&&!e.group_route_pending_point,
        "activating a published peer route restarted its retry countdown");
  e.group_route_pending_point=MovementPoint{here.x+8192,here.y};
  simulation_lab_prepare_follow_motion(f.run(),f.field,0,{},cache);
  check(state.retry==64,"a new peer route did not initialize its retry countdown");
 }

 {
  Fixture f;f.lab.step(f.field,80);const auto& e=f.e();
  check(e.combat_follow_starts>0,"live weapon never starts blocked command pursuit");
  check(e.combat_follow_stops>0&&f.run().metrics.weapon_shots_fired>0,"pursuit never stops to fire");
  check(e.actor.position.x>f.origin.x&&e.actor.position.x<f.origin.x+2.01F,"AI follow did not use a bounded hop and stop");
  check(!(e.combat_movement.flags&0x20U)&&!e.actor.move_target,"clear fire line retained movement");
  std::cout<<"PURSUIT starts="<<e.combat_follow_starts<<" stops="<<e.combat_follow_stops<<" shots="<<f.run().metrics.weapon_shots_fired<<" delta="<<e.actor.position.x-f.origin.x<<'\n';
 }
 {
  Fixture f;f.e().actor.weapons.resize(1);f.e().actor.weapons[0].cooldown_ticks=30;f.lab.step(f.field,12);
  check(f.e().target_entity_index==1&&!f.e().combat_follow_starts,"reload gate issued follow early");
  f.lab.step(f.field,60);check(f.e().combat_follow_starts>0,"reload completion never services follow");
 }
 {
  Fixture f;f.lab.step(f.field,2);V565PathTerrainCache route_cache;
  f.e().military_ai_command_kind=1;
  simulation_lab_set_site_route(f.run(),f.field,0,
      {float_to_q12(f.origin.x+4),float_to_q12(f.origin.y)}, {}, route_cache);
  check(f.e().target_entity_index==1&&f.e().combat_command_mode_flags==9,"AI site route fabricated a protected position command");
  const auto starts=f.e().combat_follow_starts;V565PathTerrainCache cache;
  simulation_lab_weapon_follow(f.run(),f.field,0,0,f.run().entities[1].actor.position,false,{},cache);
  check(f.e().combat_follow_starts==starts,"active routed site order was stolen by clamped follow");
 }
 {
  Fixture f;f.lab.step(f.field,2);V565PathTerrainCache cache;auto& e=f.e();
  e.combat_weapons.push_back(e.combat_weapons[0]);
  simulation_lab_weapon_follow(f.run(),f.field,0,1,f.run().entities[1].actor.position,false,{},cache);
  check(!e.combat_follow_starts,"secondary weapon started pursuit with a primary present");
  simulation_lab_weapon_follow(f.run(),f.field,0,0,f.run().entities[1].actor.position,false,{},cache);
  check(e.combat_follow_starts==1,"primary blocked callback failed");
  simulation_lab_weapon_follow(f.run(),f.field,0,1,f.run().entities[1].actor.position,true,{},cache);
  check(e.combat_follow_stops==1,"clear secondary line did not stop following");
 }
 {
  Fixture f;f.lab.step(f.field,2);auto& e=f.e();V565PathTerrainCache cache;
  e.actor.move_target=Vec3f{f.origin.x+4,f.origin.y,0};e.actor.move_waypoint.reset();
  e.combat_command_mode_flags=1;e.combat_command_generation=1024;e.combat_movement.flags=1;e.combat_movement.retry=0;e.combat_follow_owned=false;
  simulation_lab_weapon_follow(f.run(),f.field,0,0,f.run().entities[1].actor.position,false,{},cache);
  check(e.combat_movement.queue_count==1,"ordinary follow did not save active fallback");
  for(auto p:e.combat_movement.queue)check(p.x==float_to_q12(f.origin.x+4),"native forward-copy queue quirk lost");
  simulation_lab_weapon_follow(f.run(),f.field,0,0,f.run().entities[1].actor.position,true,{},cache);
  const auto held=f.lab.snapshot_copy();f.lab.step(f.field,80);
  check(e.combat_movement.queue_count==0&&std::abs(e.actor.position.x-f.origin.x-4)<0.01F,"queued fallback never resumed through live movement");
  check(held.runs()[0].entities[0].combat_movement.queue_count==1,"snapshot queue mutated during resume");
  std::cout<<"FALLBACK queue_saved=1 queue_consumed=1 resume_delta="<<e.actor.position.x-f.origin.x<<'\n';
 }
 {
  Fixture f;f.e().has_traversal_profile=true;f.e().traversal_low_band_index=4;f.e().traversal_high_band_index=0;
  f.lab.step(f.field,32);check(f.e().combat_follow_route_resolves==1&&f.e().combat_follow_route_failures==1,"blocked route ignored64-tick retry guard");
  f.lab.step(f.field,48);check(f.e().combat_follow_route_resolves>=2&&f.e().combat_follow_route_resolves<=3,"blocked route never retried or retried every tick");
  check(f.e().actor.position.x==f.origin.x&&f.e().combat_follow_owned,"failed solve fabricated arrival");
  std::cout<<"RETRY callbacks=80 resolves="<<f.e().combat_follow_route_resolves<<" failures="<<f.e().combat_follow_route_failures<<'\n';
 }
 {
  Fixture f;auto nodes=std::make_shared<std::vector<ModelVisualNode>>(3);(*nodes)[0].parent_index=UINT32_MAX;(*nodes)[0].model_definition_id=100;(*nodes)[1].model_definition_id=200;(*nodes)[2].model_definition_id=300;(*nodes)[1].parent_index=0;(*nodes)[1].child_slot=0;(*nodes)[2].parent_index=0;(*nodes)[2].child_slot=1;
  f.e().runtime_model_hierarchy=nodes;f.e().combat_weapons[0].profile.yaw_model_node_index=2;
  check(!simulation_lab_primary_follow_weapon(f.e(),0),"non-primary hierarchy attachment admitted");
  (*nodes)[1].flags=1;check(simulation_lab_primary_follow_weapon(f.e(),0),"absent primary failed secondary fallback");
 }
 {
  Fixture f;f.lab.step(f.field,2);auto& e=f.e();
  e.combat_weapons[0].profile.runtime_class_id=7U;
  e.target_entity_index=1;e.combat_command_mode_flags=1;e.combat_command_generation=100;
  f.run().entities[1].model_primary_occupancy_mask=0;
  const MovementPoint here{float_to_q12(e.actor.position.x),float_to_q12(e.actor.position.y)};
  e.combat_movement={};e.combat_movement.model=here;
  e.combat_movement.primary=e.combat_movement.fallback=e.combat_movement.stored=here;
  e.combat_movement.flags=0x21U;e.combat_movement_initialized=true;e.combat_follow_owned=true;
  e.actor.move_target=e.actor.position;e.actor.move_waypoint=e.actor.position;
  f.lab.step(f.field,1);
  check(!e.target_entity_index&&(e.combat_movement.flags&0x11U)==0U,
        "losing command visibility skipped the turret's second waypoint callback");
 }
 {
  Fixture f;f.lab.step(f.field,2);check(f.e().target_entity_index==1,"visibility setup failed");
  f.run().current_visibility_occupancy.assign(f.field.cells().size(),0);f.lab.step(f.field,1);
  check(f.e().target_entity_index==1,"display fog invalidated a still-visible native MODEL target");
  // Simulate the model terrain callback publishing loss/reacquisition. This
  // retained generation, rather than the GUI cell projection, owns aim gates.
  f.run().entities[1].model_primary_occupancy_mask=0;
  f.lab.step(f.field,1);
  check(!f.e().target_entity_index&&f.e().combat_command_mode_flags==0,"native invisible model retained weapon aim");
  f.run().entities[1].model_primary_occupancy_mask=2U<<(2U*f.e().faction_index);
  f.lab.step(f.field,1);check(f.e().target_entity_index==1,"native visible target did not reacquire after drop");
 }
 }
 {
  Fixture f(true);f.lab.step(f.field,14);
  check(f.run().metrics.military_ai_site_orders>0,"live site planner produced no order");
  check(f.e().target_entity_index==1&&f.e().combat_command_mode_flags!=2,"live AI site order stamped a protected position attack command");
  std::cout<<"SITE_ROUTE actual_orders="<<f.run().metrics.military_ai_site_orders<<" target_retained=1 mode="<<f.e().combat_command_mode_flags<<'\n';
 }
 std::cout<<"V641_TARGET_FOLLOW PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
