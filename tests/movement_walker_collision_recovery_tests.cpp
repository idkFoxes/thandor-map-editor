#include "thandor/simulation_lab_geometry.hpp"
#include "thandor/simulation_command_markers.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
Vec3f supported_root(const SimulationLabEntity& e){
    const auto& a=e.actor;auto feet=a.articulated_foot_anchor_world;
    if(a.articulated_contact_transition_active&&(a.articulated_was_moving||a.articulated_contact_settling)){
        const auto side=a.articulated_swing_side&1U;
        const float t=static_cast<float>(a.articulated_contact_progress_q12)/4096.F;
        const auto target=a.articulated_foot_target_world[side];auto& p=feet[side];
        p={p.x+(target.x-p.x)*t,p.y+(target.y-p.y)*t,
            p.z+(target.z-p.z)*t+4.F*q12_to_float(e.articulated_contact_lift_q12)*t*(1.F-t)};
    }
    return {(feet[0].x+feet[1].x)*.5F,(feet[0].y+feet[1].y)*.5F,
        (feet[0].z+feet[1].z)*.5F+e.articulated_shared_foot_placement_offset+q12_to_float(e.articulated_root_height_q12)};
}
void run_case(uint32_t side,bool live,uint32_t batch,bool stop_after_rejection,bool tiny_advance=false){
    auto field=FieldGrid::create(60,60,0,0);const auto cell=field.at(20,20);
    const Vec3f start{q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),1.F+.015625F};
    const Vec3f goal{start.x+8,start.y,start.z};
    SimulationLabSeedEntity unit;unit.army_asset_id=1;unit.faction_index=1;
    unit.runtime_model_class_id=3;unit.root_model_maximum_integrity=1000;unit.actor.position=start;
    unit.motion.speed_q12_per_tick=64;unit.motion.collision_radius_q12=512;
    unit.has_traversal_profile=true;unit.traversal_low_band_index=0;unit.traversal_high_band_index=4;
    unit.articulated_contact_geometry_enabled=true;
    unit.articulated_contact_local_offsets={Vec3f{0,.3F,0},Vec3f{0,-.3F,0}};
    unit.articulated_contact_interpolation_rate_q12=4096;unit.articulated_contact_lift_q12=128;
    unit.articulated_root_height_q12=4096;unit.articulated_contact_steering_limit_turn16=0x2000;
    auto& a=unit.actor;a.articulated_contacts_initialized=true;
    a.articulated_contact_transition_active=a.articulated_pose_transition_active=a.articulated_was_moving=true;
    a.articulated_contact_progress_q12=2048;a.articulated_contact_step_q12=32768;
    a.articulated_last_contact_advance_q12=tiny_advance?1U:512U;
    a.articulated_swing_side=a.articulated_pose_swing_side=side;
    a.articulated_foot_anchor_world={Vec3f{start.x-.25F,start.y+.3F,0},Vec3f{start.x-.25F,start.y-.3F,0}};
    a.articulated_foot_target_world=a.articulated_foot_anchor_world;
    a.articulated_foot_target_world[side].x=start.x+.75F;
    const V528PathRuntimeBlocker obstruction{float_to_q12(start.x+.4F),float_to_q12(start.y),512};
    SimulationLabScenarioRuntimeSeed scene;scene.enabled=true;scene.active_faction_count=1;scene.ai_planning_enabled=false;
    std::vector<SimulationLabSeedEntity> seeds{unit};
    if(live){SimulationLabSeedEntity blocker;blocker.army_asset_id=300;blocker.faction_index=1;
        blocker.runtime_model_class_id=11;blocker.building=true;blocker.root_model_maximum_integrity=1000;
        blocker.actor.position={q12_to_float(obstruction.world_x_q12),q12_to_float(obstruction.world_y_q12),0};
        blocker.motion.collision_radius_q12=512;seeds.push_back(blocker);
    }else scene.static_navigation_blockers.push_back(obstruction);
    SimulationLabConfig config;config.run_count=1;config.duration_ticks=2048;config.runtime_callback_step_ticks=batch;
    std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    SimulationLabExperiment lab;lab.reset(config,parameters,seeds,{},{},{},{},{},scene);
    auto& run=lab.runs()[0];auto& e=run.entities[0];e.actor.move_target=goal;
    // A cached route is overtaken by an obstruction during a frozen foot swing.
    // Its first physical rejection must roll back the complete pose, then replan.
    e.navigation_target_cell_valid=e.navigation_direct_route_valid=true;
    e.navigation_target_cell=v519_path_world_to_scratch(float_to_q12(goal.x),float_to_q12(goal.y));
    e.navigation_recheck_tick=1000;
    bool stopped=false;float peak_error=0;
    std::optional<unsigned> first_rejection,first_landing;
    for(unsigned tick=0;tick<2048;++tick){
        const bool landing_owned=e.actor.articulated_contact_settling;
        const auto routes=run.metrics.navigation_route_resolves;
        lab.step(field,1);const auto root=supported_root(e);
        if(landing_owned)require(run.metrics.navigation_route_resolves==routes,
            "route selection interrupted an owned landing");
        if(!first_rejection&&run.metrics.navigation_actual_step_blocks)first_rejection=tick;
        if(first_rejection&&!first_landing&&e.actor.articulated_contact_event_serial)first_landing=tick;
        const float error=std::hypot(e.actor.position.x-root.x,e.actor.position.y-root.y);
        peak_error=std::max(peak_error,error);
        require(error<1.F/4096.F,"body position detached from its historical foot contacts");
        require(std::abs(shortest_turn16_delta(e.actor.body_yaw_turn16,simulation_articulated_support_heading(e.actor)))<=1,
            "body heading detached from contact headings");
        require(std::abs(e.actor.position.z-root.z)<1.F/4096.F,"body height detached from its foot lift");
        const int64_t dx=int64_t(float_to_q12(e.actor.position.x))-obstruction.world_x_q12;
        const int64_t dy=int64_t(float_to_q12(e.actor.position.y))-obstruction.world_y_q12;
        require(dx*dx+dy*dy>int64_t(1024)*1024,"recovery moved through an obstruction");
        if(stop_after_rejection&&!stopped&&run.metrics.navigation_actual_step_blocks){
            require(e.actor.articulated_contact_progress_q12==0&&e.actor.articulated_contact_settling,
                "rejected step did not start a continuous landing at zero progress");
            e.actor.move_target.reset();e.actor.move_waypoint.reset();stopped=true;
        }
    }
    require(run.metrics.navigation_actual_step_blocks>0,"fixture did not exercise a rejected physical step");
    require(first_landing&&*first_landing-*first_rejection<128,
        "short recovery landing reused a nearly zero provisional contact advance");
    require(!e.actor.move_target&&!e.actor.move_waypoint,"recovered route did not finish");
    require(!e.actor.articulated_contact_transition_active,"moving foot did not settle after stopping");
    require(e.actor.articulated_contact_event_serial>0,"contact remained frozen without a completed landing");
    require(run.metrics.articulated_contact_completions==e.actor.articulated_contact_event_serial,
        "rejected provisional contact emitted a phantom completion");
    require(simulation_lab_command_markers(run).empty(),"finished route left a marker");
    if(!stop_after_rejection)require(std::abs(e.actor.position.x-goal.x)<=1&&std::abs(e.actor.position.y-goal.y)<=1,
        "recovery cleared a command without arriving");
    std::cout<<"CASE side="<<side<<" live="<<live<<" batch="<<batch<<" stop="<<stop_after_rejection<<" tiny_advance="<<tiny_advance
        <<" landing_ticks="<<*first_landing-*first_rejection
        <<" peak_support_error="<<peak_error<<" contacts="<<e.actor.articulated_contact_event_serial
        <<" blocks="<<run.metrics.navigation_actual_step_blocks<<'\n';
}
void verify_waypoint_ownership(){
    auto field=FieldGrid::create(60,60,0,0);const auto cell=field.at(20,20);
    const Vec3f start{q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),1.F+.015625F};
    SimulationLabSeedEntity unit;unit.army_asset_id=1;unit.faction_index=1;unit.runtime_model_class_id=3;
    unit.root_model_maximum_integrity=1000;unit.actor.position=start;unit.motion.speed_q12_per_tick=64;
    unit.has_traversal_profile=true;unit.traversal_low_band_index=0;unit.traversal_high_band_index=4;
    unit.articulated_contact_geometry_enabled=true;
    unit.articulated_contact_local_offsets={Vec3f{0,.3F,0},Vec3f{0,-.3F,0}};
    unit.articulated_contact_interpolation_rate_q12=4096;unit.articulated_contact_lift_q12=128;
    unit.articulated_root_height_q12=4096;unit.articulated_contact_steering_limit_turn16=0x2000;
    auto& a=unit.actor;a.articulated_contacts_initialized=true;
    a.articulated_contact_transition_active=a.articulated_pose_transition_active=a.articulated_was_moving=true;
    a.articulated_contact_progress_q12=2048;a.articulated_contact_step_q12=32768;
    a.articulated_last_contact_advance_q12=512;
    a.articulated_foot_anchor_world={Vec3f{start.x-.25F,start.y+.3F,0},Vec3f{start.x-.25F,start.y-.3F,0}};
    a.articulated_foot_target_world=a.articulated_foot_anchor_world;
    a.articulated_foot_target_world[0].x=start.x+.75F;
    SimulationLabScenarioRuntimeSeed scene;scene.enabled=true;scene.active_faction_count=1;scene.ai_planning_enabled=false;
    SimulationLabConfig config;config.run_count=1;config.duration_ticks=2048;
    std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    SimulationLabExperiment lab;lab.reset(config,parameters,std::span(&unit,1),{},{},{},{},{},scene);
    auto& e=lab.runs()[0].entities[0];const Vec3f goal{start.x+8,start.y,start.z};
    const Vec3f waypoint{start.x+.01F,start.y,start.z};
    e.actor.move_target=goal;e.actor.move_waypoint=waypoint;
    e.navigation_target_cell_valid=e.navigation_direct_route_valid=true;
    e.navigation_target_cell=v519_path_world_to_scratch(float_to_q12(goal.x),float_to_q12(goal.y));
    e.navigation_recheck_tick=1000;lab.step(field,1);
    const float distance=std::hypot(e.actor.position.x-waypoint.x,e.actor.position.y-waypoint.y);
    require(distance>1.F/64.F,"waypoint fixture did not separate provisional and contact-owned arrival");
    require(e.actor.move_waypoint.has_value(),"provisional body snap consumed an unreached walker waypoint");
    lab.step(field,2047);
    require(!e.actor.move_target&&!e.actor.move_waypoint,"retaining contact-owned waypoint prevented route completion");
    require(!e.actor.articulated_contact_transition_active,"waypoint completion left a moving foot");
    std::cout<<"WAYPOINT admitted_distance="<<distance<<" retained_until_arrival=1 route_finishes=1\n";
}
}
int main()try{
    verify_waypoint_ownership();
    for(uint32_t side:{0U,1U})for(bool live:{false,true})for(uint32_t batch:{1U,5U})
        for(bool tiny:{false,true})run_case(side,live,batch,false,tiny);
    run_case(0,false,1,true);
    std::cout<<"WALKER_COLLISION_RECOVERY PASS cases=18 pose_atomic=1 contact_settles=1 route_finishes=1 landing_owned=1\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
