#include "thandor/simulation_lab_targeting.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/simulation_lab_ai_behavior_gate.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
int main()try{
 const auto field=FieldGrid::create(100,60,0,0);const auto& c=field.at(25,25);
 SimulationLabSeedEntity a;a.army_asset_id=110;a.faction_index=1;a.runtime_model_class_id=3;a.root_model_maximum_integrity=10000;a.shot_impact_class_index=1;a.actor.position={q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),0};a.fog_uncover_radius_q12=30*4096;a.fog_reference_height_offset_q12=4*4096;
 SimulationWeaponProfile w;w.shot_definition_id=88;w.reload_ticks=1;w.maximum_range_q12=10*4096;w.launch_attachment_count=1;w.pitch_min_turn16=-0x4000;w.pitch_max_turn16=0x4000;ShotDefinitionRecord shot;shot.definition_id=88;shot.trajectory_mode=2;shot.mode2_selection_range_q12=10*4096;shot.target_impact_damage_q12[2]=4096;shot.projectile_lifetime_ticks=20;a.combat_weapons={{w,shot}};
 auto target=a;target.army_asset_id=331;target.runtime_model_class_id=15;target.faction_index=2;target.building=true;target.shot_impact_class_index=2;target.actor.position.x+=11;target.combat_weapons.clear();
 const std::array seeds{a,target};std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=2048;cfg.sweep_mode=SimulationLabSweepMode::Fixed;SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=scenario.assignable_faction_count=2;scenario.simulate_all_active_factions_as_ai=true;scenario.runtime_catalog=std::make_shared<SimulationLabRuntimeCatalog>();
 SimulationLabExperiment lab;lab.reset(cfg,ki,seeds,{},{},{},{},{},scenario);auto& r=lab.runs()[0];
 // Check an explicitly empty visibility image before level startup projects fog.
 const std::array initial_metrics{simulation_lab_combat_metrics(r.entities[0]),simulation_lab_combat_metrics(r.entities[1])};
 simulation_lab_service_combat_command(r,field,0,initial_metrics,1);
 const bool empty_retention=!r.entities[0].target_entity_index&&r.entities[0].combat_command_generation==511;
 r.entities[0].combat_command_generation=0;
 lab.step(field,1);check(r.entities[0].target_entity_index==1,"per-step command acquisition missing outside firing range");check(empty_retention,"empty initial visibility must keep native null-target retention");check(r.entities[0].combat_command_mode_flags==9&&r.entities[0].combat_command_generation==7,"unarmed target must stamp32>>2 then decrement");check(r.metrics.weapon_shots_fired==0,"pursuit buffer fabricated an out-of-range shot");
 lab.step(field,7);check(r.entities[0].combat_command_generation==0,"command generation did not count down");lab.step(field,1);check(r.entities[0].combat_command_generation==511&&r.entities[0].combat_command_retains==1,"same target did not retain512 generation");
 // Generation protects an active target from competing candidates/position work.
 r.entities[1].actor.position.x+=15;lab.step(field,1);check(r.entities[0].target_entity_index==1,"active generation lost target early");r.entities[0].combat_command_generation=0;lab.step(field,1);check(!r.entities[0].target_entity_index,"unavailable target command failed to clear after generation");check(r.entities[0].combat_command_fallbacks==1,"missing assigned fallback not serviced");
 r.entities[1].actor.position.x=a.actor.position.x+5;simulation_lab_set_position_command(r.entities[0]);r.entities[0].actor.move_target=Vec3f{a.actor.position.x-2,a.actor.position.y,0};lab.step(field,1);check(!r.entities[0].target_entity_index&&r.entities[0].combat_command_generation==1023,"position command was stolen by combat search");check(r.metrics.weapon_shots_fired>0,"position command incorrectly stopped independent local firing");
 r.entities[0].combat_assigned_target_entity_index=1;r.entities[0].target_entity_index=1;r.entities[0].combat_command_mode_flags=9;r.entities[0].combat_command_generation=0;r.entities[1].actor.position.x+=15;lab.step(field,1);check(r.entities[0].target_entity_index==1&&r.entities[0].combat_command_mode_flags==1&&r.entities[0].combat_command_generation==1023,"group-assigned fallback generation mismatch");
 simulation_lab_clear_released_target(r,1);check(!r.entities[0].target_entity_index&&!r.entities[0].combat_assigned_target_entity_index&&r.entities[0].combat_command_mode_flags==0,"release retained active/assigned target references");
 // Interruption and callback batches are separate from the faction behavior cooldown.
 r.entities[0].combat_command_mode_flags=4;r.entities[0].combat_command_generation=2;r.entities[1].actor.position.x=a.actor.position.x+5;
 auto metrics=std::vector{simulation_lab_combat_metrics(r.entities[0]),simulation_lab_combat_metrics(r.entities[1])};simulation_lab_service_combat_command(r,field,0,metrics,3);check(r.entities[0].combat_command_generation==-1&&r.entities[0].combat_command_mode_flags==0,"negative generation failed to clear interrupted/AI flags");
 simulation_lab_set_group_target_command(r.entities[0],1);check(r.entities[0].combat_command_mode_flags==5&&r.entities[0].combat_command_generation==32,"group command must interrupt local AI for32 generation");
 simulation_lab_service_combat_command(r,field,0,metrics,33);check(r.entities[0].combat_command_generation==-1&&r.entities[0].combat_command_mode_flags==1,"group interruption did not expire independently");
 r.entities[0].target_entity_index.reset();r.entities[0].combat_command_mode_flags=0;r.entities[0].combat_command_generation=0;r.entities[0].production_linked_to_parent=true;r.entities[0].production_parked_on_pad=true;
 simulation_lab_service_combat_command(r,field,0,metrics,1);check(!r.entities[0].target_entity_index,"combat stole parked factory product");
 r.entities[0].production_linked_to_parent=false;r.entities[0].production_parked_on_pad=false;
 const auto held=lab.snapshot_copy();lab.step(field,1);check(r.entities[0].target_entity_index==1&&held.runs()[0].entities[0].target_entity_index==std::nullopt,"reacquisition or immutable command snapshot failed");
 // An armed counter uses32 rather than the unarmed target's8 generation.
 r.entities[1].combat_weapons={{w,shot}};r.entities[1].combat_weapons[0].shot.target_impact_damage_q12.fill(0);r.entities[1].combat_weapons[0].shot.target_impact_damage_q12[1]=4096;
 metrics={simulation_lab_combat_metrics(r.entities[0]),simulation_lab_combat_metrics(r.entities[1])};
 r.entities[0].target_entity_index.reset();r.entities[0].combat_command_mode_flags=0;r.entities[0].combat_command_generation=0;
 simulation_lab_service_combat_command(r,field,0,metrics,1);check(r.entities[0].combat_command_generation==31,"armed target must stamp32 then decrement");
 // A zero aggregate follows the native positive stack-leftover path. With no
 // current target, selected==current retains the null target for 512 ticks.
 auto zero_metrics=metrics;zero_metrics[0].class_damage_q12.fill(0);zero_metrics[0].class_sum=0;
 r.entities[0].target_entity_index.reset();r.entities[0].combat_assigned_target_entity_index.reset();
 r.entities[0].combat_command_mode_flags=0;r.entities[0].combat_command_generation=0;
 simulation_lab_service_combat_command(r,field,0,zero_metrics,1);
 check(!r.entities[0].target_entity_index&&r.entities[0].combat_command_generation==511,
       "zero aggregate skipped native null-target retention");
 // Negative aggregates are repair searches. No damaged friendly winner clears
 // an active hostile target instead of reusing the group-assigned fallback.
 auto repair_metrics=metrics;repair_metrics[0].class_damage_q12.fill(0);
 repair_metrics[0].class_damage_q12[2]=-4096;repair_metrics[0].class_sum=-4096;
 r.entities[0].combat_assigned_target_entity_index=1;r.entities[0].target_entity_index=1;
 r.entities[0].combat_command_mode_flags=9;r.entities[0].combat_command_generation=0;
 const auto fallbacks_before=r.entities[0].combat_command_fallbacks;
 simulation_lab_service_combat_command(r,field,0,repair_metrics,1);
 check(!r.entities[0].target_entity_index&&r.entities[0].combat_command_mode_flags==8U&&
       r.entities[0].combat_command_generation==31&&
       r.entities[0].combat_command_fallbacks==fallbacks_before,
       "repair command reused hostile group fallback");
 // Primary owner maintenance still services an unfinished/dismantling/terminal
 // combat owner until its root MODEL owner is unlinked.
 r.entities[0].construction_complete=false;r.entities[0].runtime_flags=0x10U;
 r.entities[0].root_model_terminal=true;r.entities[0].combat_command_mode_flags=0;
 r.entities[0].combat_command_generation=0;r.entities[0].target_entity_index.reset();
 simulation_lab_service_combat_command(r,field,0,metrics,1);
 check(r.entities[0].target_entity_index==1,
       "live terminal/dismantling owner skipped native combat assignment");
 r.entities[0].construction_complete=true;r.entities[0].runtime_flags=0;
 r.entities[0].root_model_terminal=false;
 // A producer-linked child may be represented for door/exit presentation, but
 // it is not an independent combat target until the link is released.
 r.entities[1].production_linked_to_parent=true;
 r.entities[1].production_parent_entity_index=0;
 r.entities[0].combat_assigned_target_entity_index=1;
 r.entities[0].target_entity_index=1;
 r.entities[0].combat_command_mode_flags=9;
 r.entities[0].combat_command_generation=0;
 simulation_lab_service_combat_command(r,field,0,metrics,1);
 check(!r.entities[0].target_entity_index&&!r.entities[0].combat_assigned_target_entity_index,
       "retained command kept a factory-linked product target");
 r.entities[1].production_linked_to_parent=false;
 r.entities[1].production_parent_entity_index.reset();
 // ResolveCommandTarget alone must not invent a direct chase before the weapon-follow callback.
 r.entities[1].combat_weapons.clear();r.entities[1].actor.position.x=a.actor.position.x+11;
 auto& mover=r.entities[0];mover.actor.position=a.actor.position;mover.actor.move_target.reset();mover.actor.move_waypoint.reset();mover.target_entity_index.reset();mover.combat_assigned_target_entity_index.reset();mover.combat_command_mode_flags=0;mover.combat_command_generation=0;mover.military_ai_command_kind=0;mover.military_ai_source_dispatch=true;mover.runtime_model_class_id=2;mover.motion.speed_q12_per_tick=512;mover.motion.yaw_max_velocity_turn16=mover.motion.yaw_acceleration_turn16=0x7fff;
 lab.step(field,5);check(mover.target_entity_index==1&&mover.actor.position.x==a.actor.position.x&&!mover.actor.move_target,"target assignment fabricated a direct chase");
 // Open Thandor AI unit update checks native movement flags, not whether a
 // UI/presentation move_target happens to be published this tick.
 {
  int32_t cooldown=5;
  check(simulation_lab_ai_unit_behavior_ready(0U,cooldown)&&cooldown==5,
        "idle AI callback must ignore a stale behavior cooldown");
  check(!simulation_lab_ai_unit_behavior_ready(0x2U,cooldown)&&cooldown==5,
        "locked movement was interrupted by the military site planner");
  check(!simulation_lab_ai_unit_behavior_ready(0x1U,cooldown)&&cooldown==4,
        "active movement ignored the behavior callback cooldown");
  cooldown=1;
  check(simulation_lab_ai_unit_behavior_ready(0x10U,cooldown)&&cooldown==0,
        "route-point completion did not reopen the AI callback");
  cooldown=-2;
  check(simulation_lab_ai_unit_behavior_ready(0x1U,cooldown)&&cooldown==-2,
        "expired native cooldown was incorrectly made more negative");
 }
 // Native aggregate metrics count linked definitions independently of whether
 // their current callback can fire. Unlinking ends the metric contribution.
 {
  SimulationLabEntity armed{};
  armed.model_components.resize(2);
  armed.model_components[0].source_model_node_index=0;
  armed.model_components[1].source_model_node_index=1;
  armed.root_model_component_index=0;
  auto nodes=std::make_shared<std::vector<ModelVisualNode>>(2);
  (*nodes)[0].parent_index=UINT32_MAX;
  (*nodes)[1].parent_index=0;
  armed.runtime_model_hierarchy=nodes;
  SimulationWeaponProfile p{};
  p.reload_ticks=2;
  p.model_node_index=1;
  p.yaw_model_node_index=1;
  ShotDefinitionRecord shot{};
  shot.mode2_selection_range_q12=12*4096;
  shot.trajectory_mode=2;
  shot.target_impact_damage_q12[2]=4096;
  armed.combat_weapons.push_back({p,shot});
  check(simulation_lab_combat_metrics(armed).class_damage_q12[2]==4096,
        "live linked weapon was unexpectedly excluded from target metrics");
  armed.model_components[1].terminal_runtime_flag8=true;
  check(simulation_lab_combat_metrics(armed).range_q12==12*4096&&
        simulation_lab_combat_metrics(armed).class_sum==4096&&
        !simulation_lab_weapon_callback_active(armed,p),
        "terminal linked definition lost native metrics or regained a firing callback");
  armed.model_components[1].terminal_runtime_flag8=false;
  armed.model_components[1].runtime_removed=true;
  check(!simulation_lab_model_node_present(armed,1)&&
        simulation_lab_combat_metrics(armed).range_q12==0,
        "removed weapon attachment still advertised a usable range");
  armed.model_components[1].runtime_removed=false;
  armed.combat_weapons[0].profile.yaw_model_node_index=8;
  check(!simulation_lab_model_node_present(armed,8)&&
        simulation_lab_combat_metrics(armed).class_sum==0,
        "missing runtime attachment resurrected a weapon");
 }
 std::cout<<"V640_COMBAT_COMMAND PASS actual-loop acquisition,range buffer,no fake shot,8/32/512/1024 generations,position protection,retention,fallback,release,batches,snapshot,no invented chase\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
