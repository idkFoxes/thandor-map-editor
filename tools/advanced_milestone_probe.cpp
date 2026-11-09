#define main v640_replay_unused_main
#include "simlab_stock_replay.cpp"
#undef main
#include <map>
#include <set>
int main(int argc,char** argv)try{
 if(argc<2||argc>3||(argc==3&&std::string(argv[2])!="--funded"&&std::string(argv[2])!="--reinforcement"))throw std::runtime_error("usage: milestone GAME_ROOT [--funded|--reinforcement]");const bool funded=argc==3&&std::string(argv[2])=="--funded";const bool reinforce=argc==3&&std::string(argv[2])=="--reinforcement";bool reinforced=false;
 const auto catalog=AssetCatalog::scan_game_root(argv[1]);const auto runtime=simulation_lab_prepare_runtime_catalog(catalog);auto field=FieldGrid::create(120,100,0,0);FactionTechnologyMask256 mask{};mask[0]=3;
 const auto pos=[&](uint32_t x,uint32_t y){const auto& c=field.at(x,y);return Vec3f{q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),0};};
 std::vector<SimulationLabSeedEntity> seeds;for(const auto row:std::array<std::array<uint32_t,3>,6>{{{300,35,40},{330,50,40},{331,35,55},{332,65,55},{310,35,25},{301,50,25}}}){auto s=seed_for(*catalog.army_preset(row[0]),catalog,mask);s.faction_index=1;s.source_placement_index=seeds.size();s.actor.position=pos(row[1],row[2]);seeds.push_back(std::move(s));}
 for(auto x:{60U,75U,90U}){auto s=seed_for(*catalog.army_preset(300),catalog,mask);s.faction_index=2;s.source_placement_index=seeds.size();s.actor.position=pos(x,25);seeds.push_back(std::move(s));}
 for(uint32_t y=35;y<=45;++y)for(uint32_t x=45;x<=55;++x)field.at(x,y).flags_and_material|=0x800;
 for(uint32_t y=50;y<=60;++y)for(uint32_t x=60;x<=70;++x)field.at(x,y).flags_and_material|=0x1000;
 std::vector<SimulationLabProductionAsset> products;for(const auto& p:catalog.army_presets()){if(!p.registry_id||p.registry_id>=380)continue;SimulationLabProductionAsset a;a.army_asset_id=p.registry_id;a.army_runtime_flags14_raw=p.army_runtime_flags14_raw;a.ai_definition_class_value70=p.ai_definition_class_value70;a.ai_definition_class_value74=p.ai_definition_class_value74;a.ai_definition_class_value78=p.ai_definition_class_value78;a.runtime_class_specific_c0_raw=p.runtime_class_specific_c0_raw;a.xenite_cost_q4=p.retail_xenite_cost_q4;a.build_duration_ticks=p.retail_build_time_q5;a.energy_demand_q4=int32_t(p.retail_build_energy_q4);a.entity_seed=seed_for(p,catalog,{});simulation_lab_prepare_ai_product(p,catalog,a);products.push_back(std::move(a));}
 const auto ki=AiKnowledgeData::parse(catalog.load("engine/ki.dat"));auto config=simulation_lab_loaded_baseline_config(ki.parameters());config.duration_ticks=4096;
 std::array<SimulationLabFactionEconomySeed,8> economy{};auto& e=economy[1];e.xenite_current_q4=funded?200000:40000;e.tritium_current_q4=12000;e.derive_storage_limits_from_components=true;e.baseline_xenite_storage_limit_q4=funded?200000:4000;e.baseline_tritium_storage_limit_q4=4000;e.derive_energy_allocation_from_components=true;e.baseline_energy_supply_q4=e.baseline_energy_generation_capacity_q4=640;e.terrain_contribution_scale_q8=256;
 std::array<FactionTechnologyMask256,8> masks{};masks[1]=masks[2]=mask;SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.runtime_catalog=runtime;scenario.active_faction_count=scenario.assignable_faction_count=2;scenario.simulate_all_active_factions_as_ai=true;
 const auto group=std::find_if(catalog.map_groups().begin(),catalog.map_groups().end(),[](const auto& g){return g.level_path=="level/asgard.lev";});const auto level=LevelAsset::parse(catalog.load(group->level_path));
 const auto technologies=simulation_lab_prepare_technologies(catalog,&level);SimulationLabExperiment lab;lab.reset(config,ki.parameters(),seeds,economy,products,technologies,masks,{},scenario);
 const auto advanced=[&](uint32_t id){const auto* a=catalog.army_preset(id);if(!a||id>=300)return false;const auto cls=a->runtime_model_class_id;return cls==2||cls==3||cls==17||cls==19||cls==21;};
 std::map<std::string,uint32_t> milestones;std::set<uint32_t> observed;uint32_t prior_planning=0,prior_research=0;uint64_t event_sequence=0;
 const auto mark=[&](const std::string& name,uint32_t tick,uint32_t id){if(milestones.emplace(name,tick).second)std::cout<<"MILESTONE "<<name<<" tick="<<tick<<" arm="<<id<<'\n';};
 std::cout<<"INPUT scripted_nearby_reinforcement="<<reinforce<<" funded="<<funded<<" stock_KI=1 initial_advanced_factory=0 manual_advanced_technologies=0 fast_build=0 initial_base_structures=6 hostile_HQ=3 ticks_max=4096\n";
 for(uint32_t t=0;t<4096;++t){lab.step(field,1);const auto& r=lab.runs()[0];for(const auto& a:r.entities){if(a.faction_index!=1||a.runtime_removed)continue;
  if(a.synthetic&&a.construction_complete&&!a.root_model_terminal&&a.army_asset_id>=302&&a.army_asset_id<=306)mark("factory",r.tick,a.army_asset_id);
  if(a.synthetic&&advanced(a.army_asset_id)){mark("advanced_spawn",r.tick,a.army_asset_id);if(!a.production_linked_to_parent)mark("advanced_exit",r.tick,a.army_asset_id);if(a.target_entity_index)mark("advanced_target",r.tick,a.army_asset_id);if(a.weapon_volleys_fired)mark("advanced_fire",a.last_weapon_fire_tick,a.army_asset_id);}
 }
 if(reinforce&&!reinforced&&milestones.count("advanced_exit")){
  const auto it=std::find_if(r.entities.begin(),r.entities.end(),[&](const auto& a){return a.faction_index==1&&advanced(a.army_asset_id)&&!a.production_linked_to_parent;});
  if(it!=r.entities.end()){
   auto target=seed_for(*catalog.army_preset(331),catalog,mask);target.faction_index=2;target.source_placement_index=100000;target.actor.position=it->actor.position;target.actor.position.x+=3.5F;
   SimulationLabExperiment target_lab;std::array<uint32_t,AiKnowledgeData::kParameterCount> idle{};target_lab.reset(config,idle,std::span(&target,1),{}, {},{},masks,{},scenario);
   lab.runs()[0].entities.push_back(target_lab.runs()[0].entities[0]);reinforced=true;std::cout<<"REINFORCEMENT tick="<<r.tick<<" target=331 xy="<<target.actor.position.x<<','<<target.actor.position.y<<'\n';
  }
 }
 for(const auto& event:simulation_lab_one_shot_events_after(r,event_sequence)){event_sequence=std::max(event_sequence,event.sequence);if(event.faction_index==1&&event.kind==SimulationLabOneShotEventKind::WeaponLaunchEffect&&advanced(event.army_asset_id))mark("advanced_fire",r.tick,event.army_asset_id);}
 if(r.metrics.research_jobs_completed!=prior_research){prior_research=r.metrics.research_jobs_completed;std::cout<<"RESEARCH tick="<<r.tick<<" tech="<<r.ai_research_trace[1].last_completed_technology_id<<" jobs="<<prior_research<<'\n';}
 if(r.metrics.planning_cycles!=prior_planning){prior_planning=r.metrics.planning_cycles;const auto& s=r.ai_strategic_construction_trace[1];const auto& p=r.ai_purchase_trace[1];std::cout<<"PLAN tick="<<r.tick<<" cash="<<r.faction_economy[1].xenite_current_q4<<" capacity="<<r.faction_energy_generation_capacity_q4[1]<<" factory="<<s.selected_factory<<" cap_ok="<<s.factory_capacity_available<<" candidates="<<p.candidate_count;for(uint32_t i=0;i<std::min(p.recorded_count,3U);++i){const auto& c=p.candidates[i];std::cout<<" rank"<<i<<'='<<c.id<<':'<<c.cost_q4<<':'<<uint32_t(c.decision);}std::cout<<'\n';}
 }
 const auto& r=lab.runs()[0];std::cout<<"SUMMARY factories="<<milestones.count("factory")<<" spawn="<<milestones.count("advanced_spawn")<<" exit="<<milestones.count("advanced_exit")<<" target="<<milestones.count("advanced_target")<<" fire="<<milestones.count("advanced_fire")<<" research="<<r.metrics.research_jobs_completed<<" shots="<<r.metrics.weapon_shots_fired<<" damage="<<r.metrics.projectile_damage_applied<<" movement="<<r.metrics.military_movement_distance_world<<'\n';
 for(const auto& a:r.entities)if(a.faction_index==1&&advanced(a.army_asset_id))std::cout<<"ADVANCED arm="<<a.army_asset_id<<" linked="<<a.production_linked_to_parent<<" parent="<<a.production_parent_entity_index.value_or(SIZE_MAX)<<" flags="<<a.runtime_flags<<" target="<<a.target_entity_index.has_value()<<" weapons="<<a.combat_weapons.size()<<" moving="<<a.actor.move_target.has_value()<<" volleys="<<a.weapon_volleys_fired<<" in_range="<<a.actor.target_in_range<<" aligned="<<a.actor.target_aligned<<" command_flags="<<a.combat_command_mode_flags<<" generation="<<a.combat_command_generation<<" acquisitions="<<a.combat_command_acquisitions<<" retains="<<a.combat_command_retains<<" fallbacks="<<a.combat_command_fallbacks<<" military="<<a.military_ai_source_dispatch<<" xy="<<a.actor.position.x<<','<<a.actor.position.y<<'\n';
 if(!milestones.count("factory")||!milestones.count("advanced_spawn")||!milestones.count("advanced_exit")||r.metrics.research_jobs_completed<3)throw std::runtime_error("advanced acquisition/exit milestone failed within4096 callbacks");
 if(reinforce&&(!reinforced||!milestones.count("advanced_target")||!milestones.count("advanced_fire")))throw std::runtime_error("scripted reinforcement command/fire milestone failed");
 std::cout<<"V640_ADVANCED_MILESTONE PASS\n";
 std::cout<<"LIMIT supplied mature base and declared funds/storage, flat field; not a normal starting-pioneer match or GUI replay\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
