#include "thandor/simulation_lab_follow.hpp"
#include "thandor/path_diagnostics.hpp"

#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace thandor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}

SimulationLabEntity movable(V519PathWorldPointQ12 position,std::uint32_t faction,
                            std::int32_t speed=512,std::int32_t radius=256){
    SimulationLabEntity entity;
    entity.faction_index=faction;
    entity.construction_complete=true;
    entity.runtime_model_class_id=2;
    entity.actor.position={q12_to_float(position.x_q12),q12_to_float(position.y_q12),0.0F};
    entity.motion.speed_q12_per_tick=speed;
    entity.motion.collision_radius_q12=radius;
    entity.motion.depth_bin_broad_phase_radius_q12=radius;
    entity.has_traversal_profile=true;
    entity.traversal_low_band_index=0;
    entity.traversal_high_band_index=4;
    return entity;
}

void exact_priority_order(){
    const auto origin=V519PathWorldPointQ12{0,0};
    SimulationLabRun run;
    run.entities.push_back(movable(origin,2,64)); // locked other faction -> 0
    run.entities.back().production_linked_to_parent=true;
    run.entities.push_back(movable(origin,2,64)); // active other faction -> 1
    run.entities.back().actor.move_target=Vec3f{1.0F,0.0F,0.0F};
    run.entities.push_back(movable(origin,1,64)); // source active same faction -> 2
    run.entities.back().actor.move_target=Vec3f{1.0F,0.0F,0.0F};
    run.entities.push_back(movable(origin,1,100)); // idle same faction -> 102

    const auto image=simulation_lab_overlapping_route_priority_image(
        run,2,MovementPoint{4096,0});
    require(image.count==4U,"overlapping route collector lost an eligible owner");
    require(!image.capacity_reached,"small priority image reported capacity exhaustion");
    require(image.pairs[0].entity_index==0U&&image.pairs[0].priority==0,
        "locked owner did not sort first with native priority zero");
    require(image.pairs[1].entity_index==1U&&image.pairs[1].priority==1,
        "foreign active owner did not retain obstacle priority one");
    require(image.pairs[2].entity_index==2U&&image.pairs[2].priority==2,
        "same-faction active source did not retain priority two");
    require(image.pairs[3].entity_index==3U&&image.pairs[3].priority==102,
        "idle same-faction owner did not add movement-speed weight");
}

void staged_factory_birth_has_callback_visibility(){
    SimulationLabRun run;run.level_runtime_enabled=true;run.tick=80;
    run.entities.push_back(movable({0,0},1));
    auto& factory=run.entities.front();factory.runtime_model_class_id=13;
    factory.motion.speed_q12_per_tick=0;
    run.entities.push_back(movable({0,0},1));
    auto child=movable({0,0},1);
    child.synthetic=true;child.construction_start_tick=run.tick;
    child.production_linked_to_parent=true;child.production_parent_entity_index=0;
    run.entities.push_back(std::move(child));
    const auto before=simulation_lab_overlapping_route_priority_image(run,1,{4096,0},true);
    const auto after=simulation_lab_overlapping_route_priority_image(run,1,{4096,0});
    require(before.count==1U&&after.count==2U,
        "route before the older producer's callback saw its unborn child");
    run.entities[0].synthetic=true;run.entities[0].construction_start_tick=run.tick-1;
    const auto older_source=simulation_lab_overlapping_route_priority_image(run,1,{4096,0},true);
    require(older_source.count==2U,
        "route after a newer producer's callback missed its linked child");
}
void published_pending_command_retains_fallback(){
    const auto field=FieldGrid::create(40,40,0,0);
    const auto origin=v519_path_scratch_center_to_world_q12({30,30});
    SimulationLabRun run;run.level_runtime_enabled=true;run.tick=10;
    run.entities.push_back(movable(origin,1));auto& e=run.entities[0];
    auto& state=e.combat_movement;e.combat_movement_initialized=true;
    state.model={origin.x_q12,origin.y_q12};state.flags=0x201;
    state.primary={origin.x_q12+8192,origin.y_q12};
    state.fallback={origin.x_q12,origin.y_q12+4096};
    state.stored={origin.x_q12+2048,origin.y_q12-2048};
    const auto fallback=state.fallback;
    e.group_route_pending_point=state.stored;e.group_route_pending_tick=run.tick;
    e.actor.move_target.reset();e.actor.move_waypoint.reset();e.combat_follow_owned=false;
    V565PathTerrainCache cache;
    require(simulation_lab_prepare_follow_motion(run,field,0,{},cache),
        "published pending command was discarded as an idle actor");
    require(state.fallback.x==fallback.x&&state.fallback.y==fallback.y,
        "deferred actor synchronization overwrote the native pending fallback");
    require((state.flags&1U)&&e.actor.move_target&&!e.group_route_pending_point,
        "published pending command was not consumed by the next callback");
}
void pioneer_adapter_is_authoritative(){
    SimulationLabRun run;
    run.entities.push_back(movable({0,0},1));
    run.entities.push_back(movable({2048,0},2));
    auto& pioneer=run.entities[1];pioneer.runtime_model_class_id=18;
    pioneer.pioneer_movement_initialized=true;pioneer.pioneer_movement.flags=0x341;
    pioneer.combat_movement.flags=0x40A; // Retained factory adapter LOCKED bit.
    const auto image=simulation_lab_overlapping_route_priority_image(run,0,{4096,0});
    const auto peer=std::find_if(image.pairs.begin(),image.pairs.begin()+image.count,
        [](const auto& p){return p.entity_index==1U;});
    require(peer!=image.pairs.begin()+image.count&&peer->priority==1,
        "Pioneer inherited stale combat LOCKED flag during group route solve");
}
void search_uses_source_footprint(){
    SimulationLabRun run;
    run.entities.push_back(movable({0,0},1,512,256));
    run.entities[0].motion.depth_bin_broad_phase_radius_q12=65536;
    run.entities.push_back(movable({32768,0},1,512,256));
    const auto image=simulation_lab_overlapping_route_priority_image(run,0,{4096,0});
    require(image.count==1U&&image.pairs[0].entity_index==0U,
        "large source model bounds admitted a peer outside the footprint search");
}

void fixed_capacity_is_newest_first(){
    const auto origin=V519PathWorldPointQ12{0,0};
    SimulationLabRun run;
    for(std::size_t index=0;index<40U;++index){
        run.entities.push_back(movable(origin,1,32));
        run.entities.back().actor.move_target=Vec3f{1.0F,0.0F,0.0F};
    }
    const auto image=simulation_lab_overlapping_route_priority_image(
        run,0,MovementPoint{4096,0});
    require(image.count==kSimulationLabRoutePriorityPairCapacity,
        "native fixed 32-pair capacity was not enforced");
    require(image.capacity_reached,"full priority image did not report the native cap");
    std::set<std::size_t> admitted;
    for(std::size_t index=0;index<image.count;++index)admitted.insert(image.pairs[index].entity_index);
    require(admitted.size()==32U,"priority heap duplicated or lost an admitted owner");
    require(!admitted.contains(0U)&&!admitted.contains(7U),
        "oldest owners incorrectly displaced newer owners at the cap");
    require(admitted.contains(8U)&&admitted.contains(39U),
        "newest-first owner admission did not keep the expected 32 owners");
}

void stationary_authored_turret_still_coordinates_routes(){
    const auto field=FieldGrid::create(80,80,0,0);
    const auto origin=v519_path_scratch_center_to_world_q12({30,30});
    const auto destination=v519_path_scratch_center_to_world_q12({42,30});
    SimulationLabRun run;run.level_runtime_enabled=true;
    run.entities={movable(origin,1,512,256),movable(origin,1,512,256),movable(origin,1,0,384)};
    auto& source=run.entities[2];source.runtime_model_class_id=4;source.has_traversal_profile=false;
    source.traversal_low_band_index=source.traversal_high_band_index=UINT32_MAX;
    source.root_model_integrity=source.root_model_maximum_integrity=1000;
    source.runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(1);
    source.combat_weapons.resize(1);source.combat_command_mode_flags=8U;
    V565PathTerrainCache cache;
    simulation_lab_weapon_follow(run,field,2,0,{q12_to_float(destination.x_q12),q12_to_float(destination.y_q12),0},false,{},cache);
    require(source.combat_follow_starts==1,"stationary authored turret did not issue native follow command");
    require(run.metrics.navigation_overlap_group_rebuilds==1&&run.metrics.navigation_overlap_group_pairs==2,
        "stationary authored turret bypassed native overlapping-owner route coordination");
    require(source.motion.speed_q12_per_tick==0&&float_to_q12(source.actor.position.x)==origin.x_q12&&
            float_to_q12(source.actor.position.y)==origin.y_q12,"path-query side effects moved the stationary building");
}

void earlier_route_segment_replans_source(){
    const auto field=FieldGrid::create(80,80,0,0);
    const auto candidate_start=v519_path_scratch_center_to_world_q12({40,12});
    const auto candidate_end=v519_path_scratch_center_to_world_q12({40,34});
    const auto source_start=v519_path_scratch_center_to_world_q12({20,23});
    const auto target_point=v519_path_scratch_center_to_world_q12({62,23});

    SimulationLabRun run;
    run.tick=17;
    run.entities.push_back(movable(candidate_start,1,512,640));
    run.entities.back().actor.move_target=Vec3f{q12_to_float(candidate_end.x_q12),
                                                q12_to_float(candidate_end.y_q12),0.0F};
    run.entities.push_back(movable(source_start,1,512,384));
    auto target=movable(target_point,2,0,2048);
    target.runtime_model_class_id=11;
    run.entities.push_back(std::move(target));

    const auto order=simulation_lab_overlapping_route_priority_image(
        run,1,MovementPoint{target_point.x_q12,target_point.y_q12});
    require(order.count==2U&&order.pairs[0].entity_index==0U&&
            order.pairs[1].entity_index==1U,
        "equal-priority heap extraction did not place the older route before the source");

    V565PathTerrainCache cache;
    simulation_lab_set_group_target_route(run,field,1,2,{},cache);
    const auto& source=run.entities[1];
    require(run.metrics.navigation_route_resolves==1U,
        "coordinated group route changed the issue-time solve count");
    require(run.metrics.navigation_overlap_group_rebuilds==1U&&
            run.metrics.navigation_overlap_group_pairs==2U,
        "overlapping group route diagnostics did not capture the bounded pair image");
    require(run.metrics.navigation_overlap_group_source_replans==1U,
        "earlier same-faction route influence did not trigger a source replan");
    require(source.target_entity_index==2U&&source.combat_assigned_target_entity_index==2U,
        "coordinated route lost the group command target");
    require(source.combat_movement.fallback.x==target_point.x_q12&&
            source.combat_movement.fallback.y==target_point.y_q12,
        "group coordination changed the final command destination");
    require(source.combat_movement.stored.x!=source.combat_movement.fallback.x||
            source.combat_movement.stored.y!=source.combat_movement.fallback.y,
        "coordinated source did not publish a distinct primary route point");
}
}

int main()try{
    stationary_authored_turret_still_coordinates_routes();
    exact_priority_order();
    published_pending_command_retains_fallback();
    staged_factory_birth_has_callback_visibility();
    pioneer_adapter_is_authoritative();
    search_uses_source_footprint();
    fixed_capacity_is_newest_first();
    earlier_route_segment_replans_source();
    std::cout<<"V754_OVERLAPPING_GROUP_ROUTE PASS order=1 capacity=32 coordinated=1\n";
}catch(const std::exception& error){
    std::cerr<<error.what()<<'\n';
    return 1;
}
