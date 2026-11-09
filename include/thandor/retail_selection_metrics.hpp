#pragma once
#include "thandor/retail_selection_frame.hpp"
#include "thandor/simulation_lab.hpp"
#include <limits>

namespace thandor {
inline std::optional<std::array<int32_t,2>> retail_selection_reload(
 std::span<const SimulationWeaponProfile> profiles,const SimulationActorState& actor){
 for(size_t i=0;i<profiles.size()&&i<actor.weapons.size();++i){
  const auto& profile=profiles[i];const auto& state=actor.weapons[i];
  if(profile.runtime_class_id<5U||profile.runtime_class_id>9U)continue;
  const auto maximum=std::min(profile.reload_ticks,uint32_t(INT32_MAX));
  int32_t remaining=int32_t(std::min(state.cooldown_ticks,uint32_t(INT32_MAX)));
  if(profile.runtime_class_id==9U){
   uint32_t minimum=UINT32_MAX;
   for(const int32_t value:state.attachment_reload_ticks)minimum=std::min(minimum,uint32_t(value));
   remaining=std::max(int32_t(minimum),int32_t(std::min(state.inter_shot_ticks,uint32_t(INT32_MAX))));
  }
  return std::array<int32_t,2>{int32_t(maximum),int32_t(maximum)-std::clamp(remaining,0,int32_t(maximum))};
 }
 return std::nullopt;
}
inline RetailSelectionFrameMetrics retail_selection_metrics(const SimulationLabEntity& entity){
 RetailSelectionFrameMetrics out;
 const auto sat=[](uint64_t v){return uint32_t(std::min(v,uint64_t(UINT32_MAX)));};
 const auto root=std::find_if(entity.model_components.begin(),entity.model_components.end(),[](const auto& c){return !c.parent_component_index;});
 bool include_children=false;
 if(root!=entity.model_components.end()&&entity.runtime_model_hierarchy&&root->source_model_node_index<entity.runtime_model_hierarchy->size())
  include_children=((*entity.runtime_model_hierarchy)[root->source_model_node_index].simulation.runtime_render_flags&0x80U)!=0U;
 const size_t root_index=size_t(root-entity.model_components.begin());
 for(const auto& component:entity.model_components){
  if(component.terminal_runtime_flag8)continue;
  const bool selected=&component==(root==entity.model_components.end()?nullptr:&*root)||
      (include_children&&component.parent_component_index==root_index);
  if(!selected)continue;
  out.energy_total_q4=sat(uint64_t(out.energy_total_q4)+component.retail_energy_demand_q4);
  if((component.runtime_flags_ec&1U)==0U)
   out.energy_active_q4=sat(uint64_t(out.energy_active_q4)+component.retail_energy_demand_q4);
 }
 if((entity.runtime_model_class_id==11U||entity.runtime_model_class_id==13U)&&entity.production_building){
  out.top_bar=std::array<int32_t,2>{int32_t(std::min(entity.production_duration_ticks,uint32_t(INT32_MAX))),int32_t(std::min(entity.production_elapsed_ticks,uint32_t(INT32_MAX)))};
  out.force_energy_meter=true;
 }else{
  std::vector<SimulationWeaponProfile> profiles;profiles.reserve(entity.combat_weapons.size());
  for(const auto& weapon:entity.combat_weapons)profiles.push_back(weapon.profile);
  out.top_bar=retail_selection_reload(profiles,entity.actor);
  if(!out.top_bar&&(entity.research_active||entity.research_pending_payment))
   out.top_bar=std::array<int32_t,2>{int32_t(std::min(entity.research_duration_ticks,uint32_t(INT32_MAX))),int32_t(std::min(entity.research_elapsed_ticks,uint32_t(INT32_MAX)))};
 }
 return out;
}
} // namespace thandor
