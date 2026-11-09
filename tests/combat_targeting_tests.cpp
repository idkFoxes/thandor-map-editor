#include "thandor/simulation_lab_targeting.hpp"
#include "thandor/fog_preview.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main()try{
 {
  SimulationLabRun released;released.level_runtime_enabled=true;released.entities.resize(2);
  auto& owner=released.entities[0];owner.target_entity_index=1;
  owner.linked_entity_index=1;
  owner.combat_assigned_target_entity_index=1;owner.combat_command_mode_flags=9;
  owner.military_ai_group_assigned=true;owner.combat_command_generation=7;
  simulation_lab_clear_released_target(released,1);
  check(!owner.target_entity_index&&!owner.combat_assigned_target_entity_index&&
        owner.military_ai_group_assigned,"released target must preserve native AI group flag");
  check(!owner.linked_entity_index,"released model retained a collision link");
  check(owner.combat_command_mode_flags==0&&owner.combat_command_generation==7,
        "reference teardown changed the command generation");
  owner.target_entity_index=1;owner.combat_command_mode_flags=0x1dU;
  simulation_lab_clear_released_target(released,1);
  check(owner.combat_command_mode_flags==0x10U&&owner.combat_command_generation==7,
        "reference teardown cleared selection bits or restamped generation");
 }
 SimulationLabCombatScoreInput in;in.source_range_q12=4096;in.target_runtime_class=1;in.source_counter_for_target=in.target_counter_for_source=4096;
 check(simulation_lab_combat_score(in).value==4403712,"retail numeric score mismatch");
 in.target_condition_q12=2048;check(simulation_lab_combat_score(in).value==4405888,"condition deficit missing");
 in.target_condition_q12=4096;in.target_counter_for_source=0;auto score=simulation_lab_combat_score(in);
 check(score.value==1068160&&score.command_generation_shift==2,"unarmed target quarter or command shift mismatch");
 in.target_counter_for_source=4096;in.delta_x_q12=12288;check(simulation_lab_combat_score(in).value==4399104,"inclusive range+2 boundary rejected");
 ++in.delta_x_q12;check(simulation_lab_combat_score(in).value==0,"outside range+2 admitted");
 in.delta_x_q12=0;in.source_counter_for_target=0;check(simulation_lab_combat_score(in).value==0,"zero damage counter admitted");
 in.source_counter_for_target=-4096;in.source_class_count=-4096;check(simulation_lab_combat_score(in).value==0,"friendly repair score admitted full health");
 in.target_condition_q12=2048;check(simulation_lab_combat_score(in).value==1068704,"negative counter magnitude/quarter mismatch");
 in.source_counter_for_target=4096;in.source_class_count=1;in.target_counter_for_source=100000000;
 check(simulation_lab_combat_score(in).value<0,"wrapped negative score treated as unsigned priority");
 in.source_range_q12=0;check(simulation_lab_combat_score(in).value==0,"zero range divided by zero");
 auto field=FieldGrid::create(100,60,0,0);SimulationLabRun run;run.entities.resize(3);
 for(size_t i=0;i<3;++i){auto& e=run.entities[i];e.faction_index=i==0?1:2;e.construction_complete=true;e.root_model_integrity=e.root_model_maximum_integrity=1000;e.shot_impact_class_index=i==0?1:2;e.runtime_model_class_id=i==1?11:2;
  const auto& c=field.at(25+static_cast<uint32_t>(i)*3,25);e.actor.position={q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),4};}
 SimulationWeaponProfile w;w.shot_definition_id=88;w.reload_ticks=1;w.maximum_range_q12=10*4096;w.launch_attachment_count=1;w.pitch_min_turn16=-0x4000;w.pitch_max_turn16=0x4000;
 ShotDefinitionRecord shot;shot.definition_id=88;shot.trajectory_mode=2;shot.mode2_selection_range_q12=10*4096;shot.target_impact_damage_q12[2]=4096;shot.projectile_lifetime_ticks=20;
 run.entities[0].combat_weapons={{w,shot}};auto counter=shot;counter.target_impact_damage_q12.fill(0);counter.target_impact_damage_q12[1]=4096;
 for(size_t i=1;i<3;++i)run.entities[i].combat_weapons={{w,counter}};
 run.entities[0].motion.speed_q12_per_tick=512;
 run.current_visibility_occupancy.assign(field.cells().size(),uint64_t{1}<<8U);
 check(simulation_lab_select_combat_target(run,field,0).entity_index==2,"near storage displaced higher-scored armed unit");
 run.entities[2].production_linked_to_parent=true;
 run.entities[2].production_parent_entity_index=1;
 check(simulation_lab_select_combat_target(run,field,0).entity_index==2,
       "movement-locked factory ground model disappeared from native targeting");
 run.entities[2].production_parked_on_pad=true;
 check(simulation_lab_select_combat_target(run,field,0).entity_index==1,
       "parked product displaced an exposed building target");
 run.entities[2].production_parked_on_pad=false;
 run.entities[2].production_linked_to_parent=false;
 run.entities[2].production_parent_entity_index.reset();
 const auto farther_position=run.entities[2].actor.position;run.entities[2].actor.position.x=run.entities[0].actor.position.x+10.5F;
 check(simulation_lab_select_combat_target(run,field,0).entity_index==1,"outside-fire-range threat blocked a shootable target");run.entities[2].actor.position=farther_position;
 run.entities[1].runtime_model_class_id=2;run.entities[1].actor.position=run.entities[2].actor.position;
 check(simulation_lab_select_combat_target(run,field,0).entity_index==2,
       "score tie failed newest-first native owner-list rule");
 std::array<std::uint8_t,3> line_blocked{};line_blocked[2]=1U;
 check(simulation_lab_select_combat_target(run,field,0,{},false,line_blocked).entity_index==1,
       "blocked high-score target masked a clear candidate");
 run.entities[1].runtime_removed=true;run.entities[0].motion.speed_q12_per_tick=0;
 check(!simulation_lab_select_combat_target(run,field,0,{},false,line_blocked).entity_index,
       "immobile source accepted a blocked pursuit target");
 run.entities[0].motion.speed_q12_per_tick=512;run.entities[1].runtime_removed=false;
 run.entities[2].construction_complete=false;
 check(simulation_lab_select_combat_target(run,field,0).entity_index==2,
       "live unfinished target was omitted from native owner selection");
 run.entities[2].construction_complete=true;run.entities[2].runtime_flags=0x10U;
 check(simulation_lab_select_combat_target(run,field,0).entity_index==2,
       "dismantling-only target was treated as destroyed");
 run.entities[2].runtime_flags=0;
 run.entities[1].runtime_removed=true;check(simulation_lab_select_combat_target(run,field,0).entity_index==2,"removed target admitted");
 run.entities[1].runtime_removed=false;run.entities[1].model_components.resize(1);run.entities[1].root_model_component_index=0;run.entities[1].model_components[0].runtime_removed=true;
 check(simulation_lab_select_combat_target(run,field,0).entity_index==2,"removed root MODEL target admitted");
 run.entities[1].model_components[0].runtime_removed=false;
 // Preserve the original baseline state for the subsequent visibility/relation/flag checks:
 // entity 1 remains an ArmyRuntime tombstone, leaving entity 2 as the only candidate.
 run.entities[1].runtime_removed=true;
 run.entities[0].model_components.resize(1);run.entities[0].root_model_component_index=0;run.entities[0].model_components[0].runtime_removed=true;
 check(!simulation_lab_select_combat_target(run,field,0).entity_index,"removed root MODEL source acquired target");
 run.entities[0].model_components[0].runtime_removed=false;
 const auto c=retail_fog_world_to_cell(field,float_to_q12(run.entities[2].actor.position.x),float_to_q12(run.entities[2].actor.position.y));
 run.current_visibility_occupancy[c->y*field.width()+c->x]=0;check(!simulation_lab_select_combat_target(run,field,0).entity_index,"unseen target acquired");
 run.current_visibility_occupancy[c->y*field.width()+c->x]=uint64_t{1}<<8U;
 run.faction_packed_relation_states[1]=8U<<(2U*4U);run.faction_capability_flags[1]|=1U<<2U;
 check(!simulation_lab_select_combat_target(run,field,0).entity_index,"allied target acquired");
 run.faction_packed_relation_states[1]=0;run.faction_capability_flags[1]&=~(1U<<2U);
 run.entities[2].runtime_flags=8;check(!simulation_lab_select_combat_target(run,field,0).entity_index,"raw destroyed model flag ignored");run.entities[2].runtime_flags=0;
 run.entities[0].runtime_flags=0x10;check(simulation_lab_select_combat_target(run,field,0).entity_index==2,"live dismantling source was suppressed before owner unlink");run.entities[0].runtime_flags=0;
 run.entities[2].actor.position.x+=30;check(!simulation_lab_select_combat_target(run,field,0).entity_index,"distant target acquired");
 // Exercise the actual selected-run firing integration without changing its strategic site order.
 run.entities[1].runtime_removed=false;run.entities[1].runtime_model_class_id=11;run.entities[1].actor.position.x-=2;run.entities[2].actor.position.x-=30;
 std::array<SimulationLabSeedEntity,3> seeds{};
 for(size_t i=0;i<3;++i){const auto& e=run.entities[i];auto& s=seeds[i];s.army_asset_id=static_cast<uint32_t>(i+1);s.faction_index=e.faction_index;s.runtime_model_class_id=e.runtime_model_class_id;s.actor=e.actor;s.combat_weapons=e.combat_weapons;s.shot_impact_class_index=e.shot_impact_class_index;s.root_model_maximum_integrity=10000;s.fog_uncover_radius_q12=20*4096;s.fog_reference_height_offset_q12=4*4096;if(i)s.combat_weapons[0].profile.launch_attachment_count=0;}
 SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=scenario.assignable_faction_count=2;scenario.simulate_all_active_factions_as_ai=true;scenario.runtime_catalog=std::make_shared<SimulationLabRuntimeCatalog>();
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=32;std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
 SimulationLabExperiment lab;lab.reset(config,ki,seeds,{},{},{},{},{},scenario);auto& active=lab.runs()[0];active.entities[0].military_ai_source_dispatch=true;active.entities[0].military_ai_command_kind=1;const auto destination=Vec3f{-10,-10,4};active.entities[0].actor.move_target=destination;
 // Acquisition is the live aim target; a physical hit is assigned only after the shot ray hits.
 lab.step(field,3);check(active.metrics.weapon_shots_fired>0&&!active.projectiles.empty()&&active.projectiles[0].live_target_placement_index==2,"firing loop did not consume scored local target");
 check(!active.entities[0].target_entity_index&&active.entities[0].actor.move_target&&active.entities[0].actor.move_target->x==destination.x,"local fire redirected strategic command");
 // Resolve obstruction geometry only when native actually searches for a
 // command. A retained live command must not trigger another candidate pass.
 auto lazy=run;auto& retained=lazy.entities[0];retained.target_entity_index=2;
 retained.combat_command_mode_flags=9;retained.combat_command_generation=10;
 std::vector<SimulationLabCombatMetrics> lazy_metrics;
 for(const auto& e:lazy.entities)lazy_metrics.push_back(simulation_lab_combat_metrics(e));
 unsigned obstruction_passes=0;
 const auto obstruction=[&](size_t target){
  check(target==2,"obstruction ray tested a lower-scored candidate");
  ++obstruction_passes;return false;
 };
 simulation_lab_service_combat_command(lazy,field,0,lazy_metrics,1,{},obstruction);
 check(obstruction_passes==0&&retained.combat_command_generation==9,"retained command eagerly rebuilt obstruction geometry");
 retained.combat_command_generation=0;
 simulation_lab_service_combat_command(lazy,field,0,lazy_metrics,1,{},obstruction);
 check(obstruction_passes==1,"expired command did not resolve its obstruction image exactly once");
 // A switched-off turret cannot clear a retained command when its target
 // leaves visibility. The active callback clears it when power returns.
 auto turret_seeds=seeds;turret_seeds[0].runtime_model_class_id=4;turret_seeds[0].building=true;
 SimulationLabExperiment paused;paused.reset(config,ki,turret_seeds,{},{},{},{},{},scenario);
 auto& paused_run=paused.runs()[0];auto& turret=paused_run.entities[0];auto& hidden=paused_run.entities[2];
 turret.target_entity_index=2;turret.combat_command_mode_flags=9;turret.combat_command_generation=100;
 turret.runtime_flags=1;
 hidden.model_terrain_state_initialized=true;hidden.model_primary_occupancy_mask=0;
 paused.step(field,1);
 check(turret.target_entity_index==2&&turret.combat_command_generation==99,
       "switched-off turret dropped a hidden command target");
 turret.runtime_flags=0;hidden.model_primary_occupancy_mask=0;
 paused.step(field,1);
 check(turret.target_entity_index!=2,"powered turret failed to clear its hidden command target");
 std::cout<<"V618_COMBAT_TARGETING PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
