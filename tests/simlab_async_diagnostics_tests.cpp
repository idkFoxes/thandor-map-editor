#include "thandor/async_diagnostic_log.hpp"
#include "../src/editor/simulation_lab_diagnostic_writer.hpp"
#include <future>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace thandor;
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
int main()try{
 const auto dir=std::filesystem::temp_directory_path()/("thandor-async-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 std::filesystem::create_directory(dir);
 const auto file=dir/"trace.log";
 std::promise<void> entered,release;auto gate=release.get_future().share();
 {
  AsyncDiagnosticLog log(file,"HEADER\n");
  log.enqueue([&](std::ostream& out){entered.set_value();gate.wait();out<<"BLOCKED\n";},true);
  if(entered.get_future().wait_for(std::chrono::seconds(2))!=std::future_status::ready){release.set_value();throw std::runtime_error("writer never started");}
  const auto start=std::chrono::steady_clock::now();
  for(unsigned i=0;i<1000;++i)log.enqueue([i](std::ostream& out){out<<"SAMPLE "<<i<<'\n';});
  log.enqueue([](std::ostream& out){out<<"FINAL\n";},true);
  const auto coalesced=log.stats().coalesced;
  const auto elapsed=std::chrono::steady_clock::now()-start;
  release.set_value();log.close("restart");
  std::cout<<"ASYNC_PROBE requests=1000 coalesced="<<coalesced<<" enqueue_total_us="<<std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()<<" formatter_blocked=1\n";
  check(coalesced==999,"periodic backlog did not coalesce deterministically");
  check(elapsed<std::chrono::seconds(1),"UI enqueue waited for a blocked formatter");check(log.stats().written==3,"forced final sample lost");
  check(!log.enqueue([](std::ostream&){}),"closed writer accepted work");
 }
 std::ifstream in(file);std::string data((std::istreambuf_iterator<char>(in)),{});
 check(data=="HEADER\nBLOCKED\nSAMPLE 999\nFINAL\nEND reason=restart\n","log ordering or drain incorrect");
 {
  AsyncDiagnosticLog log(dir/"error.log","");
  log.enqueue([](std::ostream&){throw std::runtime_error("test formatter failure");},true);
  log.enqueue([](std::ostream& out){out<<"RECOVERED\n";},true);log.close("test");
  check(log.stats().failures==1&&log.stats().written==2,"writer failure did not stay contained");
 }
 bool failed=false;try{AsyncDiagnosticLog log(dir/"absent"/"file","");}catch(const std::exception&){failed=true;}
 check(failed,"missing log directory accepted");
 // Exercise the real formatter with immutable snapshots, not an SDK or mocked stream.
 {
  SimulationLabExperiment lab;SimulationLabConfig config;config.run_count=1;config.duration_ticks=120;
  SimulationLabSeedEntity seed;seed.army_asset_id=115;seed.faction_index=1;seed.root_model_maximum_integrity=100;
  std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};lab.reset(config,ki,std::span(&seed,1));
  auto& research=lab.runs()[0].ai_research_trace[1];research.candidate_count=1;
  research.candidates[0]={61,374,4,0,312,4,3,3};
  auto snapshot=std::make_shared<const SimulationLabExperiment>(lab);
  const auto field=FieldGrid::create(16,16,0,0);
  editor::SimulationLabDiagnosticWriter writer(dir/"sample.log","FORMATTER\n",field,{115});
  editor::SimulationLabDiagnosticSample sample;sample.snapshot=snapshot;sample.active_factions=1;sample.duration_ticks=120;
  sample.dynamic_scene_shadow_milliseconds_=1.25;sample.dynamic_scene_pre_shadow_milliseconds_=4.5;
  sample.dynamic_scene_fog_milliseconds_=0.375;sample.dynamic_scene_model_setup_milliseconds_=4.125;
  sample.dynamic_scene_effects_milliseconds_=0.5;sample.dynamic_scene_sort_milliseconds_=0.25;
  sample.generated_shadow_build_milliseconds_=9.5;sample.projected_shadows_enabled=true;
  sample.dynamic_shadow_templates=38;sample.dynamic_shadow_vertices=444;
  writer.append(sample,true);sample.dynamic_scene_shadow_milliseconds_=99;
  sample.dynamic_scene_fog_milliseconds_=99;sample.dynamic_scene_model_setup_milliseconds_=99;
  lab.runs()[0].entities[0].army_asset_id=999;
  writer.close("complete");
  check(snapshot->runs()[0].entities[0].army_asset_id==115,"writer snapshot mutated");
 }
 std::ifstream formatted(dir/"sample.log");std::string text((std::istreambuf_iterator<char>(formatted)),{});
 check(text.find("arm=115")!=std::string::npos&&text.find("arm=999")==std::string::npos,"formatter read mutable live state");
 check(text.find("gui_shadow_ms=1.25")!=std::string::npos&&text.find("gui_pre_shadow_ms=4.5")!=std::string::npos&&
       text.find("gui_fog_ms=0.375")!=std::string::npos&&text.find("gui_model_setup_ms=4.125")!=std::string::npos&&
       text.find("static_shadow_build_ms=9.5")!=std::string::npos&&text.find("projected_shadows=1")!=std::string::npos&&
       text.find("shadow_templates=38 shadow_vertices=444")!=std::string::npos,
       "background formatter lost immutable shadow-stage observations");
 check(text.find("log_coalesced=")!=std::string::npos,"writer diagnostics missing");
 check(text.find("RESEARCH_CANDIDATE")!=std::string::npos&&text.find("compatible_mdl=312")!=std::string::npos,"research cause metadata missing");
 std::filesystem::remove_all(dir);std::cout<<"V634_ASYNC_DIAGNOSTICS PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
