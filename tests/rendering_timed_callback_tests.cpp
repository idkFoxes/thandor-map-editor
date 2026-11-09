#include "thandor/simulation_lab_environment.hpp"
#include "thandor/retail_random.hpp"
#include "thandor/fog_preview.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
static void check(bool b,const char* msg){if(!b)throw std::runtime_error(msg);}
int main()try {
 auto field=FieldGrid::create(40,40,0,0);auto runtime=std::make_shared<SimulationLabRuntimeCatalog>();
 ModelVisualRecord def;def.definition_id=1;def.runtime_class_id=11;
 def.simulation.timed_effect_id=10;def.simulation.timed_effect_interval_ticks=10;
 def.simulation.runtime_render_flags=1;runtime->definitions[1]=def;
 runtime->effects[10].definition.definition_id=10;
 ModelVisualNode node;node.parent_index=UINT32_MAX;node.model_definition_id=1;
 node.runtime_class_id=11;node.sprite_path="test";node.simulation=def.simulation;
 auto hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(std::vector{node});
 SimulationLabSeedEntity seed;seed.army_asset_id=300;seed.runtime_model_class_id=11;
 seed.faction_index=1;seed.building=true;seed.root_model_maximum_integrity=1000;
 seed.actor.position={q12_to_float(field.at(15,15).world_x_q12),q12_to_float(field.at(15,15).world_y_q12),0};
 SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.ai_planning_enabled=false;
 scenario.active_faction_count=1;scenario.runtime_catalog=runtime;
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=64;
 config.sweep_mode=SimulationLabSweepMode::Fixed;std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
 auto make=[&](){SimulationLabExperiment lab;lab.reset(config,ki,std::span(&seed,1),{}, {}, {}, {}, {},scenario);
  auto& e=lab.runs()[0].entities[0];e.runtime_model_hierarchy=hierarchy;
  auto& c=e.model_components[e.root_model_component_index];c.model_definition_id=1;c.source_model_node_index=0;
  c.runtime_class_id=11;return lab;};
 uint32_t fixtures=0;
 // Authored and produced factories both service a raw research job before common research bookkeeping.
 for(bool synthetic:{false,true}) {auto lab=make();auto& run=lab.runs()[0];auto& e=run.entities[0];
  e.synthetic=synthetic;e.model_components[0].runtime_flags_ec=0x40;
  lab.step(field,1);check(run.metrics.production_timed_effects_spawned==1,"factory omitted raw research callback");
  check(simulation_lab_one_shot_events_after(run,0).size()==1,"factory duplicated journal effect");
  const auto timer=e.model_components[0].timed_effect_countdown;
  e.model_components[0].runtime_flags_ec=0x80;lab.step(field,1);
  check(e.model_components[0].timed_effect_countdown==timer,"unpaid job serviced emitter");++fixtures;
 }
 // Class14 requires health >1 and the nearest inner cell's selected resource bit.
 for(uint32_t batch=1;batch<=5;++batch){config.runtime_callback_step_ticks=batch;auto lab=make();
  auto& run=lab.runs()[0];auto& e=run.entities[0];e.runtime_model_class_id=14;
  auto& c=e.model_components[0];c.runtime_class_id=14;
  runtime->definitions[1].simulation.class_specific_c0_raw=0;
  const auto cell=retail_nearest_world_to_cell(field,float_to_q12(e.actor.position.x),float_to_q12(e.actor.position.y));
  check(cell.has_value(),"fixture grid conversion failed");field.at(cell->x,cell->y).flags_and_material|=0x800U;
  lab.step(field,1);check(run.metrics.production_timed_effects_spawned==1,"supported extractor omitted pulse");
  const auto timer=c.timed_effect_countdown;field.at(cell->x,cell->y).flags_and_material&=~0x800U;
  lab.step(field,1);check(c.timed_effect_countdown==timer,"unsupported extractor consumed timer");
  field.at(cell->x,cell->y).flags_and_material|=0x800U;c.integrity=1;
  lab.step(field,1);check(c.timed_effect_countdown==timer,"destroyed extractor consumed timer");++fixtures;
 }
 // Static emitters retain native newest-first ordering and stable per-model countdown indices.
 config.runtime_callback_step_ticks=1;runtime->definitions[1].simulation.timed_effect_random_ticks=97;
 SimulationLabEnvironmentalEmitter ambient;ambient.model_definition_id=1;ambient.pose.position=seed.actor.position;
 scenario.environmental_emitters={ambient,ambient};SimulationLabExperiment lab;
 lab.reset(config,ki,{}, {}, {}, {}, {}, {},scenario);auto expected=lab.runs()[0].rng_state;
 const auto last_timer=10U+retail_random_next(expected)%97U;
 const auto first_timer=10U+retail_random_next(expected)%97U;lab.step(field,1);
 const auto& run=lab.runs()[0];check(run.rng_state==expected,"ambient callback RNG draws differ");
 check(run.environmental_emitter_states[1].countdown==last_timer&&
       run.environmental_emitter_states[0].countdown==first_timer,"ambient owner order differs");++fixtures;
 // Tick82 runs faction2 economy/relation RNG. The static emitter must receive
 // the first draw before that world job, not the job's remaining RNG state.
 scenario.active_faction_count=3;scenario.human_faction_mask=2;
 scenario.ai_planning_enabled=true;scenario.environmental_emitters={ambient};
 config.duration_ticks=128;SimulationLabExperiment ordered;
 ordered.reset(config,ki,{}, {}, {}, {}, {}, {},scenario);
 auto& ordered_run=ordered.runs()[0];ordered_run.tick=81;
 ordered_run.faction_packed_relation_states[2]=3U<<(4U*1U);
 auto owner_seed=ordered_run.rng_state;
 const auto owner_timer=10U+retail_random_next(owner_seed)%97U;
 ordered.step(field,1);
 check(ordered_run.relation_trace.draws>0,"phase fixture did not exercise world-job RNG");
 check(ordered_run.environmental_emitter_states[0].countdown==owner_timer,
       "AI world job consumed RNG before static owner callback");++fixtures;
 std::cout<<"V656_TIMED_CALLBACK PASS fixtures="<<fixtures<<"; authored/raw research, extractor health/support, native ambient order\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
