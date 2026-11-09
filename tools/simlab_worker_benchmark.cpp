#include "thandor/simulation_lab_worker.hpp"
#include <bit>
#include <iostream>
#include <chrono>
#include <thread>
using namespace thandor;
uint64_t digest(const SimulationLabExperiment&e){uint64_t h=1469598103934665603ULL;auto add=[&](uint64_t v){h=(h^v)*1099511628211ULL;};for(const auto&r:e.runs()){add(r.tick);add(r.seed);add(r.rng_state);add(r.metrics.planning_cycles);add(r.metrics.weapon_shots_fired);add(r.metrics.collision_contacts);add(r.entities.size());for(const auto&x:r.entities){add(x.army_asset_id);add(x.root_model_integrity);add(std::bit_cast<uint32_t>(x.actor.position.x));add(std::bit_cast<uint32_t>(x.actor.position.y));add(x.actor.body_yaw_turn16);add(x.actor.articulated_contact_event_serial);}}return h;}
int main(int argc,char**argv){unsigned reps=argc>1?std::stoul(argv[1]):7;auto field=FieldGrid::create(59,59,0,0);std::array<uint32_t,AiKnowledgeData::kParameterCount> p{};
for(unsigned entities:{16U,128U})for(unsigned runs:{1U,4U,8U,16U,32U}){SimulationLabConfig c;c.run_count=runs;c.duration_ticks=256;c.parameter_index=100;c.sweep_mode=SimulationLabSweepMode::Fixed;c.vary_seed_per_run=true;std::vector<SimulationLabSeedEntity> seeds;
for(unsigned i=0;i<entities;++i){SimulationLabSeedEntity s;s.army_asset_id=1000+i;s.faction_index=1;s.root_model_maximum_integrity=1000;s.actor.position={10+float(i%8),-10-float(i/8),0};s.actor.weapons.resize(8);seeds.push_back(s);}
SimulationLabExperiment serial;serial.reset(c,p,seeds);serial.step(field,256);const auto expected=digest(serial);
for(unsigned trial=0;trial<reps;++trial){SimulationLabWorker w;auto start=std::chrono::steady_clock::now();w.start(c,p,seeds,field);const auto ready=std::chrono::steady_clock::now();auto retained=w.snapshot();w.request_advance(256);const auto deadline=ready+std::chrono::seconds(20);while(w.completed_generations()<256&&!w.error()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::microseconds(100));const auto finish=std::chrono::steady_clock::now();auto final=w.snapshot();if(w.error()||!final||w.completed_generations()!=256||digest(*final)!=expected||retained->runs()[0].tick!=0)return 1;
std::cout<<"BENCH entities="<<entities<<" runs="<<runs<<" trial="<<trial<<" threads="<<w.run_worker_count()<<" ticks=256 start_ms="<<std::chrono::duration<double,std::milli>(ready-start).count()<<" step_ms="<<std::chrono::duration<double,std::milli>(finish-ready).count()<<" copies="<<w.snapshot_publications()<<" hash="<<expected<<'\n';}}
}
