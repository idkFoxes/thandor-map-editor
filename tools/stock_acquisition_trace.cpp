#define main v639_stock_replay_unused_main
#include "simlab_stock_replay.cpp"
#undef main
int main(int argc,char** argv)try{
 if(argc<2)throw std::runtime_error("usage: probe game_assets [ticks<=4096] [--walker-ready]");
 const uint32_t ticks=argc>2?std::min(4096U,static_cast<uint32_t>(std::stoul(argv[2]))):400U;
 const auto catalog=AssetCatalog::scan_archive_root(argv[1]);const auto runtime=simulation_lab_prepare_runtime_catalog(catalog);
 const bool walker_ready=argc>3&&std::string(argv[3])=="--walker-ready";
 if(argc>3&&!walker_ready)throw std::runtime_error("unknown fixture option");
 auto field=FieldGrid::create(120,100,0,0);FactionTechnologyMask256 mask{};mask[0]=3;
 if(walker_ready)mask.fill(UINT32_MAX); // Declared test input, not research completion proof.
 const auto position=[&](uint32_t x,uint32_t y){const auto& c=field.at(x,y);return Vec3f{q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),0};};
 // Authored mature-base fixture. Stocks and structures are input seeds, never a gameplay bypass.
 std::vector<SimulationLabSeedEntity> seeds;
 for(const auto row:std::array<std::array<uint32_t,3>,6>{{{300,35,40},{330,50,40},{331,35,55},{332,65,55},{310,35,25},{301,50,25}}}){
  auto s=seed_for(*catalog.army_preset(row[0]),catalog,mask);s.faction_index=1;s.source_placement_index=seeds.size();s.actor.position=position(row[1],row[2]);seeds.push_back(std::move(s));}
 if(walker_ready){auto s=seed_for(*catalog.army_preset(303),catalog,mask);s.faction_index=1;
  s.source_placement_index=seeds.size();s.actor.position=position(80,25);seeds.push_back(std::move(s));}
 std::cout<<"FIXTURE walker_ready="<<walker_ready<<" fully_unlocked_input="<<walker_ready<<'\n';
 // Connected, sizeable resource regions give the real integer payout a nonzero yield.
 for(uint32_t y=35;y<=45;++y)for(uint32_t x=45;x<=55;++x)field.at(x,y).flags_and_material|=0x800;
 for(uint32_t y=50;y<=60;++y)for(uint32_t x=60;x<=70;++x)field.at(x,y).flags_and_material|=0x1000;
 std::vector<SimulationLabProductionAsset> products;
 for(const auto& p:catalog.army_presets()){
  if(p.registry_id>=380U)continue;SimulationLabProductionAsset a;
  a.army_asset_id=p.registry_id;a.army_runtime_flags14_raw=p.army_runtime_flags14_raw;
  a.ai_definition_class_value70=p.ai_definition_class_value70;a.ai_definition_class_value74=p.ai_definition_class_value74;a.ai_definition_class_value78=p.ai_definition_class_value78;
  a.runtime_class_specific_c0_raw=p.runtime_class_specific_c0_raw;a.xenite_cost_q4=p.retail_xenite_cost_q4;a.build_duration_ticks=p.retail_build_time_q5;
  a.energy_demand_q4=static_cast<int32_t>(p.retail_build_energy_q4);a.entity_seed=seed_for(p,catalog,{});simulation_lab_prepare_ai_product(p,catalog,a);products.push_back(a);
  if(p.registry_id>=300&&p.registry_id<=306)std::cout<<"ARM "<<p.registry_id<<" cost="<<a.xenite_cost_q4<<" ticks="<<a.build_duration_ticks<<" flags="<<a.army_runtime_flags14_raw<<'\n';
 }
 const auto ki=AiKnowledgeData::parse(catalog.load("engine/ki.dat"));auto config=simulation_lab_loaded_baseline_config(ki.parameters());config.duration_ticks=ticks;
 std::array<SimulationLabFactionEconomySeed,8> economy{};auto& e=economy[1];e.xenite_current_q4=40000;e.tritium_current_q4=12000;
 e.derive_storage_limits_from_components=true;e.baseline_xenite_storage_limit_q4=e.baseline_tritium_storage_limit_q4=4000;
 e.derive_energy_allocation_from_components=true;e.baseline_energy_supply_q4=e.baseline_energy_generation_capacity_q4=640;e.terrain_contribution_scale_q8=256;
 std::array<FactionTechnologyMask256,8> masks{};masks[1]=mask;SimulationLabScenarioRuntimeSeed scenario;
 scenario.enabled=true;scenario.runtime_catalog=runtime;scenario.active_faction_count=scenario.assignable_faction_count=1;scenario.simulate_all_active_factions_as_ai=true;
 SimulationLabExperiment lab;const auto technologies=simulation_lab_prepare_technologies(catalog);lab.reset(config,ki.parameters(),seeds,economy,products,technologies,masks,{},scenario);
 const auto start=std::chrono::steady_clock::now();uint32_t previous_plans=0,previous_research=0,previous_builds=0;
 for(uint32_t tick=0;tick<ticks;++tick){lab.step(field,1);const auto& r=lab.runs()[0];
  if(r.metrics.planning_cycles==previous_plans&&r.metrics.research_jobs_completed==previous_research&&r.metrics.completed_builds==previous_builds&&tick+1!=ticks)continue;
  previous_plans=r.metrics.planning_cycles;previous_research=r.metrics.research_jobs_completed;previous_builds=r.metrics.completed_builds;
  std::cout<<"TICK "<<r.tick<<" plans="<<previous_plans<<" built="<<previous_builds<<" entities="<<r.entities.size()<<" research="<<previous_research
   <<" xenite="<<r.faction_economy[1].xenite_current_q4<<" storage="<<r.faction_economy[1].xenite_storage_limit_q4.value_or(0)
   <<" xenite_rate="<<r.faction_xenite_extraction_rate_q4_per_tick[1]<<" tritium_rate="<<r.faction_tritium_extraction_rate_q4_per_tick[1]<<" capacity="<<r.faction_energy_generation_capacity_q4[1]
   <<" demand="<<r.faction_supplied_energy_demand_q4[1]+r.faction_unpowered_energy_demand_q4[1]<<" cache="<<r.ai_candidate_cache_count[1]
   <<" ws10="<<r.ai_workspace_diagnostics[1].large10<<" research_best="<<r.ai_research_trace[1].best_technology_id<<'\n';
  for(const auto& q:r.secondary_purchase_requests)std::cout<<" QUEUED "<<q.army_asset_id<<'\n';
  for(const auto& owner:r.entities){if(owner.runtime_model_class_id==13U)std::cout<<" FACTORY "<<owner.army_asset_id<<" phase="<<owner.production_behavior_state<<" door="<<owner.production_door_texture_offset_v<<" flags="<<owner.runtime_flags<<'\n';
   if(owner.production_linked_to_parent)std::cout<<" LINKED "<<owner.army_asset_id<<" xy="<<owner.actor.position.x<<","<<owner.actor.position.y<<" target="<<bool(owner.actor.move_target)<<" waypoint="<<bool(owner.actor.move_waypoint)<<'\n';
   if(owner.production_building)std::cout<<" BUILD "<<owner.army_asset_id<<"->"<<owner.production_asset_id<<" progress="<<owner.production_elapsed_ticks<<'/'<<owner.production_duration_ticks<<'\n';
   if(owner.research_active||owner.research_pending_payment)std::cout<<" RESEARCH "<<owner.army_asset_id<<" tech="<<owner.research_technology_id<<" progress="<<owner.research_elapsed_ticks<<'/'<<owner.research_duration_ticks<<'\n';}
  for(size_t i=0;i<r.ai_candidate_cache_count[1];++i){const auto& candidate=r.ai_candidate_cache[1][i];
   std::cout<<" CACHE "<<candidate.id<<" kind="<<candidate.kind<<" score="<<candidate.packed_score;
   for(const auto& asset:products)if(candidate.kind==1U&&asset.army_asset_id==candidate.id)std::cout<<" army_cost="<<asset.xenite_cost_q4;
   for(const auto& technology:technologies)if(candidate.kind==2U&&technology.technology_id==candidate.id)std::cout<<" tech_cost="<<technology.xenite_cost_q4;
   std::cout<<'\n';}
  const auto& strategy=r.ai_strategic_construction_trace[1];
  std::cout<<" STRATEGIC planning_tick="<<strategy.tick<<" has330="<<strategy.has_resource330<<" tec1="<<strategy.arms_factories_unlocked<<" factory_capacity="<<strategy.factory_capacity_available<<" lab_capacity="<<strategy.lab_capacity_available<<" selected_factory="<<strategy.selected_factory<<" existing="<<strategy.existing_factory_count<<" selected_lab="<<strategy.selected_lab<<'\n';
  const auto& trace=r.ai_purchase_trace[1];
  std::cout<<" PURCHASE_PASS tick="<<trace.tick<<" cached="<<trace.cached<<" count="<<trace.candidate_count<<" initial="<<trace.initial_budget_q4<<'\n';
  for(uint32_t i=0;i<trace.recorded_count;++i){const auto& c=trace.candidates[i];std::cout<<" PURCHASE rank="<<c.rank<<" id="<<c.id<<" kind="<<c.kind<<" score="<<c.packed_score<<" cost="<<c.cost_q4<<" before="<<c.budget_before_q4<<" after="<<c.budget_after_q4<<" decision="<<uint32_t(c.decision)<<'\n';}
  std::cout<<" GATES tech1="<<faction_technology_unlocked(r.faction_technology_masks[1],1)<<" capacity_add="<<ki.parameters()[0x11]<<" tech15="<<faction_technology_unlocked(r.faction_technology_masks[1],15)<<" tech21="<<faction_technology_unlocked(r.faction_technology_masks[1],21)<<'\n';
  const auto& blocked=r.ai_purchase_block_diagnostics[1];
  std::cout<<" AFFORDABILITY calls="<<blocked.calls<<" tick="<<blocked.last_tick<<" id="<<blocked.last_candidate_id
   <<" cost="<<blocked.last_cost_q4<<" remaining_planning_budget="<<blocked.last_balance_q4<<" storage="<<blocked.last_storage_cap_q4
   <<" cached="<<blocked.last_cached<<" reason="<<static_cast<uint32_t>(blocked.last_reason)<<'\n';
 }
 for(const auto& e:lab.runs()[0].entities)std::cout<<"FINAL_ENTITY army="<<e.army_asset_id<<" class="<<e.runtime_model_class_id<<" synthetic="<<e.synthetic<<" linked="<<e.production_linked_to_parent<<'\n';
 std::cout<<"CONTROLLED_MATURE_BASE elapsed="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" ticks="<<ticks<<" native_KI=1 full_match_proof=0\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
