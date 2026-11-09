#include "thandor/simulation_lab_follow.hpp"
#include "thandor/path_diagnostics.hpp"

#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

using namespace thandor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}

SimulationLabEntity owner(V519PathWorldPointQ12 position,std::uint32_t faction,
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

void authored_acceleration_controls_admission(){
    const V519PathWorldPointQ12 origin{0,0};
    SimulationLabRun run;
    run.entities.push_back(owner(origin,1,512)); // synthetic source: speed compatibility path

    auto accelerated=owner(origin,1,0);
    accelerated.runtime_model_hierarchy=
        std::make_shared<const std::vector<ModelVisualNode>>(1U);
    accelerated.motion.ground_acceleration_q12=48U;
    run.entities.push_back(std::move(accelerated));

    auto speed_only=owner(origin,1,512);
    speed_only.runtime_model_hierarchy=
        std::make_shared<const std::vector<ModelVisualNode>>(1U);
    speed_only.motion.ground_acceleration_q12=0U;
    run.entities.push_back(std::move(speed_only));

    const auto image=simulation_lab_overlapping_route_priority_image(
        run,0,MovementPoint{4096,0});
    std::set<std::size_t> admitted;
    for(std::size_t i=0;i<image.count;++i)admitted.insert(image.pairs[i].entity_index);
    require(image.count==2U,"authored acceleration admission produced the wrong owner count");
    require(admitted.contains(0U),"synthetic source lost its legacy speed-based admission");
    require(admitted.contains(1U),"authored zero-speed owner with acceleration was not admitted");
    require(!admitted.contains(2U),"authored speed-only owner bypassed the native acceleration gate");
}

void unfinished_linked_owner_remains_a_route_obstacle(){
    const auto field=FieldGrid::create(80,80,0,0);
    const auto source_point=v519_path_scratch_center_to_world_q12({20,23});
    const auto obstacle_point=v519_path_scratch_center_to_world_q12({40,23});
    const auto target_point=v519_path_scratch_center_to_world_q12({62,23});

    SimulationLabRun run;
    run.tick=29;
    run.entities.push_back(owner(source_point,1,512,256));

    auto obstacle=owner(obstacle_point,2,0,2048);
    obstacle.runtime_model_class_id=11;
    obstacle.construction_complete=false; // linked MODEL owner still exists in native routing
    run.entities.push_back(std::move(obstacle));

    auto target=owner(target_point,2,0,0);
    target.runtime_model_class_id=11;
    run.entities.push_back(std::move(target));

    V565PathTerrainCache cache;
    simulation_lab_set_group_target_route(run,field,0,2,{},cache);
    const auto& source=run.entities[0];
    require(run.metrics.navigation_route_resolves==1U,
        "unfinished-owner route changed the issue-time solve count");
    require(run.metrics.navigation_routed_solves==1U,
        "linked unfinished owner was not represented as a route obstacle");
    require(source.combat_movement.stored.x!=source.combat_movement.fallback.x||
            source.combat_movement.stored.y!=source.combat_movement.fallback.y,
        "route through an unfinished linked owner remained incorrectly direct");
    require(source.target_entity_index==2U&&source.combat_assigned_target_entity_index==2U,
        "obstacle admission changed the group command target");
}
}

int main()try{
    authored_acceleration_controls_admission();
    unfinished_linked_owner_remains_a_route_obstacle();
    std::cout<<"V757_OVERLAP_OWNER_ADMISSION PASS acceleration=1 unfinished_obstacle=1\n";
}catch(const std::exception& error){
    std::cerr<<"V757_OVERLAP_OWNER_ADMISSION FAIL "<<error.what()<<'\n';
    return 1;
}
