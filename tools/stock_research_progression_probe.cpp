#define main v637_stock_replay_unused_main
#include "simlab_stock_replay.cpp"
#undef main
int main(int argc,char** argv)try{
 if(argc<2||argc>3||(argc==3&&std::string(argv[2])!="--carrier-ready"))throw std::runtime_error("usage: probe GAME_ROOT [--carrier-ready]");
 const bool ready=argc==3;const auto catalog=AssetCatalog::scan_archive_root(argv[1]);const auto runtime=simulation_lab_prepare_runtime_catalog(catalog);
 const auto group=std::find_if(catalog.map_groups().begin(),catalog.map_groups().end(),[](const auto& g){return g.level_path=="level/asgard.lev";});
 if(group==catalog.map_groups().end())throw std::runtime_error("Asgard absent");const auto level=LevelAsset::parse(catalog.load(group->level_path));const auto technologies=simulation_lab_prepare_technologies(catalog,&level);
 std::vector<SimulationLabProductionAsset> products;
 for(const auto& p:catalog.army_presets())if(p.registry_id){SimulationLabProductionAsset a;a.army_asset_id=p.registry_id;a.army_runtime_flags14_raw=p.army_runtime_flags14_raw;a.ai_definition_class_value70=p.ai_definition_class_value70;a.ai_definition_class_value74=p.ai_definition_class_value74;a.ai_definition_class_value78=p.ai_definition_class_value78;a.entity_seed=seed_for(p,catalog,{});simulation_lab_prepare_ai_product(p,catalog,a);products.push_back(std::move(a));}
 const auto field=FieldGrid::create(100,100,0,0);unsigned completed=0;
 const auto tech=[&](uint32_t id)->const SimulationLabTechnologyAsset&{for(const auto& t:technologies)if(t.technology_id==id)return t;throw std::runtime_error("technology absent");};
 const auto closure=[&](FactionTechnologyMask256& mask){for(unsigned pass=0;pass<256;++pass){auto old=mask;for(const auto& t:technologies)if(faction_technology_unlocked(mask,t.technology_id))for(size_t i=0;i<mask.size();++i)mask[i]|=t.prerequisite_masks[i];if(old==mask)return;}throw std::runtime_error("prerequisite closure failed");};
 for(uint32_t id:{99U,108U,126U,156U,186U,187U,188U,192U,193U,194U}){
  const auto& target=tech(id);FactionTechnologyMask256 mask=target.prerequisite_masks;for(auto initial:{0U,1U,id==126?203U:206U})set_faction_technology_unlocked(mask,initial,true);closure(mask);set_faction_technology_unlocked(mask,id,false);
  const ArmyAssetPreset* source=nullptr;
  for(const auto& p:catalog.army_presets()){
   if(p.registry_id<300||p.registry_id>=400)continue;
   for(const auto& g:p.model_variant_groups)if(g.parent_group_index==UINT32_MAX)for(const auto& v:g.variants){
    if(const auto it=runtime->definitions.find(v.model_definition_id);it!=runtime->definitions.end())
     if(std::find(it->second.technology_panel_ids.begin(),it->second.technology_panel_ids.end(),id)!=it->second.technology_panel_ids.end())source=&p;
   }
   if(source)break;
  }
  if(!source)throw std::runtime_error("real source panel absent");
  uint32_t carrier=0,carrier_tech=UINT32_MAX;
  for(const auto& p:products)if((p.army_runtime_flags14_raw&1U)&&p.army_asset_id<300){
   const auto matches=[&](size_t i){const auto& ids=p.ai_linked_model_definition_ids[i];return std::find(ids.begin(),ids.end(),target.compatible_model_definition_id)!=ids.end();};
   if(matches(1)||matches(2)){
    std::cout<<"CARRIER tech="<<id<<" arm="<<p.army_asset_id<<" root_first_tech="<<p.ai_root_first_variant_required_technology_bit<<'\n';
    if(!carrier){carrier=p.army_asset_id;carrier_tech=p.ai_root_first_variant_required_technology_bit;}
   }
  }
  if(ready&&carrier_tech<256){set_faction_technology_unlocked(mask,carrier_tech,true);closure(mask);set_faction_technology_unlocked(mask,id,false);}
  auto hq=seed_for(*catalog.army_preset(330),catalog,mask);hq.faction_index=1;hq.actor.position={20,20,0};auto walker=seed_for(*catalog.army_preset(303),catalog,mask);walker.faction_index=1;walker.actor.position={40,20,0};auto lab_source=seed_for(*source,catalog,mask);lab_source.faction_index=1;lab_source.actor.position={30,20,0};
  const std::array seeds{hq,walker,lab_source};std::array<FactionTechnologyMask256,8> masks{};masks[1]=mask;
  std::array<SimulationLabFactionEconomySeed,8> economy{};economy[1].xenite_current_q4=1000000;economy[1].baseline_xenite_storage_limit_q4=1000000;economy[1].baseline_energy_supply_q4=1000000;
  SimulationLabScenarioRuntimeSeed scenario;scenario.runtime_catalog=runtime;SimulationLabConfig config;config.run_count=1;config.duration_ticks=1024;config.parameter_index=100;config.sweep_mode=SimulationLabSweepMode::Fixed;config.fast_build_cheat=true;
  std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};ki[0x0D]=4096;SimulationLabExperiment lab;lab.reset(config,ki,seeds,economy,products,technologies,masks,{},scenario);
  if(faction_technology_unlocked(lab.runs()[0].faction_technology_masks[1],id))throw std::runtime_error("target was creation-granted");lab.step(field,74);
  const auto trace=lab.runs()[0].ai_research_trace[1];const SimulationLabRun::ResearchCandidateTrace* observed=nullptr;
  for(uint32_t i=0;i<trace.candidate_count;++i)if(trace.candidates[i].technology_id==id&&trace.candidates[i].source_army_id==source->registry_id){observed=&trace.candidates[i];break;}
  if(!observed)throw std::runtime_error("target not admitted from real source");const auto c=*observed;
  lab.step(field,950);const auto& run=lab.runs()[0];const bool done=faction_technology_unlocked(run.faction_technology_masks[1],id);completed+=done;
  std::cout<<"PROGRESSION tech="<<id<<" source="<<source->registry_id<<" model="<<target.compatible_model_definition_id<<" score="<<c.score<<" reason="<<c.reason<<" arm_matches="<<c.compatible_arm_matches<<" arm_positive="<<c.compatible_arm_positive<<" missing_metadata="<<c.compatible_arm_missing_metadata<<" locked_root_matches="<<c.locked_root_child_matches<<" best_potential_arm="<<c.best_potential_arm_id<<" best_potential_score="<<c.best_potential_arm_score<<" target_complete="<<done<<" completed_jobs="<<run.metrics.research_jobs_completed<<" carrier_fixture="<<ready<<" carrier="<<carrier<<" carrier_tech="<<carrier_tech<<" tick="<<run.tick<<'\n';
  if(ready&&(id==99||id==108||id==126||id==156)&&(!c.score||!done))throw std::runtime_error("funded carrier-ready main weapon failed");
 }
 std::cout<<"V637_STOCK_PROGRESSION PASS cases=10 completed="<<completed<<" partial_mask=1 carrier_fixture="<<ready<<" ordinary_progression=0\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
