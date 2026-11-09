#include "thandor/simulation_lab_follow.hpp"
#include "thandor/path_diagnostics.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

using namespace thandor;

namespace {
void require(bool value,const char* message){
    if(!value)throw std::runtime_error(message);
}

MovementPoint q12(Vec3f value){
    return {float_to_q12(value.x),float_to_q12(value.y)};
}

SimulationLabEntity movable_entity(const V519PathWorldPointQ12& world){
    SimulationLabEntity entity;
    entity.faction_index=1;
    entity.actor.position={q12_to_float(world.x_q12),q12_to_float(world.y_q12),0.0F};
    entity.motion.speed_q12_per_tick=256;
    entity.motion.collision_radius_q12=120;
    entity.construction_complete=true;
    return entity;
}

SimulationLabRun one_entity_run(SimulationLabEntity entity,std::uint32_t tick=0){
    SimulationLabRun run;
    run.tick=tick;
    run.entities.push_back(std::move(entity));
    return run;
}

void verify_clear_traversal_route_is_published_immediately(){
    const auto field=FieldGrid::create(40,40,0,0);
    const auto source=v519_path_scratch_center_to_world_q12({30,30});
    const MovementPoint target{source.x_q12+8192,source.y_q12};
    auto entity=movable_entity(source);
    entity.has_traversal_profile=true;
    entity.traversal_low_band_index=0;
    entity.traversal_high_band_index=4;
    entity.combat_command_mode_flags=0x411U;
    entity.combat_movement_initialized=true;
    entity.combat_movement.flags=0x4B8U;
    auto run=one_entity_run(std::move(entity),7);
    V565PathTerrainCache cache;

    simulation_lab_set_site_route(run,field,0,target,{},cache);

    const auto& current=run.entities[0];
    const auto& movement=current.combat_movement;
    require(movement.flags==0x241U,"routed command did not publish native active/ordered/routed flags");
    require(movement.fallback.x==target.x&&movement.fallback.y==target.y,
        "clear routed command did not publish its resolved fallback immediately");
    require(movement.primary.x==target.x&&movement.primary.y==target.y,
        "clear routed command did not publish its primary destination immediately");
    require(movement.stored.x==target.x&&movement.stored.y==target.y,
        "clear routed command did not publish its current route point immediately");
    require(movement.retry==0x40U&&movement.last_checked.x==source.x_q12&&
        movement.last_checked.y==source.y_q12,"routed command retry anchor differs from native order setup");
    require(current.combat_command_mode_flags==0x401U,
        "unlocked routed command cleared command-mode bits beyond selection-order bit 0x10");
    require(current.actor.move_target&&q12(*current.actor.move_target).x==target.x&&
        q12(*current.actor.move_target).y==target.y,"actor did not own the resolved command target in the issue tick");
    require(!current.actor.move_waypoint,"clear direct route fabricated an intermediate waypoint");
    require(current.navigation_target_cell_valid&&current.navigation_direct_route_valid,
        "clear routed command did not publish its navigation cache in the issue tick");
    require(current.navigation_recheck_tick==15U,"routed command did not use the bounded Lab recheck schedule");
    require(run.metrics.navigation_route_resolves==1U&&run.metrics.navigation_routed_solves==0U,
        "clear routed command was not resolved exactly once at issue time");
    require(current.combat_follow_route_resolves==0U,
        "AI routed command incorrectly polluted weapon-follow route telemetry");
}

void verify_obstructed_route_publishes_primary_and_fallback(){
    const auto field=FieldGrid::create(40,40,0,0);
    const V519PathWorldPointQ12 source{51430,-30086};
    const MovementPoint requested{63718,-30086};
    const std::array blockers{V528PathRuntimeBlocker{requested.x,requested.y,1024}};
    auto entity=movable_entity(source);
    entity.has_traversal_profile=true;
    entity.traversal_low_band_index=0;
    entity.traversal_high_band_index=4;
    auto run=one_entity_run(std::move(entity),3);
    V565PathTerrainCache expected_cache;
    const auto expected=v565_resolve_route_fast_for_simulation_lab(
        field,source.x_q12,source.y_q12,requested.x,requested.y,2U,0U,4U,blockers,120,
        &expected_cache,false);
    require(expected.usable()&&expected.primary_world_q12.has_value(),
        "obstructed route fixture did not produce a resolved primary waypoint");
    V565PathTerrainCache command_cache;

    simulation_lab_set_site_route(run,field,0,requested,blockers,command_cache);

    const auto& current=run.entities[0];
    const auto& movement=current.combat_movement;
    require(movement.fallback.x==expected.resolved_world_q12.x_q12&&
        movement.fallback.y==expected.resolved_world_q12.y_q12&&
        movement.primary.x==expected.resolved_world_q12.x_q12&&
        movement.primary.y==expected.resolved_world_q12.y_q12,
        "obstructed command did not map the resolved fallback to native target fields");
    require(movement.stored.x==expected.primary_world_q12->x_q12&&
        movement.stored.y==expected.primary_world_q12->y_q12,
        "obstructed command did not publish the route primary waypoint immediately");
    require(current.actor.move_target&&q12(*current.actor.move_target).x==movement.fallback.x&&
        q12(*current.actor.move_target).y==movement.fallback.y,
        "obstructed command actor target differs from its resolved fallback");
    require(current.actor.move_waypoint&&q12(*current.actor.move_waypoint).x==movement.stored.x&&
        q12(*current.actor.move_waypoint).y==movement.stored.y,
        "obstructed command actor waypoint differs from its resolved primary");
    require(current.navigation_target_cell_valid&&
        current.navigation_target_cell==expected.resolved_cell&&
        !current.navigation_direct_route_valid,
        "obstructed command did not publish its resolved navigation cache");
    require(run.metrics.navigation_route_resolves==1U&&
        run.metrics.navigation_routed_solves==1U,
        "obstructed routed command was not solved exactly once at issue time");
}

void verify_out_of_grid_destination_is_clipped_immediately(){
    const auto field=FieldGrid::create(40,40,0,0);
    const auto source=v519_path_scratch_center_to_world_q12({40,40});
    auto run=one_entity_run(movable_entity(source));
    const MovementPoint requested{4096*1000,-4096*1000};
    const auto expected_cell=v519_path_clip_to_inner_scratch(
        field,v519_path_world_to_scratch(requested.x,requested.y));
    const auto expected=v519_path_scratch_center_to_world_q12(expected_cell);
    V565PathTerrainCache cache;

    simulation_lab_set_site_route(run,field,0,requested,{},cache);

    const auto& current=run.entities[0];
    const auto& movement=current.combat_movement;
    require(movement.fallback.x==expected.x_q12&&movement.fallback.y==expected.y_q12,
        "out-of-grid command retained an unresolved raw fallback");
    require(movement.primary.x==expected.x_q12&&movement.primary.y==expected.y_q12&&
        movement.stored.x==expected.x_q12&&movement.stored.y==expected.y_q12,
        "out-of-grid command did not publish its clipped route coordinates in the issue tick");
    require(current.actor.move_target&&q12(*current.actor.move_target).x==expected.x_q12&&
        q12(*current.actor.move_target).y==expected.y_q12,
        "actor retained an out-of-grid raw command target");
    require(current.navigation_target_cell_valid&&current.navigation_target_cell==expected_cell,
        "clipped command did not publish the resolved scratch destination");
    require(run.metrics.navigation_destination_retargets==1U&&run.metrics.navigation_route_resolves==1U,
        "clipped command telemetry did not record one immediate retarget/resolve");
}

void verify_locked_command_appends_without_replacing_active_route(){
    const auto field=FieldGrid::create(40,40,0,0);
    const auto source=v519_path_scratch_center_to_world_q12({35,35});
    const auto active=v519_path_scratch_center_to_world_q12({38,35});
    const auto waypoint=v519_path_scratch_center_to_world_q12({37,34});
    const MovementPoint queued{active.x_q12+4096,active.y_q12+4096};
    auto entity=movable_entity(source);
    entity.production_linked_to_parent=true;
    entity.combat_command_mode_flags=0x411U;
    entity.actor.move_target=Vec3f{q12_to_float(active.x_q12),q12_to_float(active.y_q12),0.0F};
    entity.actor.move_waypoint=Vec3f{q12_to_float(waypoint.x_q12),q12_to_float(waypoint.y_q12),0.0F};
    entity.navigation_target_cell_valid=true;
    entity.navigation_target_cell={38,35};
    entity.navigation_recheck_tick=1234;
    entity.navigation_direct_route_valid=false;
    auto run=one_entity_run(std::move(entity));
    V565PathTerrainCache cache;

    simulation_lab_set_site_route(run,field,0,queued,{},cache);

    const auto& current=run.entities[0];
    const auto& movement=current.combat_movement;
    require(current.actor.move_target&&q12(*current.actor.move_target).x==active.x_q12&&
        q12(*current.actor.move_target).y==active.y_q12,
        "movement-locked routed command replaced the active actor target");
    require(current.actor.move_waypoint&&q12(*current.actor.move_waypoint).x==waypoint.x_q12&&
        q12(*current.actor.move_waypoint).y==waypoint.y_q12,
        "movement-locked routed command replaced the active actor waypoint");
    require(movement.fallback.x==active.x_q12&&movement.fallback.y==active.y_q12&&
        movement.stored.x==waypoint.x_q12&&movement.stored.y==waypoint.y_q12,
        "movement-locked queue branch altered the active native route coordinates");
    require(movement.queue_count==1U&&movement.queue[0].x==queued.x&&movement.queue[0].y==queued.y,
        "movement-locked command did not append the requested waypoint");
    require((movement.flags&0x4BU)==0x4BU,
        "movement-locked command lost active/locked/queued/ordered state");
    require(current.combat_command_mode_flags==1U,
        "locked waypoint helper did not clear native 0x410 command-mode bits");
    require(current.navigation_target_cell_valid&&current.navigation_target_cell==V519PathScratchCoordinate{38,35}&&
        current.navigation_recheck_tick==1234&&!current.navigation_direct_route_valid,
        "movement-locked command replaced the active navigation cache");
    require(run.metrics.navigation_route_resolves==0U&&run.metrics.navigation_destination_retargets==0U,
        "movement-locked queue branch incorrectly invoked route resolution");
}

void verify_immobile_command_is_a_strict_no_op(){
    const auto field=FieldGrid::create(40,40,0,0);
    const auto source=v519_path_scratch_center_to_world_q12({32,32});
    auto entity=movable_entity(source);
    entity.motion.speed_q12_per_tick=0;
    entity.combat_command_mode_flags=0x411U;
    entity.combat_movement_initialized=false;
    entity.combat_movement.flags=0x2A5U;
    entity.combat_movement.retry=17;
    entity.combat_movement.primary={11,12};
    entity.actor.move_target=Vec3f{1.25F,-2.5F,3.0F};
    entity.navigation_target_cell_valid=true;
    entity.navigation_target_cell={12,13};
    entity.navigation_recheck_tick=77;
    entity.navigation_direct_route_valid=true;
    auto run=one_entity_run(std::move(entity));
    const auto before_target=*run.entities[0].actor.move_target;
    V565PathTerrainCache cache;

    simulation_lab_set_site_route(run,field,0,{999,888},{},cache);

    const auto& current=run.entities[0];
    require(!current.combat_movement_initialized&&current.combat_movement.flags==0x2A5U&&
        current.combat_movement.retry==17U&&current.combat_movement.primary.x==11&&
        current.combat_movement.primary.y==12,"immobile command mutated movement state");
    require(current.combat_command_mode_flags==0x411U,
        "immobile command mutated command-mode state");
    require(current.actor.move_target&&current.actor.move_target->x==before_target.x&&
        current.actor.move_target->y==before_target.y&&current.actor.move_target->z==before_target.z,
        "immobile command mutated actor drive state");
    require(current.navigation_target_cell_valid&&current.navigation_target_cell==V519PathScratchCoordinate{12,13}&&
        current.navigation_recheck_tick==77U&&current.navigation_direct_route_valid,
        "immobile command mutated navigation cache state");
    require(run.metrics.navigation_route_resolves==0U&&run.metrics.navigation_destination_retargets==0U,
        "immobile command invoked navigation work");
}
}

int main()try{
    verify_clear_traversal_route_is_published_immediately();
    verify_obstructed_route_publishes_primary_and_fallback();
    verify_out_of_grid_destination_is_clipped_immediately();
    verify_locked_command_appends_without_replacing_active_route();
    verify_immobile_command_is_a_strict_no_op();
    std::cout<<"MOVEMENT_ROUTED_COMMAND PASS immediate=1 obstructed=1 clipped=1 locked_queue=1 immobile_noop=1\n";
}catch(const std::exception& error){
    std::cerr<<error.what()<<'\n';
    return 1;
}
