#include "thandor/simulation_lab_diagnostics.hpp"
#include "thandor/simulation_lab_worker.hpp"
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace thandor;
using namespace std::chrono_literals;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void cadence(){
    auto last=std::chrono::steady_clock::duration::zero();bool initialized=false;
    unsigned scans=0;
    for(unsigned refresh=0;refresh<120;++refresh){
        const auto now=std::chrono::milliseconds(refresh*500);
        if(simulation_lab_log_sample_due(false,initialized,false,now-last)){
            ++scans;last=now;initialized=true;
        }
    }
    check(scans==60,"one minute of 2Hz debug refreshes must sample only 60 logs");
    check(simulation_lab_log_sample_due(false,true,true,1ms),"final snapshot must bypass cadence");
    check(simulation_lab_log_sample_due(true,true,false,0ms),"forced diagnosis must bypass cadence");
    check(!simulation_lab_log_sample_due(false,true,false,999ms),"logging ran before its deadline");
    std::cout<<"CADENCE refreshes=120 all_run_log_scans="<<scans<<" previous_scan_opportunities=120\n";
}
void queue_observations(){
    SimulationLabRun run;run.tick=72000;
    run.pending_asset_requests={{330,1,0,500},{333,2,71000,3},{300,1,72001,4}};
    run.secondary_purchase_requests={{301,1,70000,0}};
    auto summary=simulation_lab_plan_summary(run);
    check(summary.primary==3&&summary.secondary==1&&summary.oldest_primary_age_ticks==72000&&
        summary.oldest_secondary_age_ticks==2000&&summary.primary_placement_attempts==507,
        "pending plan age/count observations wrong");
    check(run.pending_asset_requests.size()==3,"observation must never expire an aged purchase");
    // A real blocked planner retry keeps a paid request. No arbitrary age-based cancellation.
    auto field=FieldGrid::create(59,59,0,0);
    Brush brush;brush.radius_cells=3;brush.shape=BrushShape::Square;brush.falloff=BrushFalloff::Constant;
    field.paint_placement_occupancy_class(20,20,brush,0,true);
    SimulationLabSeedEntity blocker;blocker.army_asset_id=256;blocker.faction_index=1;
    blocker.building=true;blocker.actor.position={20,20,0};
    blocker.motion.collision_radius_q12=float_to_q12(100.0F);
    std::array<std::uint32_t,AiKnowledgeData::kParameterCount> ki{};
    SimulationLabConfig config;config.run_count=1;config.duration_ticks=1024;
    config.parameter_index=100;config.sweep_mode=SimulationLabSweepMode::Fixed;
    SimulationLabExperiment experiment;experiment.reset(config,ki,std::span(&blocker,1));
    experiment.runs()[0].pending_asset_requests.push_back({330,1,0,0});
    experiment.step(field,1024);
    const auto& after=experiment.runs()[0];
    check(after.pending_asset_requests.size()==1&&after.pending_asset_requests[0].army_asset_id==330,
        "blocked pending request was silently discarded");
    check(after.pending_asset_requests[0].placement_attempts>1,"blocked request was never retried");
    check(simulation_lab_plan_summary(after).oldest_primary_age_ticks==1024,
        "retry incorrectly reset pending request age");
    std::cout<<"PENDING blocked_age_ticks=1024 attempts="<<after.pending_asset_requests[0].placement_attempts<<" retained=1\n";
}
void workers(std::uint32_t runs){
    SimulationLabConfig config;config.run_count=runs;config.duration_ticks=16;
    config.sweep_mode=SimulationLabSweepMode::Fixed;config.vary_seed_per_run=true;
    std::array<std::uint32_t,AiKnowledgeData::kParameterCount> ki{};
    SimulationLabSeedEntity seed;seed.army_asset_id=300;seed.faction_index=1;seed.building=true;
    seed.root_model_maximum_integrity=409600;seed.actor.position={3,3,0};
    const auto field=FieldGrid::create(12,12,0,0);
    SimulationLabExperiment serial;serial.reset(config,ki,std::span(&seed,1));serial.step(field,8);
    SimulationLabWorker worker;worker.start(config,ki,std::span(&seed,1),field);
    auto initial=worker.snapshot();worker.request_advance(8);
    const auto deadline=std::chrono::steady_clock::now()+3s;
    while(worker.completed_generations()<8&&!worker.error()&&std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(1ms);
    check(!worker.error()&&worker.completed_generations()==8,"bounded worker stalled");
    auto snapshot=worker.snapshot();const auto perf=worker.performance();
    check(perf.snapshot_copies>=1&&perf.snapshot_copies<=8,"copy count must reflect coalesced publication");
    check(perf.slowest_run_index<runs&&perf.run_sum_us>=perf.slowest_run_us,"per-run timing observations invalid");
    check(perf.peak_pending_ticks==8,"backlog peak lost requested work");
    for(std::size_t i=0;i<runs;++i){
        const auto& a=snapshot->runs()[i];const auto& b=serial.runs()[i];
        check(a.tick==b.tick&&a.rng_state==b.rng_state&&a.seed==b.seed&&
            a.metrics.planning_cycles==b.metrics.planning_cycles&&a.entities.size()==b.entities.size(),
            "performance instrumentation changed deterministic stepping");
    }
    check(initial->runs()[0].tick==0,"publication mutated held snapshot");
    std::cout<<"PERF_PROBE runs="<<runs<<" step_wall_us="<<perf.step_wall_us
        <<" run_sum_us="<<perf.run_sum_us<<" slowest_run="<<perf.slowest_run_index
        <<" slowest_us="<<perf.slowest_run_us<<" snapshot_copy_us="<<perf.snapshot_copy_us
        <<" copies="<<perf.snapshot_copies<<" peak_pending="<<perf.peak_pending_ticks<<'\n';
    worker.clear();check(worker.performance().snapshot_copies==0,"clear retained stale timings");
}
int main()try{cadence();queue_observations();for(auto runs:{1U,4U,16U,32U})workers(runs);
    std::cout<<"V632_PERFORMANCE_DIAGNOSTICS PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
