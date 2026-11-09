#include "thandor/simulation_lab_worker.hpp"
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace thandor;
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main()try{
    SimulationLabConfig config;config.run_count=4U;config.duration_ticks=2048U;
    config.parameter_index=100U;
    std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    auto field=FieldGrid::create(12,12,0,0);
    SimulationLabWorker worker;worker.start(config,parameters,{},field);
    const uint32_t requested=100000U;
    const auto accepted=worker.request_advance(requested);
    check(accepted>0U&&accepted<=256U,"backpressure accepted an unbounded queue");
    const auto perf=worker.performance();
    check(perf.accepted_ticks==accepted&&perf.deferred_ticks==requested-accepted&&
          perf.backpressure_events==1U,"queue acceptance counters are inconsistent");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(worker.completed_generations()<accepted&&!worker.error()&&
          std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(!worker.error()&&worker.completed_generations()==accepted,
          "worker did not execute exactly the accepted generation count");
    const auto snapshot=worker.snapshot();
    check(snapshot&&snapshot->runs().size()==4,"missing last snapshot");
    for(const auto& run:snapshot->runs())check(run.tick==accepted,"worker tick count differs from accepted budget");
    worker.set_paused(true);
    check(worker.request_advance(12U)==0U,"paused worker accepted work");
    std::cout<<"SIMLAB_WORKER_BACKPRESSURE PASS requested="<<requested<<" accepted="<<accepted
             <<" deferred="<<requested-accepted<<" runs=4\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
