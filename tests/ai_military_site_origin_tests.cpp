#include "thandor/simulation_lab.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main()try{
 MilitaryScoreParametersV599 params{};params.site_bias=65536;params.site_scale=4096;
 std::array<MilitarySiteV599,2> sites{{{0,0,0},{24576,0,0}}};
 for(int32_t model:{0,12288,24576}){
  auto choice=military_choose_anchor_action_v599(model,0,params,1,0,0,sites,{},{},{},false,MilitaryPositionQ12{24576,0});
  require(choice.kind==1&&choice.site_index==1,"general-site choice followed model instead of stored destination");
 }
 params.anchor_bias=65536;params.anchor_scale=4096;params.hostile_bias=65536;params.hostile_scale=4096;
 auto a=military_choose_anchor_action_v599(4096,0,params,0,1,0,{},MilitaryAnchorV599{0,0,1},{},{},false,MilitaryPositionQ12{100000,100000});
 require(a.kind==2&&a.score==57344,"anchor scoring stopped using live model X");
 std::array<MilitarySiteV599,1> targets{{{4096,0,0}}};
 auto b=military_choose_anchor_action_v599(4096,0,params,0,0,1,{},{},{},targets,false,MilitaryPositionQ12{100000,100000});
 require(b.kind==3&&b.score==65536,"hostile scoring stopped using live model position");
 for(bool active:{false,true}){
  auto field=FieldGrid::create(24,24,0,0);
  SimulationLabSeedEntity unit;unit.army_asset_id=100;unit.faction_index=1;unit.runtime_model_class_id=2;
  const auto& from=field.at(5,5);const auto& to=field.at(15,15);
  unit.actor.position={q12_to_float(from.world_x_q12),q12_to_float(from.world_y_q12),0};
  unit.motion.speed_q12_per_tick=1;unit.motion.yaw_max_velocity_turn16=512;unit.motion.yaw_acceleration_turn16=64;
  unit.root_model_maximum_integrity=1000;
  SimulationLabProductionAsset product;product.army_asset_id=100;product.entity_seed=unit;product.ai_definition_class_value70=1;
  std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};ki[0x23]=250000;ki[0x27]=4096;
  SimulationLabConfig config;config.run_count=1;config.duration_ticks=120;config.parameter_index=100;config.sweep_mode=SimulationLabSweepMode::Fixed;
  SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=1;scenario.simulate_all_active_factions_as_ai=true;
  SimulationLabExperiment lab;lab.reset(config,ki,std::span(&unit,1),{},std::span(&product,1),{},{},{},scenario);
  lab.step(field,9);auto& run=lab.runs()[0];auto& e=run.entities[0];
  e.combat_movement_initialized=true;e.combat_movement.fallback={to.world_x_q12,to.world_y_q12};
  e.combat_movement.primary=e.combat_movement.stored=e.combat_movement.fallback;
  e.actor.move_target=active?std::optional(Vec3f{q12_to_float(to.world_x_q12),q12_to_float(to.world_y_q12),0}):std::nullopt;
  // Controlled native-style workspace: only two separated, present cells.
  auto scratch=std::make_shared<SimulationLabRetailScratchImage>(*run.retail_scratch);
  std::fill(scratch->state.begin(),scratch->state.end(),0);
  std::fill(scratch->field_occupancy_masks.begin(),scratch->field_occupancy_masks.end(),0);
  scratch->field_occupancy_masks[5*field.width()+5]=uint64_t{1}<<8;
  scratch->field_occupancy_masks[15*field.width()+15]=uint64_t{1}<<8;
  run.retail_scratch=std::move(scratch);lab.step(field,1);
  const auto& after=run.entities[0];
  require(run.metrics.military_ai_site_orders==1,"live military callback did not issue site order");
  require(after.military_ai_last_choice.site_index==1,"live callback ignored persistent destination");
  require(after.actor.move_target&&float_to_q12(after.actor.move_target->x)==to.world_x_q12&&float_to_q12(after.actor.move_target->y)==to.world_y_q12,"live callback redirected unit to other site");
 }
 std::cout<<"AI_SITE_ORIGIN PASS model_and_path_separate=1 active_and_idle_commands=1\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
