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
    Vec3f root_position{};
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

inline ModelHierarchyTransform relative_transform(
    const ModelHierarchyTransform& parent,const ModelHierarchyTransform& child) noexcept {
    ModelHierarchyTransform result;
    for(std::size_t row=0;row<3U;++row){
        for(std::size_t column=0;column<3U;++column){
            result.basis[row*3U+column]=
                parent.basis[0U*3U+row]*child.basis[0U*3U+column]+
                parent.basis[1U*3U+row]*child.basis[1U*3U+column]+
                parent.basis[2U*3U+row]*child.basis[2U*3U+column];
        }
    }
    const Vec3f delta=subtract(child.translation,parent.translation);
    result.translation={
        parent.basis[0U]*delta.x+parent.basis[3U]*delta.y+parent.basis[6U]*delta.z,
        parent.basis[1U]*delta.x+parent.basis[4U]*delta.y+parent.basis[7U]*delta.z,
        parent.basis[2U]*delta.x+parent.basis[5U]*delta.y+parent.basis[8U]*delta.z};
    return result;
}

inline void recompose_descendants(
    std::span<const thandor::ModelVisualNode> hierarchy,
    std::vector<ModelHierarchyTransform>& transforms,
    std::span<const ModelHierarchyTransform> authored_local,uint32_t parent) {
    for(uint32_t index=0;index<hierarchy.size();++index){
        if(!hierarchy[index].creates_runtime_node()||hierarchy[index].parent_index!=parent)continue;
        transforms[index]=compose_model_transform(transforms[parent],authored_local[index]);
        recompose_descendants(hierarchy,transforms,authored_local,index);
    }
}

} // namespace articulated_walker_detail

// V471-backed reconstruction of runtime class 3's paired terrain-contact suspension path.
// The important authority boundary is the pair of terminal contact nodes: retail freezes a contact
// endpoint, interpolates that endpoint in Q12, recenters the root from the two terminal contacts,
// then solves local leg joints and rebuilds descendants. Rendering is allowed to read this state but
// may not silently reseed or advance it outside the 20 Hz simulation update.
template<class TerrainHeightFn,class FootPlacementOffsetFn>
ArticulatedWalkerPoseResult apply_articulated_walker_support_pose(
    std::span<const thandor::ModelVisualNode> hierarchy,
    std::vector<ModelHierarchyTransform>& transforms,
    int32_t root_height_q12,int32_t contact_interpolation_numerator_q12,
    int32_t contact_lift_q12,uint32_t contact_steering_limit_turn16,
    thandor::SimulationActorState* actor,bool commit_contact_state,
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
    // Retail contact state stores raw terrain Z. The terminal SPR placement-height offset is added
    // later by ArmyArticulatedRuntime_UpdateSuspensionHierarchy, and it uses the right terminal
    // model-resource offset for both feet. Stock Emu/Orang/Gorilla use identical left/right SPRs.
    std::array<Vec3f,2> authored_contacts{};
    const auto root_local=relative_transform(transforms[root],transforms[left[0]]);
    for(std::size_t side=0;side<chains.size();++side){
        const float lateral=(side?-1.F:1.F)*root_local.translation.y;
        authored_contacts[side]=transform_point(transforms[root],{0,lateral,0});
        authored_contacts[side].z=terrain_height(authored_contacts[side].x,authored_contacts[side].y);
    }
    const float shared_foot_placement_offset=foot_placement_offset(right[3]);

    std::array<Vec3f,2> raw_contacts=authored_contacts;
    if(actor!=nullptr){
        const uint32_t swing=actor->articulated_swing_side&1U;
        if(commit_contact_state&&!actor->articulated_contacts_initialized){
            actor->articulated_foot_anchor_world=authored_contacts;
            actor->articulated_foot_target_world=authored_contacts;
            actor->articulated_contact_heading_turn16.fill(actor->body_yaw_turn16);
            actor->articulated_contact_target_heading_turn16.fill(actor->body_yaw_turn16);
            actor->articulated_pose_swing_side=swing;
            actor->articulated_pose_transition_active=actor->articulated_contact_transition_active;
            actor->articulated_contacts_initialized=true;
        }
        if(actor->articulated_contacts_initialized){
            if(commit_contact_state){
                if(actor->articulated_pose_swing_side!=swing){
                    const uint32_t completed=actor->articulated_pose_swing_side&1U;
                    actor->articulated_foot_anchor_world[completed]=
                        actor->articulated_foot_target_world[completed];
                    actor->articulated_contact_heading_turn16[completed]=
                        actor->articulated_contact_target_heading_turn16[completed];
                    actor->articulated_pose_swing_side=swing;
                    actor->articulated_pose_transition_active=false;
                }
                if(actor->articulated_contact_transition_active&&
                   !actor->articulated_pose_transition_active){
                    Vec3f frozen_target=authored_contacts[swing];
                    const uint32_t limit=std::clamp<uint32_t>(
                        contact_steering_limit_turn16==0U?0x8000U:contact_steering_limit_turn16,
                        1U,0x8000U);
                    const int32_t requested_delta=thandor::shortest_turn16_delta(
                        actor->articulated_contact_heading_turn16[swing],actor->body_yaw_turn16);
                    const int32_t clamped_delta=std::clamp(requested_delta,
                        -static_cast<int32_t>(limit),static_cast<int32_t>(limit));
                    const uint32_t contact_heading=thandor::wrap_turn16(
                        static_cast<int64_t>(actor->articulated_contact_heading_turn16[swing])+
                        clamped_delta);
                    // authored_contacts[] was composed at the current body heading. Rotate the
                    // endpoint back to the class-3 +0xC8-clamped contact heading when necessary.
                    const int32_t correction_delta=thandor::shortest_turn16_delta(
                        actor->body_yaw_turn16,contact_heading);
                    if(correction_delta!=0){
                        const float radians=thandor::turn16_to_radians(
                            static_cast<uint32_t>(correction_delta)&0xFFFFU);
                        const float cosine=std::cos(radians),sine=std::sin(radians);
                        const float rel_x=frozen_target.x-actor->position.x;
                        const float rel_y=frozen_target.y-actor->position.y;
                        frozen_target.x=actor->position.x+rel_x*cosine-rel_y*sine;
                        frozen_target.y=actor->position.y+rel_x*sine+rel_y*cosine;
                        frozen_target.z=terrain_height(frozen_target.x,frozen_target.y);
                    }
                    actor->articulated_foot_target_world[swing]=frozen_target;
                    actor->articulated_contact_target_heading_turn16[swing]=contact_heading;
                    const Vec3f delta=subtract(frozen_target,actor->articulated_foot_anchor_world[swing]);
                    const int32_t contact_distance_q12=static_cast<int32_t>(std::lround(
                        vector_length(delta)*4096.0F));
                    actor->articulated_contact_step_q12=
                        thandor::simulation_articulated_contact_step_q12(
                            contact_interpolation_numerator_q12,contact_lift_q12,
                            contact_distance_q12);
                    actor->articulated_pose_transition_active=true;
                }else if(!actor->articulated_contact_transition_active){
                    actor->articulated_pose_transition_active=false;
                }
            }

            raw_contacts=actor->articulated_foot_anchor_world;
            if(actor->articulated_contact_transition_active&&
               (actor->articulated_was_moving||actor->articulated_contact_settling)){
                const float progress=std::clamp(
                    static_cast<float>(actor->articulated_contact_progress_q12)/4096.0F,0.0F,1.0F);
                const Vec3f start_contact=actor->articulated_foot_anchor_world[swing];
                const Vec3f end_contact=actor->articulated_foot_target_world[swing];
                raw_contacts[swing].x=start_contact.x+(end_contact.x-start_contact.x)*progress;
                raw_contacts[swing].y=start_contact.y+(end_contact.y-start_contact.y)*progress;
                // Exact fixed-point shape from UpdateSuspensionHierarchy:
                // startZ + p * ((endZ-startZ) + 4*C4*(1-p)). No smoothstep or sine easing.
                const float lift=thandor::q12_to_float(contact_lift_q12);
                raw_contacts[swing].z=start_contact.z+(end_contact.z-start_contact.z)*progress+
                    4.0F*lift*progress*(1.0F-progress);
            }
        }
    }

    std::array<Vec3f,2> targets=raw_contacts;
    for(auto& target:targets)target.z+=shared_foot_placement_offset;

    // Freeze the authored local hierarchy before moving the root. Retail writes the two terminal
    // contact world positions first, computes the root as their exact average (+ MDL +0x54 on Z),
    // rebuilds descendants, and only then writes local leg-joint angles.
    std::vector<ModelHierarchyTransform> authored_local(transforms.size());
    for(uint32_t index=0;index<hierarchy.size();++index){
        const uint32_t parent=hierarchy[index].parent_index;
        authored_local[index]=parent==std::numeric_limits<uint32_t>::max()
            ?transforms[index]:relative_transform(transforms[parent],transforms[index]);
    }
    const Vec3f target_average=scale_vector(add_vector(targets[0],targets[1]),0.5F);
    if(actor&&actor->articulated_contacts_initialized){
        const auto heading=thandor::simulation_articulated_support_heading(*actor);
        transforms[root].basis=placement_root_transform(0,0,0,
            thandor::turn16_to_radians(heading)).basis;
    }
    transforms[root].translation={target_average.x,target_average.y,
        target_average.z+thandor::q12_to_float(root_height_q12)};
    recompose_descendants(hierarchy,transforms,authored_local,root);

    // ArmyArticulatedRuntime_UpdateSuspensionHierarchy writes local joint angles. A generic
    // world-space IK solve changes the knee's bend plane and misses the mirrored right leg.
    const uint32_t root_heading=thandor::radians_to_turn16(
        std::atan2(transforms[root].basis[3],transforms[root].basis[0]));
    const auto length_q12=[](Vec3f v){
        const int64_t x=std::lround(v.x*4096.F),y=std::lround(v.y*4096.F),z=std::lround(v.z*4096.F);
        return static_cast<int32_t>(thandor::retail_uint64_sqrt(x*x+y*y+z*z));
    };
    const auto angle=[](float y,float x){return thandor::fixed_atan2_angle16_compat(
        static_cast<int32_t>(std::lround(y*4096.F)),static_cast<int32_t>(std::lround(x*4096.F)));};
    const auto set_angles=[&](uint32_t node,uint32_t a0,uint32_t a1,uint32_t a2){
        auto local=fixed_model_rotation_basis(a2,a1,a0);
        local.translation=authored_local[node].translation;
        authored_local[node]=local;
        transforms[node]=compose_model_transform(transforms[hierarchy[node].parent_index],local);
        recompose_descendants(hierarchy,transforms,authored_local,node);
    };
    const int32_t thigh=length_q12(authored_local[left[2]].translation);
    const int32_t shin=length_q12(authored_local[left[3]].translation);
    // Class-3 initialization copies the root child's local Y into lateralOffset and its child's
    // local Y into contactRadius. The latter is signed in the authored stock hierarchy.
    const float contact_radius=authored_local[left[1]].translation.y;
    for(std::size_t side=0;side<chains.size();++side){
        const auto& chain=chains[side];
        const Vec3f to_foot=subtract(targets[side],transforms[chain[0]].translation);
        const int32_t planar=length_q12({to_foot.x,to_foot.y,0});
        const uint32_t relative=thandor::wrap_turn16(int64_t(angle(to_foot.y,to_foot.x))-root_heading);
        int32_t hip_yaw=thandor::shortest_turn16_delta(0,relative);
        if(relative>=0x4000U&&relative<=0xC000U)
            hip_yaw=thandor::shortest_turn16_delta(0,thandor::wrap_turn16(int64_t(relative)-0x8000));
        if(planar<0x140)hip_yaw=planar<0x40?0:(hip_yaw*(planar-0x40))>>8;
        const uint32_t local_yaw=thandor::wrap_turn16(int64_t(hip_yaw)+(side?0x8000:0));
        set_angles(chain[0],hierarchy[chain[0]].local_rotation_angle0,
            hierarchy[chain[0]].local_rotation_angle1,local_yaw);
        const float ankle_angle=thandor::turn16_to_radians(thandor::wrap_turn16(
            int64_t(root_heading)+0x4000+hip_yaw));
        const float sign=side?-1.F:1.F;
        const Vec3f ankle{targets[side].x+sign*contact_radius*std::cos(ankle_angle),
                          targets[side].y+sign*contact_radius*std::sin(ankle_angle),targets[side].z};
        const auto delta=subtract(ankle,transforms[chain[1]].translation);
        const int32_t distance=length_q12(delta);
        int32_t elevation=static_cast<int32_t>(angle(delta.z,std::hypot(delta.x,delta.y)))+0x4000;
        const auto azimuth=thandor::wrap_turn16(int64_t(angle(delta.y,delta.x))-root_heading);
        if(azimuth>=0x4000U&&azimuth<=0xC000U)elevation=-elevation;
        // Integer projection/height and native atan lookup, including the unreachable-leg fallback.
        int32_t joint0=shin<distance?0:0x8000,joint1=joint0;
        if(distance>16){
            const int64_t s0=int64_t(shin)*shin,s1=int64_t(thigh)*thigh,s2=int64_t(distance)*distance;
            const int64_t projection=(s0-s1)/distance;
            const int64_t height_squared=-projection*projection-s2+2*s1+2*s0;
            if(height_squared>=0){
                const int32_t height=static_cast<int32_t>(thandor::retail_uint64_sqrt(height_squared))>>1;
                joint0=thandor::fixed_atan2_angle16_compat(height,static_cast<int32_t>((s1+s2-s0)/distance)>>1);
                joint1=joint0+thandor::fixed_atan2_angle16_compat(height,static_cast<int32_t>((s2-s1+s0)/distance)>>1);
            }
        }
        const int32_t bend=joint0-elevation;
        const uint32_t a0=side?(bend<0?0U:0x8000U):(bend<0?0x8000U:0U);
        set_angles(chain[1],a0,thandor::wrap_turn16(bend<0?int64_t(bend)+0x4000:0x4000-int64_t(bend)),
            hierarchy[chain[1]].local_rotation_angle2);
        set_angles(chain[2],hierarchy[chain[2]].local_rotation_angle0,
            thandor::wrap_turn16(0x4000-int64_t(joint1)),hierarchy[chain[2]].local_rotation_angle2);
        set_angles(chain[3],0,0x4000,0);
    }

    // Retail resets each terminal contact node's local rotations after the triangle solve, rebuilds
    // the hierarchy, and then composes the terrain-direction orientation back into that terminal
    // foot. The old editor left the foot basis inherited from the lower leg, which made Orang and
    // Gorilla footprints visibly tilt/point with the shin. Preserve the solved position but align
    // the terminal basis to the local terrain normal and the live contact heading.
    for(std::size_t side=0;side<chains.size();++side){
        const uint32_t foot=chains[side][3];
        const Vec3f position=transforms[foot].translation;
        constexpr float probe=1.0F/64.0F;
        const float left_height=terrain_height(position.x-probe,position.y);
        const float right_height=terrain_height(position.x+probe,position.y);
        const float back_height=terrain_height(position.x,position.y-probe);
        const float front_height=terrain_height(position.x,position.y+probe);
        const Vec3f normal=normalized({left_height-right_height,back_height-front_height,
                                       2.0F*probe},{0.0F,0.0F,1.0F});

        uint32_t heading=actor!=nullptr&&actor->articulated_contacts_initialized
            ?actor->articulated_contact_heading_turn16[side]
            :thandor::radians_to_turn16(std::atan2(transforms[root].basis[3],
                                                   transforms[root].basis[0]));
        if(actor!=nullptr&&actor->articulated_contacts_initialized&&
           actor->articulated_contact_transition_active&&(actor->articulated_swing_side&1U)==side){
            const float progress=std::clamp(
                static_cast<float>(actor->articulated_contact_progress_q12)/4096.0F,0.0F,1.0F);
            const int32_t delta=thandor::shortest_turn16_delta(
                actor->articulated_contact_heading_turn16[side],
                actor->articulated_contact_target_heading_turn16[side]);
            heading=thandor::wrap_turn16(
                static_cast<int64_t>(actor->articulated_contact_heading_turn16[side])+
                static_cast<int64_t>(std::lround(static_cast<float>(delta)*progress)));
        }
        const ModelHierarchyTransform contact_orientation=terrain_contact_root_transform(
            position,thandor::turn16_to_radians(heading),normal);
        transforms[foot].basis=contact_orientation.basis;
    }

    result.applied=true;
    result.foot_node_indices={left[3],right[3]};
    result.foot_targets=targets;
    result.root_height=transforms[root].translation.z;
    result.root_position=transforms[root].translation;
    return result;
}

// Default mutating/live-state surface retained for focused simulation tests and pre-V471 callers.
template<class TerrainHeightFn,class FootPlacementOffsetFn>
ArticulatedWalkerPoseResult apply_articulated_walker_support_pose(
    std::span<const thandor::ModelVisualNode> hierarchy,
    std::vector<ModelHierarchyTransform>& transforms,
    int32_t root_height_q12,int32_t contact_interpolation_numerator_q12,
    int32_t contact_lift_q12,thandor::SimulationActorState* actor,
    TerrainHeightFn&& terrain_height,FootPlacementOffsetFn&& foot_placement_offset) {
    return apply_articulated_walker_support_pose(hierarchy,transforms,root_height_q12,
        contact_interpolation_numerator_q12,contact_lift_q12,0x8000U,actor,true,
        std::forward<TerrainHeightFn>(terrain_height),
        std::forward<FootPlacementOffsetFn>(foot_placement_offset));
}

// Source-compatible live-state overload retained for pre-V451 call sites. New editor code passes
// the recovered +0xC0 numerator explicitly; older focused tests did not have that field in this
// function contract, so use the neutral 0x1000 numerator only for that compatibility surface.
template<class TerrainHeightFn,class FootPlacementOffsetFn>
ArticulatedWalkerPoseResult apply_articulated_walker_support_pose(
    std::span<const thandor::ModelVisualNode> hierarchy,
    std::vector<ModelHierarchyTransform>& transforms,
    int32_t root_height_q12,int32_t contact_lift_q12,
    thandor::SimulationActorState* actor,
    TerrainHeightFn&& terrain_height,FootPlacementOffsetFn&& foot_placement_offset) {
    return apply_articulated_walker_support_pose(hierarchy,transforms,root_height_q12,0x1000,
        contact_lift_q12,actor,std::forward<TerrainHeightFn>(terrain_height),
        std::forward<FootPlacementOffsetFn>(foot_placement_offset));
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
    thandor::SimulationActorState compatibility;
    compatibility.articulated_contact_progress_q12=(gait_phase_turn16&0x7FFFU)>>3U;
    compatibility.articulated_swing_side=(gait_phase_turn16&0x8000U)!=0U?1U:0U;
    compatibility.articulated_pose_swing_side=compatibility.articulated_swing_side;
    compatibility.articulated_was_moving=moving;
    compatibility.articulated_contact_transition_active=moving;
    return apply_articulated_walker_support_pose(hierarchy,transforms,root_height_q12,
        contact_interpolation_rate_q12,contact_lift_q12,moving?&compatibility:nullptr,
        std::forward<TerrainHeightFn>(terrain_height),
        std::forward<FootPlacementOffsetFn>(foot_placement_offset));
}

} // namespace editor
