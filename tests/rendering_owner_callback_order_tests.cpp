#include "thandor/simulation_lab_environment.hpp"
#include "thandor/retail_random.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
static void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main()try{
 const auto field=FieldGrid::create(40,40,0,0);
 auto runtime=std::make_shared<SimulationLabRuntimeCatalog>();
 ModelVisualRecord def;def.definition_id=1;def.runtime_class_id=11;
 def.simulation.timed_effect_id=10;def.simulation.timed_effect_interval_ticks=10;
 def.simulation.timed_effect_random_ticks=97;def.simulation.runtime_render_flags=1;
 runtime->definitions[1]=def;runtime->effects[10].definition.definition_id=10;
 ModelVisualNode node;node.parent_index=UINT32_MAX;node.model_definition_id=1;
 node.runtime_class_id=11;node.sprite_path="fixture";node.simulation=def.simulation;
 auto hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(std::vector{node});
 std::array<SimulationLabSeedEntity,2> seeds;
 for(size_t i=0;i<seeds.size();++i){auto& seed=seeds[i];seed.army_asset_id=300;
  seed.runtime_model_class_id=11;seed.faction_index=1;seed.building=true;
  seed.root_model_maximum_integrity=1000;seed.source_placement_index=20+20*i;
  seed.actor.position={q12_to_float(field.at(15,15).world_x_q12),q12_to_float(field.at(15,15).world_y_q12),0};}
 SimulationLabEnvironmentalEmitter first,last;first.model_definition_id=last.model_definition_id=1;
 first.source_placement_index=10;last.source_placement_index=30;
 SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=1;
 scenario.ai_planning_enabled=false;scenario.runtime_catalog=runtime;scenario.environmental_emitters={first,last};
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=64;config.sweep_mode=SimulationLabSweepMode::Fixed;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
 for(bool converted:{false,true}){
  SimulationLabExperiment lab;lab.reset(config,ki,seeds,{}, {}, {}, {}, {},scenario);
  auto& run=lab.runs()[0];
  for(size_t i=0;i<run.entities.size();++i){auto& entity=run.entities[i];entity.runtime_model_hierarchy=hierarchy;
   auto& component=entity.model_components[entity.root_model_component_index];
   component.model_definition_id=1;component.source_model_node_index=0;component.runtime_class_id=11;
   component.runtime_flags_ec=0x40;entity.synthetic=converted;
   entity.construction_start_tick=i==0?7:3;
  }
  run.tick=8; // Both synthetic owners have completed their creation tick.
  auto seed=run.rng_state;std::array<uint32_t,4> timers;
  for(auto& timer:timers)timer=10+retail_random_next(seed)%97;
  lab.step(field,1);
  check(run.rng_state==seed,"mixed owner callbacks changed total draw count");
  const auto timer0=run.entities[0].model_components[0].timed_effect_countdown;
  const auto timer1=run.entities[1].model_components[0].timed_effect_countdown;
  if(!converted){
   check(timer1==timers[0]&&run.environmental_emitter_states[1].countdown==timers[1]&&
         timer0==timers[2]&&run.environmental_emitter_states[0].countdown==timers[3],
         "authored static/live owner callbacks were partitioned or misordered");
  }else{
   check(timer0==timers[0]&&timer1==timers[1]&&run.environmental_emitter_states[1].countdown==timers[2]&&
         run.environmental_emitter_states[0].countdown==timers[3],
         "new runtime owners did not precede authored owners in creation order");
  }
 }
 std::cout<<"PASS: mixed static/live placement order and recreated owner insertion order\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
