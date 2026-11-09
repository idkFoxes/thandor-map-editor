#include "thandor/catalog.hpp"
#include "thandor/model.hpp"
#include "thandor/model_animation.hpp"
#include "thandor/simulation.hpp"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}

bool is_ancestor(std::span<const thandor::ModelVisualNode> hierarchy,uint32_t ancestor,uint32_t node){
    if(ancestor>=hierarchy.size()||node>=hierarchy.size())return false;
    for(uint32_t current=node;;){
        if(current==ancestor)return true;
        const uint32_t parent=hierarchy[current].parent_index;
        if(parent>=hierarchy.size()||parent==current)return false;
        current=parent;
    }
}

std::size_t direct_child_index(std::span<const thandor::ModelVisualNode> hierarchy,
                               std::size_t parent,uint32_t slot){
    for(std::size_t i=0;i<hierarchy.size();++i)
        if(hierarchy[i].parent_index==parent&&hierarchy[i].child_slot==slot&&
           hierarchy[i].creates_runtime_node()&&!hierarchy[i].sprite_path.empty())return i;
    return std::numeric_limits<std::size_t>::max();
}

uint32_t kind2_count(const thandor::AssetCatalog& catalog,
                     std::span<const thandor::ModelVisualNode> hierarchy,uint32_t node_index){
    require(node_index<hierarchy.size(),"launch node out of range");
    const auto sprite=thandor::SpriteAsset::parse(catalog.load(hierarchy[node_index].sprite_path));
    return static_cast<uint32_t>(std::count_if(sprite.attachment_records().begin(),
        sprite.attachment_records().end(),[](const auto& attachment){return attachment.kind()==2U;}));
}
}

int main(int argc,char** argv) try {
    using namespace thandor;

    // Pure regression: class-17 deployment is a runtime-child-cardinality state, not a class-wide pose.
    std::vector<ModelVisualNode> synthetic(4);
    synthetic[0].runtime_class_id=17U;
    synthetic[0].parent_index=std::numeric_limits<uint32_t>::max();
    synthetic[0].sprite_path="root.spr";
    for(uint32_t i=1;i<4U;++i){
        synthetic[i].parent_index=0U;synthetic[i].child_slot=i-1U;synthetic[i].sprite_path="child.spr";
    }
    require(hierarchy_root_uses_class17_deployment_tilt(synthetic),
            "three-child class-17 root did not arm deployment");
    synthetic.pop_back();
    require(!hierarchy_root_uses_class17_deployment_tilt(synthetic),
            "two-child class-17 root incorrectly armed deployment");

    SimulationActorState active_return;
    active_return.weapons.resize(1U);
    SimulationWeaponProfile weapon;
    weapon.shot_definition_id=1U;weapon.reload_ticks=1U;weapon.launch_attachment_count=1U;
    weapon.yaw_max_velocity_turn16=0x400U;weapon.yaw_acceleration_turn16=0x80U;
    weapon.pitch_max_velocity_turn16=0x400U;weapon.pitch_acceleration_turn16=0x80U;
    weapon.pitch_min_turn16=-0x2000;weapon.pitch_max_turn16=0x2000;
    ShotDefinitionRecord shot;
    active_return.weapons[0].yaw={0x1000U,-0x100};
    active_return.weapons[0].pitch={0x0800U,-0x100};
    const auto before_yaw=active_return.weapons[0].yaw.angle_turn16;
    const auto before_pitch=active_return.weapons[0].pitch.angle_turn16;
    const auto returning=simulation_tick_weapon(active_return,active_return.weapons[0],weapon,shot,
                                                 {},nullptr,true);
    require(returning.turned&&
            (active_return.weapons[0].yaw.angle_turn16!=before_yaw||
             active_return.weapons[0].pitch.angle_turn16!=before_pitch),
            "no-target active aim velocity did not continue toward neutral");
    SimulationActorState stopped;
    stopped.weapons.resize(1U);stopped.weapons[0].yaw.angle_turn16=0x1000U;
    stopped.weapons[0].pitch.angle_turn16=0x0800U;
    const auto held=simulation_tick_weapon(stopped,stopped.weapons[0],weapon,shot,{},nullptr,true);
    require(!held.turned&&stopped.weapons[0].yaw.angle_turn16==0x1000U&&
            stopped.weapons[0].pitch.angle_turn16==0x0800U,
            "stationary no-target weapon pose was forced to neutral");

    if(argc<2){
        std::cout<<"V451_UNIT_BUILDING_ATTACHMENT_POSE PASS source-only\n";return 0;
    }

    const auto catalog=AssetCatalog::scan_game_root(argv[1]);
    std::size_t presets=0,units=0,buildings=0,hierarchy_nodes=0,hierarchy_edges=0;
    std::size_t weapon_profiles=0,muzzles=0,generic_units=0,generic_buildings=0;
    std::size_t radar_units=0,radar_buildings=0,class17=0,class17_deploy=0,class23=0;
    std::size_t animated_weapon_overlap=0;
    std::vector<uint32_t> deploy_ids;

    for(const auto& preset:catalog.army_presets()){
        const bool unit=preset.browser_kind==PlacementBrowserKind::Unit;
        const bool building=preset.browser_kind==PlacementBrowserKind::Building;
        if(!unit&&!building)continue;
        ++presets;if(unit)++units;else ++buildings;
        const auto hierarchy=preset.resolved_model_hierarchy.empty()
            ?std::span<const ModelVisualNode>(preset.model_hierarchy)
            :std::span<const ModelVisualNode>(preset.resolved_model_hierarchy);
        hierarchy_nodes+=hierarchy.size();
        std::set<std::pair<uint32_t,uint32_t>> child_slots;
        std::size_t local_edges=0,local_muzzles=0;
        for(std::size_t node_index=0;node_index<hierarchy.size();++node_index){
            const auto& node=hierarchy[node_index];
            if(!node.creates_runtime_node()||node.sprite_path.empty())continue;
            if(node.parent_index==std::numeric_limits<uint32_t>::max())continue;
            require(node.parent_index<hierarchy.size(),"active child has invalid parent ARM "+std::to_string(preset.registry_id));
            require(hierarchy[node.parent_index].creates_runtime_node()&&!hierarchy[node.parent_index].sprite_path.empty(),
                    "active child parent is not live ARM "+std::to_string(preset.registry_id));
            require(child_slots.emplace(node.parent_index,node.child_slot).second,
                    "duplicate live child slot ARM "+std::to_string(preset.registry_id));
            const auto parent_sprite=SpriteAsset::parse(catalog.load(hierarchy[node.parent_index].sprite_path));
            require(parent_sprite.child_attachment(node.child_slot)!=nullptr,
                    "MDL child slot lacks matching SPR attachment ARM "+std::to_string(preset.registry_id));
            ++local_edges;
        }
        hierarchy_edges+=local_edges;

        const bool generic=preset.has_model_subnode_animation;
        const bool radar=preset.has_continuous_radar_subnode_animation;
        if(generic){if(unit)++generic_units;else ++generic_buildings;}
        if(radar){if(unit)++radar_units;else ++radar_buildings;}
        require(!(generic&&radar),"generic and radar callback classes collapsed on one stock preset");

        for(std::size_t parent=0;parent<hierarchy.size();++parent){
            const auto& owner=hierarchy[parent];
            const bool generic_owner=runtime_class_uses_generic_model_subnode_animation(owner.runtime_class_id);
            const bool radar_owner=runtime_class_uses_continuous_radar_subnode_animation(owner.runtime_class_id);
            if(!generic_owner&&!radar_owner)continue;
            for(uint32_t slot=0;slot<3U;++slot){
                bool time_varying=false;
                if(slot<2U&&model_runtime_child_yaw_step_turn16(owner,slot)!=0)time_varying=true;
                if(model_runtime_child_uses_bounded_vertical_channel(owner,slot))time_varying=true;
                if(!time_varying)continue;
                const auto child=direct_child_index(hierarchy,parent,slot);
                if(child==std::numeric_limits<std::size_t>::max())continue;
                const bool weapon_axis=simulation_model_node_is_weapon_aim_axis(
                    preset.simulation_weapon_profiles,static_cast<uint32_t>(child));
                const bool weapon_launch=std::any_of(preset.simulation_weapon_profiles.begin(),
                    preset.simulation_weapon_profiles.end(),[&](const SimulationWeaponProfile& profile){
                        if(profile.launch_model_node_index==child)return true;
                        return std::find(profile.launch_model_node_indices.begin(),
                                         profile.launch_model_node_indices.end(),child)!=
                               profile.launch_model_node_indices.end();
                    });
                if(weapon_axis||weapon_launch)++animated_weapon_overlap;
            }
        }
        require(animated_weapon_overlap==0U,
                "time-driven model callback overlaps weapon aim/launch attachment");

        for(const auto& profile:preset.simulation_weapon_profiles){
            ++weapon_profiles;
            require(profile.valid(),"invalid stock weapon profile ARM "+std::to_string(preset.registry_id));
            require(profile.yaw_model_node_index<hierarchy.size()&&
                    profile.pitch_model_node_index<hierarchy.size()&&
                    profile.launch_model_node_index<hierarchy.size(),
                    "stock weapon hierarchy index out of range ARM "+std::to_string(preset.registry_id));
            require(is_ancestor(hierarchy,profile.yaw_model_node_index,profile.pitch_model_node_index)&&
                    is_ancestor(hierarchy,profile.pitch_model_node_index,profile.launch_model_node_index),
                    "stock weapon yaw/pitch/launch ancestry broken ARM "+std::to_string(preset.registry_id));
            std::vector<uint32_t> launches=profile.launch_model_node_indices;
            if(launches.empty())launches.push_back(profile.launch_model_node_index);
            uint32_t count=0;for(const uint32_t launch:launches)count+=kind2_count(catalog,hierarchy,launch);
            require(count==profile.launch_attachment_count,
                    "kind-2 attachment count differs from recovered profile ARM "+std::to_string(preset.registry_id));
            local_muzzles+=count;muzzles+=count;
            if(profile.launch_policy==SimulationLaunchPolicy::SequentialKind2Attachments)
                require(profile.runtime_class_id==9U||profile.runtime_class_id==21U,
                        "sequential launch policy escaped linked MDL class 9/21");
        }

        if(preset.runtime_model_class_id==17U){
            ++class17;
            const bool derived=hierarchy_root_uses_class17_deployment_tilt(hierarchy);
            require(derived==preset.runtime_uses_class17_root_child_deployment,
                    "catalog class-17 deployment flag differs from resolved hierarchy");
            if(derived){++class17_deploy;deploy_ids.push_back(preset.registry_id);}
            if(preset.registry_id>=180U&&preset.registry_id<=188U)
                require(!derived,"Dragonfly root incorrectly enters three-child deployment");
        }
        if(preset.runtime_model_class_id==23U){
            ++class23;
            require(!preset.has_model_subnode_animation&&!preset.has_continuous_radar_subnode_animation&&
                    !preset.runtime_uses_class17_root_child_deployment,
                    "class-23 stateful vertical deployment leaked into idle time animation");
        }

        std::cout<<"ATTACHMENT_PRESET\t"<<preset.registry_id<<"\t"<<std::quoted(preset.display_name)
                 <<"\tnodes="<<hierarchy.size()<<"\tedges="<<local_edges
                 <<"\tweapons="<<preset.simulation_weapon_profiles.size()<<"\tmuzzles="<<local_muzzles
                 <<"\tgeneric="<<(generic?1:0)<<"\tradar="<<(radar?1:0)
                 <<"\tclass17Deploy="<<(preset.runtime_uses_class17_root_child_deployment?1:0)
                 <<"\tclass="<<preset.runtime_model_class_id<<"\n";
    }

    require(presets==173U&&units==130U&&buildings==43U,"unit/building stock census changed");
    require(weapon_profiles==160U&&muzzles==335U,"weapon/muzzle stock census changed");
    require(generic_units==0U&&generic_buildings==6U&&radar_units==2U&&radar_buildings==2U,
            "generic/radar callback census changed");
    require(class17==26U&&class17_deploy==5U,"class-17 deployment census changed");
    require(deploy_ids==std::vector<uint32_t>({220U,221U,222U,223U,224U}),
            "class-17 deployment exact ARM set changed");
    require(animated_weapon_overlap==0U,"stock time-driven child animation overlaps weapon attachment");

    std::cout<<"ATTACHMENT_SUMMARY\tpresets="<<presets<<"\tunits="<<units
             <<"\tbuildings="<<buildings<<"\tnodes="<<hierarchy_nodes<<"\tedges="<<hierarchy_edges
             <<"\tweapons="<<weapon_profiles<<"\tmuzzles="<<muzzles
             <<"\tgenericUnits="<<generic_units<<"\tgenericBuildings="<<generic_buildings
             <<"\tradarUnits="<<radar_units<<"\tradarBuildings="<<radar_buildings
             <<"\tclass17="<<class17<<"\tclass17Deploy="<<class17_deploy
             <<"\tclass23="<<class23<<"\tanimatedWeaponOverlap="<<animated_weapon_overlap<<"\n";
    std::cout<<"V451_UNIT_BUILDING_ATTACHMENT_POSE PASS stock\n";
    return 0;
} catch(const std::exception& error){
    std::cerr<<"V451_UNIT_BUILDING_ATTACHMENT_POSE FAIL: "<<error.what()<<'\n';return 1;
}
