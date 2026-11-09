#include "thandor/simulation_lab_catalog.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/simulation_command_markers.hpp"
#include "thandor/simulation_lab_geometry.hpp"
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
        const auto end=a.articulated_foot_target_world[side];auto& p=feet[side];
        p={p.x+(end.x-p.x)*t,p.y+(end.y-p.y)*t,
            p.z+(end.z-p.z)*t+4.F*q12_to_float(e.articulated_contact_lift_q12)*t*(1.F-t)};
    }
    return {(feet[0].x+feet[1].x)*.5F,(feet[0].y+feet[1].y)*.5F,
        (feet[0].z+feet[1].z)*.5F+e.articulated_shared_foot_placement_offset+q12_to_float(e.articulated_root_height_q12)};
}
void run_case(const AssetCatalog& catalog,const std::shared_ptr<const SimulationLabRuntimeCatalog>& runtime,
              uint32_t arm,int obstacle,bool synthetic,uint32_t batch){
    auto field=FieldGrid::create(59,59,0,0);const auto cell=field.at(29,29);
    const Vec3f center{q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
    const Vec3f rally{center.x+4,center.y-4,0};
    SimulationLabSeedEntity factory;factory.army_asset_id=303;factory.faction_index=1;
    factory.runtime_model_class_id=13;factory.building=true;factory.actor.position=center;
    factory.root_model_maximum_integrity=1000;factory.motion.collision_radius_q12=4096;
    simulation_lab_prepare_model_technologies(*catalog.army_preset(303),catalog,factory);
    SimulationLabProductionAsset product;product.army_asset_id=arm;product.entity_seed.army_asset_id=arm;
    product.entity_seed.articulated_contact_interpolation_rate_q12=
        catalog.army_preset(arm)->articulated_contact_interpolation_rate_q12;
    SimulationLabScenarioRuntimeSeed scene;scene.runtime_catalog=runtime;scene.enabled=true;
    scene.active_faction_count=scene.assignable_faction_count=1;scene.ai_planning_enabled=false;
    std::vector<SimulationLabSeedEntity> seeds{factory};
    const int32_t radius=float_to_q12(.7F);
    if(obstacle==1)scene.static_navigation_blockers.push_back({float_to_q12(rally.x),float_to_q12(rally.y),radius});
    if(obstacle==2){
        SimulationLabSeedEntity building;building.army_asset_id=333;building.faction_index=1;
        building.runtime_model_class_id=15;building.building=true;building.actor.position=rally;
        building.root_model_maximum_integrity=1000;building.motion.collision_radius_q12=radius;
        seeds.push_back(building);
    }
    FactionTechnologyMask256 all{};all.fill(~0U);std::array<FactionTechnologyMask256,8> masks{};masks[1]=all;
    SimulationLabConfig config;config.run_count=1;config.duration_ticks=4160;
    config.runtime_callback_step_ticks=batch;config.sweep_mode=SimulationLabSweepMode::Fixed;
    std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    const std::array products{product};SimulationLabExperiment lab;
    lab.reset(config,parameters,seeds,{},products,{},masks,{},scene);
    auto& run=lab.runs()[0];auto& f=run.entities[0];f.synthetic=synthetic;f.production_building=true;
    f.production_asset_id=arm;f.production_duration_ticks=1;
    if(obstacle>=0)f.production_drive_target_world=rally;
    std::optional<Vec3f> resolved_target;
    unsigned completed_tick=0;float max_support_error=0;uint64_t completion_serial=0;
    Vec3f stopped{};bool stable_checked=false;
    for(unsigned tick=1;tick<=4160;++tick){
        lab.step(field,1);
        if(run.entities.size()==seeds.size())continue;
        const auto& child=run.entities.back();const auto& a=child.actor;
        if(a.move_target)resolved_target=a.move_target;
        if(a.articulated_contacts_initialized){
            const auto support=supported_root(child);
            const float error=std::hypot(a.position.x-support.x,a.position.y-support.y);
            max_support_error=std::max(max_support_error,error);
            require(error<1.F/4096.F&&std::abs(a.position.z-support.z)<1.F/4096.F,
                "rally motion detached body from contact support");
        }
        if(obstacle>0){
            const auto r=obstacle==1?radius:run.entities[1].motion.collision_radius_q12;
            const int64_t dx=int64_t(float_to_q12(a.position.x))-float_to_q12(rally.x);
            const int64_t dy=int64_t(float_to_q12(a.position.y))-float_to_q12(rally.y);
            const int64_t sum=int64_t(r)+child.motion.collision_radius_q12;
            require(dx*dx+dy*dy>sum*sum,"rally walker entered the obstructing footprint");
        }
        if(!completed_tick&&!child.production_linked_to_parent&&!a.move_target&&!a.move_waypoint&&
           !a.articulated_contact_transition_active&&run.entities[0].production_behavior_state==0){
            completed_tick=tick;stopped=a.position;completion_serial=a.articulated_contact_event_serial;
        }
        if(completed_tick&&tick>=completed_tick+64){
            require(std::hypot(a.position.x-stopped.x,a.position.y-stopped.y)<1.F/4096.F&&
                completion_serial==a.articulated_contact_event_serial,"completed rally continued walking");
            stable_checked=true;break;
        }
    }
    const auto& child=run.entities.back();
    if(!completed_tick)std::cerr<<"RALLY_PENDING arm="<<arm<<" obstacle="<<obstacle<<" synthetic="<<synthetic
        <<" batch="<<batch<<" xy="<<child.actor.position.x-center.x<<","<<child.actor.position.y-center.y
        <<" target="<<bool(child.actor.move_target)<<" waypoint="<<bool(child.actor.move_waypoint)
        <<" phase="<<child.actor.articulated_contact_progress_q12<<" advance="<<child.actor.articulated_last_contact_advance_q12
        <<" settling="<<child.actor.articulated_contact_settling<<" contacts="<<child.actor.articulated_contact_event_serial
        <<" blocks="<<run.metrics.navigation_actual_step_blocks<<'\n';
    require(run.metrics.class13_units_spawned==1&&child.runtime_model_class_id==3,"walker was not produced");
    require(completed_tick>0&&completed_tick<=4096,"factory rally command or foot contact remained active at deadline");
    require(stable_checked,"completed rally did not receive its 64-tick idle check");
    require(!child.production_parent_entity_index&&run.entities[0].production_behavior_state==0,
        "factory did not release the child and return to idle");
    require(resolved_target.has_value(),"factory never issued its movement command");
    require(std::abs(child.actor.position.x-resolved_target->x)<=1.F&&
        std::abs(child.actor.position.y-resolved_target->y)<=1.F,
        "rally command cleared without arriving at its resolved destination");
    const auto markers=simulation_lab_command_markers(run);
    require(std::none_of(markers.begin(),markers.end(),[&](const auto& m){
        return m.owner_entity_index==run.entities.size()-1;}),"completed child retained a drive marker");
    if(obstacle>0)require(run.metrics.navigation_destination_retargets>0,"obstructed rally point was not resolved");
    if(obstacle>=0)require(run.entities[0].production_drive_target_world&&
        run.entities[0].production_drive_target_world->x==rally.x&&
        run.entities[0].production_drive_target_world->y==rally.y,"child overwrote the standing factory rally order");
    std::cout<<"RALLY arm="<<arm<<" loaded_arm="<<child.army_asset_id<<" obstacle="<<obstacle
        <<" synthetic="<<synthetic<<" batch="<<batch<<" completed_tick="<<completed_tick
        <<" blocks="<<run.metrics.navigation_actual_step_blocks<<" routes="<<run.metrics.navigation_route_resolves
        <<" support_error="<<max_support_error<<" child_markers=0\n";
}
}
int main(int argc,char** argv)try{
    require(argc==2,"usage: production_walker_rally_completion_tests GAME_ROOT");
    const auto catalog=AssetCatalog::scan_game_root(argv[1]);
    const auto runtime=simulation_lab_prepare_runtime_catalog(catalog);
    unsigned cases=0,failures=0;
    for(uint32_t arm:{111U,131U,151U})for(int obstacle:{-1,0,1,2})
        for(bool synthetic:{false,true})for(uint32_t batch:{1U,5U}){
            try{run_case(catalog,runtime,arm,obstacle,synthetic,batch);}
            catch(const std::exception& e){std::cerr<<"RALLY_FAIL arm="<<arm<<" obstacle="<<obstacle
                <<" synthetic="<<synthetic<<" batch="<<batch<<" error="<<e.what()<<'\n';++failures;}
            ++cases;
        }
    require(failures==0,"factory rally cases failed");
    std::cout<<"FACTORY_RALLY_COMPLETION PASS cases="<<cases<<" stock_body_families=3 gpu_verified=0\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
