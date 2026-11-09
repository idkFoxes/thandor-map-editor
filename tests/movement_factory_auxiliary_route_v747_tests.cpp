#include "thandor/simulation_lab_follow.hpp"
#include "thandor/path_diagnostics.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

using namespace thandor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}

SimulationLabEntity entity(V519PathWorldPointQ12 location,std::uint32_t faction,bool movable){
    SimulationLabEntity e;
    e.faction_index=faction;
    e.construction_complete=true;
    e.runtime_model_class_id=movable?2U:13U;
    e.actor.position={q12_to_float(location.x_q12),q12_to_float(location.y_q12),0.0F};
    e.motion.speed_q12_per_tick=movable?512:0;
    e.motion.collision_radius_q12=128;
    e.has_traversal_profile=movable;
    e.traversal_low_band_index=0;
    e.traversal_high_band_index=4;
    return e;
}

void factory_command_publishes_at_parent_callback(){
    const auto field=FieldGrid::create(72,72,0,0);
    const auto source=v519_path_scratch_center_to_world_q12({38,38});
    const auto exit=v519_path_scratch_center_to_world_q12({46,38});
    const auto rally=v519_path_scratch_center_to_world_q12({54,38});
    SimulationLabRun run;run.level_runtime_enabled=true;
    run.entities={entity(source,1,false),entity(source,1,true)};
    run.entities[1].production_linked_to_parent=true;run.entities[1].production_parent_entity_index=0;
    V565PathTerrainCache cache;run.defer_factory_exit_routes=true;
    simulation_lab_set_factory_exit_route(run,field,1,0,{exit.x_q12,exit.y_q12},{rally.x_q12,rally.y_q12},{},cache);
    require(run.metrics.navigation_route_resolves==0&&!run.entities[1].combat_movement_initialized,
        "factory command changed peer routes before the parent's native callback");
    run.entities[1].actor.position.x+=q12_to_float(512);
    run.defer_factory_exit_routes=false;
    simulation_lab_flush_factory_exit_routes(run,field,42,{},cache);
    require(run.metrics.navigation_route_resolves==0,"another owner's callback published the factory command");
    simulation_lab_flush_factory_exit_routes(run,field,0,{},cache);
    const auto& child=run.entities[1];
    require(run.metrics.navigation_route_resolves==1&&run.deferred_factory_exit_routes.empty(),
        "parent callback failed to publish exactly one factory exit command");
    require(child.combat_movement.last_checked.x==source.x_q12+512&&
            child.combat_movement.last_checked.y==source.y_q12,
        "deferred factory route used the position from the earlier bookkeeping pass");
    require((child.combat_movement.flags&0x24BU)==0x24BU,
        "deferred command lost the native locked auxiliary order");
}

void locked_auxiliary_state_and_parent_exclusion(uint32_t cls=2U){
    const auto field=FieldGrid::create(72,72,0,0);
    const auto source=v519_path_scratch_center_to_world_q12({38,38});
    const auto exit=v519_path_scratch_center_to_world_q12({46,38});
    const auto rally=v519_path_scratch_center_to_world_q12({54,38});
    SimulationLabRun run;
    auto parent=entity(source,1,false);
    parent.motion.collision_radius_q12=12*4096; // would cover the route if not excluded
    run.entities.push_back(parent);
    run.entities.push_back(entity(source,1,true));
    run.entities[1].production_linked_to_parent=true;
    run.entities[1].production_parent_entity_index=0;
    run.level_runtime_enabled=true;
    run.entities[1].runtime_model_class_id=cls;
    V565PathTerrainCache cache;

    simulation_lab_set_factory_exit_route(run,field,1,0,
        {exit.x_q12,exit.y_q12},{rally.x_q12,rally.y_q12},{},cache);

    auto& child=run.entities[1];
    require(child.combat_follow_owned,"factory child lost native waypoint ownership");
    require((child.combat_movement.flags&0x24BU)==0x24BU,
        "factory auxiliary order did not set active/locked/queue/ordered/routed flags");
    require((child.combat_movement.flags&0xB0U)==0U,
        "factory auxiliary order retained direct/follow/reached flags");
    require(child.combat_movement.queue_count==1U&&
            child.combat_movement.queue[0].x==rally.x_q12&&
            child.combat_movement.queue[0].y==rally.y_q12,
        "factory auxiliary order did not replace the queue with its rally point");
    require(child.combat_movement.fallback.x==exit.x_q12&&
            child.combat_movement.fallback.y==exit.y_q12,
        "factory parent was treated as a route blocker");
    require(child.actor.move_target&&child.actor.move_waypoint&&
            float_to_q12(child.actor.move_target->x)==rally.x_q12&&
            float_to_q12(child.actor.move_waypoint->x)==child.combat_movement.stored.x,
        "factory exit/rally publication does not match the native auxiliary command");
    require(run.metrics.navigation_route_resolves==1U&&child.navigation_target_cell_valid,
        "factory auxiliary order was not resolved and cached in the issue tick");
    if(cls==18U){
        child.actor.position.x+=q12_to_float(512);
        simulation_lab_finish_follow_motion(child,false,true);
        ++run.tick;
        require(simulation_lab_prepare_follow_motion(run,field,1,{},cache,true),
            "factory-linked Pioneer stopped instead of servicing its blocked waypoint");
        require(run.metrics.navigation_route_resolves==2U&&
                child.combat_movement.retry==0x40U&&(child.combat_movement.flags&2U),
            "factory-linked Pioneer deferred the native collision retry or lost the lock");
    }
}

void obstructed_exit_publishes_primary_route_point(){
    const auto field=FieldGrid::create(72,72,0,0);
    const auto source=v519_path_scratch_center_to_world_q12({34,42});
    const auto exit=v519_path_scratch_center_to_world_q12({58,42});
    const auto rally=v519_path_scratch_center_to_world_q12({62,42});
    const auto obstacle=v519_path_scratch_center_to_world_q12({46,42});
    SimulationLabRun run;
    run.entities.push_back(entity(source,1,false));
    run.entities.push_back(entity(source,1,true));
    run.entities[1].production_linked_to_parent=true;
    run.entities[1].production_parent_entity_index=0;
    const std::array blockers{V528PathRuntimeBlocker{
        obstacle.x_q12,obstacle.y_q12,3*4096}};
    V565PathTerrainCache cache;

    simulation_lab_set_factory_exit_route(run,field,1,0,
        {exit.x_q12,exit.y_q12},{rally.x_q12,rally.y_q12},blockers,cache);

    const auto& child=run.entities[1];
    require(child.actor.move_waypoint.has_value(),
        "obstructed factory exit did not publish an issue-tick route point");
    require(child.combat_movement.fallback.x==exit.x_q12&&
            child.combat_movement.fallback.y==exit.y_q12,
        "obstructed factory route lost its final exit destination");
    require(child.combat_movement.stored.x!=exit.x_q12||
            child.combat_movement.stored.y!=exit.y_q12,
        "obstructed factory exit incorrectly remained a direct route");
    require(float_to_q12(child.actor.move_target->x)==rally.x_q12&&
            float_to_q12(child.actor.move_waypoint->x)==child.combat_movement.stored.x&&
            float_to_q12(child.actor.move_waypoint->y)==child.combat_movement.stored.y,
        "obstructed factory route did not keep rally final and route primary separate");
    require(run.metrics.navigation_routed_solves==1U,
        "obstructed factory exit did not count the routed solve");
}
}

int main()try{
    factory_command_publishes_at_parent_callback();
    locked_auxiliary_state_and_parent_exclusion();
    locked_auxiliary_state_and_parent_exclusion(17U);
    locked_auxiliary_state_and_parent_exclusion(18U);
    obstructed_exit_publishes_primary_route_point();
    std::cout<<"V747_FACTORY_AUXILIARY_ROUTE PASS state=1 obstacle=1\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
