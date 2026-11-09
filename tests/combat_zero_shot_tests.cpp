#include "thandor/catalog.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main()try{
 ModelVisualNode node;node.model_definition_id=224;node.runtime_class_id=7;node.sprite_path="laser.spr";
 node.simulation.shot_definition_id=0;node.simulation.reload_ticks=14;
 node.simulation.yaw_max_velocity_turn16=node.simulation.yaw_acceleration_turn16=0x4000;
 node.simulation.pitch_max_velocity_turn16=node.simulation.pitch_acceleration_turn16=0x4000;
 node.simulation.pitch_min_turn16=-0x4000;node.simulation.pitch_max_turn16=0x4000;
 ShotDefinitionRecord shot;shot.definition_id=0;shot.trajectory_mode=0;shot.launch_speed_q12=4096;shot.projectile_lifetime_ticks=20;
 const auto lookup=[&](uint32_t id)->const ShotDefinitionRecord*{return id==0?&shot:nullptr;};
 const auto launches=[](std::string_view){return std::pair<uint32_t,uint32_t>{1,1};};
 check(node.simulation.has_weapon_definition(),"resolved stock SHT0 weapon excluded before lookup");
 const auto profiles=simulation_weapon_profiles_from_hierarchy(std::span(&node,1),lookup,launches);
 check(profiles.size()==1&&profiles[0].valid()&&profiles[0].shot_definition_id==0&&profiles[0].shot_definition_resolved,"basic laser weapon did not survive catalog preparation");
 SimulationActorState actor;SimulationWeaponState state;state.pitch.angle_turn16=0;const Vec3f target{1,0,0};
 const auto fired=simulation_tick_weapon(actor,state,profiles[0],shot,actor.position,&target,true);
 check(fired.fired&&state.cooldown_ticks==14,"resolved shot0 did not fire or lost stock reload duration");
 for(unsigned i=0;i<13;++i)check(!simulation_tick_weapon(actor,state,profiles[0],shot,actor.position,&target,true).fired,"laser fired before reload");
 check(simulation_tick_weapon(actor,state,profiles[0],shot,actor.position,&target,true).fired,"laser did not fire after stock reload");
 const auto missing=[](uint32_t)->const ShotDefinitionRecord*{return nullptr;};
 check(simulation_weapon_profiles_from_hierarchy(std::span(&node,1),missing,launches).empty(),"missing shot became a default laser");
 SimulationWeaponProfile unresolved;unresolved.reload_ticks=14;unresolved.launch_attachment_count=1;
 check(!unresolved.valid(),"unresolved default shot0 profile accepted");
 check(!ModelRuntimeSimulationProfile{}.has_weapon_definition(),"empty MDL became a weapon");
 EffectDefinitionRecord emitter;emitter.transition_kind=1;emitter.linked_shot_present=1;
 emitter.linked_shot_id=0;emitter.runtime_value_0c=4;
 check(!build_linked_shot_emissions(emitter).empty(),"explicitly present linked SHT0 emission discarded");
 emitter.linked_shot_present=0;
 check(build_linked_shot_emissions(emitter).empty(),"absent linked shot emitted a default laser");
 std::cout<<"V634_ZERO_SHOT PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
