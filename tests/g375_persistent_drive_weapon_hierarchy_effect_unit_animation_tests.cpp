#include "thandor/catalog.hpp"
#include "thandor/effect.hpp"
#include "thandor/model.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
std::string read_text(const std::filesystem::path& path){
    std::ifstream stream(path,std::ios::binary);
    if(!stream)throw std::runtime_error("cannot read "+path.string());
    std::ostringstream text;text<<stream.rdbuf();return text.str();
}
void require_token(const std::string& text,std::string_view token){
    if(text.find(token)==std::string::npos)
        throw std::runtime_error("missing source contract: "+std::string(token));
}
bool is_ancestor(std::span<const thandor::ModelVisualNode> hierarchy,
                 uint32_t ancestor,uint32_t node){
    if(ancestor>=hierarchy.size()||node>=hierarchy.size())return false;
    for(uint32_t current=node;;){
        if(current==ancestor)return true;
        const uint32_t parent=hierarchy[current].parent_index;
        if(parent>=hierarchy.size()||parent==current)return false;
        current=parent;
    }
}
uint32_t kind2_count(const thandor::AssetCatalog& catalog,
                     std::span<const thandor::ModelVisualNode> hierarchy,
                     uint32_t node_index){
    if(node_index>=hierarchy.size())throw std::runtime_error("launch node out of range");
    const auto sprite=thandor::SpriteAsset::parse(catalog.load(hierarchy[node_index].sprite_path));
    return static_cast<uint32_t>(std::count_if(sprite.attachment_records().begin(),
        sprite.attachment_records().end(),[](const auto& attachment){return attachment.kind()==2U;}));
}
void validate_effect_recursive(const thandor::AssetCatalog& catalog,uint32_t effect_id,
                               std::unordered_set<uint32_t>& visited_effects,
                               std::unordered_set<uint32_t>& visited_shots){
    if(effect_id==0U||!visited_effects.insert(effect_id).second)return;
    const auto* effect=catalog.effect_definition(effect_id);
    if(!effect)throw std::runtime_error("missing referenced EFF definition "+std::to_string(effect_id));
    if(!effect->sprite_path.empty())
        (void)thandor::SpriteAsset::parse(catalog.load(effect->sprite_path));
    if(effect->linked_effect_present&&effect->linked_effect_id)
        validate_effect_recursive(catalog,effect->linked_effect_id,visited_effects,visited_shots);
    if(effect->linked_shot_present&&effect->linked_shot_id&&
       visited_shots.insert(effect->linked_shot_id).second){
        const auto* shot=catalog.shot_definition(effect->linked_shot_id);
        if(!shot)throw std::runtime_error("missing EFF-linked SHT definition");
        if(!shot->sprite_path.empty())
            (void)thandor::SpriteAsset::parse(catalog.load(shot->sprite_path));
    }
}
void require_profile(const thandor::AssetCatalog& catalog,uint32_t registry_id,
                     uint32_t yaw,uint32_t pitch,uint32_t launch,
                     thandor::SimulationLaunchPolicy policy,uint32_t muzzle_count){
    const auto* preset=catalog.army_preset(registry_id);
    if(!preset||preset->simulation_weapon_profiles.size()!=1U)
        throw std::runtime_error("unexpected weapon profile shape for registry "+std::to_string(registry_id));
    const auto& weapon=preset->simulation_weapon_profiles.front();
    if(weapon.yaw_model_node_index!=yaw||weapon.pitch_model_node_index!=pitch||
       weapon.launch_model_node_index!=launch||weapon.launch_policy!=policy||
       weapon.launch_attachment_count!=muzzle_count)
        throw std::runtime_error("weapon hierarchy/policy mismatch for registry "+std::to_string(registry_id));
}
}

int main(int argc,char** argv) try {
    const auto source=std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto editor=read_text(source/"src/editor/editor_app.cpp");
    const auto catalog_source=read_text(source/"src/core/catalog.cpp");
    const auto render_policy=read_text(source/"include/thandor/simulation_render_policy.hpp");
    const auto vertex_shader=read_text(source/"shaders/model.vert");
    const auto fragment_shader=read_text(source/"shaders/model.frag");

    require_token(editor,"synchronize_simulated_actor_placement(index,actor)");
    require_token(editor,"placement.set_render_position_q12(x_q12,y_q12)");
    require_token(editor,"document_->mark_dirty()");
    require_token(editor,"case thandor::ContextOrderKind::MoveToTerrain:{");
    require_token(editor,"checkpoint();\n            auto& actor=simulation_state_for(selected_index)");
    require_token(editor,"weapon.yaw_model_node_index");
    require_token(editor,"weapon.pitch_model_node_index");
    require_token(editor,"weapon_state.recoil_launch_selector");
    require_token(editor,"transform_point(transforms[launch_node],attachment.local_translation)");
    require_token(editor,"shot->launch_effect_id");
    require_token(editor,"shot->secondary_effect_id");
    require_token(editor,"shot->primary_effect_id");
    require_token(editor,"shot->terrain_impact_effect_id");
    require_token(editor,"current_presentation_tick_");
    require_token(editor,"animated_unit_placements_");
    require_token(catalog_source,"SimulationLaunchPolicy::AlternateLaunchNodes");
    require_token(catalog_source,"SimulationLaunchPolicy::SequentialKind2Attachments");
    require_token(catalog_source,"runtime_uses_generic_model_subnode_animation");
    require_token(catalog_source,"preset.runtime_model_class_id == 17U");
    require_token(render_policy,"kObjectVertexAuthoredAnimatedUnitBit = 1U << 4U");
    require_token(vertex_shader,"& 31u");
    require_token(fragment_shader,"& 12u");

    if(argc<2){
        std::cout<<"G375_PERSISTENT_DRIVE_WEAPON_HIERARCHY_EFFECT_UNIT_ANIMATION PASS source-only\n";
        return 0;
    }

    const auto catalog=thandor::AssetCatalog::scan_game_root(argv[1]);
    std::size_t units=0,buildings=0,movable=0,armed=0,weapons=0,multiweapon=0,muzzles=0;
    std::size_t animated_units=0,animated_buildings=0,armed_animated_units=0,armed_animated_buildings=0;
    std::size_t alternate_channels=0,sequential_channels=0;
    std::unordered_set<uint32_t> visited_effects,visited_shots;
    for(const auto& preset:catalog.army_presets()){
        const bool unit=preset.browser_kind==thandor::PlacementBrowserKind::Unit;
        const bool building=preset.browser_kind==thandor::PlacementBrowserKind::Building;
        if(!unit&&!building)continue;
        if(unit)++units; else ++buildings;
        if(preset.simulation_motion_profile.movable())++movable;
        if(preset.has_model_subnode_animation){if(unit)++animated_units;else ++animated_buildings;}
        if(preset.simulation_weapon_profiles.empty())continue;
        ++armed;
        if(preset.has_model_subnode_animation){if(unit)++armed_animated_units;else ++armed_animated_buildings;}
        if(preset.simulation_weapon_profiles.size()>1U)++multiweapon;
        const auto hierarchy=preset.resolved_model_hierarchy.empty()
            ?std::span<const thandor::ModelVisualNode>(preset.model_hierarchy)
            :std::span<const thandor::ModelVisualNode>(preset.resolved_model_hierarchy);
        for(const auto& weapon:preset.simulation_weapon_profiles){
            ++weapons;
            if(!weapon.valid())throw std::runtime_error("invalid recovered weapon profile");
            if(weapon.yaw_model_node_index>=hierarchy.size()||
               weapon.pitch_model_node_index>=hierarchy.size()||
               weapon.launch_model_node_index>=hierarchy.size())
                throw std::runtime_error("weapon hierarchy index out of range");
            if(!is_ancestor(hierarchy,weapon.yaw_model_node_index,weapon.pitch_model_node_index)||
               !is_ancestor(hierarchy,weapon.pitch_model_node_index,weapon.launch_model_node_index))
                throw std::runtime_error("yaw -> pitch -> launch ancestry broken");
            if(hierarchy[weapon.yaw_model_node_index].model_definition_id!=weapon.model_definition_id||
               hierarchy[weapon.pitch_model_node_index].model_definition_id!=weapon.model_definition_id)
                throw std::runtime_error("weapon joint definition mismatch");
            std::vector<uint32_t> launch_nodes=weapon.launch_model_node_indices;
            if(launch_nodes.empty())launch_nodes.push_back(weapon.launch_model_node_index);
            uint32_t profile_muzzles=0;
            for(const uint32_t launch_node:launch_nodes){
                if(!is_ancestor(hierarchy,weapon.pitch_model_node_index,launch_node)||
                   hierarchy[launch_node].model_definition_id!=weapon.model_definition_id)
                    throw std::runtime_error("launch leaf is outside the recovered weapon assembly");
                profile_muzzles+=kind2_count(catalog,hierarchy,launch_node);
            }
            if(profile_muzzles!=weapon.launch_attachment_count)
                throw std::runtime_error("grouped kind-2 muzzle count differs from profile");
            muzzles+=profile_muzzles;
            if(weapon.launch_policy==thandor::SimulationLaunchPolicy::AlternateLaunchNodes){
                ++alternate_channels;
                if(launch_nodes.size()<2U)throw std::runtime_error("alternate policy without sibling launch leaves");
            }
            if(weapon.launch_policy==thandor::SimulationLaunchPolicy::SequentialKind2Attachments){
                ++sequential_channels;
                if(weapon.runtime_class_id!=9U&&preset.runtime_model_class_id!=21U)
                    throw std::runtime_error("sequential policy outside linked MDL class 9 or root class 21");
            }
            const auto* shot=catalog.shot_definition(weapon.shot_definition_id);
            if(!shot)throw std::runtime_error("missing referenced SHT definition");
            visited_shots.insert(shot->definition_id);
            if(!shot->sprite_path.empty())
                (void)thandor::SpriteAsset::parse(catalog.load(shot->sprite_path));
            validate_effect_recursive(catalog,shot->launch_effect_id,visited_effects,visited_shots);
            validate_effect_recursive(catalog,shot->secondary_effect_id,visited_effects,visited_shots);
            validate_effect_recursive(catalog,shot->primary_effect_id,visited_effects,visited_shots);
            for(const auto effect_id:shot->terrain_impact_effect_ids)
                validate_effect_recursive(catalog,effect_id,visited_effects,visited_shots);
        }
    }

    for(const uint32_t hornet_or_hover_id:{200U,201U,202U,203U,204U,205U,206U,207U,208U,
                                            220U,221U,222U,223U,224U}){
        const auto* preset=catalog.army_preset(hornet_or_hover_id);
        if(!preset||preset->runtime_model_class_id!=17U||
           !preset->runtime_uses_class17_movement_banking||
           preset->runtime_uses_generic_model_subnode_animation||
           preset->has_model_subnode_animation)
            throw std::runtime_error("class-17 Hornet/hover animation dispatch regression");
    }

    if(units!=130U||buildings!=43U||movable!=130U||armed!=125U||
       weapons!=160U||multiweapon!=35U||muzzles!=335U||
       animated_units!=0U||animated_buildings!=12U||
       armed_animated_units!=0U||armed_animated_buildings!=7U){
        std::ostringstream details;
        details<<"G375 stock census changed units="<<units<<" buildings="<<buildings
               <<" movable="<<movable<<" armed="<<armed<<" weapons="<<weapons
               <<" multiweapon="<<multiweapon<<" muzzles="<<muzzles
               <<" animatedUnits="<<animated_units<<" animatedBuildings="<<animated_buildings
               <<" armedAnimatedUnits="<<armed_animated_units
               <<" armedAnimatedBuildings="<<armed_animated_buildings;
        throw std::runtime_error(details.str());
    }
    if(alternate_channels!=8U||sequential_channels!=46U)
        throw std::runtime_error("launch policy census changed");

    require_profile(catalog,350U,1U,2U,3U,
                    thandor::SimulationLaunchPolicy::AllKind2Attachments,1U);
    require_profile(catalog,363U,1U,2U,3U,
                    thandor::SimulationLaunchPolicy::SequentialKind2Attachments,6U);
    require_profile(catalog,10U,1U,2U,3U,
                    thandor::SimulationLaunchPolicy::AlternateLaunchNodes,2U);
    require_profile(catalog,242U,0U,1U,1U,
                    thandor::SimulationLaunchPolicy::SequentialKind2Attachments,14U);
    require_profile(catalog,244U,0U,1U,1U,
                    thandor::SimulationLaunchPolicy::SequentialKind2Attachments,21U);

    std::cout<<"G375_PERSISTENT_DRIVE_WEAPON_HIERARCHY_EFFECT_UNIT_ANIMATION"
             <<" units="<<units<<" buildings="<<buildings<<" weapons="<<weapons
             <<" muzzles="<<muzzles<<" animatedUnits="<<animated_units
             <<" animatedBuildings="<<animated_buildings
             <<" armedAnimatedUnits="<<armed_animated_units
             <<" armedAnimatedBuildings="<<armed_animated_buildings
             <<" effects="<<visited_effects.size()
             <<" shots="<<visited_shots.size()<<" alternate="<<alternate_channels
             <<" sequential="<<sequential_channels<<" PASS\n";
    return 0;
} catch(const std::exception& error){
    std::cerr<<"G375_PERSISTENT_DRIVE_WEAPON_HIERARCHY_EFFECT_UNIT_ANIMATION FAIL: "
             <<error.what()<<'\n';
    return 1;
}
