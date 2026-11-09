#include "thandor/simulation_lab.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

using namespace thandor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool same_target(const std::optional<Vec3f>& a,const std::optional<Vec3f>& b){
    if(a.has_value()!=b.has_value())return false;
    if(!a)return true;
    return float_to_q12(a->x)==float_to_q12(b->x)&&
           float_to_q12(a->y)==float_to_q12(b->y);
}

SimulationLabSeedEntity unit(const FieldCell& cell,std::uint32_t runtime_class=2U){
    SimulationLabSeedEntity seed;
    seed.army_asset_id=100U+runtime_class;
    seed.faction_index=1U;
    seed.runtime_model_class_id=runtime_class;
    seed.building=runtime_class==13U;
    seed.actor.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0.0F};
    seed.root_model_maximum_integrity=1000;
    if(runtime_class!=13U){
        seed.motion.speed_q12_per_tick=64;
        seed.motion.collision_radius_q12=64;
        seed.motion.yaw_max_velocity_turn16=0x7fff;
        seed.motion.yaw_acceleration_turn16=0x7fff;
        seed.has_traversal_profile=true;
        seed.traversal_low_band_index=0;
        seed.traversal_high_band_index=4;
    }
    return seed;
}

SimulationLabExperiment experiment(std::span<const SimulationLabSeedEntity> seeds){
    SimulationLabConfig config;
    config.run_count=1;
    config.duration_ticks=512;
    config.sweep_mode=SimulationLabSweepMode::Fixed;
    std::array<std::uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    SimulationLabScenarioRuntimeSeed scenario;
    scenario.enabled=true;
    scenario.active_faction_count=1;
    scenario.assignable_faction_count=1;
    scenario.human_faction_mask=1U<<1U;
    scenario.ai_planning_enabled=false;
    SimulationLabExperiment lab;
    lab.reset(config,parameters,seeds,{},{},{},{},{},scenario);
    return lab;
}

SimulationLabPlayerCommand selection_command(SimulationLabPlayerCommandKind kind,
                                              MovementPoint target){
    SimulationLabPlayerCommand command;
    command.kind=kind;
    command.faction_index=1;
    command.world_x_q12=target.x;
    command.world_y_q12=target.y;
    return command;
}

void formation_move_and_waypoint_queue(){
    auto field=FieldGrid::create(80,80,0,0);
    const std::array seeds{unit(field.at(30,30)),unit(field.at(32,30))};
    auto lab=experiment(seeds);
    auto& run=lab.runs()[0];
    const MovementPoint target{field.at(48,38).world_x_q12,field.at(48,38).world_y_q12};
    auto move=selection_command(SimulationLabPlayerCommandKind::MoveSelection,target);
    move.selected_entity_indices[0]=0;
    move.selected_entity_indices[1]=1;
    require(lab.queue_player_command(0,move),"selection move was rejected structurally");
    lab.step(field,1);
    require(run.player_command_results.size()==1U&&run.player_command_results[0].accepted,
        "selection move was not applied");

    const MovementPoint p0{float_to_q12(seeds[0].actor.position.x),
                           float_to_q12(seeds[0].actor.position.y)};
    const MovementPoint p1{float_to_q12(seeds[1].actor.position.x),
                           float_to_q12(seeds[1].actor.position.y)};
    const MovementPoint center{static_cast<std::int32_t>((std::int64_t{p0.x}+p1.x)/2),
                               static_cast<std::int32_t>((std::int64_t{p0.y}+p1.y)/2)};
    const MovementPoint expected0{movement_sub(target.x,movement_sub(center.x,p0.x)),
                                  movement_sub(target.y,movement_sub(center.y,p0.y))};
    const MovementPoint expected1{movement_sub(target.x,movement_sub(center.x,p1.x)),
                                  movement_sub(target.y,movement_sub(center.y,p1.y))};
    require(run.entities[0].combat_movement.fallback.x==expected0.x&&
            run.entities[0].combat_movement.fallback.y==expected0.y&&
            run.entities[1].combat_movement.fallback.x==expected1.x&&
            run.entities[1].combat_movement.fallback.y==expected1.y,
        "selection move did not preserve the recentered formation offsets");
    require(run.metrics.navigation_route_resolves>=2U,
        "selection move was not routed for every eligible entry in the issue tick");

    const MovementPoint queued{field.at(54,42).world_x_q12,field.at(54,42).world_y_q12};
    auto waypoint=selection_command(SimulationLabPlayerCommandKind::QueueSelectionWaypoint,queued);
    waypoint.selected_entity_indices[0]=0;
    waypoint.selected_entity_indices[1]=1;
    const auto first_target0=run.entities[0].actor.move_target;
    const auto first_target1=run.entities[1].actor.move_target;
    require(lab.queue_player_command(0,waypoint),"selection waypoint was rejected structurally");
    lab.step(field,1);
    require(run.player_command_results[0].accepted,
        "selection waypoint was not applied");
    require(run.entities[0].combat_movement.queue_count==1U&&
            run.entities[1].combat_movement.queue_count==1U&&
            (run.entities[0].combat_movement.flags&0x48U)==0x48U&&
            (run.entities[1].combat_movement.flags&0x48U)==0x48U,
        "selection waypoint did not append to both active queues");
    require(same_target(run.entities[0].actor.move_target,first_target0)&&
            same_target(run.entities[1].actor.move_target,first_target1),
        "queued selection waypoint replaced the active route");
}

void scattered_selection_collapses_to_target(){
    auto field=FieldGrid::create(96,96,0,0);
    const std::array seeds{unit(field.at(20,20)),unit(field.at(60,60))};
    auto lab=experiment(seeds);
    auto& run=lab.runs()[0];
    const MovementPoint target{field.at(45,45).world_x_q12,field.at(45,45).world_y_q12};
    auto move=selection_command(SimulationLabPlayerCommandKind::MoveSelection,target);
    move.selected_entity_indices[0]=0;
    move.selected_entity_indices[1]=1;
    require(lab.queue_player_command(0,move),"scattered selection move was rejected");
    lab.step(field,1);
    require(run.entities[0].combat_movement.fallback.x==target.x&&
            run.entities[0].combat_movement.fallback.y==target.y&&
            run.entities[1].combat_movement.fallback.x==target.x&&
            run.entities[1].combat_movement.fallback.y==target.y,
        "selection wider than the native 5/7-unit limits retained formation offsets");
}

void single_factory_sets_rally_only(){
    auto field=FieldGrid::create(64,64,0,0);
    const std::array seeds{unit(field.at(30,30),13U)};
    auto lab=experiment(seeds);
    auto& run=lab.runs()[0];
    const MovementPoint target{field.at(44,36).world_x_q12,field.at(44,36).world_y_q12};
    auto move=selection_command(SimulationLabPlayerCommandKind::MoveSelection,target);
    move.selected_entity_indices[0]=0;
    require(lab.queue_player_command(0,move),"factory rally command was rejected structurally");
    lab.step(field,1);
    const auto& factory=run.entities[0];
    require(run.player_command_results[0].accepted&&factory.production_drive_target_world&&
            float_to_q12(factory.production_drive_target_world->x)==target.x&&
            float_to_q12(factory.production_drive_target_world->y)==target.y,
        "single class-13 selection did not store its rally point");
    require(!factory.actor.move_target&&!factory.actor.move_waypoint&&
            !factory.combat_movement_initialized,
        "immobile class-13 rally command incorrectly started actor movement");
}
}

int main()try{
    formation_move_and_waypoint_queue();
    scattered_selection_collapses_to_target();
    single_factory_sets_rally_only();
    std::cout<<"V747_SELECTION_MOVE PASS formation=1 queue=1 spread=1 factory=1\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
