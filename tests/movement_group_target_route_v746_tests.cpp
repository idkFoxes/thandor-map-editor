#include "thandor/simulation_lab_follow.hpp"
#include "thandor/path_diagnostics.hpp"
#include <iostream>
#include <stdexcept>

using namespace thandor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
SimulationLabEntity movable(V519PathWorldPointQ12 point,std::uint32_t faction){
    SimulationLabEntity e;
    e.faction_index=faction;
    e.construction_complete=true;
    e.runtime_model_class_id=2;
    e.actor.position={q12_to_float(point.x_q12),q12_to_float(point.y_q12),0.0F};
    e.motion.speed_q12_per_tick=512;
    e.motion.collision_radius_q12=128;
    e.has_traversal_profile=true;
    e.traversal_low_band_index=0;
    e.traversal_high_band_index=4;
    return e;
}
void native_arrival_waits_for_waypoint_callback(){
    const auto field=FieldGrid::create(40,40,0,0);
    const auto source=v519_path_scratch_center_to_world_q12({30,30});
    SimulationLabSeedEntity seed;
    seed.army_asset_id=61;seed.faction_index=1;seed.runtime_model_class_id=2;
    seed.root_model_maximum_integrity=6000;
    seed.actor.position={q12_to_float(source.x_q12),q12_to_float(source.y_q12),0};
    seed.motion.speed_q12_per_tick=96;seed.motion.collision_radius_q12=1;
    seed.has_traversal_profile=true;seed.traversal_low_band_index=0;seed.traversal_high_band_index=4;
    ModelVisualNode root;root.runtime_class_id=2;
    SimulationLabScenarioRuntimeSeed scene;scene.enabled=true;scene.active_faction_count=1;
    scene.ai_planning_enabled=false;
    SimulationLabConfig config;config.run_count=1;config.duration_ticks=4;
    std::array<std::uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    SimulationLabExperiment lab;lab.reset(config,parameters,std::span(&seed,1),{},{},{},{},{},scene);
    auto& e=lab.runs()[0].entities[0];
    e.runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(std::vector{root});
    const MovementPoint target{source.x_q12+80,source.y_q12};
    e.combat_movement_initialized=true;e.combat_follow_owned=true;
    auto& m=e.combat_movement;m.flags=0x241;m.model={source.x_q12,source.y_q12};
    m.primary=m.stored=m.fallback=target;
    e.actor.move_target=Vec3f{q12_to_float(target.x),q12_to_float(target.y),0};
    e.actor.move_waypoint=e.actor.move_target;
    lab.step(field,1);
    require(float_to_q12(e.actor.position.x)==target.x,"arrival fixture did not reach the destination");
    require((m.flags&0x11U)==0x11U,"arrival cleared ACTIVE before the next waypoint callback");
    lab.step(field,1);
    require((m.flags&0x11U)==0U,"next waypoint callback did not consume arrival");
    require(float_to_q12(e.actor.position.x)==target.x,"arrival callback moved past the destination");
}
void immediate_group_route(){
    const auto field=FieldGrid::create(40,40,0,0);
    const auto source=v519_path_scratch_center_to_world_q12({30,30});
    const auto target=v519_path_scratch_center_to_world_q12({38,34});
    SimulationLabRun run;
    run.tick=9;
    run.entities.push_back(movable(source,1));
    run.entities.push_back(movable(target,2));
    // The target is a physical owner but must not block routing to itself.
    run.entities[1].motion.collision_radius_q12=4096;
    V565PathTerrainCache cache;
    simulation_lab_set_group_target_route(run,field,0,1,{},cache);
    const auto& e=run.entities[0];
    require(e.target_entity_index==1&&e.combat_assigned_target_entity_index==1,
        "group route did not retain the assigned target");
    require(e.combat_command_mode_flags==5U&&e.combat_command_generation==32,
        "group route did not stamp the final interrupted command generation");
    require(e.combat_movement_initialized&&e.combat_follow_owned,
        "group target route was not published in the issue tick");
    require((e.combat_movement.flags&0x61U)==0x61U&&
            (e.combat_movement.flags&0x200U)==0U,
        "group route flags differ from target-follow plus caller ROUTED clear");
    require(e.combat_movement.fallback.x==target.x_q12&&
            e.combat_movement.fallback.y==target.y_q12,
        "group target was treated as its own route blocker");
    require(run.metrics.navigation_route_resolves==1U,
        "group route was not resolved exactly once at assignment");
}
void guarded_route_keeps_command_without_replacing_drive(){
    const auto field=FieldGrid::create(40,40,0,0);
    const auto source=v519_path_scratch_center_to_world_q12({30,30});
    const auto target=v519_path_scratch_center_to_world_q12({38,34});
    SimulationLabRun run;
    run.entities.push_back(movable(source,1));
    run.entities.push_back(movable(target,2));
    auto& e=run.entities[0];
    const auto old=v519_path_scratch_center_to_world_q12({33,30});
    e.actor.move_target=Vec3f{q12_to_float(old.x_q12),q12_to_float(old.y_q12),0.0F};
    e.combat_movement_initialized=true;
    e.combat_movement.model={source.x_q12,source.y_q12};
    e.combat_movement.primary=e.combat_movement.fallback=e.combat_movement.stored=
        MovementPoint{old.x_q12,old.y_q12};
    e.production_linked_to_parent=true; // represented native movement lock
    e.combat_movement.flags=0x203U; // ACTIVE | LOCKED | ROUTED
    V565PathTerrainCache cache;
    simulation_lab_set_group_target_route(run,field,0,1,{},cache);
    require(e.target_entity_index==1&&e.combat_assigned_target_entity_index==1,
        "guarded group route lost its target assignment");
    require(e.combat_command_mode_flags==5U&&e.combat_command_generation==32,
        "guarded group route lost final command state");
    require(e.actor.move_target&&float_to_q12(e.actor.move_target->x)==old.x_q12&&
            float_to_q12(e.actor.move_target->y)==old.y_q12,
        "locked group route replaced the active actor destination");
    require((e.combat_movement.flags&0x2U)!=0U&&
            (e.combat_movement.flags&0x200U)==0U,
        "group caller did not preserve LOCKED while clearing ROUTED");
    require(run.metrics.navigation_route_resolves==0U,
        "locked group route incorrectly invoked path resolution");
}
void group_route_publishes_to_pioneer_movement(){
    const auto field=FieldGrid::create(40,40,0,0);
    const auto source=v519_path_scratch_center_to_world_q12({30,30});
    const auto peer=v519_path_scratch_center_to_world_q12({34,32});
    const auto target=v519_path_scratch_center_to_world_q12({38,34});
    SimulationLabRun run;
    run.level_runtime_enabled=true;
    run.tick=10;
    run.entities.push_back(movable(peer,1));
    run.entities.push_back(movable(source,1));
    auto& pioneer=run.entities[0];
    pioneer.runtime_model_class_id=18;
    pioneer.pioneer_movement_initialized=true;
    pioneer.pioneer_movement.flags=0x400U; // mirror the next pending target
    pioneer.pioneer_movement.model=pioneer.pioneer_movement.stored=
        pioneer.pioneer_movement.fallback=pioneer.pioneer_movement.primary={peer.x_q12,peer.y_q12};
    auto& mover=run.entities[1];
    mover.combat_movement_initialized=mover.combat_follow_owned=true;
    mover.combat_movement.flags=0x11U;
    mover.combat_movement.stored={source.x_q12,source.y_q12};
    mover.combat_movement.fallback=mover.combat_movement.primary={target.x_q12,target.y_q12};
    auto scratch=std::make_shared<SimulationLabRetailScratchImage>();
    scratch->width=field.width()*4;
    scratch->height=field.height()*4;
    scratch->state.resize(scratch->width*scratch->height);
    // No open cell adjacent to the Pioneer. The group solver must relocate
    // its pending segment before another owner queries the priority image.
    for(size_t row=31;row<=33;++row)
        for(size_t col=33;col<=35;++col)
            scratch->state[row*scratch->width+col]=0x100U;
    run.retail_scratch=scratch;
    V565PathTerrainCache cache;
    cache.field=&field;
    cache.runtime_states=std::shared_ptr<const std::vector<uint32_t>>(scratch,&scratch->state);
    simulation_lab_prepare_follow_motion(run,field,1,{},cache,true);
    const auto& state=pioneer.pioneer_movement;
    require(state.flags==0x401U&&state.retry==64U,"peer group route did not activate the Pioneer record");
    require((state.stored.x!=peer.x_q12||state.stored.y!=peer.y_q12)&&
        state.primary.x==state.stored.x&&state.primary.y==state.stored.y&&
        state.fallback.x==state.stored.x&&state.fallback.y==state.stored.y,
        "relocated Pioneer route did not publish its mirrored coordinates");
    require(pioneer.combat_movement.flags==0U&&!pioneer.group_route_pending_point,
        "Pioneer route was deferred into the combat movement adapter");
}
}
int main()try{
    native_arrival_waits_for_waypoint_callback();
    immediate_group_route();
    guarded_route_keeps_command_without_replacing_drive();
    group_route_publishes_to_pioneer_movement();
    // A retained unseen-workspace entry can select a collected unit itself.
    // Native still writes TARGET_ARMY/INTERRUPTED and its assigned target.
    const auto field=FieldGrid::create(40,40,0,0);
    const auto point=v519_path_scratch_center_to_world_q12({30,30});
    SimulationLabRun run;run.entities.push_back(movable(point,1));
    run.entities[0].combat_movement_initialized=true;
    run.entities[0].combat_movement.flags=0x201U;
    V565PathTerrainCache cache;
    simulation_lab_set_group_target_route(run,field,0,0,{},cache);
    require(run.entities[0].target_entity_index==0&&
        run.entities[0].combat_assigned_target_entity_index==0&&
        run.entities[0].combat_command_mode_flags==5U&&
        run.entities[0].combat_command_generation==32,"self target skipped native group command writes");
    std::cout<<"V746_GROUP_TARGET_ROUTE PASS immediate=1 guarded=1\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
