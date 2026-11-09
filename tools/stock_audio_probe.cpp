#define main v643_replay_unused_main
#include "simlab_stock_replay.cpp"
#undef main
#include "thandor/simulation_lab_audio.hpp"
#include "thandor/audio.hpp"
#include <chrono>
#include <thread>
#include <unordered_set>
int main(int argc,char** argv)try{
 if(argc!=2)throw std::runtime_error("usage: stock_audio GAME_ROOT | --backend-success | --backend-failure");
 const std::string argument=argv[1];
 if(argument.starts_with("--backend-")){
  SamPcmData pcm;pcm.interleaved_stereo.assign(128*2,64);
  const auto before=audio_playback_diagnostics();const bool accepted=play_pcm_one_shot(pcm,0x8000,0x4000);
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  AudioPlaybackDiagnostics after{};
  do{after=audio_playback_diagnostics();if(after.one_shots_completed>before.one_shots_completed||after.one_shots_failed>before.one_shots_failed)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(std::chrono::steady_clock::now()<deadline);
  const bool failed=after.one_shots_failed>before.one_shots_failed;
  if(argument=="--backend-failure"?!failed:(!accepted||failed||after.one_shots_completed==before.one_shots_completed))throw std::runtime_error("host completion/failure diagnostics did not report actual backend outcome");
  std::cout<<"HOST_AUDIO accepted="<<accepted<<" completed="<<after.one_shots_completed-before.one_shots_completed<<" failed="<<after.one_shots_failed-before.one_shots_failed<<"; ALSA test endpoint, not speaker verification\n";return 0;
 }
 const auto assets=AssetCatalog::scan_archive_root(argument);const auto catalog=simulation_lab_prepare_runtime_catalog(assets);
 const auto field=FieldGrid::create(160,100,0,0);const auto& c=field.at(60,50);
 FactionTechnologyMask256 mask{};mask.fill(UINT32_MAX);std::array<FactionTechnologyMask256,8> masks{};masks[1]=masks[2]=mask;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};uint32_t tested=0;
 for(uint32_t id:{2U,60U,110U,115U,170U}){
  auto source=seed_for(*assets.army_preset(id),assets,mask);source.faction_index=1;source.source_placement_index=SIZE_MAX;
  source.actor.position={q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),0};source.actor.move_target=source.actor.position;source.actor.move_target->x+=4;
  auto target=seed_for(*assets.army_preset(331),assets,mask);target.faction_index=2;target.source_placement_index=SIZE_MAX;target.actor.position=source.actor.position;target.actor.position.x+=4;
  const std::array seeds{source,target};SimulationLabConfig config;config.run_count=1;config.duration_ticks=384;
  SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=2;scenario.assignable_faction_count=2;scenario.runtime_catalog=catalog;
  SimulationLabExperiment lab;lab.reset(config,ki,seeds,{},{},{},masks,{},scenario);auto& run=lab.runs()[0];run.entities[0].synthetic=true;
  uint64_t observed=0,loops=0,one_shots=0,contacts=0,audible_near=0,audible45=0;std::unordered_set<uint32_t> slots;
  for(uint32_t tick=0;tick<384;++tick){lab.step(field,1);const auto events=simulation_lab_one_shot_events_after(run,observed);
   const auto audio=simulation_lab_audio_frame(run,*catalog,events);observed=simulation_lab_latest_one_shot_event_sequence(run);
   loops+=audio.loops.size();one_shots+=audio.one_shots.size();for(const auto& [id,request]:audio.loops){(void)request;slots.insert(id);}for(const auto& request:audio.one_shots)slots.insert(request.sound_table_index);
   const auto check_gain=[&](const SimulationLabSoundRequest& request){
    const auto root=run.entities[0].actor.position;
    SpatialAudioListener near{{root.x,root.y,root.z},{1,0,0},{0,1,0}},overview{spatial_sound_listener_position({root.x,root.y,root.z},45*4096),{1,0,0},{0,1,0}};
    const std::array<float,3> pos{request.world_position.x,request.world_position.y,request.world_position.z};
    audible_near+=spatial_sound_gains_q15(near,pos,request.gain_q15,request.maximum_distance_q12).audible();
    audible45+=spatial_sound_gains_q15(overview,pos,request.gain_q15,request.maximum_distance_q12).audible();
   };
   for(const auto& [slot,request]:audio.loops){(void)slot;check_gain(request);}for(const auto& request:audio.one_shots)check_gain(request);
   for(const auto& event:events)if(event.kind==SimulationLabOneShotEventKind::ArticulatedContact&&event.sound_asset_id)++contacts;
  }
  if(!audible_near||slots.empty()||(id==110||id==115)&&(!contacts||!one_shots))throw std::runtime_error("stock produced owner generated no expected audio");
  for(const auto slot:slots){bool matched=false;for(const auto& asset:assets.assets())if(asset.kind==AssetKind::Sound&&sam_trailing_decimal_index(asset.normalized_path)==slot){const auto pcm=decode_sam_to_pcm(assets.load(asset.normalized_path));if(!pcm.empty())matched=true;}if(!matched)throw std::runtime_error("audio slot has no decodable stock SAM: "+std::to_string(slot));}
  std::cout<<"STOCK_AUDIO arm="<<id<<" loops="<<loops<<" one_shots="<<one_shots<<" contacts="<<contacts<<" slots="<<slots.size()<<" gain="<<run.entities[0].positioned_sound_gain_q15<<" range_q12="<<run.entities[0].positioned_sound_maximum_distance_q12<<" audible_near="<<audible_near<<" audible45="<<audible45<<" shots="<<run.metrics.weapon_shots_fired<<'\n';++tested;
 }
 std::cout<<"V643_STOCK_AUDIO PASS cases="<<tested<<"; produced-tagged isolated live actors,384 callbacks each,raw SAM decode; no audible-device claim\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
