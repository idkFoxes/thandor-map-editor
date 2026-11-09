#include "thandor/simulation_lab_geometry.hpp"
#include "thandor/simulation_command_markers.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
SimulationLabSeedEntity mover(Vec3f p,uint32_t cls=1){
    SimulationLabSeedEntity s;s.army_asset_id=1;s.faction_index=1;s.runtime_model_class_id=cls;
    s.actor.position=p;s.root_model_maximum_integrity=1000;s.has_traversal_profile=true;
    s.traversal_low_band_index=0;s.traversal_high_band_index=4;
    s.motion.speed_q12_per_tick=128;s.motion.collision_radius_q12=1500;
    s.motion.yaw_max_velocity_turn16=s.motion.yaw_acceleration_turn16=0x4000;
    return s;
}
SimulationLabConfig config(){SimulationLabConfig c;c.run_count=1;c.duration_ticks=2000;
    c.sweep_mode=SimulationLabSweepMode::Fixed;return c;}
SimulationLabScenarioRuntimeSeed scenario(){SimulationLabScenarioRuntimeSeed s;s.enabled=true;
    s.active_faction_count=s.assignable_faction_count=1;s.ai_planning_enabled=false;return s;}
void verify_escape_queries(){
    const auto field=FieldGrid::create(40,40,0,0);
    const auto base=v519_path_scratch_center_to_world_q12({50,50});unsigned count=0;
    for(int ox=-240;ox<=240;ox+=120)for(int oy=-240;oy<=240;oy+=120)
    for(int angle=0;angle<16;++angle)for(int distance:{700,1200,1800}){
        auto p=base;p.x_q12+=ox;p.y_q12+=oy;
        const double a=angle*6.283185307179586/16;
        const V528PathRuntimeBlocker b{p.x_q12+int(std::lround(std::cos(a)*distance)),
            p.y_q12+int(std::lround(std::sin(a)*distance)),1500};
        const auto r=v565_resolve_route_fast_for_simulation_lab(field,p.x_q12,p.y_q12,
            p.x_q12+8*4096,p.y_q12,2,0,4,std::span(&b,1),1500);
        require(r.disposition==V528RouteDisposition::SourceRelocated&&r.primary_world_q12,
            "overlapped source with an outward escape did not resolve it");
        require(simulation_lab_path_world_segment_clear(p,*r.primary_world_q12,std::span(&b,1),1500),
            "source relocation crosses deeper into a blocking hull");++count;
    }
    std::cout<<"ESCAPE_QUERY cases="<<count<<" physically_admitted=1\n";
}
void verify_playback(bool live,uint32_t cls){
    const auto field=FieldGrid::create(40,40,0,0);
    const Vec3f from{q12_to_float(41682),q12_to_float(-24478),0},goal{from.x+8,from.y,0};
    const V528PathRuntimeBlocker blocker{41682,-23778,1500};
    auto scene=scenario();std::vector<SimulationLabSeedEntity> seeds{mover(from,cls)};
    if(live){auto b=mover({q12_to_float(blocker.world_x_q12),q12_to_float(blocker.world_y_q12),0});
        b.army_asset_id=300;b.runtime_model_class_id=11;b.building=true;b.motion.speed_q12_per_tick=0;
        seeds.push_back(b);
    }else scene.static_navigation_blockers.push_back(blocker);
    std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};SimulationLabExperiment lab;
    lab.reset(config(),ki,seeds,{},{},{},{},{},scene);auto& run=lab.runs()[0];auto& e=run.entities[0];
    e.actor.move_target=goal;uint32_t finished=0;
    for(unsigned tick=0;tick<2000;++tick){
        const auto before=e.actor.position;lab.step(field,1);
        require(!simulation_lab_step_hits_blocker(before,e.actor.position,1500,blocker),
            "admitted playback moved deeper through the blocker");
        if(!e.actor.move_target&&!e.actor.move_waypoint){finished=run.tick;break;}
    }
    require(finished,"crowded escape repeatedly rejects the same relocation");
    require(std::abs(e.actor.position.x-goal.x)<=1&&std::abs(e.actor.position.y-goal.y)<=1,
        "escape discarded the command without arriving");
    require(simulation_lab_command_markers(run).empty(),"arrived escape retained a drive marker");
    require(run.metrics.navigation_actual_step_blocks<8,"escape kept retrying a rejected direction");
    std::cout<<"ESCAPE_PLAYBACK live="<<live<<" cls="<<cls<<" arrived_tick="<<finished
        <<" blocks="<<run.metrics.navigation_actual_step_blocks<<'\n';
}
void verify_wait(bool replace_order){
    const auto field=FieldGrid::create(60,60,0,0);const auto p=field.at(25,25);
    const Vec3f from{q12_to_float(p.world_x_q12),q12_to_float(p.world_y_q12),0};
    auto b=mover(from);b.army_asset_id=300;b.motion.speed_q12_per_tick=0;
    // A temporary large occupancy covers the bounded relocation neighborhood.
    b.motion.collision_radius_q12=32768;
    const std::array seeds{mover(from),b};auto scene=scenario();
    SimulationLabExperiment lab;std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
    lab.reset(config(),ki,seeds,{},{},{},{},{},scene);auto& run=lab.runs()[0];auto& e=run.entities[0];
    Vec3f goal{from.x+8,from.y,0};e.actor.move_target=goal;lab.step(field,32);
    require(e.actor.move_target&&!e.actor.move_waypoint,"temporary blockage cancelled the drive command");
    require(e.actor.position.x==from.x&&e.actor.position.y==from.y,"trapped unit crossed the occupancy");
    require(run.metrics.navigation_route_resolves<=4,"failed route ignored its retry backoff");
    require(simulation_lab_command_markers(run).size()==1,"waiting drive intent disappeared");
    const auto held=lab.snapshot_copy();
    if(replace_order){goal={from.x,from.y-8,0};e.actor.move_target=goal;}
    run.entities[1].runtime_removed=true;
    lab.step(field,1968);
    require(!e.actor.move_target&&!e.actor.move_waypoint,"command failed to resume after occupancy cleared");
    require(std::abs(e.actor.position.x-goal.x)<=1&&std::abs(e.actor.position.y-goal.y)<=1,
        "resumed command did not reach the current target");
    require(simulation_lab_command_markers(run).empty(),"resumed arrival retained a drive marker");
    require(held.runs()[0].entities[0].actor.move_target.has_value(),"retained waiting snapshot was mutated");
    std::cout<<"WAIT_RESUME replaced="<<replace_order<<" bounded_retries=1 arrived=1 marker_cleared=1\n";
}
}
int main(int argc,char**argv)try{
    // Selectors make the two independent failure mechanisms reviewable in ablations.
    const std::string mode=argc>1?argv[1]:"all";
    if(mode!="wait"){verify_escape_queries();for(bool live:{false,true})for(uint32_t cls:{1U,2U})verify_playback(live,cls);}
    if(mode!="escape")for(bool replace:{false,true})verify_wait(replace);
    std::cout<<"MOVEMENT_CROWDED_ESCAPE PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
