#define main v638_stock_replay_unused_main
#include "simlab_stock_replay.cpp"
#undef main
int main(int argc,char** argv)try{
 if(argc<2||argc>3||(argc==3&&std::string(argv[2])!="--without-tracked-factory"))throw std::runtime_error("usage: probe GAME_ROOT [--without-tracked-factory]");
 const bool factory=argc==2;const auto catalog=AssetCatalog::scan_archive_root(argv[1]);const auto runtime=simulation_lab_prepare_runtime_catalog(catalog);
 const auto group=std::find_if(catalog.map_groups().begin(),catalog.map_groups().end(),[](const auto& g){return g.level_path=="level/asgard.lev";});if(group==catalog.map_groups().end())throw std::runtime_error("Asgard missing");const auto level=LevelAsset::parse(catalog.load(group->level_path));const auto techs=simulation_lab_prepare_technologies(catalog,&level);
 std::vector<SimulationLabProductionAsset> products;for(const auto& p:catalog.army_presets())if(p.registry_id){SimulationLabProductionAsset a;a.army_asset_id=p.registry_id;a.army_runtime_flags14_raw=p.army_runtime_flags14_raw;a.ai_definition_class_value70=p.ai_definition_class_value70;a.ai_definition_class_value74=p.ai_definition_class_value74;a.ai_definition_class_value78=p.ai_definition_class_value78;a.entity_seed=seed_for(p,catalog,{});simulation_lab_prepare_ai_product(p,catalog,a);products.push_back(std::move(a));}
 FactionTechnologyMask256 mask{};for(auto id:{0U,1U})set_faction_technology_unlocked(mask,id,true);std::vector<SimulationLabSeedEntity> seeds;
 for(auto id:{330U,302U,321U}){if(id==302&&!factory)continue;auto s=seed_for(*catalog.army_preset(id),catalog,mask);s.faction_index=1;s.actor.position={20+float(seeds.size())*10,20,0};seeds.push_back(std::move(s));}
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=4096;config.parameter_index=100;config.sweep_mode=SimulationLabSweepMode::Fixed;config.fast_build_cheat=true;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};ki[0x0D]=4096;std::array<FactionTechnologyMask256,8> masks{};masks[1]=mask;
 std::array<SimulationLabFactionEconomySeed,8> economy{};economy[1].xenite_current_q4=10000000;economy[1].baseline_xenite_storage_limit_q4=10000000;economy[1].baseline_energy_supply_q4=10000000;
 SimulationLabScenarioRuntimeSeed scenario;scenario.runtime_catalog=runtime;scenario.enabled=true;scenario.active_faction_count=scenario.assignable_faction_count=1;scenario.simulate_all_active_factions_as_ai=true;
 SimulationLabExperiment lab;lab.reset(config,ki,seeds,economy,products,techs,masks,{},scenario);const auto field=FieldGrid::create(100,100,0,0);auto previous=lab.runs()[0].faction_technology_masks[1];
 std::cout<<"CHAIN_INPUT factory="<<factory<<" manually_unlocked=0,1 creation_grants=";for(uint32_t id=2;id<256;++id)if(faction_technology_unlocked(previous,id))std::cout<<id<<',';std::cout<<" supplied_structures=330,"<<(factory?"302,":"")<<"321 supplied_funds_and_power=1 fast_build=1 research_only_KI=1 ordinary_building_acquisition=0\n";
 uint32_t previous_jobs=0;
 for(uint32_t tick=0;tick<4096;++tick){lab.step(field,1);const auto& r=lab.runs()[0];if(r.metrics.research_jobs_completed==previous_jobs)continue;previous_jobs=r.metrics.research_jobs_completed;
  for(uint32_t id=0;id<256;++id)if(!faction_technology_unlocked(previous,id)&&faction_technology_unlocked(r.faction_technology_masks[1],id))std::cout<<"CHAIN_UNLOCK tick="<<r.tick<<" tech="<<id<<" completed_jobs="<<previous_jobs<<'\n';previous=r.faction_technology_masks[1];
 }
 const auto& run=lab.runs()[0];std::cout<<"CHAIN_SUMMARY factory="<<factory<<" jobs="<<run.metrics.research_jobs_completed;
 for(auto id:{15U,18U,21U,93U,96U,99U,102U,105U,108U})std::cout<<" tech"<<id<<'='<<faction_technology_unlocked(run.faction_technology_masks[1],id);
 std::cout<<" mask=";for(const auto word:run.faction_technology_masks[1])std::cout<<word<<',';std::cout<<'\n';
 if(factory)for(auto id:{15U,18U,21U,96U,99U,105U,108U})if(!faction_technology_unlocked(run.faction_technology_masks[1],id))throw std::runtime_error("funded tracked-factory research chain did not complete");
 if(!factory)for(auto id:{15U,18U,21U,96U,99U,105U,108U})if(faction_technology_unlocked(run.faction_technology_masks[1],id))throw std::runtime_error("negative control bypassed missing carrier/source");
 std::cout<<"V638_RESEARCH_ACQUISITION PASS ticks=4096 supplied_sources_not_a_full_match=1\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
