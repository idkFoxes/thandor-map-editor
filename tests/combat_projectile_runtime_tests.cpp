#include "thandor/projectile_runtime.hpp"
#include "thandor/projectile_contacts.hpp"
#include "core_retail_projectile_reference.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool ok,const char* m){if(!ok)throw std::runtime_error(m);}
uint32_t random_next(uint32_t& x){x=x*1664525U+1013904223U;return x;}
void native_comparisons(){
 uint32_t rng=626;
 for(int i=0;i<2000;++i){
  const int32_t pitch=int32_t(random_next(rng)%32769)-16384,yaw=int32_t(random_next(rng)&65535),speed=int32_t(random_next(rng)%20000);
  auto expected=native_v626::FixedMath_DirectionFromAnglesScaled(pitch,yaw,speed);auto actual=simulation_fixed_direction_q12(pitch,yaw,speed);
  check(actual[0]==expected.x&&actual[1]==expected.y&&actual[2]==expected.z,"fixed direction differs from native Q28 body");
  SimulationGuidanceAngles current{int32_t(random_next(rng)&65535),pitch},target{int32_t(random_next(rng)&65535),int32_t(random_next(rng)%32769)-16384};int32_t limit=int32_t(random_next(rng)%4096)+1;
  native_v626::ModelRuntimeNode node{},target_node{};node.modelPayload.worldRotationAngle0=current.yaw;node.modelPayload.worldRotationAngle1=current.pitch;
  native_v626::ModelDefinition definition{};native_v626::ModelRuntimeSlot model{{&target_node},{&definition}};
  native_v626::ShotDefinition shot;shot.guidanceTurnLimitAngle16=limit;native_v626::desired={target.yaw,target.pitch};native_v626::ShotProjectile_SteerTowardsTarget(&shot,&model,&node);
  auto steering=simulation_guidance_angles(current,target,limit);
  check(steering.yaw==node.modelPayload.worldRotationAngle0&&steering.pitch==node.modelPayload.worldRotationAngle1,"guidance axis/wrap/clamp differs from native body");
 }
 for(uint32_t ramp:{1U,3U,8U,31U})for(uint32_t age=1;age<ramp*2+4;++age){
  ShotDefinitionRecord shot;shot.trajectory_mode=3;shot.launch_speed_q12=13001;shot.trajectory_ramp_duration_ticks=ramp;shot.elevation_offset_angle16=1234;
  SimulationProjectileState p;p.runtime_flight_initialized=true;p.runtime_flight_age=age;p.runtime_flight_pitch=2000;p.runtime_elevation_offset=1234;
  native_v626::ShotDefinition d;d.launchSpeedQ12=shot.launch_speed_q12;d.trajectoryRampDurationTicks=ramp;d.elevationOffsetAngle16=1234;
  native_v626::ShotRuntimeSlot rt;rt.projectileAgeTicks=age;rt.elevationOffsetAngle16=1234;native_v626::ModelRuntimeNode n;n.modelPayload.worldRotationAngle1=2000;
  native_v626::ShotProjectile_UpdateLeadAdjustedTrajectory(&d,&rt,&n);simulation_finish_projectile_runtime_step(p,shot,nullptr,nullptr);
  check(p.runtime_flight_speed_q12==rt.launchSpeedQ12&&p.runtime_flight_pitch==n.modelPayload.worldRotationAngle1&&p.runtime_elevation_offset==rt.elevationOffsetAngle16,"native integer quadratic ramp/elevation differs");
 }
 for(uint32_t ramp:{0U,1U,3U,8U,31U})for(uint32_t age=1;age<70;++age)for(int descending=0;descending<2;++descending)for(int has_target=0;has_target<2;++has_target){
  ShotDefinitionRecord shot;shot.trajectory_mode=2;shot.launch_speed_q12=13001;shot.trajectory_ramp_duration_ticks=ramp;shot.fixed_range_transition_age_threshold_ticks=30;
  SimulationProjectileState p;p.runtime_flight_initialized=true;p.runtime_flight_age=age;p.runtime_flight_pitch=descending?-16384:16384;p.runtime_flight_speed_q12=222;p.runtime_flight_ray_end={1,2,3};
  native_v626::ShotDefinition d;d.launchSpeedQ12=13001;d.trajectoryRampDurationTicks=ramp;d.fixedRangeTransitionAgeThresholdTicks=30;
  native_v626::ShotRuntimeSlot rt;rt.definitionOrSavedId.definition=&d;rt.projectileAgeTicks=age;rt.launchSpeedQ12=222;
  native_v626::ModelRuntimeNode n,target_node;n.modelPayload.worldRotationAngle1=p.runtime_flight_pitch;n.worldTransform.translation={4096,8192,12288};target_node.worldTransform.translation={20480,24576,0};
  native_v626::ModelDefinition def;native_v626::ModelRuntimeSlot target{{&target_node},{&def}};rt.runtimeStateOrSavedOffset.runtimeState=has_target;rt.runtimeStateOrSavedOffset.targetModelRuntime=&target;
  native_v626::released=false;const bool expired=native_v626::ShotProjectile_UpdateFixedRangeTrajectory(&d,&rt,&n);Vec3f root{5,6,0};
  simulation_finish_projectile_runtime_step(p,shot,has_target?&root:nullptr,has_target?&root:nullptr);
  check(p.runtime_flight_expired==expired&&p.runtime_flight_speed_q12==rt.launchSpeedQ12&&p.runtime_flight_pitch==n.modelPayload.worldRotationAngle1,"fixed-range native cubic ramp/transition mismatch");
  check(float_to_q12(p.runtime_flight_position.x)==n.worldTransform.translation.x&&float_to_q12(p.runtime_flight_position.y)==n.worldTransform.translation.y,"fixed-range native target/root shift mismatch");
 }
 ShotDefinitionRecord ballistic;
 ballistic.trajectory_mode=1;
 ballistic.ballistic_divisor_q12=10;
 SimulationProjectileState projectile;
 projectile.runtime_ballistic_direction={-58,-336,-1980};
 simulation_finish_projectile_runtime_step(projectile,ballistic,nullptr,nullptr);
 check(projectile.runtime_flight_speed_q12==2019,
       "ballistic velocity length must use the native three-step integer square root");
 std::cout<<"native comparisons:2000 Q28 directions,2000 guidance clamps,lead-ramp ages,1380 fixed-range cases PASS\n";
}
void flight(){
 ShotDefinitionRecord shot;shot.trajectory_mode=2;shot.launch_speed_q12=4096;shot.projectile_lifetime_ticks=20;shot.fixed_range_transition_age_threshold_ticks=3;
 SimulationProjectileState p;p.launch_position={20,-20,2};p.launch_pitch_turn16=0x4000;p.lifetime_ticks=20;p.travel_ticks=20;Vec3f target{30,-10,0};
 for(uint32_t tick=1;tick<=3;++tick){check(simulation_begin_projectile_runtime_step(p,shot,tick),"fixed flight expired early");check(p.runtime_flight_ray_end.x==20&&p.runtime_flight_ray_end.y==-20,"mode2 collision ray followed its teleport");simulation_finish_projectile_runtime_step(p,shot,&target,&target);}
 check(p.runtime_flight_position.x==30&&p.runtime_flight_position.y==-10&&p.runtime_flight_pitch==-0x4000,"mode2 transition did not move AFTER the ray");
 check(simulation_begin_projectile_runtime_step(p,shot,4),"descent failed");check(p.runtime_flight_ray_end.z==4,"mode2 descent speed/position wrong");target.x=31;simulation_finish_projectile_runtime_step(p,shot,&target,&target);check(p.runtime_flight_position.x==31,"mode2 descent did not follow root");
 SimulationProjectileState orphan;orphan.launch_pitch_turn16=0x4000;orphan.launch_position={20,-20,2};for(uint32_t tick=1;tick<=3;++tick){simulation_begin_projectile_runtime_step(orphan,shot,tick);simulation_finish_projectile_runtime_step(orphan,shot,nullptr,nullptr);}check(orphan.runtime_flight_expired,"mode2 with no target survived transition");
 shot.trajectory_mode=3;shot.trajectory_ramp_duration_ticks=3;shot.mode3_lead_disabled=0x800;shot.elevation_offset_angle16=0;
 SimulationProjectileState guided;guided.launch_position={20,-20,4};guided.launch_yaw_turn16=0;guided.launch_pitch_turn16=0;target={20,10,4};
 simulation_begin_projectile_runtime_step(guided,shot,1);check(guided.runtime_flight_ray_end.x==20,"ramped shot ignored zero initial speed");simulation_finish_projectile_runtime_step(guided,shot,&target,&target);check(guided.runtime_flight_yaw==0x800,"guided shot failed to steer after movement");
 std::cout<<"mode2 rise/transition/follow/no-target and guided ramp controls PASS\n";
}
void integrated_damage(){
 auto field=FieldGrid::create(60,60,0,0);const auto& cell=field.at(25,25);const Vec3f origin{q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),3};
 auto catalog=std::make_shared<SimulationLabRuntimeCatalog>();auto& sprite=catalog->sprites["target"];sprite.has_bounds=true;sprite.bounds_min={-.2F,-.2F,-.2F};sprite.bounds_max={.2F,.2F,.2F};catalog->effect_lifetimes[44]=6;
 ModelVisualNode node;node.parent_index=UINT32_MAX;node.model_definition_id=1;node.runtime_class_id=10;node.sprite_path="target";node.shot_impact_class_index=1;node.simulation.placement_contact_kind=3;
 auto hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(std::vector{node});
 for(uint32_t mode:{1U,2U,3U})for(uint32_t contact:{0U,1U,2U}){
  SimulationLabSeedEntity owner;owner.army_asset_id=1;owner.faction_index=1;owner.runtime_model_class_id=10;owner.root_model_maximum_integrity=1000;owner.shot_impact_class_index=1;owner.actor.position=origin;
  ShotDefinitionRecord shot;shot.definition_id=88;shot.trajectory_mode=mode;shot.launch_speed_q12=4096;shot.projectile_lifetime_ticks=20;shot.fixed_range_transition_age_threshold_ticks=2;shot.target_impact_effect_ids[1]=44;shot.target_impact_damage_q12[1]=80;
  SimulationLabWeaponSeed weapon;weapon.shot=shot;owner.combat_weapons={weapon};
  auto target=owner;target.combat_weapons.clear();target.faction_index=2;target.source_placement_index=1;target.actor.position.x+=2;
  auto crossing=target;crossing.source_placement_index=2;crossing.actor.position.x-=1;crossing.actor.position.y+=2;
  if(mode!=2&&contact!=0)target.actor.position.y+=2;
  if(mode!=2&&contact==2)crossing.actor.position.y=origin.y;
  const std::array seeds{owner,target,crossing};SimulationLabScenarioRuntimeSeed scenario;scenario.runtime_catalog=catalog;
  SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=20;std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
  SimulationLabExperiment lab;lab.reset(cfg,ki,seeds,{},{},{},{},{},scenario);auto& r=lab.runs()[0];r.entities[1].runtime_model_hierarchy=hierarchy;r.entities[2].runtime_model_hierarchy=hierarchy;
  SimulationProjectileState p;p.serial=1;p.owner_placement_index=0;p.weapon_profile_index=0;p.shot_definition_id=88;p.launch_position=origin;p.target_position=target.actor.position;p.live_target_placement_index=1;p.lifetime_ticks=20;p.travel_ticks=1;p.impact_kind=SimulationImpactKind::TargetModelImpactClass;p.impact_target_placement_index=1;
  p.launch_pitch_turn16=mode==2?0x4000:0;r.projectiles={p};
  if(mode==2){lab.step(field,3);check(r.entities[1].root_model_integrity==1000,"fixed-range jump itself caused damage");lab.step(field,1);check(r.entities[1].root_model_integrity==920,"fixed-range descent failed live model damage");}
  else {lab.step(field,3);check(r.entities[1].root_model_integrity==(contact==0?920:1000),"moving shot kept stale target damage or missed actual contact");check(r.entities[2].root_model_integrity==(contact==2?920:1000),"intervening live model failed actual damage");}
 }
 std::cout<<"9 integrated ballistic/fixed-range/lead damage cases PASS\n";
}
void collision(){
 auto field=FieldGrid::create(64,64,0,0);const auto& c=field.at(30,30);const Vec3f start{q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),3};
 ShotDefinitionRecord shot;shot.trajectory_mode=3;shot.launch_speed_q12=8192;shot.projectile_lifetime_ticks=20;shot.target_impact_effect_ids[0]=7;
 SimulationLabRuntimeCatalog catalog;SimulationLabStaticShotHull box;for(unsigned i=0;i<8;++i)box.corners[i]={start.x+((i&1)?1.5F:1.F),start.y+((i&2)?1.F:-1.F),(i&4)?4.F:2.F};box.impact_class=0;
 SimulationProjectileState p;p.launch_position=start;p.launch_yaw_turn16=0;simulation_begin_projectile_runtime_step(p,shot,1);
 check(simulation_lab_refresh_projectile_interval_contact(p,shot,1,field,catalog,{},std::span(&box,1)),"moving native interval missed intervening structure");check(p.impact_kind==SimulationImpactKind::TargetModelImpactClass,"interception class lost");
 SimulationProjectileState miss;miss.launch_position=start;miss.launch_yaw_turn16=0x4000;simulation_begin_projectile_runtime_step(miss,shot,1);
 check(!simulation_lab_refresh_projectile_interval_contact(miss,shot,1,field,catalog,{},std::span(&box,1)),"aim target manufactured a collision");
 shot.projectile_lifetime_ticks=1;SimulationProjectileState expiry;expiry.launch_position=start;check(!simulation_begin_projectile_runtime_step(expiry,shot,1)&&expiry.runtime_flight_expired,"native age/expiry order wrong");
 std::cout<<"moving ray interception/miss/lifetime controls PASS\n";
}
int main(){try{native_comparisons();flight();collision();integrated_damage();std::cout<<"V626_PROJECTILE_RUNTIME PASS\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
