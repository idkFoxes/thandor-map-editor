#include "thandor/catalog.hpp"
#include "thandor/model.hpp"
#include "thandor/effect.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) try {
    if(argc<2){std::cout<<"G371_STOCK_SIMULATION_CENSUS PASS unit-only\n";return 0;}
    const auto catalog=thandor::AssetCatalog::scan_game_root(argv[1]);
    std::size_t units=0U,buildings=0U,movable=0U,armed=0U,weapons=0U,multiweapon=0U,muzzles=0U;
    for(const auto& preset:catalog.army_presets()){
        if(preset.browser_kind!=thandor::PlacementBrowserKind::Unit&&
           preset.browser_kind!=thandor::PlacementBrowserKind::Building)continue;
        if(preset.browser_kind==thandor::PlacementBrowserKind::Unit)++units;else ++buildings;
        if(preset.simulation_motion_profile.movable())++movable;
        if(preset.simulation_weapon_profiles.empty())continue;
        ++armed;if(preset.simulation_weapon_profiles.size()>1U)++multiweapon;
        const auto hierarchy=preset.resolved_model_hierarchy.empty()
            ?std::span<const thandor::ModelVisualNode>(preset.model_hierarchy)
            :std::span<const thandor::ModelVisualNode>(preset.resolved_model_hierarchy);
        for(const auto& weapon:preset.simulation_weapon_profiles){
            ++weapons;muzzles+=weapon.launch_attachment_count;
            if(!weapon.valid())throw std::runtime_error("invalid recovered weapon profile");
            if(weapon.launch_model_node_index>=hierarchy.size()||
               weapon.yaw_model_node_index>=hierarchy.size()||
               weapon.pitch_model_node_index>=hierarchy.size())
                throw std::runtime_error("weapon node index exceeds resolved hierarchy");
            const auto* shot=catalog.shot_definition(weapon.shot_definition_id);
            if(!shot)throw std::runtime_error("weapon references missing SHT definition");
            if(weapon.maximum_range_q12!=thandor::shot_selection_range_q12(*shot))
                throw std::runtime_error("weapon range does not use improved SHT formula");
            std::vector<uint32_t> launch_nodes=weapon.launch_model_node_indices;
            if(launch_nodes.empty())launch_nodes.push_back(weapon.launch_model_node_index);
            uint32_t kind2=0U;
            for(const auto launch_node:launch_nodes){
                if(launch_node>=hierarchy.size())throw std::runtime_error("grouped launch node out of range");
                const auto sprite=thandor::SpriteAsset::parse(catalog.load(hierarchy[launch_node].sprite_path));
                kind2+=static_cast<uint32_t>(std::count_if(sprite.attachment_records().begin(),
                    sprite.attachment_records().end(),[](const auto& attachment){return attachment.kind()==2U;}));
            }
            if(kind2!=weapon.launch_attachment_count)
                throw std::runtime_error("grouped kind-2 muzzle count differs from profile");
            if(weapon.launch_policy==thandor::SimulationLaunchPolicy::AlternateLaunchNodes&&
               launch_nodes.size()<2U)
                throw std::runtime_error("alternate launch policy lacks sibling leaves");
            if(weapon.launch_policy==thandor::SimulationLaunchPolicy::SequentialKind2Attachments&&
               preset.runtime_model_class_id!=21U)
                throw std::runtime_error("sequential launch policy is not class 21");
        }
    }
    if(units!=130U||movable!=130U||armed!=125U||weapons!=160U||multiweapon!=35U||buildings!=43U)
        throw std::runtime_error("stock simulation census changed");
    if(muzzles!=335U)throw std::runtime_error("stock muzzle census is unexpectedly small");
    std::cout<<"G371_STOCK_SIMULATION_CENSUS units="<<units<<" buildings="<<buildings
             <<" movable="<<movable<<" armed="<<armed<<" weapons="<<weapons
             <<" multiweapon="<<multiweapon<<" muzzles="<<muzzles<<" PASS\n";
    return 0;
} catch(const std::exception& error){
    std::cerr<<"G371_STOCK_SIMULATION_CENSUS FAIL: "<<error.what()<<'\n';return 1;
}
