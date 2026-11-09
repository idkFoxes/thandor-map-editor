#include "thandor/simulation_lab.hpp"
#include "thandor/strategic_factory_terrain_v600.hpp"
#include "../src/editor/simulation_lab_diagnostic_writer.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
SimulationLabSeedEntity seed(uint32_t id,uint32_t cls){SimulationLabSeedEntity s;s.army_asset_id=id;s.runtime_model_class_id=cls;s.faction_index=1;s.building=true;s.root_model_maximum_integrity=1000;s.actor.position={10+float(id%7),10,0};s.production_compatibility_mask=0x20;return s;}
SimulationLabProductionAsset product(uint32_t id,uint32_t flags,uint32_t cls){SimulationLabProductionAsset a;a.army_asset_id=id;a.army_runtime_flags14_raw=flags;a.xenite_cost_q4=10;a.build_duration_ticks=1000;a.entity_seed=seed(id,cls);a.entity_seed.building=id>=300;a.ai_faction_score_seeds_prepared=true;a.ai_definition_class_value74=50;a.ai_baseline_root_definition_value0c=64;a.ai_baseline_root_definition_value18=1;a.ai_baseline_root_definition_value60=1000;for(auto& s:a.ai_faction_score_seeds){s.resolved=s.all_selected_model_technologies_unlocked=true;s.root_definition_value0c=64;s.root_definition_value18=1;s.root_definition_value60=1000;}return a;}
SimulationLabConfig config(){SimulationLabConfig c;c.run_count=1;c.duration_ticks=256;c.parameter_index=100;c.sweep_mode=SimulationLabSweepMode::Fixed;return c;}
std::array<SimulationLabFactionEconomySeed,8> economy(){std::array<SimulationLabFactionEconomySeed,8> e{};e[1].xenite_current_q4=100000;e[1].baseline_xenite_storage_limit_q4=100000;return e;}
uint32_t rng(uint32_t& s){const uint32_t first=s*33U+101U;s=first*33U+101U;return (first<<14U)^(s>>2U);}
int32_t delta(uint32_t weight,uint32_t value){return int32_t(((weight*5+value%weight)<<4)&~15U);}
void order_test(){
 const auto field=FieldGrid::create(59,59,0,0);SimulationLabExperiment lab;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};ki[9]=101;ki[10]=23;ki[8]=211;
 std::array<FactionTechnologyMask256,8> masks{};set_faction_technology_unlocked(masks[1],1,true);
 const std::vector<SimulationLabSeedEntity> seeds{seed(300,11),seed(330,14),seed(301,13)};
 const std::vector<SimulationLabProductionAsset> products{product(2,0x21,2),product(302,0x11,13)};
 lab.reset(config(),ki,seeds,economy(),products,{},masks,{},{});lab.step(field,73);auto& r=lab.runs()[0];r.faction_score[1].explored_terrain_percent=0;
 auto expected=r.rng_state;const auto defense=rng(expected),explore=rng(expected),terrain=rng(expected),factory=rng(expected);rng(expected); // native pressure-family selector, no enabled lab
 const auto score2=(delta(75,defense)|1)+delta(71,explore);const auto score302=delta(211,factory)|1;
 lab.step(field,1);const auto& trace=r.ai_purchase_trace[1];
 const auto find=[&](uint32_t id)->const SimulationLabRun::PurchaseCandidateTrace*{for(uint32_t i=0;i<trace.recorded_count;++i)if(trace.candidates[i].id==id)return &trace.candidates[i];return nullptr;};
 check(trace.tick==74&&trace.recorded_count==2,"planning order fixture did not collect unit and factory");
 check(find(2)&&find(2)->packed_score==score2,"defense/exploration received the wrong RNG draws");
 check(find(302)&&find(302)->packed_score==score302,"factory weight ran before unit profiles");
 check(r.rng_state==expected,"planning callback order consumed extra/missing RNG draws");
 check(r.ai_strategic_construction_trace[1].tick==74&&r.ai_strategic_construction_trace[1].selected_factory==302,"strategic gate trace missing");
 const auto held=lab.snapshot_copy();const auto retained=held.runs()[0].ai_strategic_construction_trace[1];
 lab.step(field,4);check(held.runs()[0].ai_strategic_construction_trace[1].tick==retained.tick,"held strategic snapshot mutated");
 std::cout<<"ORDER native_call_sequence=defense,exploration,terrain,factory_weight,pressure_selector unit_score="<<score2<<" factory_score="<<score302<<" terrain_random="<<terrain<<" PASS\n";
 // Exercise the existing async formatter with the new gate fields.
 const auto path=std::filesystem::temp_directory_path()/("v639-strategy-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".log");
 {editor::SimulationLabDiagnosticWriter writer(path,"V639\n",field,{});editor::SimulationLabDiagnosticSample s;s.snapshot=std::make_shared<const SimulationLabExperiment>(held);s.active_factions=1;writer.append(s,true);writer.close("complete");}
 std::ifstream f(path);const std::string text((std::istreambuf_iterator<char>(f)),{});std::filesystem::remove(path);check(text.find("STRATEGIC_CONSTRUCTION")!=std::string::npos&&text.find("selected_factory=302")!=std::string::npos,"async strategic diagnostics missing");
}
void release_test(){
 const auto field=FieldGrid::create(59,59,0,0);std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};ki[7]=1000;
 for(int state=0;state<4;++state){SimulationLabExperiment lab;const std::vector<SimulationLabSeedEntity> seeds{seed(300,11),seed(330,14),seed(301,13)};const std::vector<SimulationLabProductionAsset> products{product(301,0x11,13)};lab.reset(config(),ki,seeds,economy(),products,{}, {},{},{});
  auto& r=lab.runs()[0];auto& old=r.entities[2];old.runtime_removed=state!=0;old.root_model_terminal=state!=0;old.root_model_integrity=state?0:1000;
  if(state==2)r.secondary_purchase_requests.push_back({301,1,0,0});
  if(state==3){auto& producer=r.entities[0];producer.production_building=true;producer.production_asset_id=301;producer.production_duration_ticks=1000;producer.production_elapsed_ticks=0;}
  lab.step(field,74);const bool selected=r.metrics.factory301_candidate_events!=0;
  check(selected==(state==1),"released factory suppresses replacement, or live/pending/producing duplicate gate lost");
  if(state==1)check(r.ai_purchase_trace[1].recorded_count&&r.ai_purchase_trace[1].candidates[0].id==301,"released factory replacement absent from actual purchase pass");
 }
 std::cout<<"RELEASE live=blocked released=replace pending=blocked producing=blocked PASS\n";
}
void pending_test(){
 const auto field=FieldGrid::create(59,59,0,0);std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};ki[7]=1000;ki[13]=1000;
 for(int state=0;state<4;++state){SimulationLabExperiment lab;auto source=seed(300,11);source.research_technology_ids={6};SimulationLabTechnologyAsset t;t.technology_id=t.dependency_technology_id=6;t.ai_base_candidate_score=100;t.research_duration_ticks=1000;
  const std::vector<SimulationLabSeedEntity> seeds{source};const std::vector<SimulationLabProductionAsset> products{product(301,0x11,13)};const std::vector<SimulationLabTechnologyAsset> techs{t};lab.reset(config(),ki,seeds,economy(),products,techs, {},{},{});lab.step(field,73);auto& r=lab.runs()[0];
  if(state==1)r.pending_asset_requests.push_back({330,1,0,0});
  if(state==2)r.secondary_purchase_requests.push_back({330,1,0,0});
  if(state==3)r.pending_asset_requests.push_back({330,2,0,0});
  lab.step(field,1);const bool own_pending=state==1||state==2;
  check(r.ai_research_trace[1].arms_factory_present==own_pending,"pending workspace ARM330 prerequisite omitted or foreign request admitted");
  check((r.ai_research_trace[1].best_technology_id==6)==own_pending,"pending ARM330 failed to admit research from a real source");
  check((r.metrics.factory301_candidate_events!=0)==own_pending,"pending ARM330 failed strategic building prerequisite");
 }
 std::cout<<"PENDING prerequisite none=blocked own_primary=admitted own_secondary=admitted foreign=blocked PASS\n";
}

}
int main(int argc,char** argv)try{const std::string mode=argc>1?argv[1]:"all";if(mode=="all"||mode=="--order-only")order_test();if(mode=="all"||mode=="--release-only")release_test();if(mode=="all"||mode=="--pending-only")pending_test();std::cout<<"V639_PLANNING_ORDER_RELEASE PASS\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
