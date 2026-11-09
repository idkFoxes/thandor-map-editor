// Test-only V632 marker reference with an observational comparison counter.
#pragma once
#include "thandor/simulation_lab.hpp"
#include <cmath>

namespace marker_reference_v632 {
using namespace thandor;
inline size_t duplicate_comparisons=0;
struct SimulationCommandMarker {
 uint32_t effect_id{},faction_index{};
 Vec3f point{};
 size_t owner_entity_index{};
};
// Match the motion service's inclusive final-target window. Waypoints use its
// tighter step-consumption radius and must not clear a standing factory rally point.
inline bool reference_drive_marker_pending(const SimulationActorState& actor,
    const SimulationMotionProfile& motion, Vec3f point, bool waypoint = false) noexcept {
    const float dx=point.x-actor.position.x,dy=point.y-actor.position.y;
    if(!waypoint)return std::abs(dx)>1.F||std::abs(dy)>1.F;
    const float speed=std::max(q12_to_float(std::abs(motion.speed_q12_per_tick)),1.F/4096.F);
    return std::hypot(dx,dy)>speed;
}
// Read only live intent. Pending purchases without a world position are not placement plans.
inline std::vector<SimulationCommandMarker> simulation_lab_command_markers(const SimulationLabRun& run){
 std::vector<SimulationCommandMarker> out;
 for(size_t index=0;index<run.entities.size();++index){
  const auto& e=run.entities[index];if(e.root_model_terminal||e.runtime_removed)continue;
  const auto add=[&](uint32_t effect,Vec3f point){
   if(std::any_of(out.begin(),out.end(),[&](const auto& m){++duplicate_comparisons;return m.owner_entity_index==index&&
       m.effect_id==effect&&m.point.x==point.x&&m.point.y==point.y&&m.point.z==point.z;}))return;
   out.push_back({effect,e.faction_index,point,index});
  };
  if(e.runtime_model_class_id==13U){
   if(const auto target=e.production_drive_target_world?e.production_drive_target_world:e.production_exit_world)
    add(143U,*target);
  }else{
   if((e.combat_command_mode_flags&2U)!=0U&&e.combat_command_target_position_world)
    add(149U,*e.combat_command_target_position_world);
   else if(e.target_entity_index&&*e.target_entity_index<run.entities.size()&&
      !run.entities[*e.target_entity_index].root_model_terminal)
    add(149U,run.entities[*e.target_entity_index].actor.position);
   else if(e.actor.move_target&&reference_drive_marker_pending(e.actor,e.motion,*e.actor.move_target))
    add(148U,*e.actor.move_target);
   else if(e.pioneer_movement_initialized&&(e.pioneer_movement.flags&3U)!=0U) {
    const Vec3f point{q12_to_float(e.pioneer_movement.stored.x),q12_to_float(e.pioneer_movement.stored.y),e.actor.position.z};
    if(reference_drive_marker_pending(e.actor,e.motion,point))add(148U,point);
   }
   if(e.actor.move_target&&e.actor.move_waypoint&&
      reference_drive_marker_pending(e.actor,e.motion,*e.actor.move_waypoint,true))add(148U,*e.actor.move_waypoint);
   if(!e.construction_complete)add(148U,e.actor.position);
  }
 }
 return out;
}
} // namespace thandor
