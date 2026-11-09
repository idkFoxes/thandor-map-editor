#include "thandor/field.hpp"
#include "thandor/field_raycast.hpp"
#include "thandor/simulation.hpp"
#include "articulated_walker_pose.hpp"

#include <array>
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
float distance(editor::Vec3f a,editor::Vec3f b){
    const float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return std::sqrt(x*x+y*y+z*z);
}
std::vector<thandor::ModelVisualNode> walker_hierarchy(){
    std::vector<thandor::ModelVisualNode> hierarchy(9);
    hierarchy[0].parent_index=std::numeric_limits<uint32_t>::max();
    for(uint32_t side=0;side<2;++side){
        const uint32_t base=1U+side*4U;
        hierarchy[base].parent_index=0U; hierarchy[base].child_slot=side;
        for(uint32_t depth=1;depth<4;++depth){
            hierarchy[base+depth].parent_index=base+depth-1U;
            hierarchy[base+depth].child_slot=0U;
        }
    }
    return hierarchy;
}
std::vector<editor::ModelHierarchyTransform> walker_transforms(){
    std::vector<editor::ModelHierarchyTransform> transforms(9);
    transforms[0]=editor::placement_root_transform(0,0,0,0);
    for(uint32_t side=0;side<2;++side){
        const uint32_t base=1U+side*4U;
        const float y=side==0?0.3F:-0.3F;
        transforms[base]=editor::placement_root_transform(0,y,0,0);
        transforms[base+1]=editor::placement_root_transform(0,y,-0.15F,0);
        transforms[base+2]=editor::placement_root_transform(0.20F,y,-0.40F,0);
        transforms[base+3]=editor::placement_root_transform(0.35F,y,-0.65F,0);
    }
    return transforms;
}
}

int main() try {
    // V451/V448 makes the common retail admission gate unambiguous. Only the diagonal c00/c11
    // endpoints are tested by this interpolation contract; rejecting c10/c01 here would be a new
    // editor invention rather than parity.
    auto field=thandor::FieldGrid::create(5U,5U,0,0U);
    require(field.runtime_interpolation_allowed(1U,1U),"clean quad rejected");
    field.at(2U,1U).flags_and_material|=0x80000000U; // c10: not part of admission gate.
    require(field.runtime_interpolation_allowed(1U,1U),"c10 incorrectly joined c00/c11 admission gate");
    field.at(2U,1U).flags_and_material&=~0x80000000U;
    field.at(1U,1U).flags_and_material|=0x00002000U;
    require(!field.runtime_interpolation_allowed(1U,1U),"c00 hard mask was not rejected");
    field.at(1U,1U).flags_and_material&=~0x00002000U;
    field.at(2U,2U).flags_and_material|=0x08000000U;
    require(!field.runtime_interpolation_allowed(1U,1U),"c11 hard mask was not rejected");

    // The same rejection must reach the public terrain query and picking surface. Use the center
    // of quad 1,1 so a vertical ray cannot migrate into a neighbouring cell.
    const auto& c00=field.at(1U,1U); const auto& c11=field.at(2U,2U);
    const float world_x=0.5F*(thandor::q12_to_float(c00.world_x_q12)+thandor::q12_to_float(c11.world_x_q12));
    const float world_y=0.5F*(thandor::q12_to_float(c00.world_y_q12)+thandor::q12_to_float(c11.world_y_q12));
    require(!field.sample_terrain(world_x,world_y).inside,"mask-rejected quad still returned a valid terrain sample");
    require(!thandor::raycast_field_surface_compatibility(field,{world_x,world_y,10.0F},{0,0,-1},20.0F,
        thandor::FieldRaycastSurface::Terrain),"raycast acquired terrain through a runtime-rejected quad");
    field.at(2U,2U).flags_and_material&=~0x08000000U;
    require(field.sample_terrain(world_x,world_y).inside,"clean quad did not recover after mask removal");
    require(thandor::raycast_field_surface_compatibility(field,{world_x,world_y,10.0F},{0,0,-1},20.0F,
        thandor::FieldRaycastSurface::Terrain).has_value(),"clean terrain could not be picked");

    // Retail class-3 contact cadence derives a per-contact step from +0xC0 and distance + 2*+0xC4.
    const int32_t base=thandor::simulation_articulated_contact_step_q12(1400,250,0x1000);
    require(base==static_cast<int32_t>((static_cast<int64_t>(1400)<<13)/(0x1000+500)),
            "class-3 contact-step equation drifted");
    require(thandor::simulation_articulated_contact_step_q12(1600,250,0x1000)>base,
            "larger +0xC0 no longer advances contact faster");
    require(thandor::simulation_articulated_contact_step_q12(1400,400,0x1000)<base,
            "larger +0xC4 no longer biases contact cadence downward");

    thandor::SimulationActorState actor;
    actor.articulated_contact_step_q12=0x2000;
    thandor::simulation_update_articulated_contact_transition(actor,{0,0,0},0,{1,0,0},0,1400);
    require(actor.articulated_contact_event_serial>=1U,"completed terrain contact did not publish an event");
    require(actor.articulated_last_completed_side<=1U,"completed contact side escaped left/right domain");

    // The V451 pose is a local-hierarchy two-segment solve. It may bend hip/knee bases, but every
    // parent-child segment must remain connected; this is the regression that previously broke
    // Orang/Gorilla/Emu when feet were dragged independently in world space.
    auto hierarchy=walker_hierarchy(); auto transforms=walker_transforms();
    const auto authored=transforms;
    thandor::SimulationActorState pose_actor;
    pose_actor.articulated_contacts_initialized=true;
    pose_actor.articulated_foot_anchor_world={transforms[4].translation,transforms[8].translation};
    pose_actor.articulated_swing_side=0U; pose_actor.articulated_pose_swing_side=0U;
    pose_actor.articulated_contact_progress_q12=0x800U;
    pose_actor.articulated_contact_transition_active=true; pose_actor.articulated_was_moving=true;
    const auto pose=editor::apply_articulated_walker_support_pose(hierarchy,transforms,1100,1400,250,&pose_actor,
        [](float x,float){return 0.25F*x;},[](uint32_t){return 0.0F;});
    require(pose.applied,"V451 connected walker solve did not apply");
    bool bent=false;
    for(uint32_t index:{2U,3U,6U,7U}){
        for(std::size_t i=0;i<9U;++i) if(std::abs(transforms[index].basis[i]-authored[index].basis[i])>1.0e-4F) bent=true;
    }
    require(bent,"walker remained rigid even though the V451 two-segment solve had a non-flat target");
    for(uint32_t side=0;side<2U;++side){
        const uint32_t base_index=1U+side*4U;
        for(uint32_t child_index=base_index+1U;child_index<=base_index+3U;++child_index){
            const uint32_t parent_index=child_index-1U;
            const float before=distance(authored[parent_index].translation,authored[child_index].translation);
            const float after=distance(transforms[parent_index].translation,transforms[child_index].translation);
            require(std::abs(before-after)<1.0e-4F,"walker local solve changed an authored segment length");
        }
    }

    // MDL +0xCC is a world spatial-sound-table index, not a DirectSound first-free registry slot
    // and not a SOUND.PCK ordinal. The completion pass resolves it through LEV resource paths.
    const std::filesystem::path root=THANDOR_SOURCE_DIR;
    const auto model_source=read_text(root/"src/core/model.cpp");
    const auto editor_source=read_text(root/"src/editor/editor_app.cpp");
    const auto model_header=read_text(root/"include/thandor/model.hpp");
    require(model_source.find("offset + 0xCCU")!=std::string::npos &&
            model_source.find("offset + 0x1ACU")!=std::string::npos &&
            model_source.find("offset + 0x274U")!=std::string::npos,
            "V451 articulated/projected sound fields are not parsed from MDL");
    require(model_header.find("articulated_contact_sound_asset_id")!=std::string::npos &&
            editor_source.find("Footstep world sound index")!=std::string::npos,
            "class-3 footstep world-sound provenance is not visible in the editor");
    require(editor_source.find("Dry / water contact effects")!=std::string::npos,
            "class-3 dry/water contact effect split is not visible in the editor");

    std::cout<<"V451_TERRAIN_WALKER_SOUND_MOVEMENT PASS\n";
    return 0;
} catch(const std::exception& error){
    std::cerr<<"V451 terrain/walker/sound/movement checks failed: "<<error.what()<<'\n';
    return 1;
}
