#include "thandor/simulation_lab.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main()try{
 const auto field=FieldGrid::create(40,40,0,0);
 SimulationLabSeedEntity source;source.army_asset_id=1;source.faction_index=1;source.runtime_model_class_id=10;source.actor.position={8,8,4};source.root_model_maximum_integrity=1000;
 for(uint32_t i=0;i<3;++i){SimulationLabModelComponentSeed c;c.source_model_node_index=i;c.maximum_integrity=1000;c.runtime_class_id=i==1?5:10;if(i)c.parent_component_index=0;source.model_components.push_back(c);}
 SimulationWeaponProfile w;w.model_node_index=w.launch_model_node_index=2;w.yaw_model_node_index=1;w.runtime_class_id=5;w.shot_definition_id=88;w.maximum_range_q12=20*4096;w.reload_ticks=5;w.launch_attachment_count=1;w.pitch_min_turn16=-0x4000;w.pitch_max_turn16=0x4000;
 ShotDefinitionRecord shot;shot.definition_id=88;shot.trajectory_mode=2;shot.mode2_selection_range_q12=20*4096;shot.projectile_lifetime_ticks=20;shot.target_impact_damage_q12.fill(100);source.combat_weapons={{w,shot}};
 auto target=source;target.army_asset_id=2;target.faction_index=2;target.actor.position.x+=3;target.combat_weapons.clear();
 const std::array seeds{source,target};std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};SimulationLabConfig config;config.run_count=1;config.duration_ticks=32;
 for(uint32_t flags:{1U,8U}){SimulationLabExperiment lab;lab.reset(config,parameters,seeds);auto& run=lab.runs()[0];auto& e=run.entities[0];e.actor.weapons.resize(1);e.actor.weapons[0].cooldown_ticks=3;e.model_components[1].runtime_flags_ec=flags;
  lab.step(field,2);check(run.metrics.weapon_shots_fired==0&&e.actor.weapons[0].cooldown_ticks==3,"inactive weapon owner advanced reload or fired");
  e.model_components[1].runtime_flags_ec=0;lab.step(field,8);check(run.metrics.weapon_shots_fired>0,"restored weapon owner did not resume firing");}
 // The launch leaf is not the callback owner; powering it off must not freeze the yaw-owner timer.
 SimulationLabExperiment leaf;leaf.reset(config,parameters,seeds);auto& e=leaf.runs()[0].entities[0];e.actor.weapons.resize(1);e.actor.weapons[0].cooldown_ticks=3;e.model_components[2].runtime_flags_ec=1;leaf.step(field,1);
 check(e.actor.weapons[0].cooldown_ticks==2,"weapon power gate used launch leaf instead of callback owner");
 std::cout<<"V618_WEAPON_POWER PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
