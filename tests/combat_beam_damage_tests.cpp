#include "thandor/simulation_lab_environment.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
int main()try{
 auto field=FieldGrid::create(60,60,0,0);const auto& c=field.at(25,25);const Vec3f origin{q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),3};
 auto catalog=std::make_shared<SimulationLabRuntimeCatalog>();auto& sprite=catalog->sprites["target"];
 sprite.has_bounds=true;sprite.bounds_min={-.2F,-.2F,-.2F};sprite.bounds_max={.2F,.2F,.2F};catalog->effect_lifetimes[44]=6;
 ModelVisualNode node;node.parent_index=UINT32_MAX;node.model_definition_id=1;node.runtime_class_id=10;node.sprite_path="target";node.shot_impact_class_index=1;node.simulation.placement_contact_kind=3;
 auto hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(std::vector{node});
 SimulationLabSeedEntity owner;owner.army_asset_id=1;owner.faction_index=1;owner.runtime_model_class_id=10;owner.root_model_maximum_integrity=1000;owner.shot_impact_class_index=1;owner.actor.position=origin;
 ShotDefinitionRecord shot;shot.definition_id=88;shot.trajectory_mode=0;shot.launch_speed_q12=4096;shot.projectile_lifetime_ticks=8;shot.target_impact_effect_ids[1]=44;shot.target_impact_damage_q12[1]=80;
 SimulationLabWeaponSeed weapon;weapon.shot=shot;owner.combat_weapons={weapon}; // Invalid callback profile prevents additional shots; this tests one existing beam.
 auto target=owner;target.combat_weapons.clear();target.faction_index=2;target.source_placement_index=1;target.actor.position.x+=2;
 auto crossing=target;crossing.source_placement_index=2;crossing.actor.position.x+=2;crossing.actor.position.y+=2;
 const std::array seeds{owner,target,crossing};SimulationLabScenarioRuntimeSeed scenario;scenario.runtime_catalog=catalog;
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=16;std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
 SimulationLabExperiment lab;lab.reset(config,ki,seeds,{},{},{},{},{},scenario);auto& r=lab.runs()[0];
 r.entities[1].runtime_model_hierarchy=hierarchy;r.entities[2].runtime_model_hierarchy=hierarchy;
 SimulationProjectileState beam;beam.serial=1;beam.owner_placement_index=0;beam.weapon_profile_index=0;beam.shot_definition_id=88;beam.launch_position=origin;beam.target_position=target.actor.position;beam.impact_position=target.actor.position;
 beam.live_target_placement_index=1;beam.impact_target_placement_index=1;beam.impact_kind=SimulationImpactKind::TargetModelImpactClass;beam.impact_material_index=1;beam.impact_effect_id=44;beam.launch_velocity={1,0,0};beam.travel_ticks=1;beam.lifetime_ticks=8;r.projectiles={beam};
 lab.step(field,1);check(r.entities[1].root_model_integrity==990,"beam did not apply one divided damage tick");
 r.entities[1].actor.position.y+=2;lab.step(field,1);
 check(r.entities[1].root_model_integrity==990,"moved target kept taking stale beam damage");
 check(!r.projectiles[0].impact_target_placement_index&&r.projectiles[0].impact_kind==SimulationImpactKind::None,"beam miss retained the previous entity/material");
 r.entities[2].actor.position.y=origin.y;lab.step(field,1);
 check(r.entities[2].root_model_integrity==990,"crossing unit did not receive beam damage");
 check(r.metrics.weapon_impact_effect_events==1,"beam emitted its once-only impact effect again");
 lab.step(field,4);check(r.entities[2].root_model_integrity==950,"beam active ages1..7 damage cadence drifted");
 lab.step(field,1);check(r.entities[2].root_model_integrity==950&&r.projectiles[0].impact_damage_applied,"beam expired after servicing an extra damage tick");
 check(r.metrics.projectile_damage_applied==60,"beam cumulative real-contact damage incorrect");
 std::cout<<"V621_BEAM_DAMAGE PASS; target-left damage=10, crossing damage=50, one impact EFF, no expiry hit\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
