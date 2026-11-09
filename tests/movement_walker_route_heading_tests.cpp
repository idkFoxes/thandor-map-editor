#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_command_markers.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void run_case(uint32_t heading,int32_t turn,uint32_t side,uint32_t batch,uint32_t limit){
    auto field=FieldGrid::create(80,80,0,0);const auto cell=field.at(40,40);
    const Vec3f start{q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),1};
    const auto requested=wrap_turn16(int64_t(heading)+turn);
    const float angle=turn16_to_radians(requested),initial_angle=turn16_to_radians(heading);
    const Vec3f goal{start.x+5.657F*std::cos(angle),start.y+5.657F*std::sin(angle),1};
    SimulationLabSeedEntity unit;unit.army_asset_id=1;unit.faction_index=1;unit.runtime_model_class_id=3;
    unit.root_model_maximum_integrity=1000;unit.actor.position=start;unit.motion.speed_q12_per_tick=64;
    unit.articulated_contact_geometry_enabled=true;unit.articulated_root_height_q12=4096;
    unit.articulated_contact_interpolation_rate_q12=1400;unit.articulated_contact_lift_q12=250;
    unit.articulated_contact_steering_limit_turn16=limit;
    unit.articulated_contact_local_offsets={Vec3f{0,.3F,0},Vec3f{0,-.3F,0}};
    auto& a=unit.actor;a.body_yaw_turn16=heading;a.articulated_contacts_initialized=true;
    a.articulated_swing_side=a.articulated_pose_swing_side=side;
    a.articulated_contact_heading_turn16.fill(heading);a.articulated_contact_target_heading_turn16.fill(heading);
    for(unsigned i=0;i<2;++i){const float sign=i==0?1.F:-1.F;
        a.articulated_foot_anchor_world[i]={start.x-sign*.3F*std::sin(initial_angle),
            start.y+sign*.3F*std::cos(initial_angle),0};}
    a.articulated_foot_target_world=a.articulated_foot_anchor_world;
    SimulationLabConfig config;config.run_count=1;config.duration_ticks=2048;config.runtime_callback_step_ticks=batch;
    SimulationLabScenarioRuntimeSeed scene;scene.enabled=true;scene.active_faction_count=1;scene.ai_planning_enabled=false;
    std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};SimulationLabExperiment lab;
    lab.reset(config,parameters,std::span(&unit,1),{},{},{},{},{},scene);
    auto& e=lab.runs()[0].entities[0];e.actor.move_target=goal;lab.step(field,1);
    require(e.actor.articulated_contact_transition_active,"fixture did not start a walking contact");
    const auto measured=radians_to_turn16(std::atan2(goal.y-start.y,goal.x-start.x));
    const auto delta=std::clamp(shortest_turn16_delta(heading,measured),-int32_t(limit),int32_t(limit));
    const auto expected=wrap_turn16(int64_t(heading)+delta);
    require(std::abs(shortest_turn16_delta(expected,e.actor.articulated_contact_target_heading_turn16[side]))<=1,
        "walking contact froze the provisional body yaw instead of the route heading");
    unsigned arrived=0;
    for(unsigned tick=2;tick<=2048;++tick){
        lab.step(field,1);
        if(!e.actor.move_target&&!e.actor.articulated_contact_transition_active){arrived=tick;break;}
    }
    require(arrived>0,"clear walking route failed to finish");
    require(std::abs(e.actor.position.x-goal.x)<=1&&std::abs(e.actor.position.y-goal.y)<=1,
        "route cleared without reaching the arrival window");
    require(simulation_lab_command_markers(lab.runs()[0]).empty(),"completed walking route retained a marker");
    std::cout<<"HEADING start="<<heading<<" turn="<<turn<<" side="<<side<<" batch="<<batch
        <<" limit="<<limit<<" first_contact="<<expected<<" arrived="<<arrived<<'\n';
}
}
int main()try{
    unsigned cases=0;
#if defined(NDEBUG)
    constexpr std::array headings{0U,0x2000U,0x8000U,0xF000U};
    constexpr std::array turns{-0x2000,-0x1000,-0x400,0,0x400,0x1000,0x2000};
    constexpr std::array limits{0x2000U,0x4000U};
    constexpr bool full_matrix=true;
#else
    // The exhaustive 224-case matrix is an optimized-runtime parity stress
    // test. Debug keeps representative wraparound, straight and opposite
    // headings across both feet and callback batches without spending several
    // minutes measuring instrumentation overhead.
    constexpr std::array headings{0U,0x8000U};
    constexpr std::array turns{-0x2000,0,0x2000};
    constexpr std::array limits{0x2000U};
    constexpr bool full_matrix=false;
#endif
    for(uint32_t heading:headings)
        for(int32_t turn:turns)
            for(uint32_t side:{0U,1U})for(uint32_t batch:{1U,5U})for(uint32_t limit:limits){
                run_case(heading,turn,side,batch,limit);++cases;
            }
    std::cout<<"WALKER_ROUTE_HEADING PASS cases="<<cases
        <<" full_matrix="<<(full_matrix?1:0)<<" native_full_controller=0\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
