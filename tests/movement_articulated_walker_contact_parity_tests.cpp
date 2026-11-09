#include "thandor/catalog.hpp"
#include "thandor/simulation.hpp"
#include "articulated_walker_pose.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}

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
    transforms[0]=editor::placement_root_transform(root_x,0.0F,0.0F,0.0F);
    for(uint32_t side=0;side<2U;++side){
        const uint32_t base=1U+side*4U;
        const float y=side==0U?0.2F:-0.2F;
        transforms[base].translation={root_x,y,0.0F};
        transforms[base+1U].translation={root_x,y,-0.10F};
        transforms[base+2U].translation={root_x+0.10F,y,-0.35F};
        transforms[base+3U].translation={root_x+0.15F,y,-0.60F};
    }
    return transforms;
}
}

int main(int argc,char**argv) try {
    auto hierarchy=walker_hierarchy();
    thandor::SimulationActorState actor;
    actor.position={0.0F,0.0F,0.0F};
    actor.body_yaw_turn16=0U;

    auto initial=walker_transforms(0.0F);
    auto seeded=editor::apply_articulated_walker_support_pose(
        hierarchy,initial,1100,1400,250,0x2000U,&actor,true,
        [](float x,float){return x;},[](uint32_t){return 0.0F;});
    require(seeded.applied&&actor.articulated_contacts_initialized,
            "class-3 contact state did not seed from terminal feet");

    actor.position.x=0.10F;
    actor.body_yaw_turn16=0x4000U;
    actor.articulated_swing_side=0U;
    actor.articulated_pose_swing_side=0U;
    actor.articulated_contact_progress_q12=0x800U;
    actor.articulated_contact_transition_active=true;
    actor.articulated_was_moving=true;
    auto first_step=walker_transforms(0.10F);
    const auto first_pose=editor::apply_articulated_walker_support_pose(
        hierarchy,first_step,1100,1400,250,0x2000U,&actor,true,
        [](float x,float){return x;},[](uint32_t){return 0.0F;});
    require(first_pose.applied,"class-3 first live step did not apply");
    const auto frozen_target=actor.articulated_foot_target_world[0];
    require(actor.articulated_contact_target_heading_turn16[0]==0x2000U,
            "MDL +0xC8 steering/contact clamp was not applied to the moving foot");

    // A render-only recomposition at a later root must not chase a new authored foot endpoint or
    // mutate the frozen contact target. This catches the old Orang/Gorilla moving-foot drift.
    actor.position.x=0.20F;
    auto render_only=walker_transforms(0.20F);
    const auto render_pose=editor::apply_articulated_walker_support_pose(
        hierarchy,render_only,1100,1400,250,0x2000U,&actor,false,
        [](float x,float){return x;},[](uint32_t){return 0.0F;});
    require(render_pose.applied,"render-only class-3 pose did not apply");
    require(std::abs(actor.articulated_foot_target_world[0].x-frozen_target.x)<1.0e-6F&&
            std::abs(actor.articulated_foot_target_world[0].y-frozen_target.y)<1.0e-6F&&
            std::abs(actor.articulated_foot_target_world[0].z-frozen_target.z)<1.0e-6F,
            "render-only walker pose mutated the frozen contact endpoint");

    // Exact V471 suspension curve: XY is linear and Z is a parabola using +0xC4, not smoothstep
    // or sine easing. The result exposes the same per-foot world target used by the runtime.
    const auto start=actor.articulated_foot_anchor_world[0];
    const float p=0.5F;
    const float lift=thandor::q12_to_float(250);
    const float expected_x=start.x+(frozen_target.x-start.x)*p;
    const float expected_y=start.y+(frozen_target.y-start.y)*p;
    const float expected_z=start.z+(frozen_target.z-start.z)*p+4.0F*lift*p*(1.0F-p);
    require(std::abs(render_pose.foot_targets[0].x-expected_x)<1.0e-5F&&
            std::abs(render_pose.foot_targets[0].y-expected_y)<1.0e-5F&&
            std::abs(render_pose.foot_targets[0].z-expected_z)<1.0e-5F,
            "class-3 foot interpolation no longer matches the V471 linear/parabolic equation");
    const auto avg=editor::Vec3f{
        0.5F*(render_pose.foot_targets[0].x+render_pose.foot_targets[1].x),
        0.5F*(render_pose.foot_targets[0].y+render_pose.foot_targets[1].y),
        0.5F*(render_pose.foot_targets[0].z+render_pose.foot_targets[1].z)};
    require(std::abs(render_pose.root_position.x-avg.x)<1.0e-5F&&
            std::abs(render_pose.root_position.y-avg.y)<1.0e-5F&&
            std::abs(render_pose.root_position.z-(avg.z+thandor::q12_to_float(1100)))<1.0e-5F,
            "class-3 root is not the exact average of terminal contacts plus MDL +0x54");

    // The terminal foot must be terrain-aligned after the joint solve. With z=x terrain the
    // recovered contact normal points roughly (-1,0,+1); inheriting the lower-leg basis would keep
    // the local Z axis vertical and is the old visibly-wrong footprint orientation.
    const uint32_t left_foot=render_pose.foot_node_indices[0];
    require(left_foot<render_only.size(),"class-3 left terminal foot index is invalid");
    require(render_only[left_foot].basis[2]<-0.6F&&render_only[left_foot].basis[8]>0.6F,
            "class-3 terminal foot basis is not aligned to the terrain contact normal");

    if(argc>=2){
        const auto catalog=thandor::AssetCatalog::scan_game_root(argv[1]);
        std::size_t emu=0U,orang=0U,gorilla=0U;
        for(const auto& preset:catalog.army_presets()){
            if(preset.browser_kind!=thandor::PlacementBrowserKind::Unit||preset.runtime_model_class_id!=3U)
                continue;
            if(preset.model_definition_id==120U){++emu;require(preset.articulated_contact_steering_limit_turn16==0x4000U,"Emu +0xC8 changed");}
            if(preset.model_definition_id==121U){++orang;require(preset.articulated_contact_steering_limit_turn16==0x2000U,"Orang +0xC8 changed");}
            if(preset.model_definition_id==122U){++gorilla;require(preset.articulated_contact_steering_limit_turn16==0x2000U,"Gorilla +0xC8 changed");}
        }
        require(emu==8U&&orang==12U&&gorilla==8U,"stock class-3 family census changed");
    }

    std::cout<<"ARTICULATED_WALKER_CONTACT_PARITY PASS\n";
    return 0;
} catch(const std::exception& error){
    std::cerr<<"ARTICULATED_WALKER_CONTACT_PARITY FAIL: "<<error.what()<<'\n';
    return 1;
}
