#pragma once

#include "model_transform.hpp"
#include "thandor/model.hpp"
#include "thandor/simulation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

namespace editor {

struct ArticulatedWalkerPoseResult {
    bool applied{};
    std::array<uint32_t, 2> foot_node_indices{
        std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max()};
    std::array<Vec3f, 2> foot_targets{};
    float root_height{};
};

namespace articulated_walker_detail {

inline Vec3f subtract(Vec3f a, Vec3f b) noexcept {
    return {a.x-b.x,a.y-b.y,a.z-b.z};
}
inline Vec3f add_vector(Vec3f a, Vec3f b) noexcept {
    return {a.x+b.x,a.y+b.y,a.z+b.z};
}
inline Vec3f scale_vector(Vec3f a,float s) noexcept {
    return {a.x*s,a.y*s,a.z*s};
}
inline float dot_vector(Vec3f a,Vec3f b) noexcept {
    return a.x*b.x+a.y*b.y+a.z*b.z;
}
inline Vec3f cross_vector(Vec3f a,Vec3f b) noexcept {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
inline float vector_length(Vec3f a) noexcept {
    return std::sqrt(dot_vector(a,a));
}
inline Vec3f normalized(Vec3f a,Vec3f fallback={1.0F,0.0F,0.0F}) noexcept {
    const float length=vector_length(a);
    return length>1.0e-8F?scale_vector(a,1.0F/length):fallback;
}

inline Vec3f rotate_around_axis(Vec3f value,Vec3f axis,float cosine,float sine) noexcept {
    const Vec3f cross=cross_vector(axis,value);
    const float projection=dot_vector(axis,value);
    return add_vector(add_vector(scale_vector(value,cosine),scale_vector(cross,sine)),
                      scale_vector(axis,projection*(1.0F-cosine)));
}

inline void rotate_basis_from_to(ModelHierarchyTransform& transform,
                                 Vec3f current_direction,Vec3f target_direction) noexcept {
    const Vec3f from=normalized(current_direction);
    const Vec3f to=normalized(target_direction,from);
    float cosine=std::clamp(dot_vector(from,to),-1.0F,1.0F);
    Vec3f axis=cross_vector(from,to);
    float sine=vector_length(axis);
    if(sine<=1.0e-7F){
        if(cosine>0.0F)return;
        axis=normalized(std::abs(from.z)<0.9F?cross_vector(from,{0.0F,0.0F,1.0F}):
                                                   cross_vector(from,{0.0F,1.0F,0.0F}));
        sine=0.0F;
        cosine=-1.0F;
    }else{
        axis=scale_vector(axis,1.0F/sine);
    }
    for(std::size_t column=0;column<3U;++column){
        const Vec3f source{transform.basis[column],transform.basis[3U+column],
                           transform.basis[6U+column]};
        const Vec3f target=rotate_around_axis(source,axis,cosine,sine);
        transform.basis[column]=target.x;
        transform.basis[3U+column]=target.y;
        transform.basis[6U+column]=target.z;
    }
}

inline uint32_t direct_child(std::span<const thandor::ModelVisualNode> hierarchy,
                             uint32_t parent,uint32_t slot) noexcept {
    for(uint32_t index=0;index<hierarchy.size();++index){
        const auto& node=hierarchy[index];
        if(node.creates_runtime_node()&&node.parent_index==parent&&node.child_slot==slot)
            return index;
    }
    return std::numeric_limits<uint32_t>::max();
}

inline std::array<uint32_t,4> leg_chain(std::span<const thandor::ModelVisualNode> hierarchy,
                                        uint32_t root,uint32_t root_slot) noexcept {
    std::array<uint32_t,4> chain{};
    chain.fill(std::numeric_limits<uint32_t>::max());
    chain[0]=direct_child(hierarchy,root,root_slot);
    for(std::size_t depth=1;depth<chain.size();++depth){
        if(chain[depth-1]==std::numeric_limits<uint32_t>::max())break;
        chain[depth]=direct_child(hierarchy,chain[depth-1],0U);
    }
    return chain;
}

inline bool valid_chain(const std::array<uint32_t,4>& chain,std::size_t transform_count) noexcept {
    return std::all_of(chain.begin(),chain.end(),[&](uint32_t index){
        return index!=std::numeric_limits<uint32_t>::max()&&index<transform_count;
    });
}

inline Vec3f solve_knee(Vec3f hip,Vec3f authored_knee,Vec3f target,
                        float upper_length,float lower_length) noexcept {
    Vec3f axis=subtract(target,hip);
    float distance=vector_length(axis);
    if(distance<=1.0e-7F)return authored_knee;
    axis=scale_vector(axis,1.0F/distance);
    const float minimum=std::abs(upper_length-lower_length)+1.0e-5F;
    const float maximum=std::max(minimum,upper_length+lower_length-1.0e-5F);
    distance=std::clamp(distance,minimum,maximum);
    const float along=(upper_length*upper_length-lower_length*lower_length+distance*distance)/
                      (2.0F*distance);
    const float perpendicular_length=std::sqrt(std::max(0.0F,
        upper_length*upper_length-along*along));
    Vec3f bend=subtract(subtract(authored_knee,hip),scale_vector(axis,
        dot_vector(subtract(authored_knee,hip),axis)));
    if(vector_length(bend)<=1.0e-7F){
        bend=std::abs(axis.z)<0.9F?cross_vector(axis,{0.0F,0.0F,1.0F}):
                                       cross_vector(axis,{0.0F,1.0F,0.0F});
    }
    bend=normalized(bend,{0.0F,1.0F,0.0F});
    return add_vector(add_vector(hip,scale_vector(axis,along)),
                      scale_vector(bend,perpendicular_length));
}

} // namespace articulated_walker_detail

// Coherent compatibility reconstruction of runtime class 3's terrain-support path.
// The binary updates local articulated-joint angles and then recursively rebuilds descendants.
// The editor preserves the complete authored hierarchy and applies its support translation as one
// rigid operation; it never edits disconnected world-space child transforms.
template<class TerrainHeightFn,class FootPlacementOffsetFn>
ArticulatedWalkerPoseResult apply_articulated_walker_support_pose(
    std::span<const thandor::ModelVisualNode> hierarchy,
    std::vector<ModelHierarchyTransform>& transforms,
    int32_t root_height_q12,int32_t contact_lift_q12,
    thandor::SimulationActorState* actor,
    TerrainHeightFn&& terrain_height,FootPlacementOffsetFn&& foot_placement_offset) {
    using namespace articulated_walker_detail;
    ArticulatedWalkerPoseResult result;
    if(hierarchy.size()!=transforms.size()||hierarchy.empty())return result;
    const auto root_it=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& node){
        return node.parent_index==std::numeric_limits<uint32_t>::max();
    });
    if(root_it==hierarchy.end())return result;
    const uint32_t root=static_cast<uint32_t>(std::distance(hierarchy.begin(),root_it));
    const auto left=leg_chain(hierarchy,root,0U);
    const auto right=leg_chain(hierarchy,root,1U);
    if(!valid_chain(left,transforms.size())||!valid_chain(right,transforms.size()))return result;

    const std::array<std::array<uint32_t,4>,2> chains{left,right};
    std::array<Vec3f,2> authored_targets{};
    for(std::size_t side=0;side<chains.size();++side){
        const uint32_t foot=chains[side][3];
        authored_targets[side]=transforms[foot].translation;
        authored_targets[side].z=terrain_height(authored_targets[side].x,authored_targets[side].y)+
            foot_placement_offset(foot);
    }

    std::array<Vec3f,2> targets=authored_targets;
    const float contact_lift=thandor::q12_to_float(std::abs(contact_lift_q12));
    if(actor!=nullptr){
        if(!actor->articulated_contacts_initialized){
            actor->articulated_foot_anchor_world=authored_targets;
            actor->articulated_contact_heading_turn16.fill(actor->body_yaw_turn16);
            actor->articulated_pose_swing_side=actor->articulated_swing_side&1U;
            actor->articulated_contacts_initialized=true;
        }
        const uint32_t swing=actor->articulated_swing_side&1U;
        if(actor->articulated_pose_swing_side!=swing){
            // A completed transition commits the previously swinging contact exactly once. The
            // other foot remains anchored while the next side begins its terrain/contact update.
            const uint32_t completed=actor->articulated_pose_swing_side&1U;
            actor->articulated_foot_anchor_world[completed]=authored_targets[completed];
            actor->articulated_contact_heading_turn16[completed]=actor->body_yaw_turn16;
            actor->articulated_pose_swing_side=swing;
        }
        targets=actor->articulated_foot_anchor_world;
        if(actor->articulated_contact_transition_active&&actor->articulated_was_moving){
            const float linear=std::clamp(
                static_cast<float>(actor->articulated_contact_progress_q12)/4096.0F,0.0F,1.0F);
            const float eased=linear*linear*(3.0F-2.0F*linear);
            const Vec3f start=actor->articulated_foot_anchor_world[swing];
            const Vec3f end=authored_targets[swing];
            targets[swing]=add_vector(start,scale_vector(subtract(end,start),eased));
            targets[swing].z=terrain_height(targets[swing].x,targets[swing].y)+
                foot_placement_offset(chains[swing][3])+
                std::sin(linear*std::numbers::pi_v<float>)*contact_lift;
        }
    }

    // Keep the authored hierarchy rigid and connected. The previous compatibility solver edited
    // independent world transforms for upper/lower/foot nodes, which bypassed parent composition
    // and detached the large Orang/Gorilla/Emu leg sprites. The retail callback writes LOCAL joint
    // angles and then rebuilds descendants; until that fixed-point joint solve is reconstructed,
    // a coherent whole-hierarchy support translation is safer and visually faithful to authored
    // attachment topology.
    const float average_current_foot_z=(transforms[left[3]].translation.z+
                                        transforms[right[3]].translation.z)*0.5F;
    const float average_target_foot_z=(targets[0].z+targets[1].z)*0.5F;
    const float contact_support_delta=average_target_foot_z-average_current_foot_z;
    const float minimum_root_z=average_target_foot_z+thandor::q12_to_float(root_height_q12);
    const float minimum_clearance_delta=minimum_root_z-transforms[root].translation.z;
    const float root_delta=std::max(contact_support_delta,minimum_clearance_delta);
    for(auto& transform:transforms)transform.translation.z+=root_delta;
    const float root_target_z=transforms[root].translation.z;

    result.applied=true;
    result.foot_node_indices={left[3],right[3]};
    result.foot_targets=targets;
    result.root_height=root_target_z;
    return result;
}



// Compatibility overload retained for the G391 focused contract. New code passes live contact
// state directly so pivot turns and planted feet are represented.
template<class TerrainHeightFn,class FootPlacementOffsetFn>
ArticulatedWalkerPoseResult apply_articulated_walker_support_pose(
    std::span<const thandor::ModelVisualNode> hierarchy,
    std::vector<ModelHierarchyTransform>& transforms,
    int32_t root_height_q12,int32_t contact_interpolation_rate_q12,int32_t contact_lift_q12,
    uint32_t gait_phase_turn16,bool moving,
    TerrainHeightFn&& terrain_height,FootPlacementOffsetFn&& foot_placement_offset) {
    (void)contact_interpolation_rate_q12;
    thandor::SimulationActorState compatibility;
    compatibility.articulated_contact_progress_q12=(gait_phase_turn16&0x7FFFU)>>3U;
    compatibility.articulated_swing_side=(gait_phase_turn16&0x8000U)!=0U?1U:0U;
    compatibility.articulated_pose_swing_side=compatibility.articulated_swing_side;
    compatibility.articulated_was_moving=moving;
    compatibility.articulated_contact_transition_active=moving;
    return apply_articulated_walker_support_pose(hierarchy,transforms,root_height_q12,
        contact_lift_q12,moving?&compatibility:nullptr,
        std::forward<TerrainHeightFn>(terrain_height),
        std::forward<FootPlacementOffsetFn>(foot_placement_offset));
}

} // namespace editor
