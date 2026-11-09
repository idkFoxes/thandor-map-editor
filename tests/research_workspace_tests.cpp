#include "thandor/simulation_lab_ai_workspaces.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
SimulationLabExperiment make_lab(uint32_t roots=1) {
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=200;config.parameter_index=100;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};ki[13]=4096;
 SimulationLabSeedEntity source;source.army_asset_id=330;source.faction_index=1;
 source.runtime_model_class_id=13;source.building=true;source.root_model_maximum_integrity=1000;
 source.actor.position={10,10,0};source.research_technology_ids={7};
 std::vector<SimulationLabSeedEntity> seeds(roots,source);
 for(uint32_t i=0;i+1<roots;++i){seeds[i].army_asset_id=390;seeds[i].research_technology_ids.clear();}
 SimulationLabTechnologyAsset tech;tech.technology_id=tech.dependency_technology_id=7;
 tech.ai_base_candidate_score=256;tech.research_duration_ticks=10000;tech.xenite_cost_q4=400;
 tech.compatible_model_lookup_complete=tech.compatible_model_resolved=true;
 tech.compatible_model_definition_id=1007;tech.compatible_model_runtime_class_id=1;
 std::array<FactionTechnologyMask256,8> masks{};set_faction_technology_unlocked(masks[1],0,true);
 std::array<SimulationLabFactionEconomySeed,8> economy{};
 economy[1].xenite_current_q4=economy[1].baseline_xenite_storage_limit_q4=10000;
 SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=1;
 SimulationLabExperiment lab;lab.reset(config,ki,seeds,economy,{},std::span(&tech,1),masks,{},scenario);
 lab.runs()[0].tick=73;return lab;
}
}
int main()try {
 const auto field=FieldGrid::create(59,59,0,0);
 // Null-runtime primary, secondary and in-production entries all have presence.
 uint32_t checks=0;
 for(uint32_t kind=0;kind<3;++kind)for(uint32_t id=302;id<=306;++id) {
  auto lab=make_lab();auto& run=lab.runs()[0];
  if(kind==0)run.pending_asset_requests.push_back({id,1,0,0});
  if(kind==1)run.secondary_purchase_requests.push_back({id,1,0,0});
  if(kind==2){auto& producer=run.entities[0];producer.runtime_model_class_id=11;
   // Keep the research panel's native score kind 3 through its selected variant.
   SimulationLabResearchVariant variant;variant.variant_index=0;variant.panel.runtime_class_id=13;
   variant.panel.ids={7};producer.research_model_variants={variant};
   producer.production_building=true;producer.production_asset_id=id;producer.production_duration_ticks=10000;}
  lab.step(field,1);const auto& trace=run.ai_research_trace[1];
  check(trace.candidate_count==1,"presence fixture lost research panel");
  check(trace.candidates[0].score==(id<=304?0U:15000U),"queued/in-production factory did not affect ground research score");++checks;
 }
 {auto lab=make_lab(128);auto& run=lab.runs()[0];run.secondary_purchase_requests.push_back({302,1,0,0});lab.step(field,1);
  check(run.ai_research_trace[1].best_score==20000,"overflow factory incorrectly blocked ground research");++checks;}
 for(bool retained:{false,true}) {
  auto lab=make_lab(130);auto& run=lab.runs()[0];auto& job=run.entities[retained?128:0];
  job.runtime_flags=0x80;job.research_technology_id=7;lab.step(field,1);
  check(run.ai_research_trace[1].candidate_count==(retained?0U:1U),"availability ignored raw assigned flag or workspace cap");++checks;
 }
 {auto lab=make_lab(2);auto& run=lab.runs()[0];run.entities.back().construction_complete=false;lab.step(field,1);
  check(run.ai_research_trace[1].best_technology_id==7,"admitted runtime panel filtered by non-native construction predicate");++checks;}
 std::cout<<"V652_RESEARCH_WORKSPACE PASS cases="<<checks<<"; queued factory scoring, capacity, raw flags and live-panel admission\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
