#include "thandor/simulation_lab_worker.hpp"
#include "thandor/simulation_lab_environment.hpp"
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace thandor;
void check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
void control(uint32_t runs,bool advance){
 auto field=FieldGrid::create(16,16,0,0);std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
 SimulationLabConfig config;config.run_count=runs;config.duration_ticks=16;config.sweep_mode=SimulationLabSweepMode::Fixed;
 SimulationLabSeedEntity seed;seed.army_asset_id=1;seed.faction_index=1;seed.root_model_maximum_integrity=100;
 seed.actor.position={q12_to_float(field.at(8,8).world_x_q12),q12_to_float(field.at(8,8).world_y_q12),0};
 auto catalog=std::make_shared<SimulationLabRuntimeCatalog>();std::weak_ptr<const SimulationLabRuntimeCatalog> weak=catalog;
 SimulationLabScenarioRuntimeSeed scenario;scenario.runtime_catalog=catalog;
 SimulationLabWorker worker;worker.start(config,ki,std::span(&seed,1),{},{},{},{},{},field,scenario);
 catalog.reset();scenario.runtime_catalog.reset();check(!weak.expired(),"active worker lost its runtime catalog");
 auto retained=worker.snapshot();check(retained&&retained->runs().size()==runs,"initial immutable snapshot missing");
 if(advance){worker.request_advance(8);const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
  while(worker.completed_generations()<8&&!worker.error()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
  check(!worker.error()&&worker.completed_generations()==8,"worker did not complete bounded generations");}
 worker.clear();check(weak.expired(),"clear leaked run threads and their experiment/catalog state");
 check(!worker.snapshot()&&worker.run_worker_count()==0,"clear retained published/active state");
 check(retained->runs().size()==runs&&retained->runs()[0].tick==0,"clear invalidated or changed a caller-held immutable snapshot");
 worker.clear();std::cout<<"clear runs="<<runs<<" advance="<<advance<<" catalog=released retained_snapshot=valid\n";
}
void restart_control(){
 auto field=FieldGrid::create(8,8,0,0);std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
 SimulationLabConfig config;config.run_count=4;config.duration_ticks=16;config.sweep_mode=SimulationLabSweepMode::Fixed;
 SimulationLabWorker worker;std::weak_ptr<const SimulationLabRuntimeCatalog> previous;
 for(unsigned attempt=0;attempt<12;++attempt){auto catalog=std::make_shared<SimulationLabRuntimeCatalog>();SimulationLabScenarioRuntimeSeed scenario;scenario.runtime_catalog=catalog;
  worker.start(config,ki,{},{},{},{},{},{},field,scenario);check(previous.expired(),"restart leaked the predecessor worker state");
  previous=catalog;scenario.runtime_catalog.reset();catalog.reset();worker.request_advance(16);
  // Restart immediately, covering both idle and in-flight stop hand-offs without a long replay.
 }
 worker.clear();check(previous.expired(),"final restart state leaked");std::cout<<"12 immediate/in-flight restarts release predecessors PASS\n";
}
void destructor_control(){
 auto field=FieldGrid::create(8,8,0,0);std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
 auto catalog=std::make_shared<SimulationLabRuntimeCatalog>();std::weak_ptr<const SimulationLabRuntimeCatalog> weak=catalog;
 {SimulationLabWorker worker;SimulationLabScenarioRuntimeSeed scenario;scenario.runtime_catalog=catalog;SimulationLabConfig config;config.run_count=4;config.duration_ticks=16;worker.start(config,ki,{},{},{},{},{},{},field,scenario);catalog.reset();scenario.runtime_catalog.reset();worker.request_advance(16);}
 check(weak.expired(),"destructor leaked per-run worker state");
}
int main()try{for(uint32_t runs:{1U,4U,16U,32U})for(bool advance:{false,true})control(runs,advance);restart_control();destructor_control();std::cout<<"V630_WORKER_LIFETIME PASS\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
