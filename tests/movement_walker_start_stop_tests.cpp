#include "thandor/catalog.hpp"
#include "thandor/simulation.hpp"
#include "articulated_walker_pose.hpp"
#include "model_transform.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string read_text(const std::filesystem::path& path){
    std::ifstream stream(path,std::ios::binary);std::ostringstream text;text<<stream.rdbuf();return text.str();
}
uint32_t child(std::span<const thandor::ModelVisualNode> hierarchy,uint32_t parent,uint32_t slot){
    for(uint32_t index=0;index<hierarchy.size();++index){
        if(hierarchy[index].creates_runtime_node()&&hierarchy[index].parent_index==parent&&
           hierarchy[index].child_slot==slot)return index;
    }
    return std::numeric_limits<uint32_t>::max();
}
std::vector<thandor::ModelVisualNode> walker_hierarchy(){
    std::vector<thandor::ModelVisualNode> hierarchy(9);
    hierarchy[0].parent_index=std::numeric_limits<uint32_t>::max();
    for(uint32_t side=0;side<2U;++side){
        const uint32_t base=1U+side*4U;
        hierarchy[base].parent_index=0U; hierarchy[base].child_slot=side;
        for(uint32_t depth=1U;depth<4U;++depth){
            hierarchy[base+depth].parent_index=base+depth-1U;
            hierarchy[base+depth].child_slot=0U;
        }
    }
    return hierarchy;
}
std::vector<editor::ModelHierarchyTransform> walker_transforms(float root_x){
    std::vector<editor::ModelHierarchyTransform> transforms(9);
    transforms[0]=editor::placement_root_transform(root_x,0,0,0);
    for(uint32_t side=0;side<2U;++side){
        const uint32_t base=1U+side*4U;
        const float y=side==0U?0.3F:-0.3F;
        transforms[base]=editor::placement_root_transform(root_x,y,0,0);
        transforms[base+1]=editor::placement_root_transform(root_x,y,-0.15F,0);
        transforms[base+2]=editor::placement_root_transform(root_x+0.20F,y,-0.40F,0);
        transforms[base+3]=editor::placement_root_transform(root_x+0.35F,y,-0.65F,0);
    }
    return transforms;
}
}

int main(int argc,char** argv) try {
    const auto hierarchy=walker_hierarchy();
    thandor::SimulationActorState actor;
    actor.body_yaw_turn16=0U;

    // Retail initializes both contacts during placement before class-3 movement runs. Reproduce
    // that order explicitly and require the initial phase to stay neutral.
    auto idle=walker_transforms(0.0F);
    auto seeded=editor::apply_articulated_walker_support_pose(
        hierarchy,idle,1100,1400,250,&actor,
        [](float,float){return 0.0F;},[](uint32_t){return 0.0F;});
    require(seeded.applied,"walker placement contact seeding failed");
    require(actor.articulated_contacts_initialized,"walker contacts were not initialized at rest");
    require(actor.articulated_contact_progress_q12==0U,"idle walker started in a partial gait phase");

    // First movement tick must begin from those planted contacts, not from a lazily initialized
    // post-move pose. The pose must derive the live step from MDL +0xC0=1400.
    thandor::simulation_update_articulated_contact_transition(
        actor,{0,0,0},0,{0.10F,0,0},0,1400);
    require(actor.articulated_contact_progress_q12>0U&&
            actor.articulated_contact_progress_q12<0x1000U,
            "first walker movement did not create a bounded partial step");
    auto moving=walker_transforms(0.10F);
    auto moving_pose=editor::apply_articulated_walker_support_pose(
        hierarchy,moving,1100,1400,250,&actor,
        [](float,float){return 0.0F;},[](uint32_t){return 0.0F;});
    require(moving_pose.applied,"walker start pose was not applied");
    require(actor.articulated_contact_step_q12!=0x1000,
            "walker live pose fell back to the obsolete neutral +0xC0 compatibility value");
    const uint64_t event_before_stop=actor.articulated_contact_event_serial;

    // Reaching the destination must not freeze the partial swing. Retail services active contact
    // bits before its <=0x40 waypoint-stop branch; the editor therefore settles exactly the current
    // contact to its planted endpoint and then clears the transition.
    bool observed_settle=false;
    editor::ArticulatedWalkerPoseResult stopped_pose{};
    for(int tick=0;tick<32&&actor.articulated_contact_transition_active;++tick){
        thandor::simulation_update_articulated_contact_transition(
            actor,{0.10F,0,0},0,{0.10F,0,0},0,1400);
        observed_settle=observed_settle||actor.articulated_contact_settling;
        auto stopped=walker_transforms(0.10F);
        stopped_pose=editor::apply_articulated_walker_support_pose(
            hierarchy,stopped,1100,1400,250,&actor,
            [](float,float){return 0.0F;},[](uint32_t){return 0.0F;});
    }
    require(observed_settle,"walker stop did not enter final-contact settling");
    require(!actor.articulated_contact_transition_active&&!actor.articulated_contact_settling,
            "walker stop left a contact transition active");
    require(!actor.articulated_was_moving,"walker stop still reports planar motion");
    require(actor.articulated_contact_progress_q12==0U,
            "walker stop froze a partial contact phase");
    require(actor.articulated_contact_event_serial==event_before_stop+1U,
            "walker final planted contact did not complete exactly once");
    require(stopped_pose.applied,"walker stop pose was not applied");
    require(std::abs(stopped_pose.foot_targets[0].z)<1.0e-5F&&
            std::abs(stopped_pose.foot_targets[1].z)<1.0e-5F,
            "walker stopped with a foot suspended above flat terrain");

    const auto source=std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto editor_source=read_text(source/"src/editor/editor_app.cpp");
    require(editor_source.find("!actor.articulated_contacts_initialized")!=std::string::npos&&
            editor_source.find("lazy post-move initialization")!=std::string::npos,
            "editor no longer seeds walker contacts before first movement");
    require(editor_source.find("preset.articulated_contact_interpolation_rate_q12,\n            preset.articulated_contact_lift_q12,preset.articulated_contact_steering_limit_turn16")!=std::string::npos,
            "live walker path does not pass recovered MDL +0xC0/+0xC4/+0xC8 contact parameters");

    if(argc>=2){
        const auto catalog=thandor::AssetCatalog::scan_game_root(argv[1]);
        std::size_t walkers=0,emu=0,orang=0,gorilla=0;
        for(const auto& preset:catalog.army_presets()){
            if(preset.browser_kind!=thandor::PlacementBrowserKind::Unit||preset.runtime_model_class_id!=3U)
                continue;
            ++walkers;
            if(preset.model_definition_id==120U)++emu;
            else if(preset.model_definition_id==121U)++orang;
            else if(preset.model_definition_id==122U)++gorilla;
            else throw std::runtime_error("unexpected stock class-3 root MDL");
            require(preset.articulated_contact_interpolation_rate_q12==1400,
                    "stock walker MDL +0xC0 drifted");
            require(preset.articulated_contact_lift_q12==250,
                    "stock walker MDL +0xC4 drifted");
            require(preset.articulated_contact_steering_limit_turn16==
                        (preset.model_definition_id==120U?0x4000U:0x2000U),
                    "stock walker MDL +0xC8 steering/contact clamp drifted");
            const auto hierarchy_view=preset.resolved_model_hierarchy.empty()
                ?std::span<const thandor::ModelVisualNode>(preset.model_hierarchy)
                :std::span<const thandor::ModelVisualNode>(preset.resolved_model_hierarchy);
            const auto root_it=std::find_if(hierarchy_view.begin(),hierarchy_view.end(),[](const auto& node){
                return node.parent_index==std::numeric_limits<uint32_t>::max();
            });
            require(root_it!=hierarchy_view.end(),"stock walker root missing");
            const uint32_t root=static_cast<uint32_t>(std::distance(hierarchy_view.begin(),root_it));
            uint32_t left=child(hierarchy_view,root,0U),right=child(hierarchy_view,root,1U);
            for(int depth=0;depth<3;++depth){left=child(hierarchy_view,left,0U);right=child(hierarchy_view,right,0U);}
            require(left!=std::numeric_limits<uint32_t>::max()&&right!=std::numeric_limits<uint32_t>::max(),
                    "stock walker terminal contact chain missing");
        }
        require(walkers==28U&&emu==8U&&orang==12U&&gorilla==8U,
                "stock Emu/Orang/Gorilla walker census changed");
        std::cout<<" stock_walkers="<<walkers<<" emu="<<emu<<" orang="<<orang<<" gorilla="<<gorilla;
    }

    std::cout<<" V451_WALKER_START_STOP PASS\n";
    return 0;
} catch(const std::exception& error){
    std::cerr<<"V451 walker start/stop checks failed: "<<error.what()<<'\n';
    return 1;
}
