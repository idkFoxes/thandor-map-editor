#include "thandor/simulation_lab_worker.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace thandor;
void check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
void wait(SimulationLabWorker& worker,uint64_t generation) {
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
 while(worker.completed_generations()<generation) {
  check(!worker.error(),"worker reported error");
  check(std::chrono::steady_clock::now()<deadline,"idle worker lost queued generation notification");
  std::this_thread::sleep_for(std::chrono::microseconds(50));
 }
}
int main()try {
 SimulationLabConfig config;config.run_count=4;config.duration_ticks=2048;config.parameter_index=100;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};auto field=FieldGrid::create(12,12,0,0);
 SimulationLabWorker worker;worker.start(config,ki,{},field);
 for(uint64_t generation=1;generation<=1000;++generation) {
  worker.request_advance(1);wait(worker,generation);
  const auto snapshot=worker.snapshot();check(snapshot&&snapshot->runs().size()==4,"generation snapshot missing");
  for(const auto& run:snapshot->runs())check(run.tick==generation,"run generation barrier lost coherence");
  // Every request follows a drained queue, exercising the wait/requeue edge.
  if(generation%25==0){worker.set_paused(true);worker.request_advance(3);
   check(worker.pending_ticks()==0,"paused worker accepted queued ticks");worker.set_paused(false);}
 }
 worker.clear();config.duration_ticks=16;
 for(uint32_t restart=0;restart<32;++restart) {
  worker.start(config,ki,{},field);worker.request_advance(1);wait(worker,1);worker.clear();
 }
 std::cout<<"V653_WORKER_WAKEUP PASS single_generation_requests=1000 runs=4 pause_cycles=40 startup_restarts=32\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
