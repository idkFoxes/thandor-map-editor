#include "thandor/simulation_placement.hpp"
#include "thandor/retail_selection_frame.hpp"
#include "thandor/simulation_lab_worker.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/simulation_lab_catalog.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace thandor;
namespace {
void check(bool yes,const char* what){if(!yes)throw std::runtime_error(what);}
SimulationLabConfig config(uint32_t ticks=128){SimulationLabConfig c;c.run_count=1;c.duration_ticks=ticks;c.parameter_index=100;return c;}
SimulationLabSeedEntity actor(uint32_t id,Vec3f position,uint32_t cls=1){
 SimulationLabSeedEntity s;s.army_asset_id=id;s.faction_index=1;s.runtime_model_class_id=cls;
 s.root_model_maximum_integrity=1000;s.actor.position=position;s.building=cls==13;
 s.motion.collision_radius_q12=float_to_q12(cls==13?1.F:.25F);
 s.motion.speed_q12_per_tick=cls==13?0:float_to_q12(.1F);
 s.motion.yaw_max_velocity_turn16=0x1000;s.motion.yaw_acceleration_turn16=0x1000;
 return s;
}
}
int main(int argc,char** argv)try{
 auto field=FieldGrid::create(59,59,0,0);const auto cell=field.at(29,29);
 const Vec3f center{q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
 {
  auto factory=actor(303,center,13);factory.production_exit_local=Vec3f{0,-3,0};
  check(simulation_placement_factory_exit_clear(field,center,0,factory,{}),"flat factory exit rejected");
  auto distant=field;const auto origin=retail_fog_world_to_cell(field,float_to_q12(center.x),float_to_q12(center.y));
  check(origin.has_value(),"fixture root off grid");distant.at(origin->x+3,origin->y).terrain_height_q12=0x1000;
  check(simulation_placement_terrain_clear(distant,center,0,0xC00,0),"small footprint gained an extra terrain ring");
  check(!simulation_placement_terrain_clear(distant,center,0,0x1800,0),"large footprint ignored its outer terrain ring");
  auto wet=field;for(auto& c:wet.cells())c.water_delta_q12=100;
  check(simulation_placement_terrain_clear(wet,center,0,0xC00,1),"water-contact footprint used ground-water rejection");
  check(!simulation_placement_terrain_clear(wet,center,0,0xC00,0),"ground footprint admitted water");
  auto blocker=actor(331,{center.x,center.y-3,0},15);SimulationLabEntity obstacle;
  obstacle.runtime_model_class_id=15;obstacle.actor=blocker.actor;obstacle.motion=blocker.motion;
  const std::array owners{obstacle};
  check(!simulation_placement_factory_exit_clear(field,center,0,factory,owners),"building on exit admitted");
  check(simulation_placement_factory_exit_clear(field,center,0x4000,factory,owners),"rotation did not clear exit obstruction");
  auto blocked=field;const auto endpoint=retail_fog_world_to_cell(field,float_to_q12(center.x),float_to_q12(center.y-3));
  check(endpoint.has_value(),"fixture endpoint off map");
  blocked.at(endpoint->x,endpoint->y).terrain_height_q12=0x1000;
  check(!simulation_placement_factory_exit_clear(blocked,center,0,factory,{}),"bad landscape at exit admitted");
  check(simulation_placement_factory_exit_clear(blocked,center,0x4000,factory,{}),"rotated exit still used old landscape");
  obstacle.runtime_model_class_id=13;obstacle.actor.position={center.x,center.y+3,0};
  obstacle.production_exit_local=Vec3f{0,-3,0};
  check(simulation_placement_factory_anchor_overlap(center,0xC00,obstacle,field),"existing factory exit was not reserved");
  check(!simulation_placement_factory_anchor_overlap(center,0,obstacle,field),"zero-radius placement gained a collision");
 }
 {
  auto parent=actor(303,center,13),child=actor(111,center);
  child.has_traversal_profile=true;child.traversal_low_band_index=0;child.traversal_high_band_index=1;
  parent.production_exit_world=Vec3f{center.x+3,center.y,0};
  auto parent2=parent,child2=child;parent2.actor.position.x+=6;child2.actor.position.x+=6;
  parent2.production_exit_world=Vec3f{center.x+9,center.y,0};
  const std::array seeds{parent,child,parent2,child2};
  SimulationLabScenarioRuntimeSeed scenario;
  scenario.static_navigation_blockers.push_back({float_to_q12(center.x),float_to_q12(center.y),float_to_q12(100.F)});
  std::array<uint32_t,AiKnowledgeData::kParameterCount> p{};SimulationLabExperiment e;
  e.reset(config(),p,seeds,{}, {}, {}, {}, {},scenario);
  auto& run=e.runs()[0];
  for(size_t i:{1U,3U}){
   run.entities[i-1].production_behavior_state=3;
   run.entities[i].production_parent_entity_index=i-1;run.entities[i].production_linked_to_parent=true;
   run.entities[i].actor.move_target=run.entities[i-1].production_exit_world;
  }
  e.step(field,8);
  check(run.entities[1].navigation_recheck_tick>8&&run.entities[3].navigation_recheck_tick>8,
      "first blocked unit monopolized the route budget");
  check(run.metrics.factory_exit_route_retries>=2&&run.metrics.factory_exit_route_retries<8,
      "failed exit retried the expensive path every tick");
  check(run.entities[1].production_linked_to_parent&&run.entities[3].production_linked_to_parent,
      "blocked units were falsely released from factories");
 }
 {
  std::vector<SimulationLabSeedEntity> seeds;
  for(unsigned i=0;i<64;++i){auto s=actor(1000+i,center,0);s.motion={};s.actor.weapons.resize(8);seeds.push_back(s);}
  auto c=config();c.run_count=4;
  std::array<uint32_t,AiKnowledgeData::kParameterCount> p{};
  SimulationLabExperiment serial;serial.reset(c,p,seeds);serial.step(field,128);
  SimulationLabWorker worker;worker.start(c,p,seeds,field);const auto initial=worker.snapshot();
  const auto started=std::chrono::steady_clock::now();worker.request_advance(128);
  const auto deadline=started+std::chrono::seconds(10);
  std::shared_ptr<const SimulationLabExperiment> final;
  while(std::chrono::steady_clock::now()<deadline){final=worker.snapshot();if(final&&final->finished()&&worker.completed_generations()>=128)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  check(final&&final->finished(),"bounded worker fixture did not complete");
  check(initial->runs()[0].tick==0,"initial immutable snapshot changed");
  for(size_t i=0;i<final->runs().size();++i){
   check(final->runs()[i].tick==128&&final->runs()[i].rng_state==serial.runs()[i].rng_state,
      "publication coalescing changed ticks or RNG");
   check(final->runs()[i].entities.size()==serial.runs()[i].entities.size(),"publication coalescing lost entities");
  }
  check(worker.completed_generations()==128,"published generation counter omitted ticks");
  check(worker.snapshot_publications()<128,"worker still copied a snapshot every backlog tick");
  std::cout<<"WORKER ticks=128 runs=4 publications="<<worker.snapshot_publications()<<" elapsed_ms="
      <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()<<'\n';
 }
 if(argc==2){
  const auto catalog=AssetCatalog::scan_game_root(argv[1]);auto runtime=simulation_lab_prepare_runtime_catalog(catalog);
  FactionTechnologyMask256 mask{};mask.fill(~0U);std::array<FactionTechnologyMask256,8> masks{};masks[1]=mask;
  const auto frame=RetailSelectionFrameAssets::parse(catalog.load("gfx/panel/select.dat"),
      GfxAsset::parse(catalog.load("gfx/panel/select.gfx")));
  const auto healthy=retail_selection_frame_blits(frame,20,20,80,80,4096);
  const auto damaged=retail_selection_frame_blits(frame,20,20,80,80,2048);
  check(!healthy.empty()&&healthy.front().x==15&&healthy.front().y==15,"native selection corner offsets were lost");
  check(retail_selection_show_damage_frame(999,1000)&&!retail_selection_show_damage_frame(1000,1000),"damage selection trigger is not strictly below full HP");
  const auto base=frame.cells[25].subresource;
  check(std::any_of(damaged.begin(),damaged.end(),[&](const auto& blit){return blit.subresource==base+6;}),"native half-health fill frame was not selected");
  for(uint32_t marker:{143U,148U,149U}){
   const auto* effect=catalog.effect_definition(marker);check(effect,"native marker missing");
   auto colored=*effect;colored.state_tint_argb=0xFFFF0000;
   const auto tint=retail_effect_model_tint_argb(colored,0,4);
   check(tint&&((*tint>>16)&255)>0&&(*tint&65535)==0,"native marker did not accept faction tint");
  }
  std::cout<<"STOCK_UI native selection cells and EGATH0/EWAYP0/ETARG0 faction tint PASS\n";
  {
   auto one=actor(1,center),two=actor(1,{center.x,center.y+0.5F,0}),enemy=actor(333,{center.x+3,center.y,0},15);
   enemy.faction_index=2;enemy.building=true;
   SimulationLabScenarioRuntimeSeed scenario;scenario.runtime_catalog=runtime;scenario.enabled=true;
   scenario.active_faction_count=2;scenario.simulate_all_active_factions_as_ai=true;
   std::array<uint32_t,AiKnowledgeData::kParameterCount> p{};const std::array seeds{one,two,enemy};
   SimulationLabProductionAsset jeep;jeep.army_asset_id=1;const auto* preset=catalog.army_preset(1);
   jeep.ai_definition_class_value70=preset->ai_definition_class_value70;
   jeep.ai_definition_class_value74=preset->ai_definition_class_value74;
   jeep.ai_definition_class_value78=preset->ai_definition_class_value78;
   const std::array products{jeep};
   SimulationLabExperiment e;e.reset(config(64),p,seeds,{},products,{},masks,{},scenario);
   for(size_t i=0;i<2;++i)e.runs()[0].entities[i].military_ai_source_dispatch=true;
   e.step(field,64);const auto& run=e.runs()[0];
   std::cout<<"STOCK_GROUP orders="<<run.metrics.military_ai_group_orders<<" movement="<<run.metrics.military_movement_distance_world<<" shots="<<run.metrics.weapon_shots_fired<<'\n';
   const auto ecell=retail_fog_world_to_cell(field,float_to_q12(run.entities[2].actor.position.x),float_to_q12(run.entities[2].actor.position.y));
   std::cout<<"GROUP_DIAG calls="<<run.metrics.military_ai_selector_calls<<" sites="<<run.metrics.military_ai_site_orders
       <<" visibility="<<(ecell?run.current_visibility_occupancy[size_t(ecell->y)*field.width()+ecell->x]:0)
       <<" ratio="<<simulation_lab_hierarchy_condition_ratio_q12(run.entities[0])<<","<<simulation_lab_hierarchy_condition_ratio_q12(run.entities[1])
       <<" class="<<run.entities[0].runtime_model_class_id<<" life="<<unsigned(run.faction_lifecycle_states[1])<<" fog="<<run.entities[0].fog_uncover_radius_q12<<'\n';
   check(run.metrics.military_ai_group_orders>=2,"visible hostile did not receive a group attack order");
   check(run.metrics.military_movement_distance_world>0&&run.metrics.weapon_shots_fired>0,"group did not drive and fire at the discovered hostile");
  }

  for(bool custom:{false,true})for(bool synthetic:{false,true}){
   auto factory=actor(303,center,13);
   simulation_lab_prepare_model_technologies(*catalog.army_preset(303),catalog,factory);
   SimulationLabProductionAsset product;product.army_asset_id=111;product.entity_seed.army_asset_id=111;
   const auto* walker=catalog.army_preset(111);check(walker,"stock walker absent");
   product.entity_seed.articulated_contact_interpolation_rate_q12=walker->articulated_contact_interpolation_rate_q12;
   SimulationLabScenarioRuntimeSeed scenario;scenario.runtime_catalog=runtime;scenario.enabled=true;
   scenario.active_faction_count=1;scenario.simulate_all_active_factions_as_ai=true;
   std::array<uint32_t,AiKnowledgeData::kParameterCount> p{};
   const std::array seeds{factory};const std::array products{product};SimulationLabExperiment e;
   e.reset(config(256),p,seeds,{},products,{},masks,{},scenario);
   auto& f=e.runs()[0].entities[0];f.synthetic=synthetic;f.production_building=true;
   f.production_asset_id=111;f.production_duration_ticks=1;
   if(custom)f.production_drive_target_world=Vec3f{center.x+4,center.y-4,0};
   e.step(field,256);const auto& run=e.runs()[0];const auto& child=run.entities.back();
   std::cout<<"STOCK_WALKER custom="<<custom<<" synthetic="<<synthetic<<" units="<<run.metrics.class13_units_spawned<<" state="
       <<run.entities[0].production_behavior_state<<" linked="<<child.production_linked_to_parent<<" distance="
       <<std::hypot(child.actor.position.x-center.x,child.actor.position.y-center.y)<<'\n';
   check(run.metrics.class13_units_spawned==1&&child.runtime_model_class_id==3,"stock walker was not produced");
   check(!child.production_linked_to_parent&&std::hypot(child.actor.position.x-center.x,child.actor.position.y-center.y)>1,
      "stock walker did not drive out of its factory");
   check(run.entities[0].production_behavior_state==0,"walker factory did not return to idle");
   if(custom){
    check(run.entities[0].production_drive_target_world&&run.entities[0].production_drive_target_world->x==center.x+4,"custom destination was overwritten by the default exit");
    check(std::hypot(child.actor.position.x-center.x,child.actor.position.y-center.y)>2.5F,"walker did not continue from exit to stored drive destination");
   }
  }
 }
 std::cout<<"V611 factory clearance, route fairness, and worker publication PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
