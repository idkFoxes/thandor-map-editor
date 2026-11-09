#include "thandor/simulation_command_markers.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/simulation_lab_targeting.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

using namespace thandor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}

SimulationLabWeaponSeed weapon_seed(bool armed=true){
    SimulationWeaponProfile weapon;
    weapon.shot_definition_id=88;
    weapon.runtime_class_id=9;
    weapon.reload_ticks=1;
    weapon.maximum_range_q12=20*4096;
    weapon.launch_attachment_selector_mask=1U;
    weapon.launch_attachment_count=1;
    weapon.yaw_max_velocity_turn16=0x7fff;
    weapon.yaw_acceleration_turn16=0x7fff;
    weapon.pitch_max_velocity_turn16=0x7fff;
    weapon.pitch_acceleration_turn16=0x7fff;
    weapon.pitch_min_turn16=-0x4000;
    weapon.pitch_max_turn16=0x4000;
    ShotDefinitionRecord shot;
    shot.definition_id=88;
    shot.trajectory_mode=2;
    shot.mode2_selection_range_q12=20*4096;
    shot.projectile_lifetime_ticks=20;
    if(armed){
        shot.target_impact_damage_q12[0]=4096; // stateOrTechnologyId gate
        shot.target_impact_damage_q12[2]=4096;
    }
    return {weapon,shot};
}

SimulationLabSeedEntity unit(const FieldCell& cell,std::uint32_t faction,
                             std::uint32_t runtime_class,bool armed){
    SimulationLabSeedEntity seed;
    seed.army_asset_id=100U+runtime_class+faction;
    seed.faction_index=faction;
    seed.runtime_model_class_id=runtime_class;
    seed.root_model_maximum_integrity=10000;
    seed.shot_impact_class_index=runtime_class==15U?2U:1U;
    seed.actor.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
    seed.fog_uncover_radius_q12=40*4096;
    seed.fog_reference_height_offset_q12=4*4096;
    if(armed)seed.combat_weapons.push_back(weapon_seed());
    return seed;
}

SimulationLabExperiment experiment(std::span<const SimulationLabSeedEntity> seeds){
    SimulationLabConfig config;
    config.run_count=1;
    config.duration_ticks=128;
    config.sweep_mode=SimulationLabSweepMode::Fixed;
    std::array<std::uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    SimulationLabScenarioRuntimeSeed scenario;
    scenario.enabled=true;
    scenario.active_faction_count=2;
    scenario.assignable_faction_count=2;
    scenario.human_faction_mask=1U<<1U;
    scenario.ai_planning_enabled=false;
    scenario.runtime_catalog=std::make_shared<SimulationLabRuntimeCatalog>();
    SimulationLabExperiment lab;
    lab.reset(config,parameters,seeds,{},{},{},{},{},scenario);
    return lab;
}

void direct_writer_contracts(){
    auto field=FieldGrid::create(48,48,0,0);
    const std::array seeds{
        unit(field.at(20,20),1,7,true),
        unit(field.at(21,20),1,7,false),
        unit(field.at(25,20),2,15,false)};
    auto lab=experiment(seeds);
    auto& run=lab.runs()[0];
    auto& armed=run.entities[0];
    auto& unarmed=run.entities[1];
    armed.combat_movement.flags=0x221U;
    armed.combat_command_target_position_world=Vec3f{1,2,3};
    require(simulation_lab_apply_selection_army_target(armed,2),
        "armed selection target writer rejected the native nonzero state");
    require(armed.target_entity_index==2&&armed.combat_assigned_target_entity_index==2&&
            armed.combat_command_mode_flags==0x15U&&armed.combat_command_generation==1024&&
            !armed.combat_command_target_position_world&&
            (armed.combat_movement.flags&0x200U)==0U,
        "target-army selection writer did not stamp the recovered command image");
    require(!simulation_lab_apply_selection_army_target(unarmed,2),
        "zero stateOrTechnologyId selection incorrectly received a target command");

    armed.combat_movement.flags|=0x220U;
    const Vec3f point{12.25F,9.5F,3.0F};
    require(simulation_lab_apply_selection_target_position(armed,point),
        "armed selection target-position writer rejected the command");
    require(!armed.target_entity_index&&armed.combat_assigned_target_entity_index==2&&
            armed.combat_command_target_position_world&&
            armed.combat_command_target_position_world->x==point.x&&
            armed.combat_command_target_position_world->y==point.y&&
            armed.combat_command_target_position_world->z==point.z&&
            armed.combat_command_mode_flags==0x16U&&armed.combat_command_generation==4096&&
            (armed.combat_movement.flags&0x220U)==0U,
        "target-position writer did not preserve assigned target / clear follow and routed bits");
}

void queued_target_army_command(){
    auto field=FieldGrid::create(64,64,0,0);
    const std::array seeds{
        unit(field.at(25,25),1,7,true),
        unit(field.at(26,25),1,7,false),
        unit(field.at(30,25),2,15,false)};
    auto lab=experiment(seeds);
    auto& run=lab.runs()[0];
    run.current_visibility_occupancy.assign(field.cells().size(),std::uint64_t{1}<<8U);
    SimulationLabPlayerCommand command;
    command.kind=SimulationLabPlayerCommandKind::TargetArmySelection;
    command.faction_index=1;
    command.entity_index=2;
    command.selected_entity_indices[0]=0;
    command.selected_entity_indices[1]=1;
    require(lab.queue_player_command(0,command),"target-army command was rejected structurally");
    lab.step(field,1);
    require(run.player_command_results.size()==1U&&run.player_command_results[0].accepted,
        "target-army command was not consumed");
    const auto& armed=run.entities[0];
    const auto& unarmed=run.entities[1];
    require(armed.target_entity_index==2&&armed.combat_assigned_target_entity_index==2&&
            armed.combat_command_mode_flags==0x15U&&armed.combat_command_generation==1023,
        "queued target-army command did not survive the owner maintenance tick");
    require(!unarmed.target_entity_index&&unarmed.combat_command_mode_flags==0U,
        "queued target-army command ignored the zero-damage selection gate");
    const auto markers=simulation_lab_command_markers(run);
    require(std::any_of(markers.begin(),markers.end(),[](const auto& marker){
        return marker.owner_entity_index==0U&&marker.effect_id==149U;
    }),"entity-target command marker was not published");
}

void queued_target_position_command_fires_without_entity(){
    auto field=FieldGrid::create(64,64,0,0);
    const std::array seeds{unit(field.at(25,25),1,7,true)};
    auto lab=experiment(seeds);
    auto& run=lab.runs()[0];
    const auto& target_cell=field.at(33,25);
    SimulationLabPlayerCommand command;
    command.kind=SimulationLabPlayerCommandKind::TargetPositionSelection;
    command.faction_index=1;
    command.selected_entity_indices[0]=0;
    command.world_x_q12=target_cell.world_x_q12;
    command.world_y_q12=target_cell.world_y_q12;
    command.world_z_q12=2*4096;
    require(lab.queue_player_command(0,command),"target-position command was rejected structurally");
    lab.step(field,1);
    const auto& entity=run.entities[0];
    require(run.player_command_results.size()==1U&&run.player_command_results[0].accepted,
        "target-position command was not consumed");
    require(!entity.target_entity_index&&entity.combat_command_target_position_world&&
            entity.combat_command_mode_flags==0x16U&&entity.combat_command_generation==4095,
        "target-position command image was not retained by owner maintenance");
    require(run.metrics.weapon_shots_fired==1U&&!run.projectiles.empty()&&
            !run.projectiles.back().live_target_placement_index,
        "explicit command point did not launch an ownerless-target projectile");
    const auto markers=simulation_lab_command_markers(run);
    require(markers.size()==1U&&markers[0].effect_id==149U&&
            float_to_q12(markers[0].point.x)==command.world_x_q12&&
            float_to_q12(markers[0].point.y)==command.world_y_q12&&
            float_to_q12(markers[0].point.z)==command.world_z_q12,
        "explicit target-position marker did not use the stored world point");
}

void static_weapon_owner_receives_native_ai_command(){
    auto field=FieldGrid::create(64,64,0,0);
    const std::array seeds{
        unit(field.at(25,25),1,7,true),
        unit(field.at(29,25),2,15,false)};
    auto lab=experiment(seeds);
    auto& run=lab.runs()[0];
    run.current_visibility_occupancy.assign(field.cells().size(),std::uint64_t{1}<<8U);
    std::array<SimulationLabCombatMetrics,2> metrics{
        simulation_lab_combat_metrics(run.entities[0]),
        simulation_lab_combat_metrics(run.entities[1])};
    const auto choice=simulation_lab_select_combat_target(run,field,0,metrics,true,
        std::array<std::uint8_t,2>{0,0});
    simulation_lab_service_combat_command(run,field,0,metrics,1U,
        std::array<std::uint8_t,2>{0,0});
    const auto& source=run.entities[0];
    require(source.runtime_model_class_id==7U&&source.target_entity_index==1U&&
            source.combat_command_mode_flags==9U&&source.combat_command_generation==7,
        "primary owner maintenance still excluded an armed immobile runtime class");
}
}

int main()try{
    direct_writer_contracts();
    queued_target_army_command();
    queued_target_position_command_fires_without_entity();
    static_weapon_owner_receives_native_ai_command();
    std::cout<<"V753_SELECTION_TARGET_COMMANDS PASS army=1 position=1 marker=1 projectile=1 all_owner_ai=1\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
