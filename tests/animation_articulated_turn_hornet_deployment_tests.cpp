#include "thandor/catalog.hpp"
#include "thandor/simulation.hpp"
#include "articulated_walker_pose.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string read_text(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);std::ostringstream out;out<<file.rdbuf();return out.str();}
std::vector<thandor::ModelVisualNode> walker_hierarchy(){
    std::vector<thandor::ModelVisualNode> hierarchy(9);
    hierarchy[0].parent_index=std::numeric_limits<uint32_t>::max();
    for(uint32_t side=0;side<2;++side){
        const uint32_t base=1U+side*4U;
        hierarchy[base].parent_index=0U;hierarchy[base].child_slot=side;
        for(uint32_t depth=1;depth<4;++depth){
            hierarchy[base+depth].parent_index=base+depth-1U;
            hierarchy[base+depth].child_slot=0U;
        }
    }
    return hierarchy;
}
std::vector<editor::ModelHierarchyTransform> walker_transforms(float root_x=0.0F){
    std::vector<editor::ModelHierarchyTransform> transforms(9);
    transforms[0]=editor::placement_root_transform(root_x,0,0,0);
    for(uint32_t side=0;side<2;++side){
        const uint32_t base=1U+side*4U;
        const float y=side==0?0.2F:-0.2F;
        transforms[base].translation={root_x,y,0};
        transforms[base+1].translation={root_x,y,-0.1F};
        transforms[base+2].translation={root_x+0.1F,y,-0.35F};
        transforms[base+3].translation={root_x+0.15F,y,-0.60F};
    }
    return transforms;
}
float planar_distance(thandor::Vec3f a,thandor::Vec3f b){return std::hypot(a.x-b.x,a.y-b.y);}
}

int main(int argc,char**argv) try {
    thandor::SimulationMotionProfile walker_motion;
    walker_motion.speed_q12_per_tick=132;
    walker_motion.yaw_max_velocity_turn16=0;
    walker_motion.yaw_acceleration_turn16=0;
    thandor::SimulationActorState walker;
    walker.position={0,0,0};walker.body_yaw_turn16=0;
    walker.move_target=thandor::Vec3f{-8.0F,0,0};
    uint32_t turn_ticks=0;
    for(;turn_ticks<128U;++turn_ticks){
        const auto previous_position=walker.position;
        const auto previous_yaw=walker.body_yaw_turn16;
        thandor::simulation_tick_articulated_actor_motion(walker,walker_motion,1400);
        thandor::simulation_update_articulated_contact_transition(
            walker,previous_position,previous_yaw,walker.position,walker.body_yaw_turn16,1400);
        if(std::abs(thandor::shortest_turn16_delta(walker.body_yaw_turn16,0x8000U))<=0x800)break;
    }
    require(turn_ticks<128U,"class-3 180-degree turn did not converge");
    require(walker.body_turn_velocity_turn16!=1&&walker.body_turn_velocity_turn16!=-1,
            "class-3 turn fell back to the zero generic yaw fields");
    require(walker.articulated_was_moving&&walker.articulated_contact_transition_active,
            "class-3 pivot turn did not advance alternating contacts");

    thandor::SimulationActorState pivot;
    thandor::simulation_update_articulated_contact_transition(
        pivot,{0,0,0},0,{0,0,0},0x400,1400);
    require(pivot.articulated_contact_progress_q12!=0U&&pivot.articulated_turning,
            "yaw-only pivot did not advance the contact interpolation domain");

    auto hierarchy=walker_hierarchy();
    auto transforms=walker_transforms();
    thandor::SimulationActorState contact;
    contact.body_yaw_turn16=0;
    auto first=editor::apply_articulated_walker_support_pose(
        hierarchy,transforms,1024,250,&contact,
        [](float,float){return 0.0F;},[](uint32_t){return 0.0F;});
    require(first.applied&&contact.articulated_contacts_initialized,
            "walker contact anchors were not initialized");
    const auto planted_before=contact.articulated_foot_anchor_world[1];
    transforms=walker_transforms(0.25F);
    contact.articulated_contact_progress_q12=0x800U;
    contact.articulated_contact_transition_active=true;
    contact.articulated_was_moving=true;
    contact.articulated_swing_side=0;
    auto moving=editor::apply_articulated_walker_support_pose(
        hierarchy,transforms,1024,250,&contact,
        [](float,float){return 0.0F;},[](uint32_t){return 0.0F;});
    require(moving.applied,"walker anchored transition pose was not applied");
    require(planar_distance(moving.foot_targets[1],planted_before)<1.0e-5F,
            "planted walker foot drifted while the opposite foot swung");
    require(moving.foot_targets[0].z>0.0F,
            "swinging walker foot did not receive the contact lift");

    thandor::SimulationActorState fresh_hornet;
    fresh_hornet.body_yaw_turn16=0x1234U;
    thandor::simulation_initialize_class17_deployment(fresh_hornet,true);
    require(fresh_hornet.class17_rotor_deploy_ticks_remaining==24U&&
            fresh_hornet.class17_rotor_tilt_turn16==0U,
            "fresh Hornet did not begin folded");
    for(uint32_t tick=0;tick<24U;++tick)
        thandor::simulation_tick_class17_deployment(fresh_hornet);
    require(fresh_hornet.class17_rotor_deploy_ticks_remaining==0U&&
            fresh_hornet.class17_rotor_tilt_turn16==0x3FF0U,
            "Hornet did not complete the exact 24-step rotor deployment");
    thandor::SimulationActorState loaded_hornet;
    thandor::simulation_initialize_class17_deployment(loaded_hornet,false);
    require(loaded_hornet.class17_rotor_tilt_turn16==0x3FF0U&&
            loaded_hornet.class17_rotor_deploy_ticks_remaining==0U,
            "loaded Hornet did not begin deployed");
    thandor::SimulationActorState dragonfly;
    thandor::simulation_initialize_class17_deployment(dragonfly,true,false);
    require(dragonfly.class17_rotor_tilt_turn16==0U&&
            dragonfly.class17_rotor_deploy_ticks_remaining==0U,
            "class-17 root with <=2 children incorrectly armed deployment tilt");

    const auto source=std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto editor_source=read_text(source/"src/editor/editor_app.cpp");
    require(editor_source.find("simulation_tick_articulated_actor_motion")!=std::string::npos,
            "editor does not dispatch class-3 motion separately");
    require(editor_source.find("SimulationStateOrigin::FreshPlacement")!=std::string::npos,
            "new placement does not select the folded Hornet lifecycle");
    require(editor_source.find("update_idle_class17_deployment")!=std::string::npos,
            "Hornet deployment remains command-gated");
    require(editor_source.find("+0x3FF0")!=std::string::npos,
            "loaded/static Hornet deployed pose is missing");

    if(argc>=2){
        const auto catalog=thandor::AssetCatalog::scan_game_root(argv[1]);
        std::size_t emu=0,orang=0,gorilla=0,hornet=0,class17=0,class17_deploy=0;
        std::vector<uint32_t> class17_deploy_ids;
        for(const auto& preset:catalog.army_presets()){
            if(preset.runtime_model_class_id==3U){
                require(preset.simulation_motion_profile.yaw_max_velocity_turn16==0&&
                        preset.simulation_motion_profile.yaw_acceleration_turn16==0,
                        "stock class-3 generic yaw fields unexpectedly became nonzero");
                if(preset.display_name.find("Emu")!=std::string::npos)++emu;
                if(preset.display_name.find("Orang")!=std::string::npos)++orang;
                if(preset.display_name.find("Gorilla")!=std::string::npos)++gorilla;
            }
            if(preset.runtime_model_class_id==17U){
                ++class17;
                if(preset.runtime_uses_class17_root_child_deployment){
                    ++class17_deploy;class17_deploy_ids.push_back(preset.registry_id);
                }
                if(preset.display_name.find("Hornet")!=std::string::npos)++hornet;
            }
        }
        require(emu==8U&&orang==12U&&gorilla==8U,
                "stock Emu/Orang/Gorilla census changed");
        require(hornet==14U,"stock Hornet census changed");
        require(class17==26U&&class17_deploy==5U,
                "stock class-17 deployment cardinality census changed");
        require(class17_deploy_ids==std::vector<uint32_t>({220U,221U,222U,223U,224U}),
                "class-17 deployment escaped exact ARM 220..224 set");
        for(uint32_t id=180U;id<=188U;++id){
            const auto* preset=catalog.army_preset(id);
            require(preset&&preset->runtime_model_class_id==17U&&
                    !preset->runtime_uses_class17_root_child_deployment,
                    "Dragonfly incorrectly enters three-child class-17 deployment");
        }
        std::cout<<" emu="<<emu<<" orang="<<orang<<" gorilla="<<gorilla<<" hornet="<<hornet
                 <<" class17="<<class17<<" class17Deploy="<<class17_deploy;
    }
    std::cout<<" G392_ARTICULATED_TURN_HORNET_DEPLOYMENT PASS\n";
    return 0;
} catch(const std::exception& error){
    std::cerr<<"G392_ARTICULATED_TURN_HORNET_DEPLOYMENT FAIL: "<<error.what()<<'\n';
    return 1;
}
