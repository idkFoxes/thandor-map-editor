#define main v641_replay_unused_main
#include "simlab_stock_replay.cpp"
#undef main
#include "thandor/simulation_lab_targeting.hpp"
#include <cmath>
#include "thandor/simulation_lab_shot_rays.hpp"
int main(int argc,char** argv)try{
 if(argc<2)throw std::runtime_error("usage: stock_pursuit GAME_ROOT [--cannon-near] [--trace]");
 bool cannon_near=false,trace=false;
 for(int i=2;i<argc;++i){const std::string option=argv[i];if(option=="--cannon-near")cannon_near=true;else if(option=="--trace")trace=true;else throw std::runtime_error("unknown pursuit option "+option);}
 const auto catalog=AssetCatalog::scan_archive_root(argv[1]);const auto runtime=simulation_lab_prepare_runtime_catalog(catalog);
 const auto field=FieldGrid::create(240,160,0,0);const auto& c=field.at(100,80);
 FactionTechnologyMask256 mask{};mask.fill(UINT32_MAX);std::array<FactionTechnologyMask256,8> masks{};masks[1]=masks[2]=mask;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};SimulationLabConfig config;config.run_count=1;config.duration_ticks=384;uint32_t starts=0;
 for(uint32_t id:{2U,60U,110U,115U,170U}){
  if(cannon_near&&id!=60)continue;const float extra=cannon_near?.5F:1.75F;
  auto source=seed_for(*catalog.army_preset(id),catalog,mask);source.faction_index=1;source.actor.position={q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),0};
  int32_t range=0;for(const auto& w:source.combat_weapons)range=std::max(range,w.profile.maximum_range_q12);
  auto target=seed_for(*catalog.army_preset(331),catalog,mask);target.faction_index=2;target.source_placement_index=1;target.actor.position=source.actor.position;target.actor.position.x+=q12_to_float(range)+extra;
  auto observer=seed_for(*catalog.army_preset(300),catalog,mask);observer.faction_index=1;observer.source_placement_index=2;observer.actor.position=target.actor.position;observer.actor.position.y+=4;
  const std::array seeds{source,target,observer};SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=scenario.assignable_faction_count=2;scenario.simulate_all_active_factions_as_ai=true;scenario.runtime_catalog=runtime;
  SimulationLabExperiment lab;lab.reset(config,ki,seeds,{},{},{},masks,{},scenario);auto& run=lab.runs()[0];run.entities[0].military_ai_source_dispatch=true;const auto initial=run.entities[0].actor.position;
  for(uint32_t t=0;t<384;++t){lab.step(field,1);
   if(trace&&t%32==31 && (id==60 || id==110)) {const auto& a=run.entities[0]; const auto& w=a.combat_weapons[0];
    const auto g=simulation_lab_weapon_geometry(a,w.profile,a.actor.weapons[0],field,*runtime);
    const auto aim=simulation_lab_target_aim_point(run.entities[1],field,*runtime);
    const auto angles=simulation_shot_line_of_fire_angles(g.aim_origin,aim,w.shot);
    std::cout<<"TRACE arm="<<id<<" tick="<<t+1<<" x="<<a.actor.position.x-initial.x<<" y="<<a.actor.position.y-initial.y<<" target_dx="<<aim.x-g.aim_origin.x<<" target_dz="<<aim.z-g.aim_origin.z<<" angle_valid="<<angles.valid<<" pitch="<<int16_t(angles.pitch_turn16)<<" pitch_min="<<w.profile.pitch_min_turn16<<" pitch_max="<<w.profile.pitch_max_turn16<<" mode="<<w.shot.trajectory_mode<<" speed="<<w.shot.launch_speed_q12<<" gravity="<<w.shot.ballistic_divisor_q12<<" flags="<<a.combat_movement.flags<<" retry="<<a.combat_movement.retry<<" stride="<<a.articulated_contact_interpolation_rate_q12<<" foot0x="<<a.actor.articulated_foot_anchor_world[0].x-initial.x<<" foot1x="<<a.actor.articulated_foot_anchor_world[1].x-initial.x<<" endpoint="<<a.actor.articulated_foot_target_world[a.actor.articulated_swing_side&1].x-initial.x<<" phase="<<a.actor.articulated_contact_progress_q12<<'\n';
   }
  }
  const auto& a=run.entities[0];starts+=a.combat_follow_starts;
  if(id!=60||cannon_near)if(!a.combat_follow_starts||!a.combat_follow_stops||!run.metrics.weapon_shots_fired||!run.metrics.projectile_damage_applied)
    throw std::runtime_error("stock approach/stop/fire/damage missing for ARM"+std::to_string(id));
  std::cout<<"STOCK_PURSUIT arm="<<id<<" range_q12="<<range<<" distance_extra="<<extra<<" primary_node="<<(a.combat_weapons.empty()?UINT32_MAX:a.combat_weapons[0].profile.yaw_model_node_index)<<" fog="<<a.fog_uncover_radius_q12<<" acquire="<<a.combat_command_acquisitions<<" blocked="<<a.combat_follow_blocked_callbacks<<" starts="<<a.combat_follow_starts<<" stops="<<a.combat_follow_stops<<" solves="<<a.combat_follow_route_resolves<<" movement="<<std::hypot(a.actor.position.x-initial.x,a.actor.position.y-initial.y)<<" shots="<<run.metrics.weapon_shots_fired<<" damage="<<run.metrics.projectile_damage_applied<<" failures="<<a.combat_follow_route_failures<<" physical_blocks="<<run.metrics.navigation_actual_step_blocks<<" contacts="<<a.actor.articulated_contact_event_serial<<" speed="<<a.motion.speed_q12_per_tick<<" yawmax="<<a.motion.yaw_max_velocity_turn16<<" yawacc="<<a.motion.yaw_acceleration_turn16<<" stored_dx="<<q12_to_float(a.combat_movement.stored.x)-a.actor.position.x<<" stored_dy="<<q12_to_float(a.combat_movement.stored.y)-a.actor.position.y<<" yaw="<<a.actor.body_yaw_turn16<<" flags="<<a.combat_movement.flags<<'\n';
 }
 if(!starts)throw std::runtime_error("no stock weapon serviced pursuit");
 std::cout<<"V642_STOCK_PURSUIT PASS; fully unlocked isolated units,real friendly HQ observer off path,flat field/native assets,no injected attack or move command,384 ticks each; not ordinary progression proof\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
