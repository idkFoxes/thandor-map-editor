#include "thandor/simulation_lab.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main()try{
 const auto field=FieldGrid::create(40,40,0,0);
 std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=64;config.sweep_mode=SimulationLabSweepMode::Fixed;
 for(const uint32_t cls:{11U,13U,22U})for(uint32_t state=0;state<5;++state){
  SimulationLabSeedEntity seed;seed.army_asset_id=301;seed.faction_index=1;seed.building=true;
  seed.runtime_model_class_id=cls;seed.production_compatibility_mask=0x04;seed.production_storage_capacity=1;
  seed.actor.position={10,10,10};seed.root_model_maximum_integrity=1000;
  SimulationLabProductionAsset product;product.army_asset_id=2;product.army_runtime_flags14_raw=cls==11?0x10U:cls==22?0x08U:0x04U;
  product.xenite_cost_q4=10;product.build_duration_ticks=16;product.entity_seed.army_asset_id=2;product.entity_seed.runtime_model_class_id=2;
  std::array<SimulationLabFactionEconomySeed,8> economy{};economy[1].xenite_current_q4=100;
  // Keep research maintenance enabled without requiring the AI scheduler or stock assets.
  SimulationLabTechnologyAsset technology;technology.technology_id=6;technology.dependency_technology_id=6;
  SimulationLabExperiment lab;lab.reset(config,parameters,std::span(&seed,1),economy,std::span(&product,1),std::span(&technology,1),{},{});
  auto& run=lab.runs()[0];auto& producer=run.entities[0];
  if(state==0){producer.research_active=true;producer.research_duration_ticks=2;producer.research_technology_id=6;}
  if(state==1){producer.research_pending_payment=true;producer.research_duration_ticks=2;producer.research_technology_id=6;producer.research_xenite_due_q4=30;}
  if(state==2)producer.model_components[producer.root_model_component_index].runtime_flags_ec|=1;
  if(state==3)producer.runtime_removed=true;
  run.secondary_purchase_requests.push_back({2,1,0,0});lab.step(field,1);
  if(state==4){check(producer.production_building,"idle producer no longer starts compatible production");continue;}
  check(!producer.production_building&&run.secondary_purchase_requests.size()==1,"busy, unpowered or removed producer consumed queued production");
  check(run.faction_economy[1].xenite_current_q4==(state==1?70U:100U),"blocked production spent Xenite");
  if(state<=1){check(producer.research_active,"research did not progress independently of the blocked queue");
   lab.step(field,4);check(run.metrics.research_jobs_completed==1,"research failed to complete");
   check(producer.production_building&&run.secondary_purchase_requests.empty(),"producer failed to resume queued production after research");
   check(run.faction_economy[1].xenite_current_q4==(state==1?60U:90U),"resumed production did not charge exactly once");}
 }
 std::cout<<"V617_PRODUCER_RESEARCH_EXCLUSION PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
