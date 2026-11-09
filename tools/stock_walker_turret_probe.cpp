#include <cmath>
#define main v634_stock_replay_unused_main
#include "simlab_stock_replay.cpp"
#undef main
int main(int argc,char** argv)try{
 if(argc!=2)throw std::runtime_error("usage: probe game_assets");
 const auto catalog=AssetCatalog::scan_archive_root(argv[1]);
 const auto runtime=simulation_lab_prepare_runtime_catalog(catalog);
 const auto technologies=simulation_lab_prepare_technologies(catalog);
 for(const auto& preset:catalog.army_presets()){
  if(preset.registry_id<340||preset.registry_id>377)continue;
  std::cout<<"TURRET arm="<<preset.registry_id<<" name="<<preset.localized_name<<'\n';
  for(const auto& group:preset.model_variant_groups)for(const auto& v:group.variants){
   std::cout<<" VARIANT parent="<<group.parent_group_index<<" slot="<<group.child_slot<<" mdl="<<v.model_definition_id<<" tech="<<v.technology_unlock_id<<" title="<<v.technology_title<<'\n';
   for(const auto& rep:v.faction_technology_replacements)std::cout<<" REPLACEMENT mdl="<<rep.model_definition_id<<" tech="<<rep.technology_unlock_id<<" title="<<rep.technology_title<<'\n';
   for(const auto& t:technologies)if(t.technology_id==v.technology_unlock_id)
    std::cout<<" TEC id="<<t.technology_id<<" base="<<t.ai_base_candidate_score<<" category="<<t.category<<" compatible="<<t.compatible_model_definition_id<<" class="<<t.compatible_model_runtime_class_id<<'\n';
  }
 }

 for(const auto& p:catalog.army_presets())if(p.registry_id>=340&&p.registry_id<370)
  std::cout<<"DEFENSE_ASSET arm="<<p.registry_id<<" name="<<p.localized_name<<" class="<<p.runtime_model_class_id<<'\n';
 for(bool all:{false,true}){
  FactionTechnologyMask256 mask{};if(all)mask.fill(UINT32_MAX);
  for(uint32_t id:{370U,374U,375U,376U,377U}){
   const auto hierarchy=resolve_faction_technology_hierarchy(*catalog.army_preset(id),mask);
   for(const auto& n:hierarchy)if(n.runtime_class_id==7||n.runtime_class_id==9){
    const auto sprite=runtime->sprites.find(n.sprite_path);
    std::cout<<"WEAPON_METADATA arm="<<id<<" unlocked="<<all<<" mdl="<<n.model_definition_id<<" shot="<<n.simulation.shot_definition_id
     <<" reload="<<n.simulation.reload_ticks<<" has_definition="<<n.simulation.has_weapon_definition()
     <<" launches="<<(sprite==runtime->sprites.end()?0:sprite->second.launch_geometry.first)<<'\n';
   }
  }
 }
 const auto field=FieldGrid::create(100,100,0,0);const auto cell=field.at(50,50);
 for(bool unlocked:{false,true})for(uint32_t unit:{110U,115U,120U,375U}){
  const auto* preset=catalog.army_preset(unit);if(!preset)continue;
  FactionTechnologyMask256 mask{};if(unlocked)mask.fill(UINT32_MAX);
  std::array<FactionTechnologyMask256,8> masks{};masks[1]=masks[2]=mask;
  auto source=seed_for(*preset,catalog,mask);source.faction_index=1;source.source_placement_index=SIZE_MAX;
  source.actor.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
  auto target=seed_for(*catalog.army_preset(331),catalog,mask);target.faction_index=2;target.source_placement_index=1;
  target.actor.position=source.actor.position;target.actor.position.x+=3;
  SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=scenario.assignable_faction_count=2;scenario.simulate_all_active_factions_as_ai=true;scenario.runtime_catalog=runtime;
  SimulationLabConfig config;config.run_count=1;config.duration_ticks=256;
  std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
  const std::array seeds{source,target};SimulationLabExperiment lab;lab.reset(config,parameters,seeds,{},{},{},masks,{},scenario);
  auto& run=lab.runs()[0];run.entities[0].military_ai_source_dispatch=true;
  for(unsigned i=0;i<256;++i)lab.step(field,1);
  const auto& actor=run.entities[0];
  std::cout<<"COMBAT arm="<<unit<<" unlocked="<<unlocked<<" mdl="<<actor.model_components[actor.root_model_component_index].model_definition_id
   <<" contact="<<actor.articulated_contact_geometry_enabled<<" initialized="<<actor.actor.articulated_contacts_initialized
   <<" volleys="<<actor.weapon_volleys_fired<<" damage="<<run.metrics.projectile_damage_applied<<" z="<<actor.actor.position.z<<'\n';
  if(actor.runtime_model_class_id==3U&&(!actor.articulated_contact_geometry_enabled||!actor.actor.articulated_contacts_initialized))throw std::runtime_error("stock walker contacts not prepared");
  if(!actor.weapon_volleys_fired)throw std::runtime_error("stock walker/laser turret failed to fire bounded ground fixture");
 }

 // Real production path: paid stock Emu, untouched stock timer/exit geometry.
 FactionTechnologyMask256 all{};all.fill(UINT32_MAX);
 const auto* emu=catalog.army_preset(115);
 const ArmyAssetPreset* factory=nullptr;
 for(const auto& p:catalog.army_presets())if(p.registry_id>=302&&p.registry_id<=304&&
     ai_v601_producer_accepts(13,p.runtime_class_specific_c4_raw,emu->army_runtime_flags14_raw)){factory=&p;break;}
 if(!factory)throw std::runtime_error("no stock Emu producer");
 auto seed=seed_for(*factory,catalog,all);seed.faction_index=1;
 seed.actor.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
 SimulationLabProductionAsset product;product.army_asset_id=115;product.army_runtime_flags14_raw=emu->army_runtime_flags14_raw;
 product.xenite_cost_q4=emu->retail_xenite_cost_q4;product.build_duration_ticks=emu->retail_build_time_q5;
 product.entity_seed=seed_for(*emu,catalog,all);simulation_lab_prepare_ai_product(*emu,catalog,product);
 std::array<FactionTechnologyMask256,8> masks{};masks[1]=all;
 std::array<SimulationLabFactionEconomySeed,8> economy{};economy[1].xenite_current_q4=product.xenite_cost_q4;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=2048;
 SimulationLabScenarioRuntimeSeed scenario;scenario.runtime_catalog=runtime;
 SimulationLabExperiment production;production.reset(config,ki,std::span(&seed,1),economy,std::span(&product,1),{},masks,{},scenario);
 auto& run=production.runs()[0];run.secondary_purchase_requests.push_back({115,1,0,0});
 for(uint32_t t=0;t<2048;++t){production.step(field,1);
  if(run.entities.size()==2&&!run.entities[1].production_linked_to_parent&&run.entities[0].production_behavior_state==0)break;}
 if(run.entities.size()!=2)throw std::runtime_error("Emu not produced in bounded stock timer fixture");
 const auto& child=run.entities[1];
 std::cout<<"PRODUCED_EMU factory="<<factory->registry_id<<" tick="<<run.tick<<" linked="<<child.production_linked_to_parent
  <<" contacts="<<child.articulated_contact_geometry_enabled<<" contact_serial="<<child.actor.articulated_contact_event_serial
  <<" distance="<<std::hypot(child.actor.position.x-seed.actor.position.x,child.actor.position.y-seed.actor.position.y)<<'\n';
 if(child.production_linked_to_parent||!child.articulated_contact_geometry_enabled||!child.actor.articulated_contact_event_serial)
  throw std::runtime_error("produced Emu failed animated factory exit");
 std::cout<<"V634_STOCK_WALKER_TURRET PASS; 256 tick fixtures, isolated combat, not autonomous attack or GPU proof\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
