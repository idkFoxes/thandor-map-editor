#include "thandor/simulation_lab_runtime.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
int main()try {
 auto field=FieldGrid::create(8,8,0,0);uint32_t cases=0;
 for(uint32_t batch=1;batch<=5;++batch)for(uint32_t state=0;state<3;++state) {
  SimulationLabConfig config;config.run_count=1;config.duration_ticks=40;config.parameter_index=100;config.runtime_callback_step_ticks=batch;
  SimulationLabSeedEntity seed;seed.army_asset_id=300;seed.faction_index=1;seed.runtime_model_class_id=18;seed.root_model_maximum_integrity=1000;
  for(uint32_t cls:{18U,11U,3U}){SimulationLabModelComponentSeed c;c.runtime_class_id=cls;c.maximum_integrity=1000;c.model_definition_id=cls;if(cls!=18)c.parent_component_index=0;seed.model_components.push_back(c);}
  SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=1;scenario.ai_planning_enabled=false;
  using K=LevelScheduledConditionKind;
  const auto set=[&](size_t i,K kind,uint32_t a,uint32_t b=0,uint32_t c=0){auto& condition=scenario.scheduled_conditions[i];condition.status_and_kind=uint8_t(kind);condition.set_operand(0,a);condition.set_operand(1,b);condition.set_operand(2,c);};
  set(0,K::MatchingDefinitionAndRuntimeIdActiveEntityCountAtLeast,1,2,300);
  set(1,K::NoActiveEntityWithDefinitionAndClassCommandGroupA,1);
  set(2,K::NoActiveEntityWithDefinition,1);
  set(3,K::NoActiveEntityWithClassIdOutsideClassCommandGroupA,3);
  set(4,K::XeniteCurrentAtLeast,1,1);set(5,K::TritiumCurrentAtLeast,1,0x80000000);
  set(6,K::ActiveArmyScaleValueAtLeast,1,0x80000000);set(7,K::XeniteStorageLimitAtMost0FA0,1);
  set(8,K::FactionTerrainOccupancyPercentAtLeast,1,100);set(9,K::CountdownElapsed,0,2);
  std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};SimulationLabExperiment lab;lab.reset(config,ki,std::span(&seed,1),{},{},{},{},{},scenario);
  auto& run=lab.runs()[0];run.tick=19;auto& entity=run.entities[0];
  if(state==1){entity.root_model_terminal=true;entity.model_components[0].terminal_runtime_flag8=true;}
  if(state==2)entity.model_components[0].runtime_removed=true;
  run.faction_economy[1].xenite_current_q4=0xffffffff;run.faction_economy[1].tritium_current_q4=1000;
  run.faction_tritium_extraction_rate_q4_per_tick[1]=1;run.faction_economy[1].xenite_storage_limit_q4=0xffffffff;
  auto scratch=std::make_shared<SimulationLabRetailScratchImage>();scratch->width=scratch->height=32;scratch->state.resize(1024);scratch->field_occupancy_masks.assign(64,uint64_t{1}<<8);run.retail_scratch=scratch;
  lab.step(field,1);const auto& c=run.scheduled_conditions;
  const bool expected[]{false,true,state==2,true,false,true,true,true,true,batch>=2};
  for(size_t i=0;i<10;++i){check(c[i].satisfied()==expected[i],"root/terminal/signed/persisted/countdown condition mismatch");++cases;}
  check(c[9].operand(1)==(batch==1?1U:0U),"callback batch did not decrement countdown");
 }
 std::cout<<"V654_LEVEL_CONDITIONS PASS assertions="<<cases<<" fixtures=15 batches=1..5; roots, terminal/unlinked state, signed comparisons and persisted occupancy\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
