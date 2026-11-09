#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_targeting.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main()try{
 const auto field=FieldGrid::create(40,40,0,0);
 std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=128;config.sweep_mode=SimulationLabSweepMode::Fixed;
 SimulationLabSeedEntity seed;seed.army_asset_id=301;seed.faction_index=1;seed.building=true;
 seed.runtime_model_class_id=13;seed.production_compatibility_mask=4;seed.actor.position={10,10,10};seed.root_model_maximum_integrity=1000;
 seed.production_door_step_q12=0x40000;
 SimulationLabProductionAsset product;product.army_asset_id=2;product.army_runtime_flags14_raw=4;
 product.build_duration_ticks=16;product.entity_seed.army_asset_id=2;product.entity_seed.runtime_model_class_id=2;
 // Both represented flag sources describe the same model callback's inactive mask.
 for(const uint32_t flags:{1U,8U})for(const bool root_flags:{true,false})for(const uint32_t state:{2U,3U,4U,1U}){
  SimulationLabExperiment lab;lab.reset(config,parameters,std::span(&seed,1),{},std::span(&product,1));
  auto& run=lab.runs()[0];auto& factory=run.entities[0];factory.production_behavior_state=state;
  factory.production_door_texture_offset_v=state==4?0x80000U:0U;
  factory.production_building=state==1;factory.production_asset_id=2;factory.production_duration_ticks=16;
  auto& flag_field=root_flags?factory.model_components[factory.root_model_component_index].runtime_flags_ec:factory.runtime_flags;
  flag_field|=flags;
  lab.step(field,1);
  if(state==1){
   check(factory.production_elapsed_ticks==0&&factory.production_animated_subnode_ticks==0,"inactive building timer or production pose advanced");
   flag_field&=~flags;lab.step(field,1);
   check(factory.production_elapsed_ticks==1&&factory.production_animated_subnode_ticks==1,"restored production failed to resume exactly once");
  }else if(state==2){
   check(factory.production_door_texture_offset_v==0x40000U,"power loss froze factory opening");
   lab.step(field,1);check(factory.production_behavior_state==3&&factory.production_door_texture_offset_v==0x80000U,"inactive opening failed to reach waiting-exit");
  }else if(state==3){
   check(factory.production_behavior_state==4,"power loss prevented released-child observation");
  }else{
   check(factory.production_door_texture_offset_v==0x40000U,"power loss froze factory closing");
   lab.step(field,1);check(factory.production_behavior_state==0&&factory.production_door_texture_offset_v==0,"inactive closing failed to return idle");
  }
  check(run.metrics.class13_units_spawned==0,"door servicing spawned an unpaid unit");
 }
 // A powered-down door must still issue the child's exit and rally command.
 SimulationLabExperiment lab;lab.reset(config,parameters,std::span(&seed,1),{},std::span(&product,1));
 auto& run=lab.runs()[0];auto child=run.entities[0];child.building=false;child.runtime_model_class_id=2;child.production_linked_to_parent=true;
 child.production_parent_entity_index=0;run.entities.push_back(child);
 auto& factory=run.entities[0];factory.production_behavior_state=2;factory.production_door_texture_offset_v=0x40000;
 const auto exit_q12=v519_path_scratch_center_to_world_q12({30,30});
 const auto rally_q12=v519_path_scratch_center_to_world_q12({34,30});
 factory.production_linked_child_entity_index=1;
 factory.production_exit_world=Vec3f{q12_to_float(exit_q12.x_q12),q12_to_float(exit_q12.y_q12),10};
 factory.production_drive_target_world=Vec3f{q12_to_float(rally_q12.x_q12),q12_to_float(rally_q12.y_q12),10};
 factory.model_components[factory.root_model_component_index].runtime_flags_ec|=1;
 lab.step(field,1);
 check(factory.production_behavior_state==3&&run.entities[1].actor.move_target&&run.entities[1].actor.move_waypoint,"inactive door did not dispatch linked child's exit command");
 check(float_to_q12(run.entities[1].actor.move_target->x)==rally_q12.x_q12&&
       float_to_q12(run.entities[1].actor.move_waypoint->x)==exit_q12.x_q12,
       "inactive door lost rally or intermediate exit position");
 check((run.entities[1].combat_movement.flags&0x24BU)==0x24BU&&
       (run.entities[1].combat_movement.flags&0xB0U)==0U&&
       run.entities[1].combat_movement.queue_count==1U&&
       run.entities[1].combat_movement.queue[0].x==rally_q12.x_q12,
       "factory dispatch did not initialize the locked auxiliary route state");
 // Death alone does not clear the child's native factory-model link. Keep
 // waiting until the retained hierarchy releases or the child unlinks.
 auto& held=run.entities[1];held.actor.move_target.reset();held.actor.move_waypoint.reset();
 held.root_model_integrity=0;held.root_model_terminal=true;
 held.model_components[held.root_model_component_index].integrity=0;
 held.model_components[held.root_model_component_index].terminal_runtime_flag8=true;
 factory.production_behavior_state=3;
 lab.step(field,1);
 check(factory.production_behavior_state==3&&held.production_linked_to_parent,
       "factory closed on death before its child's model link was cleared");
 held.runtime_removed=true;simulation_lab_clear_released_target(run,1);
 check(!factory.production_linked_child_entity_index,"release retained the factory's child reference");
 lab.step(field,1);
 check(factory.production_behavior_state==4,"factory did not close after its child was released");
 // MIRROR_TARGET belongs to the child's movement image at birth, not to
 // its factory link. Releasing a destroyed factory must retain that bit.
 for(const uint32_t cls:{1U,2U,3U,17U,18U,19U}){
  auto produced=product;produced.entity_seed.runtime_model_class_id=cls;
  produced.entity_seed.root_model_maximum_integrity=1000;
  SimulationLabScenarioRuntimeSeed scene;scene.enabled=true;scene.active_faction_count=1;
  scene.human_faction_mask=2;
  SimulationLabExperiment spawned;spawned.reset(config,parameters,std::span(&seed,1),{},std::span(&produced,1),{}, {},{},scene);
  auto& r=spawned.runs()[0];auto& parent=r.entities[0];
  parent.production_building=true;parent.production_behavior_state=1;
  parent.production_asset_id=2;parent.production_duration_ticks=1;
  spawned.step(field,1);
  check(r.entities.size()==2&&r.entities[1].production_linked_to_parent,
        "factory mirror fixture did not spawn a linked child");
  check((r.entities[1].combat_movement.flags&0x402U)==0x402U,
        "new factory child omitted persistent mirror/lock movement flags");
  r.entities[0].runtime_removed=true;
  r.entities[0].model_components[r.entities[0].root_model_component_index].runtime_removed=true;
  spawned.step(field,1);
  const auto& born=r.entities[1];
  const auto& movement=born.pioneer_movement_initialized?born.pioneer_movement:born.combat_movement;
  check(!born.production_linked_to_parent&&(movement.flags&0x400U)!=0U&&
        (movement.flags&2U)==0U,
        "factory release lost mirror state or retained the movement lock");
 }
 std::cout<<"V619_FACTORY_DOOR_POWER PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
