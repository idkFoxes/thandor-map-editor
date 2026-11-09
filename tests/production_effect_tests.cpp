#include "thandor/simulation_lab_environment.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main()try{
 auto runtime=std::make_shared<SimulationLabRuntimeCatalog>();ModelVisualRecord definition;definition.definition_id=1;definition.runtime_class_id=11;
 definition.simulation.timed_effect_id=10;definition.simulation.timed_water_effect_id=11;definition.simulation.timed_effect_interval_ticks=1;definition.simulation.runtime_render_flags=1;
 runtime->definitions.emplace(1,definition);runtime->effects[10].definition.definition_id=10;runtime->effects[11].definition.definition_id=11;
 runtime->sprites["test"].effect_point_records={{0,{1,0,0}},{2,{2,0,0}}};
 SimulationLabScenarioRuntimeSeed scenario;scenario.runtime_catalog=runtime;SimulationLabSeedEntity seed;seed.army_asset_id=300;seed.runtime_model_class_id=11;seed.faction_index=1;seed.building=true;seed.root_model_maximum_integrity=1000;
 auto field=FieldGrid::create(40,40,0,0);seed.actor.position={q12_to_float(field.at(15,15).world_x_q12),q12_to_float(field.at(15,15).world_y_q12),0};
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=64;config.sweep_mode=SimulationLabSweepMode::Fixed;std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
 SimulationLabProductionAsset product;product.army_asset_id=2;product.entity_seed=seed;
 SimulationLabExperiment lab;lab.reset(config,ki,std::span(&seed,1),{},std::span(&product,1),{},{},{},scenario);
 auto& run=lab.runs()[0];auto& e=run.entities[0];ModelVisualNode root;root.parent_index=UINT32_MAX;root.model_definition_id=1;root.runtime_class_id=11;root.sprite_path="test";root.simulation=definition.simulation;
 e.runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(std::vector{root});auto& component=e.model_components[e.root_model_component_index];component.model_definition_id=1;component.source_model_node_index=0;component.runtime_class_id=11;
 e.synthetic=true;e.production_building=true;e.production_asset_id=2;e.production_duration_ticks=1000;
 lab.step(field,3);const auto events=simulation_lab_one_shot_events_after(run,0);
 check(events.size()==3,"active callback missed serviced pulses");
 check(std::abs(events[0].root_world_position.x-e.actor.position.x-1)<1.e-5F&&std::abs(events[1].root_world_position.x-e.actor.position.x)<1.e-5F&&std::abs(events[2].root_world_position.x-e.actor.position.x-2)<1.e-5F,"sparse type6 keys used compact vector indexing");
 check(events[0].effect_world_basis.has_value()&&events[0].effect_world_angles.has_value(),"historical emitter orientation was not transported");
 e.production_building=false;e.research_pending_payment=true;const auto timer=component.timed_effect_countdown;lab.step(field,3);
 check(run.metrics.production_timed_effects_spawned==3&&component.timed_effect_countdown==timer,"unpaid research emitted or consumed timer");
 e.research_pending_payment=false;e.research_active=true;lab.step(field,1);check(run.metrics.production_timed_effects_spawned==4,"active research omitted timed effect");
 e.runtime_flags|=1;lab.step(field,3);check(run.metrics.production_timed_effects_spawned==4,"raw inactive flag allowed production smoke");e.runtime_flags&=~1U;
 for(auto& cell:field.cells())cell.water_delta_q12=4096;lab.step(field,1);
 const auto latest=simulation_lab_one_shot_events_after(run,4);check(latest.size()==1&&latest[0].effect_id==11,"positive water delta did not select water effect");
 e.runtime_removed=true;lab.step(field,1);check(run.metrics.production_timed_effects_spawned==5,"released producer emitted another puff");
 // Static emitter metadata uses the same sparse key domain, not compact vector positions.
 for(auto& cell:field.cells())cell.water_delta_q12=0;
 runtime->effects[10].definition.linked_shot_id=99;
 runtime->effects[10].emissions.push_back({100U,0U});
 SimulationLabEnvironmentalEmitter ambient;ambient.model_definition_id=1;ambient.pose.position=seed.actor.position;
 ambient.world_effect_point_records={{0,{seed.actor.position.x+1,seed.actor.position.y,0}},{2,{seed.actor.position.x+2,seed.actor.position.y,0}}};
 scenario.environmental_emitters={ambient};SimulationLabExperiment static_lab;
 static_lab.reset(config,ki,{}, {},{}, {},{}, {},scenario);static_lab.step(field,3);
 const auto& pending=static_lab.runs()[0].environmental_effects;
 check(pending.size()==3&&std::abs(pending[0].pose.position.x-seed.actor.position.x-1)<1.e-5F&&
       std::abs(pending[1].pose.position.x-seed.actor.position.x)<1.e-5F&&
       std::abs(pending[2].pose.position.x-seed.actor.position.x-2)<1.e-5F,"static sparse effect keys lost origin fallback");
 // An attached class-10 radar rotates while its factory is idle. Its
 // simulation clock is independent of both production and global map time.
 for(const uint32_t radar_class:{4U,10U}){
  auto radar_catalog=std::make_shared<SimulationLabRuntimeCatalog>();
  ModelVisualRecord factory_definition;factory_definition.definition_id=1;factory_definition.runtime_class_id=13;
  ModelVisualRecord radar_definition;radar_definition.definition_id=2;radar_definition.runtime_class_id=radar_class;
  radar_definition.simulation.yaw_max_velocity_turn16=512;
  radar_catalog->definitions.emplace(1,factory_definition);radar_catalog->definitions.emplace(2,radar_definition);
  ModelVisualNode factory_node;factory_node.model_definition_id=1;factory_node.runtime_class_id=13;
  factory_node.parent_index=UINT32_MAX;factory_node.simulation.placement_contact_kind=3;
  ModelVisualNode radar_node;radar_node.model_definition_id=2;radar_node.runtime_class_id=radar_class;
  radar_node.parent_index=0;radar_node.child_slot=0;radar_node.simulation=radar_definition.simulation;
  ModelVisualNode spinner;spinner.parent_index=1;spinner.child_slot=0;
  const auto hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(std::vector{factory_node,radar_node,spinner});
  SimulationLabSeedEntity factory_seed;factory_seed.army_asset_id=302;factory_seed.faction_index=1;
  factory_seed.runtime_model_class_id=13;factory_seed.building=true;factory_seed.root_model_maximum_integrity=1000;
  factory_seed.actor.position=seed.actor.position;
  SimulationLabModelComponentSeed root_component;root_component.model_definition_id=1;root_component.runtime_class_id=13;
  root_component.source_model_node_index=0;root_component.maximum_integrity=1000;
  auto radar_component=root_component;radar_component.model_definition_id=2;radar_component.runtime_class_id=radar_class;
  radar_component.source_model_node_index=1;radar_component.parent_component_index=0;
  factory_seed.model_components={root_component,radar_component};
  SimulationLabScenarioRuntimeSeed radar_scene;radar_scene.enabled=true;radar_scene.active_faction_count=1;
  radar_scene.ai_planning_enabled=false;radar_scene.runtime_catalog=radar_catalog;
  SimulationLabExperiment radar_lab;radar_lab.reset(config,ki,std::span(&factory_seed,1),{},{},{},{},{},radar_scene);
  auto& factory=radar_lab.runs()[0].entities[0];factory.runtime_model_hierarchy=hierarchy;
  const auto before=simulation_lab_current_model_poses(factory,field,*radar_catalog);
  radar_lab.step(field,3);
  check(factory.production_animated_subnode_ticks==0&&factory.model_components[1].animated_subnode_ticks==3,
        "attached radar used the idle factory clock or missed its callbacks");
  const auto after=simulation_lab_current_model_poses(factory,field,*radar_catalog);
  check(before[2].native_basis&&after[2].native_basis&&before[2].native_basis!=after[2].native_basis,
        "attached radar's collision pose stayed frozen");
  factory.model_components[1].runtime_flags_ec|=1U;radar_lab.step(field,2);
  check(factory.model_components[1].animated_subnode_ticks==3,"switched-off radar kept rotating");
  factory.model_components[1].runtime_flags_ec=8U;radar_lab.step(field,1);
  const auto completed_ticks=radar_class==10U?4U:3U;
  check(factory.model_components[1].animated_subnode_ticks==completed_ticks,"animated model used the wrong destroyed-bit gate");
  factory.model_components[1].runtime_removed=true;radar_lab.step(field,1);
  check(factory.model_components[1].animated_subnode_ticks==completed_ticks,"released radar kept rotating");
  if(radar_class==4U){
   factory.model_components[1].runtime_removed=false;factory.model_components[1].runtime_flags_ec=0;
   radar_catalog->definitions.at(2).simulation.timed_effects_require_research=1;
   radar_lab.step(field,1);
   check(factory.model_components[1].animated_subnode_ticks==completed_ticks,"research-gated attachment animated while idle");
   factory.model_components[1].runtime_flags_ec=0x40U;radar_lab.step(field,1);
   check(factory.model_components[1].animated_subnode_ticks==completed_ticks+1U,"research-gated attachment failed to resume");
  }
 }
 std::cout<<"V620_PRODUCTION_EFFECT PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
