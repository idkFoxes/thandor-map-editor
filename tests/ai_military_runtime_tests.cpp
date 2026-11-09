#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_visual.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
SimulationLabConfig config(){SimulationLabConfig c;c.run_count=1;c.duration_ticks=256;
    c.sweep_mode=SimulationLabSweepMode::Fixed;c.parameter_index=100;return c;}
SimulationLabScenarioRuntimeSeed scenario(){SimulationLabScenarioRuntimeSeed s;
    s.enabled=true;s.active_faction_count=2;s.assignable_faction_count=2;
    s.simulate_all_active_factions_as_ai=true;return s;}
SimulationLabSeedEntity scout(const FieldGrid& field){SimulationLabSeedEntity s;
    s.army_asset_id=100;s.faction_index=1;s.runtime_model_class_id=2;
    const auto& cell=field.at(20,20);
    s.actor.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
    s.motion.speed_q12_per_tick=4096;s.motion.yaw_max_velocity_turn16=0x7fff;
    s.motion.yaw_acceleration_turn16=0x7fff;s.root_model_maximum_integrity=1000;
    s.has_traversal_profile=true;s.traversal_low_band_index=0;s.traversal_high_band_index=4;
    s.fog_uncover_radius_q12=8*4096;s.fog_reference_height_offset_q12=4*4096;
    s.occupancy_mark_radius_units=4096;
    SimulationLabModelComponentSeed node;node.runtime_class_id=2;node.maximum_integrity=1000;
    node.placement_contact_kind=1;s.model_components.push_back(node);return s;}
}
int main() try {
    MilitaryScoreParametersV599 p{};
    p.site_bias=0;p.site_scale=0;
    MilitarySiteV599 high{0,0,INT32_MIN};
    check(military_site_score_v599(0,0,high,p,1)==-524288,"site bonus lost native signed SAR12");
    p.anchor_bias=0x80000;p.anchor_scale=4096;
    p.hostile_bias=0x80000;p.hostile_scale=4096;
    check(military_anchor_score_v599(0,{0,0,1},p,1)==-524288,"anchor product lost native signed SAR12");
    check(military_secondary_score_v599(0,0,{0,0,0},p,1)==-524288,"secondary product lost native signed SAR12");
    const auto weights=military_arm_weights_v602(11,22,33);
    check(weights.site==11&&weights.anchor==22&&weights.secondary==33,"ARM scorer offsets are swapped");
    // Nested child damage affects group admission even with a full root.
    SimulationLabEntity hierarchy;hierarchy.root_model_component_index=0;
    hierarchy.model_components.resize(3);
    for(auto& node:hierarchy.model_components){node.maximum_integrity=1000;node.integrity=1000;}
    hierarchy.model_components[1].parent_component_index=0;
    hierarchy.model_components[2].parent_component_index=1;
    check(simulation_lab_hierarchy_condition_ratio_q12(hierarchy)==4096,"full hierarchy ratio failed");
    hierarchy.model_components[2].integrity=0;
    check(simulation_lab_hierarchy_condition_ratio_q12(hierarchy)==3072,"nested damage was ignored");
    hierarchy.model_components[1].integrity=0;
    check(simulation_lab_hierarchy_condition_ratio_q12(hierarchy)==2048,"child damage was ignored");
    auto field=FieldGrid::create(59,59,0,0);
    std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
    {
        // Live source callback must consume the binary-verified +0x70 site weight.
        auto unit=scout(field);SimulationLabProductionAsset product;
        product.army_asset_id=unit.army_asset_id;product.entity_seed=unit;
        product.ai_definition_class_value70=1;
        ki[0x23]=250000;ki[0x27]=4096;
        std::array seeds{unit};std::array products{product};SimulationLabExperiment lab;
        lab.reset(config(),ki,seeds,{},products,{},{},{},scenario());
        lab.step(field,14);const auto& run=lab.runs()[0];
        check(run.metrics.military_ai_site_orders>=1,"live +0x70 site weight produced no command");
        check(run.entities[0].military_site_weight==1&&run.entities[0].military_anchor_weight==0,
              "live ARM scorer mapping failed");
    }
    {
        // Workspace-01 behavior is owner-list based: unfinished, dismantling
        // and terminal-effect roots stay dispatchable until owner unlink.
        auto unit=scout(field);unit.actor.position.x+=2.0F;
        SimulationLabProductionAsset product;product.army_asset_id=unit.army_asset_id;
        product.entity_seed=unit;product.ai_definition_class_value70=1;
        std::array seeds{unit};std::array products{product};SimulationLabExperiment lab;
        lab.reset(config(),ki,seeds,{},products,{},{},{},scenario());
        auto& actor=lab.runs()[0].entities[0];
        actor.construction_complete=false;actor.runtime_flags=0x10U;
        actor.root_model_terminal=true;
        lab.step(field,14);
        check(actor.military_ai_source_dispatch,
              "live terminal/dismantling WS01 owner was not dispatched");
    }
    {
        // A locked native movement command can have no visible move_target yet.
        // Its bit-0x2 lock must suppress the faction site/anchor callback (Open Thandor
        // AiUnitBehavior_UpdateOwnUnits), regardless of the UI move-target fields.
        auto unit=scout(field);SimulationLabProductionAsset product;
        product.army_asset_id=unit.army_asset_id;product.entity_seed=unit;
        product.ai_definition_class_value70=1;
        std::array seeds{unit};std::array products{product};SimulationLabExperiment lab;
        lab.reset(config(),ki,seeds,{},products,{},{},{},scenario());
        lab.step(field,9);
        auto& actor=lab.runs()[0].entities[0];
        // Retain an anchored native movement state through the owner callback.
        // A raw proxy flag alone is reconstructed by ordinary motion sync.
        actor.combat_follow_owned=true;actor.combat_movement_initialized=true;
        actor.combat_movement.primary=actor.combat_movement.fallback=actor.combat_movement.stored=
            actor.combat_movement.model={float_to_q12(actor.actor.position.x),float_to_q12(actor.actor.position.y)};
        actor.combat_movement.flags|=0x2U;
        actor.military_ai_busy_cooldown=8;
        lab.step(field,1);
        check(lab.runs()[0].metrics.military_ai_site_orders==0 &&
              lab.runs()[0].metrics.military_ai_anchor_orders==0,
              "locked movement was overridden by a faction AI route");
    }
    ki.fill(0);
    {
        // Local weapon acquisition must not redirect a persistent exploration order.
        auto unit=scout(field);auto enemy=unit;enemy.army_asset_id=200;enemy.faction_index=2;
        enemy.runtime_model_class_id=11;enemy.building=true;enemy.motion={};
        enemy.actor.position.x+=8;
        SimulationWeaponProfile weapon;weapon.runtime_class_id=2;weapon.shot_definition_id=88;
        weapon.maximum_range_q12=4096;weapon.reload_ticks=100;
        ShotDefinitionRecord shot;shot.definition_id=88;
        unit.combat_weapons.push_back({weapon,shot});unit.actor.weapons.resize(1);
        std::array seeds{unit,enemy};SimulationLabExperiment lab;
        lab.reset(config(),ki,seeds,{},{},{},{},{},scenario());auto& run=lab.runs()[0];
        auto& actor=run.entities[0];actor.military_ai_source_dispatch=true;
        actor.military_ai_command_kind=1;
        const Vec3f destination{unit.actor.position.x-5,unit.actor.position.y,0};
        actor.actor.move_target=destination;
        lab.step(field,2);
        check(!run.entities[0].target_entity_index,"weapon acquisition replaced the movement target");
        check(run.entities[0].actor.move_target&&run.entities[0].actor.move_target->x==destination.x,
              "weapon acquisition redirected the exploration destination");
        check(run.metrics.military_movement_distance_world>0,"military motion counter stayed zero");
    }
    {
        // A zero-damage hit is still a non-repair hostility notification.
        // Classification reads selected MDL +0x18 even for a stationary target.
        auto shooter=scout(field), target=shooter;
        target.army_asset_id=200;target.faction_index=2;target.runtime_model_class_id=11;
        target.building=true;target.motion={};target.actor.position.x+=3;
        target.shot_impact_class_index=2;
        ShotDefinitionRecord shot;shot.definition_id=88;
        shooter.combat_weapons.push_back({{},shot});
        SimulationLabProductionAsset product;product.army_asset_id=200;
        product.entity_seed=target;
        for(auto& score:product.ai_faction_score_seeds){score.resolved=true;score.root_definition_value18=1;}
        std::array seeds{shooter,target};std::array products{product};SimulationLabExperiment lab;
        lab.reset(config(),ki,seeds,{},products,{},{},{},scenario());auto& run=lab.runs()[0];
        SimulationProjectileState projectile;projectile.owner_placement_index=0;
        projectile.weapon_profile_index=0;projectile.live_target_placement_index=1;
        projectile.impact_kind=SimulationImpactKind::TargetModelImpactClass;
        projectile.travel_ticks=1;projectile.lifetime_ticks=20;
        run.projectiles.push_back(projectile);lab.step(field,1);
        check(run.ai_secondary_impact_anchor[2].cooldown==150&&
              run.ai_primary_impact_anchor[2].cooldown==0,
              "zero-damage non-repair hit did not use selected MDL acceleration");
        // The repair route uses SHT slot0, independently of target impact class.
        run.ai_secondary_impact_anchor[2].cooldown=0;
        run.entities[0].combat_weapons[0].shot.target_impact_damage_q12[0]=-1;
        projectile.spawn_tick=run.tick;projectile.impact_damage_applied=false;
        run.projectiles.push_back(projectile);lab.step(field,1);
        check(run.ai_secondary_impact_anchor[2].cooldown==0,"repair shot created a hostile alert");
    }
    {
        SimulationLabSeedEntity factory;factory.army_asset_id=301;factory.faction_index=1;
        factory.runtime_model_class_id=13;factory.building=true;factory.root_model_maximum_integrity=1000;
        factory.production_compatibility_mask=2;factory.motion.speed_q12_per_tick=4096;
        factory.actor.position=scout(field).actor.position;
        factory.production_spawn_local=Vec3f{2,0,0};factory.production_exit_local=Vec3f{4,0,0};
        SimulationLabProductionAsset unit;unit.army_asset_id=100;unit.army_runtime_flags14_raw=2;
        unit.build_duration_ticks=3;unit.entity_seed=scout(field);
        std::array seeds{factory};std::array products{unit};SimulationLabExperiment lab;
        lab.reset(config(),ki,seeds,{},products,{},{},{},scenario());auto& run=lab.runs()[0];
        run.secondary_purchase_requests.push_back({100,1,0,0});
        lab.step(field,1);check(run.entities[0].production_animated_subnode_ticks==0,
              "build acceptance advanced the pose before the first build service");
        lab.step(field,3);check(run.entities[0].production_animated_subnode_ticks==3,
              "active production did not accumulate its own pose clock");
        lab.step(field,10);check(run.entities[0].production_animated_subnode_ticks==3,
              "production pose kept advancing after unit completion");
        check(run.entities[0].production_door_texture_offset_v>0,
              "door opening did not advance separately from the frozen production pose");
        ModelVisualNode parent,node;parent.runtime_class_id=13;
        parent.simulation.yaw_max_velocity_turn16=256;node.child_slot=0;
        SimulationActorState actor;
        const auto a=simulation_runtime_child_pose_v602(parent,node,1,{},actor,true,100,3U);
        const auto b=simulation_runtime_child_pose_v602(parent,node,1,{},actor,true,200,3U);
        check(a.angle2==768&&b.angle2==a.angle2,"production pose used global time instead of its frozen clock");
        parent.runtime_class_id=10;
        check(simulation_runtime_child_pose_v602(parent,node,1,{},actor,true,4,{}).angle2==1024,
              "continuous radar lost its animation clock");
        SimulationWeaponProfile weapon;weapon.yaw_model_node_index=1;
        weapon.pitch_model_node_index=2;weapon.launch_model_node_index=3;
        actor.weapons.resize(1);actor.weapons[0].yaw.angle_turn16=1234;
        std::array weapons{weapon};
        check(simulation_runtime_child_pose_v602(parent,node,1,weapons,actor,true,4,{}).angle2==1234,
              "synthetic weapon aim was overwritten by authored animation");
    }
    std::cout<<"V706 signed scoring, ARM weights, hierarchy condition and persistent orders passed\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
