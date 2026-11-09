#define main v620_stock_replay_unused_main
#include "simlab_stock_replay.cpp"
#undef main
int main(int argc,char** argv)try{
 if(argc!=2)throw std::runtime_error("usage: stock_smoke_probe game_assets");
 const auto catalog=AssetCatalog::scan_archive_root(argv[1]);const auto runtime=simulation_lab_prepare_runtime_catalog(catalog);
 auto field=FieldGrid::create(40,40,0,0);FactionTechnologyMask256 mask{};mask.fill(UINT32_MAX);
 std::array<FactionTechnologyMask256,8> masks{};masks[1]=mask;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};uint32_t tested=0;
 for(uint32_t id:{300U,301U,303U})for(uint32_t job:{0U,1U}){
  const auto* army=catalog.army_preset(id);auto seed=seed_for(*army,catalog,mask);seed.faction_index=1;
  seed.actor.position={q12_to_float(field.at(20,20).world_x_q12),q12_to_float(field.at(20,20).world_y_q12),0};
  SimulationLabProductionAsset product;product.army_asset_id=2;product.build_duration_ticks=100000;product.entity_seed=seed;
  SimulationLabConfig config;config.run_count=1;config.duration_ticks=64;config.sweep_mode=SimulationLabSweepMode::Fixed;
  SimulationLabScenarioRuntimeSeed scenario;scenario.runtime_catalog=runtime;
  SimulationLabExperiment lab;lab.reset(config,ki,std::span(&seed,1),{},std::span(&product,1),{},masks,{},scenario);
  auto& run=lab.runs()[0];auto& e=run.entities[0];e.synthetic=true;
  const auto& root=(*e.runtime_model_hierarchy)[0];if(root.simulation.timed_effect_id==0&&root.simulation.timed_water_effect_id==0)continue;
  auto count=[&](){uint32_t n=0;for(const auto& event:simulation_lab_one_shot_events_after(run,0))if(static_cast<uint32_t>(event.kind)==9U)++n;return n;};
  lab.step(field,2);if(count()!=0)throw std::runtime_error("idle synthetic producer emitted smoke");
  e.production_building=job==0;e.research_active=job==1;
  e.production_behavior_state=job==0&&e.runtime_model_class_id==13?1:0;e.production_asset_id=2;e.production_duration_ticks=100000;
  lab.step(field,1);if(count()!=1)throw std::runtime_error("active synthetic producer emitted no timed effect");
  e.model_components[e.root_model_component_index].runtime_flags_ec|=1;
  const auto paused=count();const auto countdown=e.model_components[e.root_model_component_index].timed_effect_countdown;
  lab.step(field,8);if(e.model_components[e.root_model_component_index].timed_effect_countdown!=countdown)throw std::runtime_error("inactive producer consumed emitter countdown");
  if(count()!=paused)throw std::runtime_error("unpowered producer continued smoke");
  e.model_components[e.root_model_component_index].runtime_flags_ec&=~1U;
  lab.step(field,1);e.production_building=false;e.research_active=false;e.production_behavior_state=2;const auto done=count();
  lab.step(field,8);if(count()!=done)throw std::runtime_error("completed/opening factory continued smoke");
  std::cout<<"SMOKE arm="<<id<<" job="<<(job?"research":"build")<<" effect="<<root.simulation.timed_effect_id<<" pulses="<<done<<" idle=0 outage=paused completed=paused\n";++tested;
 }
 if(!tested)throw std::runtime_error("no stock factory emitter tested");
 std::cout<<"V620_STOCK_PRODUCTION_SMOKE PASS cases="<<tested<<"; manually active jobs, not autonomous progression proof\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
