#include "thandor/simulation_lab_follow.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void check(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
bool same(MovementPoint a, MovementPoint b) { return a.x==b.x && a.y==b.y; }
bool same(std::optional<MovementPoint> a, MovementPoint b) { return a && same(*a,b); }
SimulationLabSeedEntity pioneer(const FieldCell& cell, uint32_t faction) {
    SimulationLabSeedEntity seed{};
    seed.army_asset_id=50;seed.faction_index=faction;seed.runtime_model_class_id=18;
    seed.actor.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
    seed.motion.speed_q12_per_tick=256;
    seed.motion.yaw_max_velocity_turn16=0x7fff;
    seed.motion.yaw_acceleration_turn16=0x7fff;
    seed.root_model_maximum_integrity=1000;
    return seed;
}
}
int main() try {
    auto field=FieldGrid::create(80,80,0,0);
    const std::array seeds{
        pioneer(field.at(30,30),1),pioneer(field.at(32,30),1),pioneer(field.at(34,30),2)};
    SimulationLabConfig config{};config.run_count=1;config.duration_ticks=1000;
    SimulationLabScenarioRuntimeSeed scenario{};
    scenario.enabled=true;scenario.active_faction_count=scenario.assignable_faction_count=2;
    scenario.human_faction_mask=(1U<<1)|(1U<<2);scenario.ai_planning_enabled=false;
    std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
    SimulationLabExperiment lab;
    lab.reset(config,ki,seeds,{},{},{},{},{},scenario);
    auto& run=lab.runs()[0];
    SimulationLabPlayerCommand move{};
    move.kind=SimulationLabPlayerCommandKind::MovePioneer;move.faction_index=1;
    move.entity_index=0;
    // Mirror the stock player-input fixture: a short native Pioneer route.
    move.world_x_q12=field.at(30,30).world_x_q12+20000;
    move.world_y_q12=field.at(30,30).world_y_q12;
    check(lab.queue_player_command(0,move),"pioneer move not accepted into input queue");
    lab.step(field);
    check(run.player_command_results.size()==1 && run.player_command_results.front().accepted,
        "fixture movement command rejected");
    lab.step(field,4);
    auto& first=run.entities.at(0);
    check(first.pioneer_movement_initialized,"pioneer runtime did not initialize");
    check((first.pioneer_movement.flags&1U)!=0U,"fixture lacks active pioneer route");
    const auto original_position=first.actor.position;
    const MovementPoint current{float_to_q12(original_position.x),float_to_q12(original_position.y)};
    first.ai_common_tracked_coordinate_q12=MovementPoint{101,202};
    first.ai_damage_tracked_coordinate_q12=MovementPoint{303,404};
    first.combat_movement.flags|=0x239U|0x400U;
    auto& locked=run.entities.at(1);
    locked.production_linked_to_parent=true; // owning state preserves the lock during synchronization
    locked.pioneer_movement_initialized=true;
    locked.pioneer_movement.flags|=0x2U|0x1U|0x200U;
    locked.pioneer_movement.fallback=MovementPoint{99,88};
    locked.ai_path_coordinate_q12=MovementPoint{99,88};
    const auto enemy_prior=run.entities.at(2).ai_path_coordinate_q12;
    SimulationLabPlayerCommand stop{};
    stop.kind=SimulationLabPlayerCommandKind::StopMovement;stop.faction_index=1;
    stop.selected_entity_indices[0]=0;
    stop.selected_entity_indices[1]=1; // movement locked: must not change
    stop.selected_entity_indices[2]=2; // enemy: must not change
    stop.selected_entity_indices[3]=9999; // stale selection slot
    stop.selected_entity_indices[31]=0; // duplicate last slot
    check(lab.queue_player_command(0,stop),"selection stop failed structural admission");
    lab.step(field);
    check(run.player_command_results.size()==1 && run.player_command_results[0].accepted,
        "selection stop with an eligible entry failed");
    check(same(first.ai_path_coordinate_q12,current),"path pair not anchored");
    check(same(first.ai_common_tracked_coordinate_q12,current),"common tracked pair not anchored");
    check(same(first.ai_damage_tracked_coordinate_q12,current),"damage tracked pair not anchored");
    check((first.pioneer_movement.flags&0x239U)==0U,"pioneer runtime still has active route");
    check((first.combat_movement.flags&0x239U)==0U,"combat runtime still has active route");
    check((first.combat_movement.flags&0x400U)!=0U,"unrelated movement flag lost");
    check(!first.actor.move_target&&!first.actor.move_waypoint,"actor still has movement target");
    check((locked.pioneer_movement.flags&0x203U)==0x203U,"selection stop changed locked unit");
    check(same(locked.ai_path_coordinate_q12,MovementPoint{99,88}),"locked unit path changed");
    check(run.entities[2].ai_path_coordinate_q12.has_value()==enemy_prior.has_value() &&
          (!enemy_prior || same(*run.entities[2].ai_path_coordinate_q12,*enemy_prior)),
          "enemy selection entry changed");
    lab.step(field,10);
    check(std::abs(first.actor.position.x-original_position.x)<0.0003F &&
          std::abs(first.actor.position.y-original_position.y)<0.0003F,
          "pioneer continued route after explicit stop");
    // Empty / foreign / locked entries must not count as an applied command.
    stop.selected_entity_indices={};stop.selected_entity_indices[0]=1;
    stop.selected_entity_indices[1]=2;
    check(lab.queue_player_command(0,stop),"blocked-only stop rejected structurally");
    lab.step(field);
    check(!run.player_command_results[0].accepted,"locked/foreign selection falsely accepted");
    // Stop is not a permanent movement ban: a later new command can move again.
    check(lab.queue_player_command(0,move),"new move after stop failed to queue");
    lab.step(field,4);
    check(first.actor.position.x>original_position.x,"new command did not restart Pioneer");
    std::cout<<"V731_SELECTION_STOP PASS fixed 32 slots, faction/lock, 3 coordinate pairs, Pioneer cancellation and resume\n";
} catch(const std::exception& e) {
    std::cerr<<e.what()<<'\n';return 1;
}
