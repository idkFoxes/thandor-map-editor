#pragma once
#include "thandor/model_animation.hpp"
#include "thandor/simulation.hpp"
#include <optional>
#include <span>

namespace thandor {
struct SimulationRuntimeChildPoseV602 {
    std::uint32_t angle0{},angle1{},angle2{};
    std::int32_t recoil_translation_q12{};
    std::optional<std::uint32_t> vertical_animation_tick;
};
inline SimulationRuntimeChildPoseV602 simulation_runtime_child_pose_v602(
        const ModelVisualNode& parent,const ModelVisualNode& node,std::size_t node_index,
        std::span<const SimulationWeaponProfile> profiles,const SimulationActorState& actor,
        bool animate,std::uint32_t visual_tick,std::optional<std::uint32_t> production_tick) {
    SimulationRuntimeChildPoseV602 pose{node.local_rotation_angle0,node.local_rotation_angle1,
        node.local_rotation_angle2,0,{}};
    bool weapon_axis=false;
    for(std::size_t i=0;i<profiles.size()&&i<actor.weapons.size();++i){
        const auto& profile=profiles[i];const auto& state=actor.weapons[i];
        if(profile.yaw_model_node_index==node_index){pose.angle2=state.yaw.angle_turn16;weapon_axis=true;}
        if(profile.pitch_model_node_index==node_index){pose.angle1=state.pitch.angle_turn16;weapon_axis=true;}
        const auto& nodes=profile.launch_model_node_indices;
        const bool launch=nodes.empty()?node_index==profile.launch_model_node_index:
            (profile.launch_policy==SimulationLaunchPolicy::AlternateLaunchNodes
                ?node_index==nodes[state.recoil_launch_selector%nodes.size()]
                :std::find(nodes.begin(),nodes.end(),static_cast<std::uint32_t>(node_index))!=nodes.end());
        const auto first_barrel=nodes.empty()?profile.launch_model_node_index:nodes.front();
        const bool spinning_barrel=profile.runtime_class_id==8U?node_index==first_barrel:launch;
        if(spinning_barrel)pose.angle2=wrap_turn16(static_cast<std::int64_t>(pose.angle2)+state.recoil_angle_offset_turn16);
        if(launch){
            if(profile.runtime_class_id!=8U) {
                pose.recoil_translation_q12+=state.recoil_translation_offset_q12;
            }
            weapon_axis=true;
        }
    }
    const bool producer=parent.runtime_class_id==11U||parent.runtime_class_id==13U||parent.runtime_class_id==22U;
    const bool advance=producer?production_tick.has_value():
        (animate||runtime_class_directly_dispatches_animated_model_subnodes(parent.runtime_class_id));
    const auto tick=producer?production_tick.value_or(0U):visual_tick;
    if(advance&&!weapon_axis){
        pose.angle2=wrap_turn16(static_cast<std::int64_t>(pose.angle2)+
            static_cast<std::int64_t>(model_runtime_child_yaw_step_turn16(parent,node.child_slot))*tick);
    }
    if(advance&&model_runtime_child_uses_bounded_vertical_channel(parent,node.child_slot))
        pose.vertical_animation_tick=tick;
    return pose;
}
} // namespace thandor
