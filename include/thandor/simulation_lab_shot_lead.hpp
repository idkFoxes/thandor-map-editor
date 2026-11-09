#pragma once

#include "thandor/simulation_lab.hpp"
#include "thandor/retail_contact.hpp"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace thandor {

inline std::optional<SimulationLabGroundAimSnapshot> simulation_lab_ground_aim_snapshot(
    const SimulationLabEntity& entity) noexcept {
    const auto cls=entity.runtime_model_class_id;
    if(cls!=1U&&cls!=2U&&cls!=3U&&cls!=17U&&cls!=18U&&cls!=19U)return {};
    const auto route=entity.pioneer_movement_initialized
        ?entity.pioneer_movement.stored:entity.combat_movement.stored;
    return SimulationLabGroundAimSnapshot{entity.actor.position,
        Vec3f{q12_to_float(route.x),q12_to_float(route.y),entity.actor.position.z},
        !entity.last_translated&&!entity.last_turned,
        entity.ai_path_coordinate_q12,entity.ai_damage_tracked_coordinate_q12};
}

// Open Thandor: ArmyRuntime_ResolveShotAimPoint + ShotDefinition_GetLeadSpeed
// + ShotDefinition_ComputeRampUpLeadTime. Model-position selection (including
// the surviving class-21 child) happens before this function. This calculation
// changes the shot endpoint, never the target's live component selection.
//
// Native lead checks the target MDL acceleration field and stationary state.
// For ground units the Lab's current actual step and route supply the heading;
// for an active class-21 sortie its flight phase/speed/heading supply it. Never
// manufacture velocity from a stale AI target or retained model transform.
inline Vec3f simulation_lab_moving_target_aim_point(
    const SimulationLabEntity& target, const Vec3f& source,
    Vec3f current_aim, const ShotDefinitionRecord& shot,
    std::optional<float> root_aim_z = std::nullopt,
    const SimulationLabGroundAimSnapshot* previous = nullptr) noexcept {
    // A destroyed native model can keep moving and remain the command target
    // until its owner is unlinked. Use its live movement state, or the retained
    // pre-callback state of an older owner. Metadata-free callers retain their
    // terminal-target compatibility behavior.
    if ((target.root_model_terminal && !previous && !root_aim_z) || target.runtime_removed ||
        target.motion.ground_acceleration_q12 == 0U ||
        shot.launch_speed_q12 <= 0 ||
        shot.trajectory_kind() == ShotTrajectoryMode::DirectLine ||
        shot.mode3_lead_disabled != 0U)
        return current_aim;

    double dx{}, dy{};
    std::int64_t speed{};
    if (target.runtime_model_class_id == 21U) {
        // The Lab's class-21 callback does not set last_translated: aircraft
        // positions advance in SimulationLabExperiment::step_airports.
        const auto phase=target.aircraft.runtime.phase;
        const bool flying=phase==SimulationClass21LinkedChildPhase::OutboundRelease ||
            phase==SimulationClass21LinkedChildPhase::MissionPass ||
            phase==SimulationClass21LinkedChildPhase::ReturningToPlatform;
        if (!target.aircraft.initialized || target.aircraft.landed ||
            target.aircraft.offmap || !flying || target.aircraft_definition.speed_q12 <= 0)
            return current_aim;
        const auto heading=retail_direction_scaled(0U,target.actor.body_yaw_turn16,4096);
        dx=heading[0]; dy=heading[1];
        speed=target.aircraft_definition.speed_q12;
    } else {
        const auto& position=previous?previous->position:target.actor.position;
        const bool stationary=previous?previous->stationary:
            (!target.last_translated && !(root_aim_z && target.last_turned));
        if (stationary || target.motion.speed_q12_per_tick == 0)
            return current_aim;
        // The native callback uses its stored segment coordinate even when
        // the previous callback arrived there.
        auto path=previous?previous->route_point:target.actor.move_waypoint;
        if(!previous&&root_aim_z&&target.pioneer_movement_initialized&&
           (target.runtime_model_class_id==18U||target.runtime_model_class_id==19U)){
            const auto point=target.pioneer_movement.stored;
            path=Vec3f{q12_to_float(point.x),q12_to_float(point.y),position.z};
        }else if (!previous && root_aim_z && target.combat_movement_initialized &&
            (target.runtime_model_class_id == 1U || target.runtime_model_class_id == 2U ||
             target.runtime_model_class_id == 3U || target.runtime_model_class_id == 17U ||
             target.runtime_model_class_id == 19U)){
            const auto point=target.combat_movement.stored;
            path=Vec3f{q12_to_float(point.x),q12_to_float(point.y),position.z};
        }else if (!previous&&(!path || std::hypot(path->x - position.x,
                                path->y - position.y) < 1.0F/4096.F))
            path=target.actor.move_target;
        if (!path) return current_aim;
        dx=double(path->x) - position.x;
        dy=double(path->y) - position.y;
        speed=std::abs(std::int64_t(target.motion.speed_q12_per_tick));
    }
    const double direction_length=std::hypot(dx,dy);
    if (!root_aim_z && !(direction_length > 1.0/4096.0)) return current_aim;

    const auto squared=[](int64_t value){return uint64_t(value)*uint64_t(value);};
    const auto distance_q12=[&]() -> int64_t {
        if(root_aim_z){
            const auto x=int64_t(float_to_q12(current_aim.x))-float_to_q12(source.x);
            const auto y=int64_t(float_to_q12(current_aim.y))-float_to_q12(source.y);
            const auto z=int64_t(float_to_q12(current_aim.z))-float_to_q12(source.z);
            return retail_uint64_sqrt(squared(x)+squared(y)+squared(z));
        }
        const double distance=std::hypot(std::hypot(double(current_aim.x)-source.x,
                                                  double(current_aim.y)-source.y),
                                         double(current_aim.z)-source.z);
        return static_cast<int64_t>(std::min(distance*4096.0,double(INT32_MAX)));
    }();
    std::int64_t lead_q12 = distance_q12 * speed / shot.launch_speed_q12;
    if (shot.trajectory_kind() == ShotTrajectoryMode::LeadAdjusted)
        lead_q12 += (std::uint64_t(shot.trajectory_ramp_duration_ticks) * 171U >> 8) * speed;
    if (lead_q12 <= 0) return current_aim;
    std::array<int32_t,3> lead{};
    if(root_aim_z){
        const auto heading=fixed_atan2_angle16_compat(
            float_to_q12(static_cast<float>(dy)),float_to_q12(static_cast<float>(dx)));
        lead=retail_direction_scaled(0U,heading,static_cast<int32_t>(lead_q12));
    }
    // ArmyRuntime_ResolveShotAimPoint: when damage-tracked and path coordinates
    // agree, and that anchor is within the calculated horizontal lead radius,
    // aim at the anchor rather than projecting beyond a stopped destination.
    // The source compares squared Q12 distances, including exact equality.
    // No heuristic clamp is valid when the independent tracked pair is absent.
    // The native code applies this to class 21 too; its anchored Z comes from
    // the root rather than the separately animated flying child.
    const auto& path_coordinate=previous?previous->path_coordinate:target.ai_path_coordinate_q12;
    const auto& damage_coordinate=previous?previous->damage_tracked_coordinate:target.ai_damage_tracked_coordinate_q12;
    if (path_coordinate && damage_coordinate &&
        path_coordinate->x == damage_coordinate->x &&
        path_coordinate->y == damage_coordinate->y) {
        const auto& path=*path_coordinate;
        // Native comparison uses targetNode (the selected flying child for
        // class 21), not the entity's army-root model translation.
        const std::int64_t diff_x=std::int64_t(path.x)-float_to_q12(current_aim.x);
        const std::int64_t diff_y=std::int64_t(path.y)-float_to_q12(current_aim.y);
        const std::uint64_t x2=std::uint64_t(std::llabs(diff_x))*std::uint64_t(std::llabs(diff_x));
        const std::uint64_t y2=std::uint64_t(std::llabs(diff_y))*std::uint64_t(std::llabs(diff_y));
        const std::uint64_t lead_squared=root_aim_z?squared(lead[0])+squared(lead[1]):squared(lead_q12);
        if (x2<=lead_squared && y2<=lead_squared-x2) {
            current_aim.x=q12_to_float(path.x);
            current_aim.y=q12_to_float(path.y);
            if (root_aim_z) current_aim.z=*root_aim_z;
            return current_aim;
        }
    }
    if(root_aim_z){
        current_aim.x+=q12_to_float(lead[0]);
        current_aim.y+=q12_to_float(lead[1]);
    }else{
        const double lead_world = double(lead_q12) / 4096.0;
        current_aim.x += static_cast<float>(dx / direction_length * lead_world);
        current_aim.y += static_cast<float>(dy / direction_length * lead_world);
    }
    return current_aim;
}

} // namespace thandor
