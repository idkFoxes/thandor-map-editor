#include "thandor/simulation_lab.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
SimulationLabSeedEntity seed(){SimulationLabSeedEntity s;s.army_asset_id=301;s.faction_index=1;s.building=true;s.runtime_model_class_id=13;
 s.production_compatibility_mask=4;s.actor.position={10,10,10};s.root_model_maximum_integrity=1000;s.research_technology_ids={6};s.production_door_step_q12=0x20000;return s;}
int main()try{
 const auto field=FieldGrid::create(40,40,0,0);const auto factory=seed();std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
 SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=128;cfg.sweep_mode=SimulationLabSweepMode::Fixed;
 SimulationLabProductionAsset product;product.army_asset_id=2;product.army_runtime_flags14_raw=4;product.build_duration_ticks=160;product.entity_seed.army_asset_id=2;product.entity_seed.runtime_model_class_id=2;
 SimulationLabTechnologyAsset tech;tech.technology_id=6;tech.dependency_technology_id=6;tech.research_duration_ticks=160;
 for(uint32_t batch:{1U,3U,5U}){
  cfg.runtime_callback_step_ticks=batch;
  SimulationLabExperiment lab;lab.reset(cfg,ki,std::span(&factory,1),{},std::span(&product,1),std::span(&tech,1),{},{});
  auto& r=lab.runs()[0];auto& e=r.entities[0];e.production_building=true;e.production_asset_id=2;e.production_duration_ticks=160;
  lab.step(field,1);check(e.production_elapsed_ticks==batch&&e.production_animated_subnode_ticks==batch,"native production callback batch dropped ticks");
  e.production_building=false;e.production_behavior_state=2;e.production_door_texture_offset_v=0;lab.step(field,1);
  check(e.production_door_texture_offset_v==std::min(0x80000U,0x20000U*batch),"native door callback batch dropped motion");
  e.production_behavior_state=0;e.research_active=true;e.research_technology_id=6;e.research_duration_ticks=160;e.production_animated_subnode_ticks=0;
  lab.step(field,1);check(e.research_elapsed_ticks==batch&&e.production_animated_subnode_ticks==batch,"native research callback batch dropped ticks or pose");
 }
 cfg.runtime_callback_step_ticks=1;
 for(bool fast:{false,true}){
  cfg.fast_build_cheat=fast;SimulationLabExperiment lab;lab.reset(cfg,ki,std::span(&factory,1),{},std::span(&product,1),std::span(&tech,1),{},{});
  auto& r=lab.runs()[0];r.secondary_purchase_requests.push_back({2,1,0,0});lab.step(field,1);
  check(r.entities[0].production_duration_ticks==(fast?11U:160U),"fast-build duration was not transformed at job acceptance");
  r.entities[0].production_building=false;r.entities[0].production_behavior_state=0;
  r.entities[0].research_pending_payment=true;r.entities[0].research_technology_id=6;r.entities[0].research_duration_ticks=fast?11U:160U;
  lab.step(field,1);check(r.entities[0].research_active,"research payment timeline did not activate");
 }
 // Research acceptance applies the same native cheat transform before payment, then advances
 // on the next callback. AI chooses a normal positive candidate behind the ARM330 gate.
 for(bool fast:{false,true}){
  cfg.fast_build_cheat=fast;auto source=factory;source.runtime_model_class_id=11;auto arms=factory;arms.army_asset_id=330;arms.research_technology_ids.clear();arms.source_placement_index=1;
  std::array seeds{source,arms};auto technology=tech;technology.ai_base_candidate_score=100;std::array<SimulationLabFactionEconomySeed,8> economy{};economy[1].xenite_current_q4=100;
  ki[0x0D]=4096;SimulationLabExperiment lab;lab.reset(cfg,ki,seeds,economy,{},std::span(&technology,1),{},{});lab.step(field,75);
  const auto& r=lab.runs()[0];check(r.metrics.research_jobs_queued==1&&r.entities[0].research_active,"research acceptance control failed to queue/pay");
  check(r.entities[0].research_duration_ticks==(fast?11U:160U),"fast-build research duration not transformed on acceptance");
 }
 cfg.fast_build_cheat=false;ki[0x0D]=0;
 // Primary inventory has a mandatory 80Q4 charge even when supply is zero. Native subtraction wraps;
 // secondary queued purchases contribute no mandatory inventory demand until completion.
 for(uint32_t batch:{1U,3U,5U})for(uint32_t stock:{0U,1U,0x10000000U}){
  cfg.runtime_callback_step_ticks=batch;std::array<SimulationLabFactionEconomySeed,8> economy{};economy[1].derive_energy_allocation_from_components=true;
  economy[1].tritium_current_q4=stock;economy[1].tritium_storage_limit_q4=UINT32_MAX;economy[1].baseline_energy_generation_capacity_q4=1024;
  auto held=product;held.ai_definition_class_value74=1;SimulationLabExperiment lab;lab.reset(cfg,ki,{},economy,std::span(&held,1));auto& r=lab.runs()[0];r.pending_asset_requests.push_back({2,1,0,0});r.secondary_purchase_requests.push_back({2,1,0,0});
  lab.step(field,8);check(r.faction_supplied_energy_demand_q4[1]==80,"energy charged purchases or omitted completed primary inventory");
  check(r.faction_last_tritium_energy_burn_q4[1]==5U*batch&&r.faction_economy[1].tritium_current_q4==stock-5U*batch,"native unclamped fuel burn or callback multiplier drifted");
 }
 // Overflowed supply is compared with capacity as signed32 in native code.
 {auto powered=factory;SimulationLabModelComponentSeed component;component.maximum_integrity=1000;component.runtime_class_id=13;component.retail_energy_demand_q4=1600;powered.model_components={component};
  std::array<SimulationLabFactionEconomySeed,8> economy{};economy[1].derive_energy_allocation_from_components=true;economy[1].tritium_current_q4=0x08000000U;economy[1].tritium_storage_limit_q4=UINT32_MAX;economy[1].baseline_energy_generation_capacity_q4=1024;
  cfg.runtime_callback_step_ticks=1;SimulationLabExperiment lab;lab.reset(cfg,ki,std::span(&powered,1),economy,{});lab.step(field,8);const auto& r=lab.runs()[0];
  check(r.faction_supplied_energy_demand_q4[1]==1600&&r.faction_economy[1].tritium_current_q4==0x08000000U-100U,"native supply dword/signed capacity cap changed");}
 // Explicit native callback scaling is separate from repeated coordinator calls.
 for(uint32_t batch:{1U,3U,5U}){
  SimulationMotionProfile motion;motion.speed_q12_per_tick=4096;motion.yaw_max_velocity_turn16=512;motion.yaw_acceleration_turn16=128;
  SimulationActorState drive;drive.move_target=Vec3f{100,0,0};simulation_tick_actor_motion(drive,motion,batch);
  check(drive.position.x==float(batch)&&drive.position.y==0,"ground travel callback batch lost distance");
  SimulationActorState turn;turn.move_target=Vec3f{0,100,0};simulation_tick_actor_motion(turn,motion,batch);
  check(turn.body_turn_velocity_turn16==int32_t(128U*batch),"ground turn acceleration callback batch lost ticks");
  SimulationActorState walker;walker.move_target=Vec3f{100,0,0};simulation_tick_articulated_actor_motion(walker,motion,4096,batch);
  check(walker.position.x==float(batch)&&walker.position.y==0,"articulated travel callback batch lost distance");
 }
 for(uint32_t input:{0U,UINT32_MAX}){cfg.runtime_callback_step_ticks=input;SimulationLabExperiment lab;lab.reset(cfg,ki,{});check(lab.config().runtime_callback_step_ticks==(input==0?1U:5U),"callback batch replay input escaped native1..5 bounds");}
 // Ordinary worker/coordinator batching remains an exact sequence of single callbacks.
 cfg.runtime_callback_step_ticks=1;SimulationLabExperiment one,many;one.reset(cfg,ki,std::span(&factory,1),{},std::span(&product,1));many.reset(cfg,ki,std::span(&factory,1),{},std::span(&product,1));
 for(auto* lab:{&one,&many}){auto& e=lab->runs()[0].entities[0];e.production_building=true;e.production_asset_id=2;e.production_duration_ticks=160;}
 one.step(field,12);for(int i=0;i<12;++i)many.step(field,1);check(one.runs()[0].entities[0].production_elapsed_ticks==12&&many.runs()[0].entities[0].production_elapsed_ticks==12,"worker batching changed internal callback ticks");
 std::cout<<"V626_CALLBACK_TIMING_ENERGY PASS; production/research/door batches,fast-build,primary inventory,wrapping fuel,coordinator batching\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
