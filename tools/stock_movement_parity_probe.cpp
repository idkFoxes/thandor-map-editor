// Uses actual PCK mover definitions with controlled terrain/building fixtures.
#define main stock_replay_main
#include "simlab_stock_replay.cpp"
#undef main
#include "thandor/simulation_lab_geometry.hpp"
int main(int argc,char**argv)try{
 if(argc!=2)throw std::runtime_error("Usage: stock_movement_parity_probe PCK_ROOT");
 const auto catalog=AssetCatalog::scan_archive_root(argv[1]);
 auto runtime=simulation_lab_prepare_runtime_catalog(catalog);
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
 unsigned cases=0,failed=0;std::array<unsigned,24> classes{};
 for(const auto& preset:catalog.army_presets()){
  if(!preset.simulation_motion_profile.movable()||preset.registry_id==0)continue;
  if(preset.browser_kind!=PlacementBrowserKind::Unit&&preset.runtime_model_class_id!=18U)continue;
  ++classes.at(preset.runtime_model_class_id);
  for(int terrain=0;terrain<3;++terrain){
   auto field=FieldGrid::create(67,67,0,0);
   const bool water=preset.has_traversal_profile&&preset.traversal_high_band_index==0;
   for(auto& c:field.cells()){
    if(terrain==1)c.terrain_height_q12=float_to_q12(c.world_x_q12/4096.F*.07F);
    if(water)c.water_delta_q12=900;
   }
   auto seed=seed_for(preset,catalog,{});seed.faction_index=1;
   const auto& origin=field.at(24,30);
   seed.actor.position={q12_to_float(origin.world_x_q12),q12_to_float(origin.world_y_q12),0};
   seed.actor.position.z=field.sample_terrain(seed.actor.position.x,seed.actor.position.y).height;
   seed.actor.body_yaw_turn16=0;seed.combat_weapons.clear();
   const Vec3f target{seed.actor.position.x+5,seed.actor.position.y,seed.actor.position.z};
   SimulationLabScenarioRuntimeSeed scene;scene.enabled=true;scene.active_faction_count=1;
   scene.assignable_faction_count=1;scene.human_faction_mask=2;scene.ai_planning_enabled=false;
   scene.runtime_catalog=runtime;
   if(terrain==2){
    // Two real-sized building circles flank a corridor with 0.35 units
    // beyond the mover's native route margin. All stock footprint classes fit.
    const int32_t radius=2000;
    const int32_t side=radius+seed.motion.collision_radius_q12+499+float_to_q12(.35F);
    for(int sign:{-1,1})scene.static_navigation_blockers.push_back({
      float_to_q12(seed.actor.position.x+2.5F),float_to_q12(seed.actor.position.y)+sign*side,radius,11});
   }
   SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=2400;cfg.sweep_mode=SimulationLabSweepMode::Fixed;
   SimulationLabExperiment lab;lab.reset(cfg,ki,std::span(&seed,1),{},{},{},{},{},scene);
   auto& run=lab.runs()[0];auto& entity=run.entities[0];
   // A direct point order exercises the runtime callback and route admission,
   // without selection formation offsets or autonomous military orders.
   if(preset.runtime_model_class_id==18U){
    SimulationLabPlayerCommand command;command.kind=SimulationLabPlayerCommandKind::MovePioneer;
    command.faction_index=1;command.entity_index=0;
    command.world_x_q12=float_to_q12(target.x);command.world_y_q12=float_to_q12(target.y);
    if(!lab.queue_player_command(0,command))throw std::runtime_error("pioneer order rejected");
   }else entity.actor.move_target=target;
   float traveled=0;bool hit=false;
   for(unsigned tick=0;tick<2400;++tick){
    const auto before=entity.actor.position;lab.step(field,1);
    traveled+=std::hypot(entity.actor.position.x-before.x,entity.actor.position.y-before.y);
    if(preset.runtime_model_class_id!=21U)for(const auto& b:scene.static_navigation_blockers)
      hit|=simulation_lab_step_hits_blocker(before,entity.actor.position,entity.motion.collision_radius_q12,b);
    if(!entity.actor.move_target&&!entity.actor.move_waypoint)break;
   }
   const bool arrived=std::abs(entity.actor.position.x-target.x)<=1.05F&&std::abs(entity.actor.position.y-target.y)<=1.05F;
   const bool ok=arrived&&!hit;
   std::cout<<"STOCK_MOVE arm="<<preset.registry_id<<" cls="<<preset.runtime_model_class_id
    <<" terrain="<<terrain<<" water="<<water<<" arrived="<<arrived<<" hit="<<hit
    <<" distance="<<traveled<<" blocks="<<run.metrics.navigation_actual_step_blocks
    <<" x="<<entity.actor.position.x-target.x<<" y="<<entity.actor.position.y-target.y
    <<" result="<<(ok?"PASS":"FAIL")<<std::endl;
   ++cases;if(!ok)++failed;
  }
 }
 for(size_t cls=0;cls<classes.size();++cls)if(classes[cls])std::cout<<"CLASS "<<cls<<" movers="<<classes[cls]<<'\n';
 std::cout<<"STOCK_MOVEMENT cases="<<cases<<" failed="<<failed<<'\n';return failed?1:0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 2;}
