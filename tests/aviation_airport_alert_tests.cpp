#include "aviation_airport_alert_fixture.hpp"
#include "thandor/simulation_lab_worker.hpp"
#include <chrono>
#include <iostream>
#include <thread>
using namespace v650;
int main() try {
    const auto field=FieldGrid::create(32,32,0,0);
    AlertCase base;base.x=field.at(12,12).world_x_q12;base.y=field.at(12,12).world_y_q12;
    for(uint32_t batch=1;batch<=5;++batch)for(int mode=0;mode<10;++mode){
        auto c=base;c.batch=batch;
        if(mode==1)c.timer=16;
        if(mode==2)c.friendly=true;
        if(mode==3)c.occupancy=3;
        if(mode==4)c.sound_valid=false;
        if(mode==5)c.countdown=0;
        if(mode==6)c.x=-100000000;
        if(mode==7)c.source=c.listener;
        if(mode==8)c.reverse_friendly=true; // Deliberately asymmetric relations.
        if(mode==9)c.countdown=1; // First substep on-map, later ones may leave.
        auto lab=alert_lab(field,c);lab.step(field,1);const auto& r=lab.runs()[0];
        const bool expected=mode==0||mode==8||mode==9;
        const auto events=alerts(r);
        check(events.size()==size_t(expected),"airport alert gate/batch result differs");
        check(r.faction_relation_transition_ticks[c.listener]==(expected?0:c.timer),"alert cooldown consumed without a submitted sound");
        if(expected){
            check(events[0].sound_listener_faction_index==c.listener&&events[0].root_world_position.x==q12_to_float(c.x),"historical listener or on-map event point lost");
            const auto copy=lab.snapshot_copy();lab.step(field,1);
            const auto held=alerts(copy.runs()[0]);check(held.size()==1,"immutable alert journal changed");
            SimulationLabRuntimeCatalog catalog;
            const auto audio=simulation_lab_audio_frame(copy.runs()[0],catalog,held);
            check(audio.global_one_shots.size()==1&&audio.one_shots.empty(),"global alert was attenuated as positioned audio");
            check(simulation_lab_one_shot_events_after(copy.runs()[0],held[0].sequence).empty(),"consumed alert repeated");
        }
    }
    // Bootstrap 17; economy job 0 increments once per eight world callbacks,
    // independently of callback batch. Reduced updates bypass the economy.
    for(uint32_t batch:{1U,5U}){
        auto c=base;c.batch=batch;c.timer=0;c.sound_valid=false;
        auto lab=alert_lab(field,c);auto& r=lab.runs()[0];
        lab.step(field,6);check(r.faction_relation_transition_ticks[1]==0,"cooldown advanced outside economy job 0");
        lab.step(field,1);check(r.faction_relation_transition_ticks[1]==1,"economy cooldown used simulation batch or wrong phase");
        r.reduced_update=true;lab.step(field,16);check(r.faction_relation_transition_ticks[1]==1,"reduced update ran alert cooldown");
        const auto rng=r.rng_state;check(lab.set_active_view_faction(3)&&!lab.set_active_view_faction(8),"view faction bounds differ");
        check(r.active_view_faction_index==3&&r.rng_state==rng,"view switch reset RNG");
    }
    // UI input reaches every run only at a coherent worker generation barrier.
    SimulationLabWorker worker;SimulationLabConfig cfg;cfg.run_count=2;cfg.duration_ticks=100;
    std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
    worker.start(cfg,ki,{},field);worker.set_active_view_faction(5);worker.request_advance(1);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(worker.completed_generations()<1&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    while(worker.snapshot()->runs()[0].tick<1&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    const auto snap=worker.snapshot();check(!worker.error()&&snap->runs()[0].tick==1,"worker generation did not finish");
    for(const auto& r:snap->runs())check(r.active_view_faction_index==5,"worker view input was not coherent");
    worker.clear();
    std::cout<<"V650_AIRPORT_ALERT PASS 50 gate/batch cases, global transport, immutable history, cooldown cadence, worker view switch\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
