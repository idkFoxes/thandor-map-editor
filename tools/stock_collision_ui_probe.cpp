#define main stock_replay_unused_main
#include "simlab_stock_replay.cpp"
#undef main
#include "thandor/simulation_lab_presentation.hpp"
#include <cmath>
int main(int argc,char** argv)try{
 if(argc!=3)throw std::runtime_error("usage: stock_collision_ui_probe GAME_ROOT before|after");
 const bool after=std::string_view(argv[2])=="after";
 const auto catalog=AssetCatalog::scan_archive_root(argv[1]);const auto runtime=simulation_lab_prepare_runtime_catalog(catalog);
 FactionTechnologyMask256 mask{};mask.fill(UINT32_MAX);std::array<FactionTechnologyMask256,8> masks{};masks[1]=mask;
 for(auto id:{1U,2U,50U,60U,110U,170U,250U,300U,301U,340U}){
  const auto* p=catalog.army_preset(id);if(!p)throw std::runtime_error("stock ARM missing");
  std::cout<<"RADIUS arm="<<id<<" class="<<p->runtime_model_class_id<<" radius_q12="<<p->simulation_motion_profile.collision_radius_q12<<'\n';
  if(after&&p->simulation_motion_profile.collision_radius_q12<=0)throw std::runtime_error("stock physical owner still has zero radius");
 }
 auto field=FieldGrid::create(80,80,0,0);const auto& cell=field.at(40,40);const Vec3f center{q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
 for(bool building:{true,false}){
  auto a=seed_for(*catalog.army_preset(2),catalog,mask);a.faction_index=1;a.actor.position={center.x-2,center.y,0};
  auto b=seed_for(*catalog.army_preset(building?340:2),catalog,mask);b.faction_index=1;b.actor.position={center.x,center.y,0};b.source_placement_index=1;
  const std::array seeds{a,b};std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
  SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=256;cfg.sweep_mode=SimulationLabSweepMode::Fixed;
  SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=scenario.assignable_faction_count=1;scenario.runtime_catalog=runtime;
  SimulationLabExperiment lab;lab.reset(cfg,ki,seeds,{}, {},{},masks,{},scenario);auto& run=lab.runs()[0];
  // Isolate the actual physical admission gate from path planning. Keep the real
  // initialized stock owner, speed, heading, radius, and movement callbacks.
  for(auto& e:run.entities){e.military_ai_source_dispatch=true;e.has_traversal_profile=false;}
  run.entities[0].actor.move_target=Vec3f{center.x+2,center.y,0};float closest=100;
  for(unsigned t=0;t<256;++t){lab.step(field,1);const auto p=run.entities[0].actor.position,q=run.entities[1].actor.position;closest=std::min(closest,std::hypot(p.x-q.x,p.y-q.y));}
  const auto& u=run.entities[0];const auto& blocker=run.entities[1];
  const float radius=q12_to_float(u.motion.collision_radius_q12+blocker.motion.collision_radius_q12);
  std::cout<<"STOCK_COLLISION building="<<building<<" closest="<<closest<<" required="<<radius<<" final_dx="<<u.actor.position.x-center.x<<" blocks="<<run.metrics.navigation_actual_step_blocks<<'\n';
  if(after&&(closest<radius-.001F||!run.metrics.navigation_actual_step_blocks))throw std::runtime_error("stock unit crossed physical owner");
  if(!after&&u.actor.position.x<=center.x)throw std::runtime_error("immutable before control did not reproduce passage");
 }
 if(after){
  SimulationLabEntity entity;auto seed=seed_for(*catalog.army_preset(60),catalog,mask);
  if(!simulation_lab_initialize_runtime_entity(entity,seed,mask,*runtime,true))throw std::runtime_error("stock preparation failed");
  const auto& hierarchy=*entity.runtime_model_hierarchy;constexpr unsigned calls=2000;uint64_t sum=0;unsigned fallbacks=0;
  const auto fallback=[&]{++fallbacks;return catalog.simulation_weapon_profiles_for_hierarchy(hierarchy);};
  auto t=std::chrono::steady_clock::now();for(unsigned i=0;i<calls;++i){auto profiles=fallback();for(const auto& p:profiles)sum+=p.shot_definition_id;}
  const auto old_us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-t).count();const auto expected=sum;sum=0;fallbacks=0;
  t=std::chrono::steady_clock::now();for(unsigned i=0;i<calls;++i){auto profiles=simulation_lab_render_weapon_profiles(&entity,fallback);for(const auto& p:profiles)sum+=p.shot_definition_id;}
  const auto prepared_us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-t).count();
  if(sum!=expected||fallbacks)throw std::runtime_error("prepared renderer changed weapon profiles or decoded sprites");
  std::cout<<"PROFILE_REUSE calls="<<calls<<" fallback_us="<<old_us<<" prepared_us="<<prepared_us<<" prepared_fallback_calls="<<fallbacks<<" checksum="<<sum<<" (isolated CPU benchmark, not frame rate)\n";
 }
 std::cout<<"V622_STOCK_"<<(after?"AFTER":"BEFORE")<<" PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
