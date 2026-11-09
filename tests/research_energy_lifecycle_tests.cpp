#include "thandor/simulation_lab.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
SimulationLabSeedEntity researcher(){SimulationLabSeedEntity s;s.army_asset_id=401;s.faction_index=1;s.building=true;s.runtime_model_class_id=11;
 s.actor.position={10,10,10};s.research_technology_ids={6};SimulationLabModelComponentSeed c;c.runtime_class_id=11;c.maximum_integrity=1000;c.retail_energy_demand_q4=16;s.model_components={c};return s;}
int main()try{
 const auto field=FieldGrid::create(40,40,0,0);auto seed=researcher();
 std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=128;config.sweep_mode=SimulationLabSweepMode::Fixed;
 SimulationLabTechnologyAsset technology;technology.technology_id=6;technology.dependency_technology_id=6;technology.ai_base_candidate_score=100;technology.research_duration_ticks=1000;
 SimulationLabProductionAsset product;product.army_asset_id=2;product.army_runtime_flags14_raw=0x10;product.build_duration_ticks=1000;product.energy_demand_q4=80;
 std::array<SimulationLabFactionEconomySeed,8> economy{};economy[1].derive_energy_allocation_from_components=true;
 economy[1].baseline_energy_supply_q4=64;economy[1].baseline_energy_generation_capacity_q4=1024;
 for(uint32_t job=0;job<2;++job){
  SimulationLabExperiment lab;lab.reset(config,parameters,std::span(&seed,1),economy,std::span(&product,1),std::span(&technology,1),{},{});
  auto& run=lab.runs()[0];auto& e=run.entities[0];auto& c=e.model_components[e.root_model_component_index];
  if(job==0){e.research_active=true;e.research_technology_id=6;e.research_duration_ticks=1000;e.research_energy_demand_q4=80;run.faction_active_research_energy_q4[1]=80;}
  else{e.production_building=true;e.production_asset_id=2;e.production_duration_ticks=1000;e.production_active_energy_demand_q4=80;run.faction_active_production_energy_q4[1]=80;}
  lab.step(field,8);
  check(run.faction_unpowered_energy_demand_q4[1]==96&&run.faction_supplied_energy_demand_q4[1]==0&&(c.runtime_flags_ec&1),"job load absent from root power allocation");
  const auto elapsed=job==0?e.research_elapsed_ticks:e.production_elapsed_ticks;lab.step(field,8);
  check((job==0?e.research_elapsed_ticks:e.production_elapsed_ticks)==elapsed,"unpowered job advanced");
  run.faction_economy[1].baseline_energy_supply_q4=256;lab.step(field,8);
  check(!(c.runtime_flags_ec&1)&&run.faction_supplied_energy_demand_q4[1]==96,"restored supply did not power combined load");
  if(job==0)e.research_duration_ticks=e.research_elapsed_ticks+1;else e.production_duration_ticks=e.production_elapsed_ticks+1;
  lab.step(field,8);
  // Class11 completion transfers its output to the primary inventory, holding 16 Q4 there.
  const uint32_t after_completion_demand=job==0?16U:32U;
  check(!e.research_active&&!e.production_building&&run.faction_supplied_energy_demand_q4[1]==after_completion_demand,"completed job retained load or charged it twice");
  check(run.faction_active_research_energy_q4[1]==0&&run.faction_active_production_energy_q4[1]==0,"completed job retained aggregate load");
 }
 for(uint32_t job=0;job<3;++job){
  SimulationLabExperiment lab;lab.reset(config,parameters,std::span(&seed,1),{},std::span(&product,1),std::span(&technology,1),{},{});
  auto& run=lab.runs()[0];auto& e=run.entities[0];e.model_components[e.root_model_component_index].destruction_complete_tick=1;
  if(job==0){e.research_active=true;e.research_duration_ticks=1000;e.research_energy_demand_q4=80;run.faction_active_research_energy_q4[1]=80;}
  if(job==1){e.production_building=true;e.production_asset_id=2;e.production_duration_ticks=1000;e.production_active_energy_demand_q4=80;run.faction_active_production_energy_q4[1]=80;}
  if(job==2){e.research_pending_payment=true;e.research_xenite_due_q4=30;}
  lab.step(field,1);check(e.runtime_removed&&!e.research_active&&!e.research_pending_payment&&!e.production_building,"released owner retained job state");
  check(run.faction_active_research_energy_q4[1]==0&&run.faction_active_production_energy_q4[1]==0,"released owner retained aggregate load");
  lab.step(field,8);check(run.faction_active_research_energy_q4[1]==0&&run.faction_active_production_energy_q4[1]==0,"owner release subtracted job load twice");
 }
 auto live=seed;live.source_placement_index=1;auto extractor=seed;extractor.army_asset_id=330;extractor.source_placement_index=2;extractor.research_technology_ids.clear();
 const std::array seeds{seed,live,extractor};parameters[0x0D]=4096;economy[1].derive_energy_allocation_from_components=false;economy[1].xenite_current_q4=100;
 SimulationLabExperiment lab;lab.reset(config,parameters,seeds,economy,{},std::span(&technology,1),{},{});auto& run=lab.runs()[0];
 run.entities[0].runtime_removed=true;run.entities[0].root_model_terminal=true;run.entities[0].research_active=true;run.entities[0].research_technology_id=6;
 lab.step(field,74);check(run.metrics.research_jobs_queued==1&&run.entities[1].research_pending_payment,"removed researcher permanently blocked technology reassignment");
 std::cout<<"V617_RESEARCH_ENERGY_LIFECYCLE PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
