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
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
std::string read_text(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);std::ostringstream s;s<<f.rdbuf();return s.str();}
uint32_t child(std::span<const thandor::ModelVisualNode> h,uint32_t parent,uint32_t slot){
    for(uint32_t i=0;i<h.size();++i)if(h[i].parent_index==parent&&h[i].child_slot==slot&&h[i].creates_runtime_node())return i;
    return std::numeric_limits<uint32_t>::max();
}
}

int main(int argc,char**argv) try {
    thandor::SimulationActorState forward;
    thandor::simulation_update_tank_track_offsets(forward,{0,0,0},0,{1,0,0},0,-0.5F,259);
    require(forward.primary_track_texture_offset_u!=0,"forward track offset remained zero");
    require(forward.primary_track_texture_offset_u==forward.secondary_track_texture_offset_u,
            "straight movement did not advance both tracks equally");
    thandor::SimulationActorState turning;
    thandor::simulation_update_tank_track_offsets(turning,{0,0,0},0,{0,0,0},0x0800,-0.5F,259);
    require((turning.primary_track_texture_offset_u<0&&turning.secondary_track_texture_offset_u>0)||
            (turning.primary_track_texture_offset_u>0&&turning.secondary_track_texture_offset_u<0),
            "turn-in-place did not counter-rotate the tracks");
    turning.primary_track_texture_offset_u=0x0FFFF0;
    thandor::simulation_update_tank_track_offsets(turning,{0,0,0},0,{1,0,0},0,-0.5F,259);
    require(turning.primary_track_texture_offset_u<=0x100000&&
            turning.primary_track_texture_offset_u>=-0x100000,"track offset wrap escaped runtime period");

    thandor::SimulationActorState gait;
    thandor::simulation_update_articulated_gait_phase(gait,{0,0,0},{0.25F,0,0},1400);
    require(gait.articulated_gait_phase_turn16!=0U&&gait.articulated_was_moving,
            "walker gait did not advance from planar travel");
    thandor::simulation_update_articulated_gait_phase(gait,{0.25F,0,0},{0.25F,0,0},1400);
    require(!gait.articulated_was_moving,"walker gait remained moving at zero distance");

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
    std::vector<editor::ModelHierarchyTransform> transforms(9);
    transforms[0]=editor::placement_root_transform(0,0,0,0);
    for(uint32_t side=0;side<2;++side){
        const uint32_t base=1U+side*4U;
        const float y=side==0?0.2F:-0.2F;
        transforms[base].translation={0,y,0};
        transforms[base+1].translation={0,y,-0.1F};
        transforms[base+2].translation={0.1F,y,-0.35F};
        transforms[base+3].translation={0.15F,y,-0.60F};
    }
    const auto pose=editor::apply_articulated_walker_support_pose(
        hierarchy,transforms,1024,1400,250,0U,false,
        [](float,float){return 0.0F;},[](uint32_t){return 0.0F;});
    require(pose.applied,"synthetic four-node walker chains were not recognized");
    require(transforms[0].translation.z>=0.25F,
            "walker root did not retain the MDL +0x54 minimum clearance");
    require(std::abs(transforms[4].translation.z)<1.0e-5F&&
            std::abs(transforms[8].translation.z)<1.0e-5F,
            "coherent walker support did not place both authored terminal feet on terrain");
    require(std::abs((transforms[3].translation.z-transforms[4].translation.z)-0.25F)<1.0e-5F&&
            std::abs((transforms[7].translation.z-transforms[8].translation.z)-0.25F)<1.0e-5F,
            "walker support changed parent-child segment lengths");

    const auto source=std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto editor_source=read_text(source/"src/editor/editor_app.cpp");
    require(editor_source.find("simulation_update_tank_track_offsets")!=std::string::npos,
            "editor does not update tank tracks");
    require(editor_source.find("apply_walker_pose_to_instances")!=std::string::npos,
            "editor does not apply articulated walker support");
    require(editor_source.find("normalize_model_texture_offset_u")!=std::string::npos,
            "dynamic tank fixed-coordinate U offset is not applied to texture coordinates");

    if(argc>=2){
        const auto catalog=thandor::AssetCatalog::scan_game_root(argv[1]);
        std::size_t tanks=0,walkers=0;
        for(const auto& preset:catalog.army_presets()){
            if(preset.browser_kind!=thandor::PlacementBrowserKind::Unit)continue;
            const auto hierarchy_view=preset.resolved_model_hierarchy.empty()
                ?std::span<const thandor::ModelVisualNode>(preset.model_hierarchy)
                :std::span<const thandor::ModelVisualNode>(preset.resolved_model_hierarchy);
            if(preset.runtime_model_class_id==2U){
                ++tanks;
                require(preset.primary_animated_subresource==20U&&
                        preset.secondary_animated_subresource==98U,
                        "stock tank track selector pair changed");
                require(preset.has_primary_texture_animation&&preset.has_secondary_texture_animation,
                        "stock tank selector flags missing");
                require(preset.track_texture_travel_scale==259,
                        "stock tank texture travel scale changed");
            }else if(preset.runtime_model_class_id==3U){
                ++walkers;
                require(preset.articulated_root_height_q12==1100||
                        preset.articulated_root_height_q12==1200,
                        "stock walker root clearance changed");
                require(preset.articulated_contact_interpolation_rate_q12==1400&&
                        preset.articulated_contact_lift_q12==250,
                        "stock walker contact interpolation/lift fields changed");
                const uint32_t left=child(hierarchy_view,0U,0U),right=child(hierarchy_view,0U,1U);
                require(left!=std::numeric_limits<uint32_t>::max()&&
                        right!=std::numeric_limits<uint32_t>::max(),
                        "stock walker lost paired root leg chains");
                uint32_t l=left,r=right;
                for(int depth=0;depth<3;++depth){l=child(hierarchy_view,l,0U);r=child(hierarchy_view,r,0U);}
                require(l!=std::numeric_limits<uint32_t>::max()&&
                        r!=std::numeric_limits<uint32_t>::max(),
                        "stock walker lost four-node terminal foot chains");
            }
        }
        require(tanks==24U,"stock tank count changed");
        require(walkers==28U,"stock walker count changed");
        std::cout<<" tanks="<<tanks<<" walkers="<<walkers;
    }
    std::cout<<" G391_TANK_TRACK_ARTICULATED_WALKER_UNIT_CLASS PASS\n";
    return 0;
} catch(const std::exception& error){
    std::cerr<<"G391_TANK_TRACK_ARTICULATED_WALKER_UNIT_CLASS FAIL: "<<error.what()<<'\n';
    return 1;
}
