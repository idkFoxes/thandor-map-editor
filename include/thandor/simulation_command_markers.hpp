#pragma once
#include "thandor/simulation_lab.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace thandor {
struct SimulationCommandMarker {
 uint32_t effect_id{},faction_index{};
 Vec3f point{};
 size_t owner_entity_index{};
};
struct SimulationCommandMarkerDiagnostics {
 size_t owners_examined{},owners_filtered{},candidates{},duplicate_comparisons{};
 size_t duplicates{},markers_visited{};
 bool stopped_early{};
};
// Match the motion service's inclusive final-target window. Waypoints use its
// tighter step-consumption radius and must not clear a standing factory rally point.
inline bool simulation_drive_marker_pending(const SimulationActorState& actor,
    const SimulationMotionProfile& motion, Vec3f point, bool waypoint = false) noexcept {
    const float dx=point.x-actor.position.x,dy=point.y-actor.position.y;
    if(!waypoint)return std::abs(dx)>1.F||std::abs(dy)>1.F;
    const float speed=std::max(q12_to_float(std::abs(motion.speed_q12_per_tick)),1.F/4096.F);
    return std::hypot(dx,dy)>speed;
}
// Stream live intent in owner/marker order. Filter before constructing markers;
// returning false from visit stops immediately, including within the current owner.
// At most target, waypoint and construction intents exist for any one owner, so
// duplicate detection examines only its three stack records, never earlier owners.
// Pending purchases without a world position are not placement plans.
template<class OwnerVisible,class Visitor>
void simulation_lab_visit_command_markers(const SimulationLabRun& run,OwnerVisible&& owner_visible,
    Visitor&& visit,SimulationCommandMarkerDiagnostics* diagnostics=nullptr){
 bool keep_going=true;
 for(size_t index=0;index<run.entities.size()&&keep_going;++index){
  if(diagnostics)++diagnostics->owners_examined;
  const auto& e=run.entities[index];if(e.root_model_terminal||e.runtime_removed)continue;
  // Idle owners have no markers and need no fog query.
  const bool has_intent=e.runtime_model_class_id==13U
      ?static_cast<bool>(e.production_drive_target_world||e.production_exit_world)
      :e.target_entity_index||e.combat_command_target_position_world||e.actor.move_target||!e.construction_complete||
          (e.pioneer_movement_initialized&&(e.pioneer_movement.flags&3U)!=0U);
  if(!has_intent)continue;
  if(!owner_visible(index,e)){if(diagnostics)++diagnostics->owners_filtered;continue;}
  std::array<SimulationCommandMarker,3> own_markers{};
  size_t own_count=0;
  const auto add=[&](uint32_t effect,Vec3f point){
   if(!keep_going)return;
   if(diagnostics)++diagnostics->candidates;
   if(std::any_of(own_markers.begin(),own_markers.begin()+own_count,[&](const auto& m){
       if(diagnostics)++diagnostics->duplicate_comparisons;
       return m.effect_id==effect&&m.point.x==point.x&&m.point.y==point.y&&m.point.z==point.z;
      })) {if(diagnostics)++diagnostics->duplicates;return;}
   const SimulationCommandMarker marker{effect,e.faction_index,point,index};
   own_markers[own_count++]=marker;
   if(diagnostics)++diagnostics->markers_visited;
   keep_going=visit(marker);
  };
  if(e.runtime_model_class_id==13U){
   if(const auto target=e.production_drive_target_world?e.production_drive_target_world:e.production_exit_world)
    add(143U,*target);
  }else{
   if((e.combat_command_mode_flags&2U)!=0U&&e.combat_command_target_position_world)
    add(149U,*e.combat_command_target_position_world);
   else if(e.target_entity_index&&*e.target_entity_index<run.entities.size()&&
      !run.entities[*e.target_entity_index].root_model_terminal&&
      !run.entities[*e.target_entity_index].runtime_removed)
    add(149U,run.entities[*e.target_entity_index].actor.position);
   else if(e.actor.move_target&&simulation_drive_marker_pending(e.actor,e.motion,*e.actor.move_target))
    add(148U,*e.actor.move_target);
   else if(e.pioneer_movement_initialized&&(e.pioneer_movement.flags&3U)!=0U) {
    const Vec3f point{q12_to_float(e.pioneer_movement.stored.x),q12_to_float(e.pioneer_movement.stored.y),e.actor.position.z};
    if(simulation_drive_marker_pending(e.actor,e.motion,point))add(148U,point);
   }
   if(e.actor.move_target&&e.actor.move_waypoint&&
      simulation_drive_marker_pending(e.actor,e.motion,*e.actor.move_waypoint,true))add(148U,*e.actor.move_waypoint);
   if(!e.construction_complete)add(148U,e.actor.position);
  }
 }
 if(diagnostics)diagnostics->stopped_early=!keep_going;
}
// Compatibility API for callers that need the full list. Rendering uses the
// visitor directly so hidden owners and the draw cap do not allocate a full list.
inline std::vector<SimulationCommandMarker> simulation_lab_command_markers(const SimulationLabRun& run){
 std::vector<SimulationCommandMarker> out;
 simulation_lab_visit_command_markers(run,[](size_t,const SimulationLabEntity&){return true;},
     [&](const SimulationCommandMarker& marker){out.push_back(marker);return true;});
 return out;
}
} // namespace thandor
