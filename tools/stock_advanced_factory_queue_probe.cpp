#include <cmath>
#define main v617_stock_replay_unused_main
#include "simlab_stock_replay.cpp"
#undef main
int main(int argc,char** argv)try{
 if(argc!=2)throw std::runtime_error("usage: stock_factory_probe game_assets");
 const auto catalog=AssetCatalog::scan_archive_root(argv[1]);const auto runtime=simulation_lab_prepare_runtime_catalog(catalog);
 auto field=FieldGrid::create(160,160,0,0);const auto& c=field.at(80,80);
 FactionTechnologyMask256 mask{};mask.fill(UINT32_MAX);
 std::array<FactionTechnologyMask256,8> masks{};masks[1]=mask;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
 for(uint32_t factory_id=302;factory_id<=305;++factory_id){
  const auto* preset=catalog.army_preset(factory_id);if(!preset)throw std::runtime_error("missing stock factory");
  auto seed=seed_for(*preset,catalog,mask);seed.faction_index=1;seed.actor.position={q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),0};
  const ArmyAssetPreset* selected=nullptr;
  for(const auto& unit:catalog.army_presets())if(unit.registry_id<300&&(unit.army_runtime_flags14_raw&1U)&&
    ai_v601_producer_accepts(13,seed.production_compatibility_mask,unit.army_runtime_flags14_raw)&&
    (!selected||unit.registry_id<selected->registry_id))selected=&unit;
  if(!selected)throw std::runtime_error("stock factory has no compatible unit");
  for(auto& cell:field.cells())cell.water_delta_q12=factory_id==305?4096:0;
  SimulationLabProductionAsset product;product.army_asset_id=selected->registry_id;product.army_runtime_flags14_raw=selected->army_runtime_flags14_raw;
  product.xenite_cost_q4=selected->retail_xenite_cost_q4;product.build_duration_ticks=selected->retail_build_time_q5;
  product.entity_seed=seed_for(*selected,catalog,mask);simulation_lab_prepare_ai_product(*selected,catalog,product);
  std::array<SimulationLabFactionEconomySeed,8> economy{};economy[1].xenite_current_q4=product.xenite_cost_q4;
  SimulationLabConfig config;config.run_count=1;config.sweep_mode=SimulationLabSweepMode::Fixed;config.duration_ticks=2048;
  SimulationLabScenarioRuntimeSeed scenario;scenario.runtime_catalog=runtime;
  SimulationLabExperiment lab;lab.reset(config,parameters,std::span(&seed,1),economy,std::span(&product,1),{},masks,{},scenario);
  auto& run=lab.runs()[0];run.secondary_purchase_requests.push_back({product.army_asset_id,1,0,0});
  for(uint32_t t=0;t<2048;++t){lab.step(field,1);if(run.entities.size()==2&&!run.entities[1].production_linked_to_parent&&run.entities[0].production_behavior_state==0)break;}
  std::cout<<"FACTORY "<<factory_id<<" unit="<<product.army_asset_id<<" bands="<<selected->traversal_low_band_index<<","<<selected->traversal_high_band_index<<" class="<<product.entity_seed.runtime_model_class_id<<" ticks="<<run.tick<<" spawned="<<run.metrics.class13_units_spawned;
  if(run.entities.size()==2)std::cout<<" linked="<<run.entities[1].production_linked_to_parent<<" exit_distance="<<std::hypot(run.entities[1].actor.position.x-seed.actor.position.x,run.entities[1].actor.position.y-seed.actor.position.y);
  std::cout<<" phase="<<run.entities[0].production_behavior_state<<'\n';
  if(run.entities.size()!=2||run.entities[1].production_linked_to_parent||run.entities[0].production_behavior_state!=0)
   throw std::runtime_error("advanced stock factory did not complete its production/exit cycle");
 }
 std::cout<<"V617_STOCK_CLASS13_ADVANCED_FACTORIES PASS; isolated queue/fully unlocked input; not autonomous progression proof\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
