#include "thandor/simulation_lab_follow.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
static void check(bool v,const char*m){if(!v)throw std::runtime_error(m);}
static bool same(std::optional<MovementPoint>a,MovementPoint b){return a&&a->x==b.x&&a->y==b.y;}
int main()try{
 auto field=FieldGrid::create(59,59,0,0);const auto&cell=field.at(20,20);const auto&a=field.at(25,20);const auto&b=field.at(30,20);
 SimulationLabRun run;run.entities.resize(1);auto&e=run.entities[0];e.faction_index=1;e.army_asset_id=1;e.runtime_model_class_id=1;e.root_model_maximum_integrity=e.root_model_integrity=100;e.motion.speed_q12_per_tick=168;e.actor.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
 const MovementPoint target{a.world_x_q12,a.world_y_q12},queued{b.world_x_q12,b.world_y_q12};V565PathTerrainCache cache;
 simulation_lab_set_site_route(run,field,0,target,{},cache);
 check(same(e.ai_path_coordinate_q12,target),"AI path stayed at spawn after routed site order");
 check(same(e.ai_common_tracked_coordinate_q12,e.combat_movement.primary)&&same(e.ai_damage_tracked_coordinate_q12,e.combat_movement.stored),"native tracked movement aliases were not published");
 e.actor.position.x+=1;simulation_lab_sync_follow(e);check(same(e.ai_path_coordinate_q12,target),"AI path was replaced by live model translation");
 simulation_lab_append_waypoint_or_start_route(run,field,0,queued,{},cache);check(same(e.ai_path_coordinate_q12,target)&&e.combat_movement.queue_count==1,"queued point replaced the active AI path");
 e.production_linked_to_parent=true;e.combat_movement.flags|=2;simulation_lab_set_site_route(run,field,0,queued,{},cache);check(same(e.ai_path_coordinate_q12,target),"locked order replaced the active AI path");
 e.production_linked_to_parent=false;e.combat_movement.flags&=~2U;check(simulation_lab_explicit_stop_movement(e),"explicit stop rejected");const MovementPoint current{float_to_q12(e.actor.position.x),float_to_q12(e.actor.position.y)};
 check(same(e.ai_path_coordinate_q12,current)&&same(e.ai_common_tracked_coordinate_q12,current)&&same(e.ai_damage_tracked_coordinate_q12,current),"explicit stop failed to anchor coordinate aliases");
 simulation_lab_sync_follow(e);check(same(e.ai_path_coordinate_q12,current),"idle synchronization resurrected the stopped path");
 check(e.combat_movement.fallback.x==current.x&&e.combat_movement.primary.x==current.x&&e.combat_movement.stored.x==current.x,"movement and common coordinate aliases disagree after stop");
 std::cout<<"V777_AI_PATH PASS routed target, moving model, queued/locked orders and stop aliases\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
