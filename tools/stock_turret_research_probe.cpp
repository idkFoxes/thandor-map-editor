#define main v635_stock_replay_unused_main
#include "simlab_stock_replay.cpp"
#undef main
int main(int argc,char** argv)try{
 if(argc!=2)throw std::runtime_error("usage: probe game_assets");
 const auto catalog=AssetCatalog::scan_archive_root(argv[1]);const auto runtime=simulation_lab_prepare_runtime_catalog(catalog);
 const auto technologies=simulation_lab_prepare_technologies(catalog);
 std::vector<SimulationLabProductionAsset> products;
 for(const auto& p:catalog.army_presets())if(p.registry_id){SimulationLabProductionAsset a;a.army_asset_id=p.registry_id;a.army_runtime_flags14_raw=p.army_runtime_flags14_raw;a.entity_seed=seed_for(p,catalog,{});simulation_lab_prepare_ai_product(p,catalog,a);products.push_back(std::move(a));}
 const auto field=FieldGrid::create(100,100,0,0);const auto cell=field.at(50,50);
 unsigned checked=0;
 for(uint32_t id:{203U,206U}){
  const ArmyAssetPreset* source=nullptr;
  for(const auto& p:catalog.army_presets()){
   if(p.registry_id<300)continue;
   for(const auto& v:p.model_variant_groups)if(v.parent_group_index==UINT32_MAX)
    for(const auto& variant:v.variants)for(const auto& n:variant.model_hierarchy)
     if(n.parent_index==UINT32_MAX){const auto model=runtime->definitions.find(n.model_definition_id);
      if(model!=runtime->definitions.end()&&std::find(model->second.technology_panel_ids.begin(),model->second.technology_panel_ids.end(),id)!=model->second.technology_panel_ids.end())source=&p;}
   if(source)break;
  }
  if(!source){std::cout<<"STOCK_RESEARCH tech="<<id<<" no_stock_panel=1\n";continue;}
  FactionTechnologyMask256 mask{};
  // Controlled availability: preserve stock panels/definitions/scoring, unlock
  // prerequisites but leave this chassis and every future weapon family locked.
  const auto target=std::find_if(technologies.begin(),technologies.end(),[&](const auto& t){return t.technology_id==id;});
  if(target==technologies.end())throw std::runtime_error("stock technology absent");
  mask=target->prerequisite_masks;set_faction_technology_unlocked(mask,0,true);
  set_faction_technology_unlocked(mask,id==203?126U:99U,true); // admit its native category
  set_faction_technology_unlocked(mask,id,false);
  auto hq=seed_for(*catalog.army_preset(330),catalog,mask);hq.faction_index=1;hq.actor.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
  auto tower=seed_for(*source,catalog,mask);tower.faction_index=1;tower.actor.position=hq.actor.position;tower.actor.position.x+=4;
  const std::array seeds{hq,tower};std::array<FactionTechnologyMask256,8> masks{};masks[1]=mask;
  std::array<SimulationLabFactionEconomySeed,8> economy{};economy[1].xenite_current_q4=100000;economy[1].baseline_xenite_storage_limit_q4=100000;economy[1].baseline_energy_supply_q4=100000;
  SimulationLabScenarioRuntimeSeed scenario;scenario.runtime_catalog=runtime;
  SimulationLabConfig config;config.run_count=1;config.duration_ticks=128;config.parameter_index=100;config.sweep_mode=SimulationLabSweepMode::Fixed;
  std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};ki[0x0D]=4096;
  SimulationLabExperiment lab;lab.reset(config,ki,seeds,economy,products,technologies,masks,{},scenario);lab.step(field,74);
  const auto& trace=lab.runs()[0].ai_research_trace[1];bool found=false;
  for(uint32_t i=0;i<trace.candidate_count;++i)if(trace.candidates[i].technology_id==id){const auto& c=trace.candidates[i];
   std::cout<<"STOCK_RESEARCH tech="<<id<<" source="<<source->registry_id<<" class="<<c.source_class<<" score="<<c.score<<" reason="<<c.reason<<" best="<<trace.best_technology_id<<'\n';
   if(!c.score)throw std::runtime_error("stock turret chassis has zero research score");found=true;++checked;
  }
  if(!found)throw std::runtime_error("stock chassis research not admitted; inspect real source panel selection");
 }
 if(!checked)throw std::runtime_error("no real stock chassis candidate exercised");
 std::cout<<"V635_STOCK_TURRET_RESEARCH PASS; existing class11 stock panels, controlled prerequisites/category/funds; category-path fix covered separately; not ordinary-match progression proof\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
