#include "thandor/simulation_lab.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
struct Options {bool root_candidate{},root_unlocked{true},category_unlocked{true},prerequisite{true},resolved{true};};
SimulationLabExperiment fixture(Options o){
 SimulationLabSeedEntity hq;hq.army_asset_id=330;hq.faction_index=1;hq.building=true;hq.runtime_model_class_id=11;hq.root_model_maximum_integrity=1000;hq.actor.position={10,10,0};
 auto tower=hq;tower.army_asset_id=390;tower.runtime_model_class_id=4;tower.actor.position={14,10,0};
 const uint32_t id=o.root_candidate?206:99;tower.research_technology_ids={id};
 SimulationLabTechnologyAsset target;target.technology_id=id;target.dependency_technology_id=id;target.research_duration_ticks=2;target.ai_base_candidate_score=240;target.category=3;target.compatible_model_resolved=true;target.compatible_model_definition_id=o.root_candidate?322:225;target.compatible_model_runtime_class_id=o.root_candidate?4:7;
 if(!o.prerequisite)target.prerequisite_masks[0]=1U<<5;
 SimulationLabTechnologyAsset category;category.technology_id=208;category.category=3;
 SimulationLabProductionAsset product;product.army_asset_id=351;product.army_runtime_flags14_raw=0x11;
 product.entity_seed.army_asset_id=351;product.entity_seed.runtime_model_class_id=4;
 product.ai_faction_score_seeds_prepared=true;product.ai_baseline_root_definition_value60=1000;
 product.ai_baseline_root_category_maximum_index5c=1;product.ai_root_first_variant_required_technology_bit=206;
 product.ai_linked_model_definition_ids[0]={322};product.ai_linked_model_definition_ids[1]={225};
 auto& seed=product.ai_faction_score_seeds[1];seed.resolved=o.resolved;seed.all_selected_model_technologies_unlocked=false;
 seed.root_definition_value60=1000;seed.root_definition_value18=1;seed.root_category_maximum_index5c=1;
 std::array<FactionTechnologyMask256,8> masks{};set_faction_technology_unlocked(masks[1],0,true);
 if(o.root_unlocked&&!o.root_candidate)set_faction_technology_unlocked(masks[1],206,true);
 if(o.category_unlocked)set_faction_technology_unlocked(masks[1],208,true);
 std::array<SimulationLabFactionEconomySeed,8> economy{};economy[1].xenite_current_q4=60000;economy[1].baseline_xenite_storage_limit_q4=60000;economy[1].baseline_energy_supply_q4=60000;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};ki[0x0D]=4096;
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=128;config.parameter_index=100;config.sweep_mode=SimulationLabSweepMode::Fixed;
 const std::array seeds{hq,tower};const std::array technologies{target,category};SimulationLabExperiment lab;
 lab.reset(config,ki,seeds,economy,std::span(&product,1),technologies,masks,{},{});return lab;
}
int main()try{
 const auto field=FieldGrid::create(59,59,0,0);
 for(bool root:{false,true}){
  auto lab=fixture({.root_candidate=root});lab.step(field,74);const auto& run=lab.runs()[0];
  const uint32_t id=root?206:99;
  const auto& t=run.ai_research_trace[1];std::cout<<"TRACE tick="<<run.tick<<" sources="<<t.source_count<<" arms="<<t.arms_factory_present<<" admitted="<<t.admitted_candidates<<" positive="<<t.positive_candidates<<" best="<<t.best_technology_id<<" score="<<t.best_score<<" workspace11="<<run.ai_army_profile_trace[1].workspace11_count<<'\n';
  for(uint32_t ci=0;ci<t.candidate_count;++ci)std::cout<<" CANDIDATE tech="<<t.candidates[ci].technology_id<<" score="<<t.candidates[ci].score<<" reason="<<t.candidates[ci].reason<<'\n';
  check(run.ai_research_trace[1].best_technology_id==id&&run.ai_research_trace[1].best_score>0,"locked future definition received no research score");
  check(run.ai_army_profile_trace[1].workspace11_count==0,"research scoring made a locked asset purchasable");
  check(run.metrics.research_jobs_queued==1,"future upgrade was not assigned to its source");
  check(!faction_technology_unlocked(run.faction_technology_masks[1],id),"planning unlocked unpaid/uncompleted technology");
  std::cout<<"RESEARCH_UNLOCK tech="<<id<<" score="<<run.ai_research_trace[1].best_score<<" workspace11="<<run.ai_army_profile_trace[1].workspace11_count<<'\n';
  lab.step(field,5);check(faction_technology_unlocked(lab.runs()[0].faction_technology_masks[1],id),"assigned future upgrade failed completion");
 }
 for(const auto options:{Options{.root_unlocked=false},Options{.category_unlocked=false},Options{.prerequisite=false},Options{.resolved=false}}){
  auto lab=fixture(options);lab.step(field,74);const auto& run=lab.runs()[0];
  check(run.metrics.research_jobs_queued==0&&run.ai_research_trace[1].best_score==0,"research bypassed a root/category/prerequisite/resolution gate");
  check(run.ai_army_profile_trace[1].workspace11_count==0,"locked purchase eligibility changed");
 }
 std::cout<<"V635_RESEARCH_UNLOCK PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
