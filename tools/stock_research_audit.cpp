#define main v636_stock_replay_unused_main
#include "simlab_stock_replay.cpp"
#undef main
#include <set>
int main(int argc,char** argv)try{
 if(argc!=2)throw std::runtime_error("usage: audit GAME_ROOT");
 const auto catalog=AssetCatalog::scan_archive_root(argv[1]);const auto runtime=simulation_lab_prepare_runtime_catalog(catalog);
 const auto group=std::find_if(catalog.map_groups().begin(),catalog.map_groups().end(),[](const auto& g){return g.level_path=="level/asgard.lev";});
 if(group==catalog.map_groups().end())throw std::runtime_error("Asgard absent");
 const auto level=LevelAsset::parse(catalog.load(group->level_path));
 const auto technologies=simulation_lab_prepare_technologies(catalog,&level),legacy=simulation_lab_prepare_technologies(catalog);
 uint32_t changed=0;
 for(size_t i=0;i<technologies.size();++i){const auto& t=technologies[i];const auto& old=legacy[i];
  changed+=t.compatible_model_resolved!=old.compatible_model_resolved||t.compatible_model_definition_id!=old.compatible_model_definition_id||t.compatible_model_runtime_class_id!=old.compatible_model_runtime_class_id;
  std::cout<<"TECH id="<<t.technology_id<<" base="<<t.ai_base_candidate_score<<" category="<<t.category<<" xenite="<<t.xenite_cost_q4<<" duration="<<t.research_duration_ticks
   <<" model="<<t.compatible_model_definition_id<<" class="<<t.compatible_model_runtime_class_id<<" resolved="<<t.compatible_model_resolved<<" complete="<<t.compatible_model_lookup_complete
   <<" old_model="<<old.compatible_model_definition_id<<" old_class="<<old.compatible_model_runtime_class_id<<" dependency="<<t.dependency_technology_id<<" prereq=";
  for(uint32_t bit=0;bit<256;++bit)if(faction_technology_unlocked(t.prerequisite_masks,bit))std::cout<<bit<<',';
  std::cout<<'\n';
 }
 std::vector<SimulationLabProductionAsset> products;
 for(const auto& p:catalog.army_presets())if(p.registry_id&&catalog.placement_available_in_level(p,level)){
  SimulationLabProductionAsset a;a.army_asset_id=p.registry_id;a.army_runtime_flags14_raw=p.army_runtime_flags14_raw;
  a.ai_definition_class_value70=p.ai_definition_class_value70;a.ai_definition_class_value74=p.ai_definition_class_value74;a.ai_definition_class_value78=p.ai_definition_class_value78;
  a.entity_seed=seed_for(p,catalog,{});simulation_lab_prepare_ai_product(p,catalog,a);
  products.push_back(std::move(a));
 }
 const auto field=FieldGrid::create(100,100,0,0);unsigned checked=0,positive=0,completed=0,blocked=0,granted=0;
 for(const auto& preset:catalog.army_presets()){
  if(preset.registry_id<300||preset.registry_id>=400||!catalog.placement_available_in_level(preset,level))continue;
  std::set<uint32_t> panel;
  for(const auto& g:preset.model_variant_groups)if(g.parent_group_index==UINT32_MAX)for(const auto& v:g.variants){
   const auto retain=[&](uint32_t model){if(auto it=runtime->definitions.find(model);it!=runtime->definitions.end())for(auto id:it->second.technology_panel_ids)if(id&&id<256)panel.insert(id);};
   retain(v.model_definition_id);for(const auto& replacement:v.faction_technology_replacements)retain(replacement.model_definition_id);
  }
  for(uint32_t id:panel){
   auto mask=FactionTechnologyMask256{};mask.fill(UINT32_MAX);set_faction_technology_unlocked(mask,id,false);
   auto hq=seed_for(*catalog.army_preset(330),catalog,mask);hq.faction_index=1;hq.actor.position={20,20,0};
   auto source=seed_for(preset,catalog,mask);source.faction_index=1;source.actor.position={30,20,0};
   std::vector<SimulationLabSeedEntity> seeds{hq};if(preset.registry_id==330)seeds[0]=source;else seeds.push_back(source);
   std::array<FactionTechnologyMask256,8> masks{};masks[1]=mask;
   std::array<SimulationLabFactionEconomySeed,8> economy{};economy[1].xenite_current_q4=1000000;economy[1].baseline_xenite_storage_limit_q4=1000000;economy[1].baseline_energy_supply_q4=1000000;
   SimulationLabScenarioRuntimeSeed scenario;scenario.runtime_catalog=runtime;
   SimulationLabConfig config;config.run_count=1;config.duration_ticks=1024;config.parameter_index=100;config.sweep_mode=SimulationLabSweepMode::Fixed;config.fast_build_cheat=true;
   std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};ki[0x0D]=4096;
   SimulationLabExperiment lab;lab.reset(config,ki,seeds,economy,products,technologies,masks,{},scenario);++checked;
   if(faction_technology_unlocked(lab.runs()[0].faction_technology_masks[1],id)){++granted;std::cout<<"PANEL source="<<preset.registry_id<<" tech="<<id<<" creation_granted=1\n";continue;}
   lab.step(field,74);const auto& trace=lab.runs()[0].ai_research_trace[1];
   const SimulationLabRun::ResearchCandidateTrace* observed=nullptr;
   for(uint32_t i=0;i<trace.candidate_count;++i)if(trace.candidates[i].technology_id==id&&trace.candidates[i].source_army_id==preset.registry_id){observed=&trace.candidates[i];break;}
   const uint32_t score=observed?observed->score:0,reason=observed?observed->reason:99,kind=observed?observed->score_kind:99;
   const auto queued=lab.runs()[0].metrics.research_jobs_queued;
   if(score){++positive;if(!queued)throw std::runtime_error("positive single locked stock technology could not be assigned");
    const auto duration=technologies[id].research_duration_ticks;const auto ticks=(duration>>4)+8;
    if(ticks>950)throw std::runtime_error("fixture duration exceeds bounded contract");lab.step(field,ticks);
    if(!faction_technology_unlocked(lab.runs()[0].faction_technology_masks[1],id))throw std::runtime_error("funded stock technology failed completion");++completed;
   }else ++blocked;
   std::cout<<"PANEL source="<<preset.registry_id<<" tech="<<id<<" kind="<<kind<<" score="<<score<<" reason="<<reason<<" queued="<<queued<<" completed="<<lab.runs()[0].metrics.research_jobs_completed<<'\n';
  }
 }
 std::cout<<"V636_STOCK_RESEARCH_AUDIT PASS technologies="<<technologies.size()<<" changed_lookup="<<changed<<" panel_cases="<<checked<<" positive="<<positive<<" completed="<<completed<<" blocked="<<blocked<<" creation_granted="<<granted<<" controlled_all_other_tech_unlocked=1 ordinary_progression=0\n";
 if(checked<50||completed<20)throw std::runtime_error("research matrix did not cover stock panels");
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
