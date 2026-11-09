#include "thandor/simulation_lab_visibility.hpp"
#include "thandor/simulation_lab_targeting.hpp"
#include "thandor/simulation_lab_follow.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
SimulationLabEntity army(uint32_t faction,Vec3f position){
 SimulationLabEntity e;e.faction_index=faction;e.runtime_model_class_id=2;e.army_asset_id=1;
 e.actor.position=position;e.shot_impact_class_index=1;e.motion.collision_radius_q12=1000;
 e.model_components.resize(1);e.model_components[0].source_model_node_index=0;
 e.model_components[0].maximum_integrity=e.model_components[0].integrity=4096;
 e.root_model_component_index=0;
 auto h=std::make_shared<std::vector<ModelVisualNode>>(1);(*h)[0].parent_index=UINT32_MAX;
 e.runtime_model_hierarchy=h;
 return e;
}
int main()try{
 const auto field=FieldGrid::create(59,59,0,0);const auto& c=field.at(25,25);
 const Vec3f origin{q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),0};
 SimulationLabRun run;run.level_runtime_enabled=true;
 run.entities={army(1,origin),army(2,{origin.x+2,origin.y,0})};
 auto& source=run.entities[0];auto& target=run.entities[1];
 source.motion.speed_q12_per_tick=64;source.motion.ground_acceleration_q12=32;
 target.model_terrain_state_initialized=true;
 const auto cell=retail_fog_world_to_cell(field,float_to_q12(target.actor.position.x),float_to_q12(target.actor.position.y));
 check(bool(cell),"fixture outside field");const auto offset=size_t(cell->y)*field.width()+cell->x;
 run.current_visibility_occupancy.assign(field.cells().size(),0x10ULL<<8);
 std::vector<SimulationLabCombatMetrics> metrics(2);metrics[0].range_q12=8*4096;
 metrics[0].class_sum=4096;metrics[0].class_damage_q12[1]=4096;
 metrics[1].condition_q12=4096;
 check(!simulation_lab_select_combat_target(run,field,0,metrics,true).entity_index,
       "GUI projection overrode the model's retained zero visibility");
 check(simulation_lab_ai_hostile_perception(run,field,1,target)==SimulationLabHostilePerception::Unseen,
       "planning used GUI visibility ahead of the model callback");
 target.model_primary_occupancy_mask=2U<<(2U*1U);
 run.current_visibility_occupancy.assign(field.cells().size(),0);
 check(simulation_lab_select_combat_target(run,field,0,metrics,true).entity_index==1,
       "retained visible MODEL disappeared when the display projection changed");
 check(simulation_lab_ai_hostile_perception(run,field,1,target)==SimulationLabHostilePerception::Visible,
       "planning discarded retained MODEL visibility");
 source.target_entity_index=1;
 check(simulation_lab_visible_command_target(run,field,0)==1,"weapon command aim used another visibility generation");
 for(uint32_t faction=1;faction<8;++faction){
  target.model_primary_occupancy_mask=3U<<(2U*faction);
  check(simulation_lab_model_visibility_bits(run,field,faction,target)==3,"faction visibility shift differs");
  for(uint32_t other=1;other<8;++other)if(other!=faction)
   check(simulation_lab_model_visibility_bits(run,field,other,target)==0,"visibility leaked into another faction");
 }
 target.model_primary_occupancy_mask=1U<<2;target.runtime_model_class_id=15;
 check(simulation_lab_ai_hostile_perception(run,field,1,target)==SimulationLabHostilePerception::Visible,
       "remembered group-A model disappeared from the planning workspace");
 check(!simulation_lab_select_combat_target(run,field,0,metrics,true).entity_index,
       "remembered model became a visible direct combat target");
 target.runtime_model_class_id=2;
 check(simulation_lab_ai_hostile_perception(run,field,1,target)==SimulationLabHostilePerception::Unseen,
       "remembered mobile entered the visible workspace");
 target.model_primary_occupancy_mask=2U<<2;
 std::array<uint8_t,2> blocked{0,1};source.motion.ground_acceleration_q12=0;
 check(!simulation_lab_select_combat_target(run,field,0,metrics,true,blocked).entity_index,
       "speed-only definition bypassed native zero-acceleration blocked-target rejection");
 source.motion.ground_acceleration_q12=1;
 check(simulation_lab_select_combat_target(run,field,0,metrics,true,blocked).entity_index==1,
       "accelerating source lost quarter-score obstruction candidate");
 metrics[0].class_damage_q12[1]=-4096;metrics[0].class_sum=4096;
 check(simulation_lab_select_combat_target(run,field,0,metrics,true).entity_index==1,
       "command scoring discarded the native negative-counter magnitude branch");
 // Aggregate shot metrics belong to the linked model definitions, not power/callback state.
 SimulationWeaponProfile w;w.reload_ticks=1;w.yaw_model_node_index=w.model_node_index=0;
 ShotDefinitionRecord shot;shot.trajectory_mode=2;shot.mode2_selection_range_q12=8*4096;shot.target_impact_damage_q12[1]=4096;
 source.combat_weapons={{w,shot}};
 for(uint32_t flags:{0U,1U,8U,9U}){
  source.model_components[0].runtime_flags_ec=flags;
  auto m=simulation_lab_combat_metrics(source);
  check(m.class_sum==4096&&m.range_q12==8*4096,"inactive linked definition lost native derived metrics");
  check(simulation_lab_weapon_callback_active(source,w)==(flags==0),"metrics change enabled inactive weapon firing");
 }
 source.model_components[0].runtime_removed=true;
 check(simulation_lab_combat_metrics(source).class_sum==0,"unlinked root retained derived combat damage");
 std::cout<<"AI_RETAINED_VISIBILITY_METRICS PASS retained versus display visibility, all factions, remembered classes, acceleration, mixed counters, power, unlink\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
