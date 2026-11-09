#include "thandor/simulation_lab.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
SimulationLabSeedEntity source(uint32_t arm,std::vector<uint32_t> panel){
 SimulationLabSeedEntity s;s.army_asset_id=arm;s.faction_index=1;s.building=true;s.runtime_model_class_id=11;
 s.root_model_maximum_integrity=1000;s.actor.position={10,10,0};s.research_technology_ids=std::move(panel);return s;
}
SimulationLabTechnologyAsset technology(uint32_t id,int score=1){SimulationLabTechnologyAsset t;t.technology_id=id;t.dependency_technology_id=id;t.ai_base_candidate_score=score;t.research_duration_ticks=20;return t;}
SimulationLabExperiment reset(bool native,std::span<const SimulationLabSeedEntity> seeds,std::span<const SimulationLabTechnologyAsset> techs){
 SimulationLabConfig c;c.run_count=1;c.duration_ticks=256;c.parameter_index=100;c.sweep_mode=SimulationLabSweepMode::Fixed;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};ki[0x0D]=4096;
 std::array<FactionTechnologyMask256,8> masks{};set_faction_technology_unlocked(masks[1],0,true);
 std::array<SimulationLabFactionEconomySeed,8> economy{};economy[1].xenite_current_q4=10000;economy[1].baseline_xenite_storage_limit_q4=10000;economy[1].baseline_energy_supply_q4=10000;
 SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=native;scenario.assignable_faction_count=scenario.active_faction_count=1;scenario.simulate_all_active_factions_as_ai=true;
 SimulationLabExperiment lab;lab.reset(c,ki,seeds,economy,{},techs,masks,{},scenario);return lab;
}
}
int main()try{
 const auto field=FieldGrid::create(59,59,0,0);
 for(bool native:{false,true}){
  std::vector<uint32_t> older,newer;std::vector<SimulationLabTechnologyAsset> techs;
  for(uint32_t id=6;id<34;++id){older.push_back(id);techs.push_back(technology(id));}
  for(uint32_t id=100;id<107;++id){newer.push_back(id);techs.push_back(technology(id,id==100?10000:1));}
  const std::array seeds{source(330,older),source(321,newer)};auto lab=reset(native,seeds,techs);lab.step(field,74);
  const auto& run=lab.runs()[0];const auto& trace=run.ai_research_trace[1];
  std::cout<<"ORDER native="<<native<<" best="<<trace.best_technology_id<<" score="<<trace.best_score<<" admitted="<<trace.admitted_candidates<<" first_source="<<trace.candidates[0].source_entity_index<<'\n';
  check(trace.admitted_candidates==32,"native research cap changed");
  check(trace.best_technology_id==(native?100U:33U),"new lab starved by oldest-first capped collection");
  if(native)check(trace.candidates[0].source_entity_index==1&&trace.candidates[0].panel_slot==7,"newest source / descending serialized slot not traced");
  check(run.entities[native?1:0].research_pending_payment,"purchase used wrong chosen technology source");
 }
 // Same technology in two panels: first source in owner order receives it.
 for(bool native:{false,true}){const std::array seeds{source(330,{6}),source(321,{6})};const auto t=technology(6);auto lab=reset(native,seeds,std::span(&t,1));lab.step(field,74);
  check(lab.runs()[0].entities[native?1:0].research_pending_payment&&!lab.runs()[0].entities[native?0:1].research_pending_payment,"executor source order differs from candidate collection");}
 // Payment also follows the owner walk; one balance can start only one job.
 for(bool native:{false,true}){const std::array seeds{source(330,{}),source(321,{})};const std::array techs{technology(6),technology(7)};auto lab=reset(native,seeds,techs);auto& run=lab.runs()[0];run.faction_economy[1].xenite_current_q4=48;
  for(size_t i=0;i<2;++i){auto& e=run.entities[i];e.research_pending_payment=true;e.research_technology_id=6+i;e.research_xenite_due_q4=48;e.research_duration_ticks=100;}
  lab.step(field,1);check(run.entities[native?1:0].research_active&&run.entities[native?0:1].research_pending_payment,"pending payment consumed Xenite in wrong owner order");
  check(run.faction_economy[1].xenite_current_q4==0&&run.metrics.research_jobs_started==1,"research spent one balance more than once");}
 std::cout<<"V636_RESEARCH_OWNER_ORDER PASS; capped new-lab admission, ties, executor and payment\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
