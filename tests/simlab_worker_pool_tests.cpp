#include "thandor/simulation_lab_worker.hpp"
#include <array>
#include <bit>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace thandor;
namespace {
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
void wait(SimulationLabWorker& worker,uint64_t generation){
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while(worker.completed_generations()<generation){
        check(!worker.error(),"worker pool reported an error");
        check(std::chrono::steady_clock::now()<end,"worker pool lost a generation");
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}
uint64_t digest(const SimulationLabRun& run){
    uint64_t h=1469598103934665603ULL;auto add=[&](uint64_t x){h=(h^x)*1099511628211ULL;};
    add(run.tick);add(run.seed);add(run.rng_state);add(run.metrics.weapon_shots_fired);
    add(run.metrics.collision_contacts);add(run.metrics.planning_cycles);add(run.metrics.target_assignments);
    add(run.metrics.navigation_actual_step_blocks);add(run.entities.size());add(run.projectiles.size());
    for(const auto& e:run.entities){
        add(e.army_asset_id);add(e.root_model_integrity);add(e.runtime_flags);add(e.root_model_terminal);
        const auto& a=e.actor;add(std::bit_cast<uint32_t>(a.position.x));add(std::bit_cast<uint32_t>(a.position.y));
        add(std::bit_cast<uint32_t>(a.position.z));add(a.body_yaw_turn16);add(a.body_turn_velocity_turn16);
        add(a.move_target.has_value());if(a.move_target){add(std::bit_cast<uint32_t>(a.move_target->x));add(std::bit_cast<uint32_t>(a.move_target->y));}
        add(a.articulated_contact_event_serial);add(e.weapon_volleys_fired);
        for(const auto& w:a.weapons){add(w.cooldown_ticks);add(w.inter_shot_ticks);add(w.yaw.angle_turn16);add(w.pitch.angle_turn16);}
    }
    for(const auto& p:run.projectiles){add(p.serial);add(p.shot_definition_id);add(p.spawn_tick);add(p.travel_ticks);add(p.impact_damage_applied);}
    for(const auto& mask:run.faction_technology_masks)for(auto v:mask)add(v);
    for(const auto& economy:run.faction_economy){add(economy.xenite_current_q4);add(economy.tritium_current_q4);}
    return h;
}
void fixture(uint32_t runs,uint32_t threads){
    auto field=FieldGrid::create(40,40,0,0);const auto c=field.at(20,20);
    SimulationLabSeedEntity a;a.army_asset_id=1;a.faction_index=1;a.runtime_model_class_id=1;
    a.root_model_maximum_integrity=1000000;a.actor.position={q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),0};
    a.motion.speed_q12_per_tick=128;a.motion.yaw_max_velocity_turn16=a.motion.yaw_acceleration_turn16=0x4000;
    a.shot_impact_class_index=2;
    SimulationWeaponProfile weapon;weapon.shot_definition_id=1;weapon.reload_ticks=3;
    weapon.maximum_range_q12=8*4096;weapon.launch_attachment_count=1;
    weapon.yaw_max_velocity_turn16=weapon.yaw_acceleration_turn16=0x4000;
    weapon.pitch_max_velocity_turn16=weapon.pitch_acceleration_turn16=0x4000;
    weapon.pitch_min_turn16=-0x4000;weapon.pitch_max_turn16=0x4000;
    ShotDefinitionRecord shot;shot.definition_id=1;shot.trajectory_mode=0;shot.projectile_lifetime_ticks=4;
    shot.launch_speed_q12=4096;shot.target_impact_damage_q12[2]=256;
    a.combat_weapons={{weapon,shot}};a.actor.weapons.resize(1);
    auto b=a;b.army_asset_id=2;b.faction_index=2;b.actor.position.x+=4;
    const std::array seeds{a,b};std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    SimulationLabConfig cfg;cfg.run_count=runs;cfg.duration_ticks=128;cfg.worker_thread_limit=threads;
    cfg.parameter_index=100;cfg.vary_seed_per_run=true;
    SimulationLabExperiment serial;serial.reset(cfg,parameters,seeds);serial.step(field,128);
    check(serial.runs()[0].metrics.weapon_shots_fired>0,"pool fixture did not exercise combat");
    SimulationLabWorker worker;worker.start(cfg,parameters,seeds,field);auto initial=worker.snapshot();
    const auto count=simulation_lab_worker_thread_count(runs,threads,std::thread::hardware_concurrency());
    check(worker.run_worker_count()==count,"worker pool ignored the thread limit");
    worker.request_advance(7);wait(worker,7);auto held=worker.snapshot();
    for(const auto& r:held->runs())check(r.tick==7,"partial generation was published");
    worker.set_paused(true);worker.request_advance(5);check(worker.pending_ticks()==0,"pause accepted work");worker.set_paused(false);
    worker.request_advance(121);wait(worker,128);auto final=worker.snapshot();
    for(size_t i=0;i<runs;++i)check(digest(final->runs()[i])==digest(serial.runs()[i]),
        "pool changed combat, movement, resource or RNG state versus serial stepping");
    worker.clear();check(worker.run_worker_count()==0&&!worker.snapshot(),"pool clear retained active state");
    check(initial->runs()[0].tick==0&&held->runs()[0].tick==7,"publication changed a held snapshot");
    std::cout<<"POOL runs="<<runs<<" requested="<<threads<<" workers="<<count
        <<" tick=128 shots="<<final->runs()[0].metrics.weapon_shots_fired<<" digest="<<digest(final->runs()[0])<<'\n';
}
}
int main()try{
    check(simulation_lab_worker_thread_count(0,0,8)==0,"empty pool policy");
    for(uint32_t hw:{0U,1U,2U,3U})check(simulation_lab_worker_thread_count(32,0,hw)==1,"small host policy");
    check(simulation_lab_worker_thread_count(32,0,8)==6,"automatic editor headroom policy");
    check(simulation_lab_worker_thread_count(32,0,64)==32,"pool exceeded run limit");
    check(simulation_lab_worker_thread_count(32,2,64)==2,"manual pool limit");
    check(simulation_lab_worker_thread_count(4,32,64)==4,"more workers than runs");
    check(simulation_lab_worker_thread_count(32,100,64)==32,"invalid large limit was not bounded");
    for(uint32_t runs:{1U,4U,16U,32U})for(uint32_t limit:{0U,1U,2U,32U})fixture(runs,limit);
    std::cout<<"SIMLAB_WORKER_POOL PASS cases=16 serial_combat_equal=1 pause=1 held_snapshots=1\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
