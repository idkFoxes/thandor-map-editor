#include "thandor/simulation_lab_audio.hpp"
#include "thandor/audio.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool x,const char* why){if(!x)throw std::runtime_error(why);}
int main()try{
 SimulationLabRun run;SimulationLabRuntimeCatalog catalog;
 SimulationLabEntity entity;entity.synthetic=true;entity.source_placement_index=SIZE_MAX;
 entity.construction_complete=true;entity.runtime_model_class_id=2;entity.actor.position={1,2,3};
 entity.last_translated=true;entity.runtime_sound_profile={2,7,8,0,0};
 entity.positioned_sound_gain_q15=0x8000;entity.positioned_sound_maximum_distance_q12=409600;
 run.entities.push_back(entity);
 auto f=simulation_lab_audio_frame(run,catalog,{});check(f.loops.contains(7),"produced movement owner lost audio");
 run.entities[0].runtime_removed=true;check(simulation_lab_audio_frame(run,catalog,{}).loops.empty(),"released actor retained sound");
 run.entities[0]=entity;run.entities[0].root_model_terminal=true;check(simulation_lab_audio_frame(run,catalog,{}).loops.empty(),"terminal actor retained movement loop");
 auto& e=run.entities[0];e=entity;e.runtime_model_class_id=13;e.runtime_sound_profile={13,0,0,9,10};
 check(simulation_lab_audio_frame(run,catalog,{}).loops.empty(),"idle factory ran production sound");
 e.production_behavior_state=1;check(simulation_lab_audio_frame(run,catalog,{}).loops.contains(9),"building factory silent");
 e.runtime_flags=1;check(simulation_lab_audio_frame(run,catalog,{}).loops.empty(),"switched off factory ran build loop");
 e.production_behavior_state=2;f=simulation_lab_audio_frame(run,catalog,{});check(f.loops.contains(10)&&!f.loops.contains(9),"door sound did not use independent native state gate");
 e.runtime_flags=0;e.production_behavior_state=0;e.research_active=true;check(simulation_lab_audio_frame(run,catalog,{}).loops.contains(9),"researching factory silent");
 e.runtime_model_class_id=11;e.runtime_sound_profile.runtime_class_id=11;e.research_active=false;
 check(simulation_lab_audio_frame(run,catalog,{}).loops.empty(),"idle HQ ran work sound");
 SimulationLabOneShotEvent contact;contact.kind=SimulationLabOneShotEventKind::ArticulatedContact;
 contact.sound_asset_id=12;contact.army_asset_id=115;contact.source_placement_index=SIZE_MAX;contact.root_world_position={4,5,6};
 catalog.armies[115].positioned_sound_gain_q15=0x6000;catalog.armies[115].positioned_sound_maximum_distance_q12=409600;
 auto& effect=catalog.effects[44].definition;effect.terrain_grid_mask_index=13;effect.positioned_sound_argument0=0x7000;effect.positioned_sound_argument1=409600;
 auto launch=contact;launch.sound_asset_id=0;launch.effect_id=44;launch.kind=SimulationLabOneShotEventKind::WeaponLaunchEffect;
 std::array events{contact,launch};f=simulation_lab_audio_frame(run,catalog,events);
 check(f.one_shots.size()==2&&f.one_shots[0].sound_table_index==12&&f.one_shots[1].sound_table_index==13,"produced contact or effect sound lost by presentation transport");
 check(f.one_shots[0].world_position.x==4&&f.one_shots[0].gain_q15==0x6000,"historical contact pose/gain changed");
 check(simulation_lab_audio_frame(run,catalog,{}).one_shots.empty(),"observed events replayed without new journal entries");
 SimulationProjectileState shot;shot.shot_definition_id=55;shot.runtime_flight_initialized=true;shot.runtime_flight_position={9,8,7};run.projectiles.push_back(shot);
 catalog.shots[55].terrain_grid_mask_index=14;catalog.shots[55].positioned_sound_gain_q15=0x8000;catalog.shots[55].positioned_sound_maximum_distance_q12=409600;
 f=simulation_lab_audio_frame(run,catalog,{});check(f.loops.contains(14)&&f.loops[14].world_position.x==9,"live shot projected loop missing");
 run.projectiles[0].impact_started=true;check(simulation_lab_audio_frame(run,catalog,{}).loops.contains(14),"live beam owner lost audio after continuous contact");
 run.projectiles[0].impact_damage_applied=true;check(!simulation_lab_audio_frame(run,catalog,{}).loops.contains(14),"released flight retained audio through its impact visual");
 contact.sound_gain_and_distance=std::array<uint32_t,2>{0x4321,55555};
 f=simulation_lab_audio_frame(run,catalog,std::span(&contact,1));check(f.one_shots[0].gain_q15==0x4321&&f.one_shots[0].maximum_distance_q12==55555,"historical variant sound metadata replaced by base ARM");
 const auto origin=spatial_sound_listener_position({3,4,2},45*4096);
 check(origin[0]==3&&origin[1]==4&&origin[2]==13.25F,"native quarter-distance target listener was replaced by camera eye");
 check(spatial_sound_gains_q15({origin,{1,0,0},{0,1,0}},{3,4,2},0x8000,50000).audible(),"stock sound at camera target inaudible at default zoom");
 const SpatialAudioListener listener{{0,0,0},{1,0,0},{0,1,0}};
 const auto left=spatial_sound_gains_q15(listener,{-10,0,0},0x8000,100*4096);
 const auto right=spatial_sound_gains_q15(listener,{10,0,0},0x8000,100*4096);
 check(left.left_q15>left.right_q15&&right.right_q15>right.left_q15,"request stereo projection failed");
 {
  auto runtime=std::make_shared<SimulationLabRuntimeCatalog>(catalog);
  runtime->definitions[1].simulation.timed_effect_id=44;
  runtime->definitions[1].simulation.timed_effect_interval_ticks=1;
  SimulationLabScenarioRuntimeSeed scenario;scenario.runtime_catalog=runtime;
  SimulationLabEnvironmentalEmitter emitter;emitter.model_definition_id=1;emitter.pose.position={5,5,0};scenario.environmental_emitters.push_back(emitter);
  SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=3;
  std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};SimulationLabExperiment lab;
  lab.reset(cfg,parameters,{},{},{},{},{},{},scenario);const auto field=FieldGrid::create(30,30,0,0);lab.step(field,3);
  const auto events=simulation_lab_one_shot_events_after(lab.runs()[0],0);
  check(events.size()==3,"ambient effect audio lost while snapshots skipped multiple ticks");
  const auto frame=simulation_lab_audio_frame(lab.runs()[0],*runtime,events);
  check(frame.one_shots.size()==3,"ambient sound-only journal did not reach audio collector");
 }
 std::cout<<"V643_SIMULATION_AUDIO PASS produced movement/contact,effect events,flight loop,activity gates,release,historical pose,stereo\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
