#include "thandor/simulation_lab.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
SimulationLabExperiment make_lab(bool source=true,uint32_t faction=1) {
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=200;config.parameter_index=100;
 SimulationLabSeedEntity seed;seed.army_asset_id=330;seed.faction_index=faction;seed.building=true;seed.runtime_model_class_id=13;seed.root_model_maximum_integrity=1000;seed.research_technology_ids={7};
 SimulationLabModelComponentSeed component;component.runtime_class_id=13;component.maximum_integrity=1000;seed.model_components.push_back(component);
 std::array<SimulationLabTechnologyAsset,8> techs{};for(uint32_t id=0;id<8;++id){techs[id].technology_id=techs[id].dependency_technology_id=id;techs[id].research_duration_ticks=3;techs[id].xenite_cost_q4=10;techs[id].energy_demand_q4=4;}
 std::array<FactionTechnologyMask256,8> masks{};for(auto& mask:masks)set_faction_technology_unlocked(mask,0,true);
 std::array<SimulationLabFactionEconomySeed,8> economy{};economy[faction].xenite_current_q4=20;
 SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=3;scenario.ai_planning_enabled=false;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};SimulationLabExperiment lab;lab.reset(config,ki,source?std::span(&seed,1):std::span<const SimulationLabSeedEntity>{},economy,{},techs,masks,{},scenario);return lab;
}
}
int main()try {
 const auto field=FieldGrid::create(16,16,0,0);uint32_t fixtures=0;
 for(uint32_t faction:{0U,1U})for(bool component_flags:{false,true}) {
  auto lab=make_lab(true,faction);auto& run=lab.runs()[0];auto& e=run.entities[0];
  (component_flags?e.model_components[0].runtime_flags_ec:e.runtime_flags)=0x81;
  e.research_technology_id=7;e.research_duration_ticks=3;e.research_xenite_due_q4=10;e.research_energy_demand_q4=4;
  lab.step(field,1);check(e.research_active&&!e.research_pending_payment,"switched-off pending job did not pay");check(run.faction_economy[faction].xenite_current_q4==10,"research did not pay once");
  lab.step(field,2);check(e.research_elapsed_ticks==0,"switched-off research advanced");
  e.runtime_flags&=~1U;e.model_components[0].runtime_flags_ec&=~1U;
  lab.step(field,3);check(faction_technology_unlocked(run.faction_technology_masks[faction],7),"paid raw research failed to unlock");check(!e.research_active&&!e.research_pending_payment,"completed raw flags resurrected");check(run.faction_active_research_energy_q4[faction]==0,"completion retained research energy");
  lab.step(field,2);check(run.metrics.research_jobs_completed==1&&run.metrics.research_jobs_started==1,"completed job ran twice");++fixtures;
 }
 {auto lab=make_lab();auto& run=lab.runs()[0];auto& e=run.entities[0];e.research_active=true;e.research_technology_id=7;e.research_elapsed_ticks=0x7fffffff;e.research_duration_ticks=1;lab.step(field,1);check(e.research_elapsed_ticks==0x80000000&&!faction_technology_unlocked(run.faction_technology_masks[1],7),"signed progress overflow completed research");++fixtures;}
 {auto lab=make_lab();auto& e=lab.runs()[0].entities[0];e.runtime_flags=0xc0;e.research_technology_id=7;e.research_duration_ticks=1;e.research_xenite_due_q4=10;lab.step(field,1);check(e.research_active&&!e.research_pending_payment&&lab.runs()[0].metrics.research_jobs_completed==1&&lab.runs()[0].metrics.research_jobs_started==1,"both research bits did not complete then pay in callback order");++fixtures;}
 {auto lab=make_lab();auto& e=lab.runs()[0].entities[0];e.model_components[0].runtime_removed=true;e.runtime_flags=0x80;e.research_technology_id=7;e.research_xenite_due_q4=10;lab.step(field,1);check(lab.runs()[0].faction_economy[1].xenite_current_q4==20,"unlinked owner paid research");++fixtures;}
 for(uint32_t state=0;state<16;++state) {
  auto lab=make_lab(false);auto& run=lab.runs()[0];run.tick=6;run.faction_packed_relation_states[2]=state<<4;
  set_faction_technology_unlocked(run.faction_technology_masks[1],1,true);set_faction_technology_unlocked(run.faction_technology_masks[2],2,true);
  lab.step(field,1);check(!faction_technology_unlocked(run.faction_technology_masks[1],2),"exchange ran before phase zero");lab.step(field,1);
  check(faction_technology_unlocked(run.faction_technology_masks[1],2)==(state>=8&&state<=10),"exchange relation range/direction incorrect");check(faction_technology_unlocked(run.faction_technology_masks[2],1)==(state>=8&&state<=10),"exchange was not bilateral");++fixtures;
 }
 {auto lab=make_lab(false);auto& run=lab.runs()[0];run.tick=7;run.faction_packed_relation_states[2]=8U<<4;set_faction_technology_unlocked(run.faction_technology_masks[1],1,true);lab.step(field,1);check(!faction_technology_unlocked(run.faction_technology_masks[2],1),"subset pair received a one-sided gift");++fixtures;}
 {auto lab=make_lab(false);auto& run=lab.runs()[0];run.tick=7;run.faction_packed_relation_states[2]=8U<<4;run.faction_packed_relation_states[3]=(8U<<4)|(8U<<8);for(uint32_t f=1;f<=3;++f)set_faction_technology_unlocked(run.faction_technology_masks[f],f,true);lab.step(field,1);for(uint32_t f=1;f<=3;++f)check((run.faction_technology_masks[f][0]&15U)==15U,"exchange did not use the sequential pair image");++fixtures;}
 std::cout<<"V655_RESEARCH_LIFECYCLE PASS fixtures="<<fixtures<<"; raw lifecycle, neutral ownership, signed overflow, unlinked roots, phase and bilateral ordered exchange\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
