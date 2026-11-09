#include "thandor/simulation_lab_shot_rays.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
SimulationLabStaticShotHull box(Vec3f p){SimulationLabStaticShotHull h;h.impact_class=1;for(uint32_t i=0;i<8;++i)h.corners[i]={p.x+((i&1)?.1F:-.1F),p.y+((i&2)?.1F:-.1F),p.z+((i&4)?.1F:-.1F)};return h;}
int main()try{
 auto field=FieldGrid::create(60,60,0,0);const auto& c=field.at(25,25);Vec3f start{q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),3},aim{start.x+4,start.y,3};
 SimulationLabRun run;run.entities.resize(3);run.entities[0].faction_index=1;run.entities[1].faction_index=2;run.entities[2].faction_index=1;
 SimulationWeaponProfile weapon;weapon.reload_ticks=1;weapon.pitch_min_turn16=-0x4000;weapon.pitch_max_turn16=0x4000;
 ShotDefinitionRecord shot;shot.trajectory_mode=0;shot.launch_speed_q12=4096;shot.projectile_lifetime_ticks=8;
 auto clear=[&](std::span<const SimulationLabLiveShotHull> hulls,std::optional<size_t> target=1){return simulation_lab_weapon_line_of_fire_clear(run,0,target,start,aim,weapon,shot,field,hulls,{});};
 std::vector<SimulationLabLiveShotHull> hulls{{box({start.x+1,start.y,3}),2,0},{box(aim),1,0}};
 // source class0 damage positive => own owner blocks, other owners pass. Actual target always passes.
 SimulationLabWeaponSeed gun;gun.profile=weapon;gun.shot.target_impact_damage_q12[0]=100;run.entities[0].combat_weapons={gun};
 check(!clear(hulls),"positive commandState failed friendly obstruction");
 hulls[0].entity_index=1;check(clear(hulls),"command target blocked its own LOS");
 hulls[0].entity_index=2;run.entities[2].faction_index=2;check(clear(hulls),"positive commandState failed native other-owner pass");
 const std::array stone{box({start.x+1,start.y,3})};
 check(simulation_lab_weapon_line_of_fire_clear(run,0,1,start,aim,weapon,shot,field,{},stone),
       "positive commandState rejected neutral MODEL ownership");
 run.entities[0].combat_weapons[0].shot.target_impact_damage_q12[0]=0;
 check(!simulation_lab_weapon_line_of_fire_clear(run,0,1,start,aim,weapon,shot,field,{},stone),
       "nonpositive commandState ignored a neutral MODEL obstruction");
 run.entities[0].combat_weapons[0].shot.target_impact_damage_q12[0]=100;
 const std::array behind{box({start.x+6,start.y,3})};
 check(simulation_lab_weapon_line_of_fire_clear(run,0,1,start,aim,weapon,shot,field,{},behind),
       "background map model incorrectly obstructed the aim point");
 run.entities[0].combat_weapons[0].shot.target_impact_damage_q12[0]=-1;
 check(!clear(hulls),"negative commandState failed other-owner obstruction");run.entities[2].faction_index=1;check(clear(hulls),"negative commandState failed own-owner pass");
 shot.trajectory_mode=2;weapon.pitch_min_turn16=1;weapon.pitch_max_turn16=2;check(clear(hulls),"fixed-range LOS did not bypass all tests");
 shot.trajectory_mode=0;check(!clear({}),"unguided elevation escaped pitch window");shot.mode3_lead_disabled=1;check(clear({}),"guidance flag did not bypass LOS pitch window");
 weapon.pitch_min_turn16=-0x4000;weapon.pitch_max_turn16=0x4000;shot.mode3_lead_disabled=0;
 aim={start.x+8,start.y,3};check(!clear({}),"LOS ignored native lifetime-minus-one range");
 run.entities[1].motion.collision_radius_q12=8192;check(clear({}),"LOS failed target half-radius range adjustment");
 run.level_runtime_enabled=true;
 run.entities[1].motion.collision_radius_q12=5;
 aim={start.x+q12_to_float(7*4096+3),start.y,3+q12_to_float(1)};
 check(clear({}),"odd target footprint rejected the exact native range boundary");
 aim.x+=q12_to_float(1);
 check(!clear({}),"odd target footprint accepted one Q12 beyond native range");
 run.level_runtime_enabled=false;
 run.entities[1].motion.collision_radius_q12=8192;
 aim={start.x+2,start.y,-1};check(!clear({},1),"terrain obstruction allowed entity shot");
 // Ground shot tolerance uses geometric target distance and native0x400 distance tolerance.
 aim={start.x+1,start.y,0};check(clear({},std::nullopt),"ground shot landing at aim distance was blocked");
 shot.trajectory_mode=1;shot.ballistic_divisor_q12=4096;shot.launch_speed_q12=1;aim={start.x+4,start.y,3};
 check(!clear({}),"unsolvable ballistic arc was accepted");
 shot.launch_speed_q12=4*4096;shot.ballistic_divisor_q12=100;run.entities[0].combat_weapons[0].shot.target_impact_damage_q12[0]=100;
 // Ballistic native quirk owner-tests originNode on any hit, not the actual hit entity.
 hulls[0]={box({start.x+1,start.y,3.01F}),2,0};hulls.resize(1);
 check(!clear(hulls),"ballistic origin-owner quirk was not retained");
 // Integration: a same-owner blocker prevents a prepared worker weapon from firing.
 auto runtime=std::make_shared<SimulationLabRuntimeCatalog>();auto& spr=runtime->sprites["test"];
 spr.has_bounds=true;spr.bounds_min={-.1F,-.1F,-.1F};spr.bounds_max={.1F,.1F,.1F};spr.shot_points={{0,{0,0,0}}};
 ModelVisualNode node;node.parent_index=UINT32_MAX;node.model_definition_id=1;node.runtime_class_id=10;node.shot_impact_class_index=1;node.sprite_path="test";node.simulation.placement_contact_kind=3;
 auto hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(std::vector{node});
 weapon.shot_definition_id=88;weapon.maximum_range_q12=20*4096;weapon.launch_attachment_count=1;
 weapon.yaw_max_velocity_turn16=weapon.pitch_max_velocity_turn16=0x4000;weapon.yaw_acceleration_turn16=weapon.pitch_acceleration_turn16=0x4000;
 shot.trajectory_mode=0;shot.launch_speed_q12=4096;shot.ballistic_divisor_q12=0;shot.target_impact_effect_ids[1]=44;shot.target_impact_damage_q12[0]=100;shot.target_impact_damage_q12[1]=100;
 SimulationLabSeedEntity shooter;shooter.army_asset_id=1;shooter.faction_index=1;shooter.runtime_model_class_id=10;shooter.root_model_maximum_integrity=1000;shooter.shot_impact_class_index=1;shooter.actor.position=start;shooter.combat_weapons={{weapon,shot}};
 // V704 requires an actual component-to-hierarchy mapping before activating a
 // weapon callback. The old synthetic root defaulted to SIZE_MAX, so this test
 // never reached the intended worker line-of-fire validation.
 SimulationLabModelComponentSeed root;root.source_model_node_index=0;
 root.model_definition_id=1;root.runtime_class_id=10;root.maximum_integrity=1000;
 shooter.model_components={root};
 auto enemy=shooter;enemy.combat_weapons.clear();enemy.faction_index=2;enemy.actor.position.x+=4;enemy.source_placement_index=1;
 auto ally=enemy;ally.faction_index=1;ally.actor.position.x=start.x+1;ally.source_placement_index=2;
 const std::array seeds{shooter,enemy,ally};SimulationLabScenarioRuntimeSeed scenario;scenario.runtime_catalog=runtime;
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=80;std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
 SimulationLabExperiment lab;lab.reset(config,ki,seeds,{},{},{},{},{},scenario);auto& live=lab.runs()[0];for(auto& e:live.entities)e.runtime_model_hierarchy=hierarchy;
 lab.step(field,16);
 check(live.metrics.weapon_shots_fired==0&&live.entities[0].actor.weapons[0].target_aligned&&!live.entities[0].actor.weapons[0].line_of_fire_clear,"worker bypassed a blocked aligned weapon");
 live.entities[2].actor.position.y+=2;lab.step(field,16);check(live.metrics.weapon_shots_fired>0&&live.metrics.projectile_damage_applied>0,"worker failed to resume firing after obstruction cleared");
 // A disabled primary weapon still constrains an immobile owner's AI search.
 // Admitting an out-of-range target while disabled would retain it when power returns.
 scenario.enabled=true;scenario.active_faction_count=scenario.assignable_faction_count=2;
 shooter.runtime_model_class_id=4U;shooter.building=true;
 shooter.runtime_flags=1U;
 shooter.fog_uncover_radius_q12=100000;
 enemy.actor.position={start.x+9,start.y,start.z};
 const std::array disabled_seeds{shooter,enemy};
 lab.reset(config,ki,disabled_seeds,{},{},{},{},{},scenario);
 auto& disabled=lab.runs()[0];
 for(auto& e:disabled.entities)e.runtime_model_hierarchy=hierarchy;
 disabled.entities[0].runtime_model_class_id=4U;
 disabled.entities[0].runtime_flags|=1U;
 lab.step(field,16);
 check(!disabled.entities[0].target_entity_index&&
       disabled.entities[0].combat_command_acquisitions==0,
       "disabled primary weapon bypassed the immobile owner's range admission");
 // Native's pitch helper returns the remaining difference while turning;
 // its caller can mistake that difference for an already-reached positive request.
 SimulationActorState pitch_actor{};SimulationWeaponState pitch_state{};
 SimulationWeaponProfile pitch_weapon=weapon;
 pitch_weapon.runtime_class_id=7U;
 pitch_weapon.pitch_min_turn16=-0x4000;pitch_weapon.pitch_max_turn16=0x4000;
 pitch_weapon.maximum_range_q12=100000;pitch_weapon.reload_ticks=14;
 pitch_weapon.pitch_acceleration_turn16=82;pitch_weapon.pitch_max_velocity_turn16=658;
 ShotDefinitionRecord pitch_shot=shot;pitch_shot.trajectory_mode=0;
 pitch_shot.elevation_offset_angle16=0;
 const Vec3f pitch_target{4,0,0.25F};
 const auto request=fixed_atan2_angle16_compat(1024,16384);
 pitch_state.pitch.angle_turn16=2U*request;
 const auto pitch_result=simulation_tick_weapon(pitch_actor,pitch_state,pitch_weapon,
     pitch_shot,{},&pitch_target,true,UINT32_MAX,{},true,true);
 check(pitch_result.fired&&pitch_state.pitch.angle_turn16==2U*request&&
       pitch_state.cooldown_ticks==14,
       "native pitch return-value coincidence did not fire while still turning");
 std::cout<<"V621_LINE_OF_FIRE PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
