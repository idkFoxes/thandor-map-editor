#include "thandor/simulation.hpp"
#include "thandor/field_raycast.hpp"
#include "thandor/retail_placement.hpp"
#include "thandor/retail_contact.hpp"

#include <bit>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <stdexcept>

namespace thandor {

SimulationLaunchPolicy simulation_launch_policy_for_runtime_class(uint32_t runtime_class_id) noexcept {
    return (runtime_class_id == 9U || runtime_class_id == 21U)
        ? SimulationLaunchPolicy::SequentialKind2Attachments
        : SimulationLaunchPolicy::AllKind2Attachments;
}
namespace {
float distance3(Vec3f a, Vec3f b) noexcept {
    const float x=b.x-a.x, y=b.y-a.y, z=b.z-a.z;
    return std::sqrt(x*x+y*y+z*z);
}

int32_t positive_magnitude(int32_t value) noexcept {
    if(value==std::numeric_limits<int32_t>::min())return std::numeric_limits<int32_t>::max();
    return std::max(1,std::abs(value));
}

int32_t signed_pitch(uint32_t value) noexcept {
    return static_cast<int32_t>(static_cast<int16_t>(value&0xFFFFU));
}
}

int32_t shortest_turn16_delta(uint32_t current, uint32_t target) noexcept {
    int32_t delta=static_cast<int32_t>((target-current)&0xFFFFU);
    if(delta>0x8000)delta-=0x10000;
    return delta;
}

uint32_t wrap_turn16(int64_t value) noexcept {
    return static_cast<uint32_t>(value)&0xFFFFU;
}

float turn16_to_radians(uint32_t value) noexcept {
    return static_cast<float>(value&0xFFFFU)*(2.0F*std::numbers::pi_v<float>/65536.0F);
}

uint32_t radians_to_turn16(float radians) noexcept {
    const double turns=static_cast<double>(radians)/(2.0*std::numbers::pi);
    return wrap_turn16(static_cast<int64_t>(std::llround(turns*65536.0)));
}

bool simulation_step_yaw(SimulationAxisState& state,uint32_t target_turn16,
                         int32_t maximum_velocity_turn16,int32_t acceleration_turn16,
                         uint32_t tolerance_turn16) noexcept {
    state.angle_turn16&=0xFFFFU;target_turn16&=0xFFFFU;
    const uint32_t delta=(target_turn16-state.angle_turn16)&0xFFFFU;
    const int32_t max_velocity=positive_magnitude(maximum_velocity_turn16);
    const int32_t acceleration=positive_magnitude(acceleration_turn16);
    const int32_t applied=state.velocity_turn16;

    // Exact branch shape of ModelNodeRuntime_SmoothYawTowardTarget for a one-tick update.
    if(delta<=0x8000U){
        if(applied>=0){
            if(delta<=static_cast<uint32_t>(applied)){
                state.angle_turn16=target_turn16;state.velocity_turn16=0;return true;
            }
            state.angle_turn16=wrap_turn16(static_cast<int64_t>(state.angle_turn16)+applied);
            state.velocity_turn16=std::min(max_velocity,applied+acceleration);
        }else state.velocity_turn16=0;
    }else{
        if(applied<=0){
            if(static_cast<uint32_t>(applied+0x10000)<=delta){
                state.angle_turn16=target_turn16;state.velocity_turn16=0;return true;
            }
            state.angle_turn16=wrap_turn16(static_cast<int64_t>(state.angle_turn16)+applied);
            state.velocity_turn16=std::max(-max_velocity,applied-acceleration);
        }else state.velocity_turn16=0;
    }
    const uint32_t residual=(state.angle_turn16-target_turn16)&0xFFFFU;
    return residual<=tolerance_turn16||residual>=0x10000U-tolerance_turn16;
}

bool simulation_step_pitch(SimulationAxisState& state,uint32_t target_turn16,
                           int32_t maximum_velocity_turn16,int32_t acceleration_turn16,
                           int32_t minimum_pitch_turn16,int32_t maximum_pitch_turn16,
                           uint32_t tolerance_turn16) noexcept {
    // ShotDefinition_ComputeLaunchAnglesRegs returns a signed turn16 pitch in the low word.
    // Treating a downward angle such as 0xF800 as +63488 clamps it to the positive MDL
    // limit and makes every turret point upward. The original compares signed dwords.
    int32_t target=signed_pitch(target_turn16);
    target=std::clamp(target,minimum_pitch_turn16,maximum_pitch_turn16);
    int32_t angle=signed_pitch(state.angle_turn16);
    const int32_t max_velocity=positive_magnitude(maximum_velocity_turn16);
    const int32_t acceleration=positive_magnitude(acceleration_turn16);
    const int32_t applied=state.velocity_turn16;
    if(target==angle){state.velocity_turn16=0;return true;}
    if(target<angle){
        if(applied<=0){
            if(applied<=target-angle){
                angle=target;state.velocity_turn16=0;
            }else{
                angle+=applied;
                state.velocity_turn16=std::max(-max_velocity,applied-acceleration);
            }
        }else state.velocity_turn16=0;
    }else{
        if(applied>=0){
            if(applied>=target-angle){
                angle=target;state.velocity_turn16=0;
            }else{
                angle+=applied;
                state.velocity_turn16=std::min(max_velocity,applied+acceleration);
            }
        }else state.velocity_turn16=0;
    }
    state.angle_turn16=wrap_turn16(angle);
    const uint32_t residual=(state.angle_turn16-wrap_turn16(target))&0xFFFFU;
    return residual<tolerance_turn16||residual>0x10000U-tolerance_turn16;
}

bool simulation_step_axis(SimulationAxisState& state,uint32_t target_turn16,
                          int32_t maximum_velocity_turn16,int32_t acceleration_turn16,
                          uint32_t tolerance_turn16) noexcept {
    return simulation_step_yaw(state,target_turn16,maximum_velocity_turn16,
                               acceleration_turn16,tolerance_turn16);
}

SimulationAimAngles simulation_target_angles(Vec3f source,Vec3f target,
                                              int32_t elevation_offset_angle16) noexcept {
    const float dx=target.x-source.x,dy=target.y-source.y,dz=target.z-source.z;
    const float horizontal=std::hypot(dx,dy);
    SimulationAimAngles result;
    result.yaw_turn16=radians_to_turn16(std::atan2(dy,dx));
    const float pitch=std::atan2(dz,std::max(horizontal,1.0e-8F));
    result.pitch_turn16=wrap_turn16(static_cast<int64_t>(radians_to_turn16(pitch))+
                                    elevation_offset_angle16);
    return result;
}

namespace {
SimulationAimAngles native_shot_launch_angles(Vec3f source,Vec3f target,
                                                  const ShotDefinitionRecord& shot) noexcept {
    const int32_t x=float_to_q12(target.x)-float_to_q12(source.x);
    const int32_t y=float_to_q12(target.y)-float_to_q12(source.y);
    const int32_t z=float_to_q12(target.z)-float_to_q12(source.z);
    const auto horizontal=retail_uint64_sqrt(uint64_t(int64_t(x)*x)+uint64_t(int64_t(y)*y));
    if(shot.trajectory_mode==2U){
        SimulationAimAngles result;
        result.yaw_turn16=0U;result.pitch_turn16=0x4000U;return result;
    }
    if(shot.trajectory_mode==1U){
        const int32_t speed_squared=std::bit_cast<int32_t>(
            uint32_t(shot.launch_speed_q12)*uint32_t(shot.launch_speed_q12));
        const int32_t range_divisor=std::bit_cast<int32_t>(uint32_t(horizontal)*uint32_t(shot.ballistic_divisor_q12));
        const int32_t height_term=std::bit_cast<int32_t>(uint32_t(speed_squared)-
            uint32_t(shot.ballistic_divisor_q12)*uint32_t(z)*2U);
        const int64_t discriminant=int64_t(height_term)*speed_squared-int64_t(range_divisor)*range_divisor;
        auto root=retail_uint64_sqrt(static_cast<uint64_t>(std::max<int64_t>(0,discriminant)));
        if(z>-4096&&z<4096)root=0U-root;
        SimulationAimAngles result;
        result.yaw_turn16=fixed_atan2_angle16_compat(y,x)&0xFFFFU;
        result.pitch_turn16=fixed_atan2_angle16_compat(
            std::bit_cast<int32_t>(uint32_t(speed_squared)+root),range_divisor);
        return result;
    }
    const auto heading=fixed_atan2_angle16_compat(y,x)&0xFFFFU;
    const auto elevation=std::min(0x4000,static_cast<int32_t>(
        fixed_atan2_angle16_compat(z,static_cast<int32_t>(horizontal)))+
        shot.elevation_offset_angle16);
    SimulationAimAngles result;
    result.yaw_turn16=heading;
    result.pitch_turn16=static_cast<uint32_t>(elevation);
    return result;
}
}
SimulationAimAngles simulation_shot_launch_angles(Vec3f source,Vec3f target,
                                                   const ShotDefinitionRecord& shot) noexcept {
    const float dx=target.x-source.x,dy=target.y-source.y,dz=target.z-source.z;
    const float horizontal=std::hypot(dx,dy);
    SimulationAimAngles result;
    result.yaw_turn16=radians_to_turn16(std::atan2(dy,dx));
    if(shot.trajectory_mode==2){
        result.yaw_turn16=0U;result.pitch_turn16=0x4000U;return result;
    }
    if(shot.trajectory_mode==1){
        const float speed=std::abs(q12_to_float(shot.launch_speed_q12));
        const float gravity=std::abs(q12_to_float(shot.ballistic_divisor_q12));
        if(speed<=1.0e-8F||gravity<=1.0e-8F||horizontal<=1.0e-8F){
            result.valid=false;return result;
        }
        const double speed2=static_cast<double>(speed)*speed;
        // ShotDefinition_ComputeLaunchAnglesRegs clamps a negative discriminant to zero for
        // projectile creation. This is intentionally NOT the LOS behavior below.
        double discriminant=speed2*speed2-
            static_cast<double>(gravity)*(static_cast<double>(gravity)*horizontal*horizontal+
                                          2.0*static_cast<double>(dz)*speed2);
        if(discriminant<0.0)discriminant=0.0;
        double root=std::sqrt(discriminant);
        // Immutable 0x0052B8C0 negates the sqrt term when the vertical separation is strictly
        // inside +/-0x1000 Q12, selecting the low ballistic root near equal height.
        if(dz>-1.0F&&dz<1.0F)root=-root;
        const double tangent=(speed2+root)/(static_cast<double>(gravity)*horizontal);
        result.pitch_turn16=radians_to_turn16(static_cast<float>(std::atan(tangent)));
        return result;
    }
    auto direct=simulation_target_angles(source,target,shot.elevation_offset_angle16);
    const int32_t clamped=std::clamp(signed_pitch(direct.pitch_turn16),-0x4000,0x4000);
    direct.pitch_turn16=wrap_turn16(clamped);
    return direct;
}

SimulationAimAngles simulation_shot_line_of_fire_angles(Vec3f source,Vec3f target,
                                                         const ShotDefinitionRecord& shot) noexcept {
    if(shot.trajectory_mode!=1)return simulation_shot_launch_angles(source,target,shot);
    const float dx=target.x-source.x,dy=target.y-source.y,dz=target.z-source.z;
    const float horizontal=std::hypot(dx,dy);
    SimulationAimAngles result;
    result.yaw_turn16=radians_to_turn16(std::atan2(dy,dx));
    const float speed=std::abs(q12_to_float(shot.launch_speed_q12));
    const float gravity=std::abs(q12_to_float(shot.ballistic_divisor_q12));
    if(speed<=1.0e-8F||gravity<=1.0e-8F||horizontal<=1.0e-8F){result.valid=false;return result;}
    const double speed2=static_cast<double>(speed)*speed;
    const double discriminant=speed2*speed2-
        static_cast<double>(gravity)*(static_cast<double>(gravity)*horizontal*horizontal+
                                      2.0*static_cast<double>(dz)*speed2);
    if(discriminant<0.0){result.valid=false;return result;}
    double root=std::sqrt(discriminant);
    if(dz>-1.0F&&dz<1.0F)root=-root;
    result.pitch_turn16=radians_to_turn16(static_cast<float>(std::atan(
        (speed2+root)/(static_cast<double>(gravity)*horizontal))));
    return result;
}

uint32_t simulation_relative_yaw_from_world_basis(
    std::span<const float> world_basis,uint32_t current_local_yaw_turn16,
    Vec3f source,Vec3f target) noexcept {
    if(world_basis.size()<9U){
        const auto world=simulation_target_angles(source,target);
        return world.yaw_turn16;
    }
    const Vec3f d{target.x-source.x,target.y-source.y,target.z-source.z};
    // The editor stores local->world basis vectors in columns. Transpose-multiply the desired
    // world direction to reproduce the inverse world rotation used by 0x004BEBC0.
    const float local_x=world_basis[0]*d.x+world_basis[3]*d.y+world_basis[6]*d.z;
    const float local_y=world_basis[1]*d.x+world_basis[4]*d.y+world_basis[7]*d.z;
    if(std::abs(local_x)+std::abs(local_y)<=1.0e-8F)return current_local_yaw_turn16&0xFFFFU;
    const uint32_t delta=radians_to_turn16(std::atan2(local_y,local_x));
    return wrap_turn16(static_cast<int64_t>(current_local_yaw_turn16)+delta);
}

bool simulation_target_within_range(Vec3f source,Vec3f target,
                                    const SimulationWeaponProfile& weapon) noexcept {
    return weapon.maximum_range_q12>0&&distance3(source,target)<=q12_to_float(weapon.maximum_range_q12);
}

uint32_t simulation_class9_mesh_mask_after_fire(uint32_t current_mask,
                                                uint32_t selector) noexcept {
    if(selector>=32U)return current_mask;
    const uint32_t prefix_clear_mask=selector==31U?0U:(0xFFFFFFFEU<<selector);
    return current_mask&prefix_clear_mask;
}

uint32_t simulation_class9_render_mesh_mask(const SimulationWeaponState& state,
                                            const SimulationWeaponProfile& weapon) noexcept {
    (void)weapon;
    return state.launch_mesh_group_mask;
}

float simulation_mode0_beam_length_world(const SimulationProjectileState& projectile,
                                         const ShotDefinitionRecord& shot) noexcept {
    const float resolved=distance3(projectile.launch_position,projectile.target_position);
    if(shot.trajectory_mode!=0)return resolved;
    const int64_t product=static_cast<int64_t>(static_cast<int32_t>(shot.projectile_lifetime_ticks))*
                          static_cast<int64_t>(shot.launch_speed_q12);
    if(product<=0)return resolved;
    const float maximum=q12_to_float(static_cast<int32_t>(std::min<int64_t>(
        product,std::numeric_limits<int32_t>::max())));
    return std::min(resolved,maximum);
}

Vec3f simulation_mode0_stretch_local_position(Vec3f local_position,
                                              float beam_length_world) noexcept {
    const float half=std::max(0.0F,beam_length_world)*0.5F;
    if(local_position.z>=0.0F){
        local_position.z+=half;
        if(local_position.z!=half)local_position.z+=half;
    }
    return local_position;
}

std::array<uint32_t, 2> simulation_runtime_looping_sound_indices(
    const SimulationRuntimeSoundProfile& profile,
    const SimulationRuntimeSoundState& state) noexcept {
    std::array<uint32_t, 2> result{};
    if(!state.enabled)return result;
    const bool moving=state.translated||state.turned;
    auto request=[&](uint32_t index){
        if(index==0U)return;
        if(result[0]==0U)result[0]=index;
        else if(result[0]!=index&&result[1]==0U)result[1]=index;
    };
    switch(profile.runtime_class_id){
    case 1U: case 2U: case 17U: case 18U: case 19U:
        // The dual projected movement callbacks refresh +D0 for translation/turn work and +D8
        // on their turn-state branch. Variant selection changes positioning/contact ownership, not
        // these serialized root sound slots.
        if(state.turned)request(profile.projected_sound_asset_id_d8);
        if(moving)request(profile.projected_sound_asset_id_d0);
        break;
    case 5U: case 6U: case 7U: case 8U:
        if(state.turned)request(profile.projected_sound_asset_id_d8);
        break;
    case 4U: case 10U: case 11U: case 14U: case 16U: case 21U:
        request(profile.projected_sound_asset_id_primary);
        break;
    case 13U: case 22U:
        request(profile.projected_sound_asset_id_primary);
        if(state.secondary_state_active)request(profile.projected_sound_asset_id_secondary);
        break;
    default:
        // Includes class 3 (contact one-shots), class 23 (binary-proven no-op), and classes whose
        // method-D owner does not request a persistent projected channel.
        break;
    }
    return result;
}

bool simulation_segment_intersects_expanded_radius(
    Vec3f from,Vec3f to,Vec3f obstacle_center,int32_t mover_radius_q12,
    int32_t obstacle_radius_q12) noexcept {
    if(mover_radius_q12==0||obstacle_radius_q12==0)return false;
    const float radius=q12_to_float(std::abs(mover_radius_q12))+q12_to_float(std::abs(obstacle_radius_q12));
    const float vx=to.x-from.x,vy=to.y-from.y;
    const float wx=obstacle_center.x-from.x,wy=obstacle_center.y-from.y;
    const float length_sq=vx*vx+vy*vy;
    float t=0.0F;
    if(length_sq>1.0e-12F)t=std::clamp((wx*vx+wy*vy)/length_sq,0.0F,1.0F);
    const float dx=from.x+vx*t-obstacle_center.x;
    const float dy=from.y+vy*t-obstacle_center.y;
    return dx*dx+dy*dy<=radius*radius;
}

uint32_t simulation_depth_bin_mask(int32_t interval_radius_q12, int32_t center_q12) noexcept {
    const int64_t radius = std::llabs(static_cast<int64_t>(interval_radius_q12));
    const int64_t center = center_q12;
    const int64_t first = (center - radius) >> 14;
    const int64_t last = (center + radius) >> 14;
    uint32_t mask = 0U;
    for (int64_t bin = first; bin <= last; ++bin)
        mask |= uint32_t{1} << (static_cast<uint32_t>(bin) & 31U);
    return mask;
}

bool simulation_depth_bin_overlap(Vec3f first_center, int32_t first_radius_q12,
                                  Vec3f second_center, int32_t second_radius_q12) noexcept {
    const auto first_x = simulation_depth_bin_mask(first_radius_q12, float_to_q12(first_center.x));
    const auto first_y = simulation_depth_bin_mask(first_radius_q12, float_to_q12(first_center.y));
    const auto second_x = simulation_depth_bin_mask(second_radius_q12, float_to_q12(second_center.x));
    const auto second_y = simulation_depth_bin_mask(second_radius_q12, float_to_q12(second_center.y));
    return (first_x & second_x) != 0U && (first_y & second_y) != 0U;
}

namespace {
uint32_t ground_advance_for_pose(uint32_t advance,uint32_t yaw,int32_t elevation,
                                uint32_t azimuth,const SimulationMotionProfile& motion) noexcept {
    uint32_t limit=static_cast<uint32_t>(motion.speed_q12_per_tick);
    if(elevation<motion.ground_secondary_slope_threshold_turn16){
        const uint32_t delta=(yaw-azimuth)&0xFFFFU;
        limit=limit*5U>>4;
        if(motion.ground_slope_threshold_turn16<=elevation){
            limit=static_cast<uint32_t>(motion.speed_q12_per_tick);
            if(0x4000U<delta&&delta<0xC000U)limit=limit*5U>>3;
        }
    }
    if(advance<limit){
        const uint32_t accelerated=advance+motion.ground_acceleration_q12;
        if(accelerated<limit)return accelerated;
    }
    return limit;
}
}

uint32_t simulation_ground_advance_q12(const SimulationActorState& actor,
                                     const SimulationMotionProfile& motion) noexcept {
    return ground_advance_for_pose(actor.ground_advance_q12,actor.body_yaw_turn16,
        actor.ground_normal_elevation_turn16,actor.ground_normal_azimuth_turn16,motion);
}

namespace {
// Ground steering has a strict half-turn branch and permits zero limits.
// Weapon smooth-yaw uses different equality/tolerance rules.
void simulation_step_ground_yaw(SimulationAxisState& body,uint32_t target,
                                int32_t limit,int32_t acceleration) noexcept {
    const auto delta=(target-body.angle_turn16)&0xFFFFU;
    const auto velocity=body.velocity_turn16;
    if(delta<0x8000U){
        if(velocity<0){body.velocity_turn16=0;return;}
        if(delta<=static_cast<uint32_t>(velocity)){
            body.angle_turn16=target;body.velocity_turn16=0;return;
        }
        body.angle_turn16=wrap_turn16(int64_t(body.angle_turn16)+velocity);
        body.velocity_turn16=static_cast<int32_t>(std::min<int64_t>(limit,int64_t(velocity)+acceleration));
    }else{
        if(velocity>0){body.velocity_turn16=0;return;}
        if(static_cast<uint32_t>(int64_t(velocity)+0x10000)<=delta){
            body.angle_turn16=target;body.velocity_turn16=0;return;
        }
        body.angle_turn16=wrap_turn16(int64_t(body.angle_turn16)+velocity);
        body.velocity_turn16=static_cast<int32_t>(std::max<int64_t>(-limit,int64_t(velocity)-acceleration));
    }
}
bool simulation_tick_actor_motion_impl(SimulationActorState& actor,
                                       const SimulationMotionProfile& motion,
                                       uint32_t step_ticks,
                                       bool consume_at_half_distance) noexcept {
    step_ticks=std::max(1U,step_ticks);
    if(!actor.move_target||!motion.movable()){
        if(motion.ground_speed_control){actor.ground_advance_q12=0;actor.body_turn_velocity_turn16=0;}
        return false;
    }
    const bool using_waypoint=actor.move_waypoint.has_value();
    const Vec3f destination=using_waypoint?*actor.move_waypoint:*actor.move_target;
    const float dx=destination.x-actor.position.x,dy=destination.y-actor.position.y;
    const auto dx_q12=static_cast<int32_t>(uint32_t(float_to_q12(destination.x))-uint32_t(float_to_q12(actor.position.x)));
    const auto dy_q12=static_cast<int32_t>(uint32_t(float_to_q12(destination.y))-uint32_t(float_to_q12(actor.position.y)));
    const auto ground_distance_q12=retail_uint64_sqrt(uint64_t(int64_t(dx_q12)*dx_q12)+uint64_t(int64_t(dy_q12)*dy_q12));
    const float distance=motion.ground_speed_control?q12_to_float(static_cast<int32_t>(ground_distance_q12)):std::hypot(dx,dy);
    const float constant_speed=std::max(q12_to_float(std::abs(motion.speed_q12_per_tick)),1.0F/4096.0F)*static_cast<float>(step_ticks);
    // Compact non-ground fixtures retain their final-target window. Native ground
    // motion must first reach its primary route point; only then can the native
    // ROUTE_POINT_REACHED branch test the final-target window. Checking it on
    // every active step stopped units about one world unit early.
    // ArmyRuntime_UpdateMovementAndWaypoints clears final state when both
    // signed axis deltas fit the inclusive +/-0x1000 Q12 window. It returns the current model
    // position rather than snapping the model onto the requested command point. Keep transient
    // route waypoints on the tighter step/consume rule: they are not the retail final target.
    if(!motion.ground_speed_control&&!using_waypoint&&std::abs(dx)<=1.0F&&std::abs(dy)<=1.0F){
        actor.move_target.reset();
        if(motion.ground_speed_control){actor.ground_advance_q12=0;actor.body_turn_velocity_turn16=0;}
        return true;
    }
    if(!motion.ground_speed_control&&distance<=constant_speed){
        actor.position.x=destination.x;actor.position.y=destination.y;
        if(using_waypoint)actor.move_waypoint.reset();else actor.move_target.reset();
        return true;
    }
    const uint32_t target_yaw=motion.ground_speed_control
        ? ((dx_q12==0&&dy_q12==0)?actor.body_yaw_turn16:(fixed_atan2_angle16_compat(dy_q12,dx_q12)&0xFFFFU))
        : radians_to_turn16(std::atan2(dy,dx));
    SimulationAxisState body{actor.body_yaw_turn16,actor.body_turn_velocity_turn16};
    const auto turn_limit=static_cast<int32_t>(std::clamp<int64_t>(int64_t(motion.yaw_max_velocity_turn16)*step_ticks,0,INT32_MAX));
    const auto turn_acceleration=static_cast<int32_t>(std::clamp<int64_t>(int64_t(motion.yaw_acceleration_turn16)*step_ticks,0,INT32_MAX));
    if(motion.ground_speed_control){
        simulation_step_ground_yaw(body,target_yaw,turn_limit,turn_acceleration);
        body.angle_turn16&=0xFFFFU;
    }
    else simulation_step_yaw(body,target_yaw,turn_limit,turn_acceleration);
    actor.body_yaw_turn16=body.angle_turn16;actor.body_turn_velocity_turn16=body.velocity_turn16;
    uint32_t allowed_heading_error = 0x1800U;
    if (motion.heading_interpolation_distance_q12 > 0 &&
        (motion.far_heading_error_limit_turn16 != 0U || motion.near_heading_error_limit_turn16 != 0U)) {
        const int32_t distance_q12 = std::max(0, float_to_q12(distance));
        allowed_heading_error = motion.far_heading_error_limit_turn16 & 0xFFFFU;
        if (distance_q12 < motion.heading_interpolation_distance_q12) {
            const int64_t near_limit = motion.near_heading_error_limit_turn16 & 0xFFFFU;
            const int64_t far_limit = motion.far_heading_error_limit_turn16 & 0xFFFFU;
            allowed_heading_error = static_cast<uint32_t>(std::clamp<int64_t>(
                near_limit + (far_limit - near_limit) * distance_q12 /
                    motion.heading_interpolation_distance_q12, 0, 0xFFFF));
        }
    }
    const uint32_t unsigned_error = static_cast<uint32_t>(
        static_cast<uint16_t>(actor.body_yaw_turn16 - target_yaw));
    const bool heading_allows_advance = allowed_heading_error >= 0x8000U ||
        !(allowed_heading_error < unsigned_error && unsigned_error < 0x10000U - allowed_heading_error);
    // ArmyGroundMovement_SteerAndDrive applies its own near/far heading gate.
    // The yaw smoother's weapon-aim tolerance must not admit planar movement
    // outside that gate (notably the Pioneer's far limit of only 121 turn16).
    if(heading_allows_advance){
        if(motion.ground_speed_control)actor.ground_advance_q12=simulation_ground_advance_q12(actor,motion);
        const float speed=motion.ground_speed_control
            ? static_cast<float>(uint64_t(actor.ground_advance_q12)*step_ticks)/4096.0F : constant_speed;
        // The ground classes and runtime class 17 use separate native callbacks,
        // but both consume their current route point once the callback's travel
        // reaches half the remaining distance.  Other constant-speed classes keep
        // the ordinary full-step arrival rule.  A point at the current position
        // must also be consumed so the route can advance on the next callback.
        const uint64_t callback_travel_q12=motion.ground_speed_control
            ? uint64_t(actor.ground_advance_q12)*step_ticks
            : uint64_t(static_cast<uint32_t>(std::abs(motion.speed_q12_per_tick)))*step_ticks;
        if(consume_at_half_distance&&callback_travel_q12>=
                (static_cast<uint32_t>(std::max(0,float_to_q12(distance)))>>1)){
            actor.position.x=destination.x;actor.position.y=destination.y;
            if(using_waypoint)actor.move_waypoint.reset();else actor.move_target.reset();
            return true;
        }
        if(motion.ground_speed_control){
            // Native FixedTrig_ProjectPlanarPoint truncates each signed Q28
            // product before adding it to the Q12 position on every callback.
            const auto heading=actor.body_yaw_turn16&0xFFFFU;
            const auto travel=static_cast<int32_t>(callback_travel_q12);
            const auto x_step=(int64_t(retail_placement_sine_table_entry(32768U+heading))*travel)>>28;
            const auto y_step=(int64_t(retail_placement_sine_table_entry(16384U+heading))*travel)>>28;
            actor.position.x=q12_to_float(static_cast<int32_t>(uint32_t(float_to_q12(actor.position.x))+uint32_t(x_step)));
            actor.position.y=q12_to_float(static_cast<int32_t>(uint32_t(float_to_q12(actor.position.y))+uint32_t(y_step)));
        }else{
            const float step=std::min(distance,speed);
            actor.position.x+=std::cos(turn16_to_radians(actor.body_yaw_turn16))*step;
            actor.position.y+=std::sin(turn16_to_radians(actor.body_yaw_turn16))*step;
        }
        return true;
    }
    if(motion.ground_speed_control)actor.ground_advance_q12=0;
    return false;
}
}

bool simulation_tick_actor_motion(SimulationActorState& actor,
                                  const SimulationMotionProfile& motion,uint32_t step_ticks) noexcept {
    return simulation_tick_actor_motion_impl(actor,motion,step_ticks,
                                             motion.ground_speed_control);
}

bool simulation_tick_class17_actor_motion(SimulationActorState& actor,
                                          const SimulationMotionProfile& motion,
                                          uint32_t step_ticks) noexcept {
    if(motion.ground_speed_control){
        if(!actor.move_target||!motion.movable()){
            actor.ground_advance_q12=0;actor.body_turn_velocity_turn16=0;return false;
        }
        const bool waypoint=actor.move_waypoint.has_value();
        const auto destination=waypoint?*actor.move_waypoint:*actor.move_target;
        const int32_t tx=float_to_q12(destination.x),ty=float_to_q12(destination.y);
        auto& previous=actor.class17_previous_route_point_q12;
        // Original AND: a segment changing only one coordinate retains speed.
        if(tx!=previous[0]&&ty!=previous[1]){
            previous={tx,ty};actor.ground_advance_q12=0;actor.body_turn_velocity_turn16=0;
        }
        const int32_t x=float_to_q12(actor.position.x),y=float_to_q12(actor.position.y);
        const int32_t dx=static_cast<int32_t>(uint32_t(tx)-uint32_t(x));
        const int32_t dy=static_cast<int32_t>(uint32_t(ty)-uint32_t(y));
        const auto distance=retail_uint64_sqrt(uint64_t(int64_t(dx)*dx)+uint64_t(int64_t(dy)*dy));
        const auto heading=(dx==0&&dy==0)?actor.body_yaw_turn16:fixed_atan2_angle16_compat(dy,dx)&0xFFFFU;
        const auto old_heading=actor.body_yaw_turn16;
        if(actor.body_turn_velocity_turn16!=0||distance>=uint32_t(std::max(0,motion.heading_interpolation_distance_q12))){
            SimulationAxisState body{actor.body_yaw_turn16,actor.body_turn_velocity_turn16};
            simulation_step_ground_yaw(body,heading,
                static_cast<int32_t>(std::min<int64_t>(INT32_MAX,int64_t(motion.yaw_max_velocity_turn16)*step_ticks)),
                static_cast<int32_t>(std::min<int64_t>(INT32_MAX,int64_t(motion.yaw_acceleration_turn16)*step_ticks)));
            actor.body_yaw_turn16=body.angle_turn16&0xFFFFU;actor.body_turn_velocity_turn16=body.velocity_turn16;
        }
        // The activation helper reads the root's previously published bank
        // and recoil angles. The terrain contact pose is rebuilt only after
        // this step, then banking is applied again for the next callback.
        if(const auto& pose=actor.ground_recoil_angles)
            actor.ground_advance_q12=ground_advance_for_pose(actor.ground_advance_q12,
                actor.body_yaw_turn16,static_cast<int32_t>((*pose)[1]),(*pose)[2],motion);
        else actor.ground_advance_q12=simulation_ground_advance_q12(actor,motion);
        const auto travel=actor.ground_advance_q12*std::max(1U,step_ticks);
        if(travel>=distance/2U){
            actor.position.x=destination.x;actor.position.y=destination.y;
            if(waypoint)actor.move_waypoint.reset();else actor.move_target.reset();
        }else{
            // Banking units slide along the route direction while the body turns.
            const auto delta=retail_direction_scaled(0U,heading,static_cast<int32_t>(travel));
            actor.position.x=q12_to_float(static_cast<int32_t>(uint32_t(x)+uint32_t(delta[0])));
            actor.position.y=q12_to_float(static_cast<int32_t>(uint32_t(y)+uint32_t(delta[1])));
        }
        return x!=float_to_q12(actor.position.x)||y!=float_to_q12(actor.position.y)||old_heading!=actor.body_yaw_turn16;
    }
    // ArmyRuntimeClass_MoveBankingUnitTowardsRoutePoint compares the current
    // callback travel with half of the remaining route-point length before it
    // asks ArmyRuntime_UpdateMovementAndWaypoints to advance the route.
    return simulation_tick_actor_motion_impl(actor,motion,step_ticks,true);
}

bool simulation_tick_articulated_actor_motion(
    SimulationActorState& actor,const SimulationMotionProfile& motion,
    int32_t contact_interpolation_rate_q12,uint32_t step_ticks) noexcept {
    step_ticks=std::max(1U,step_ticks);
    if(!actor.move_target||!motion.movable())return false;
    const bool using_waypoint=actor.move_waypoint.has_value();
    const Vec3f destination=using_waypoint?*actor.move_waypoint:*actor.move_target;
    const float dx=destination.x-actor.position.x;
    const float dy=destination.y-actor.position.y;
    const float distance=std::hypot(dx,dy);
    const float speed=std::max(q12_to_float(std::abs(motion.speed_q12_per_tick)),1.0F/4096.0F)*static_cast<float>(step_ticks);
    // Same final-target window as ArmyRuntime_UpdateMovementAndWaypoints. Class-3 contact
    // settling remains active after the command is cleared; only the root command stops here.
    if(!using_waypoint&&std::abs(dx)<=1.0F&&std::abs(dy)<=1.0F){
        actor.move_target.reset();
        actor.body_turn_velocity_turn16=0;
        actor.articulated_turning=false;
        return true;
    }
    if(distance<=speed){
        actor.position.x=destination.x;
        actor.position.y=destination.y;
        if(using_waypoint)actor.move_waypoint.reset();else actor.move_target.reset();
        actor.body_turn_velocity_turn16=0;
        actor.articulated_turning=false;
        return true;
    }

    const uint32_t target_yaw=radians_to_turn16(std::atan2(dy,dx));
    const int32_t yaw_delta=shortest_turn16_delta(actor.body_yaw_turn16,target_yaw);
    // The original contact callback changes modes at 0x0800 and 0x2000 heading error and never
    // routes class 3 through the zero generic yaw fields. Scale the contact-heading step from the
    // serialized planar advance, bounded by those exact thresholds.
    const int32_t speed_q12=std::max(std::abs(motion.speed_q12_per_tick),1);
    const int32_t contact_scale=std::max(std::abs(contact_interpolation_rate_q12),1);
    const int32_t derived_step=static_cast<int32_t>(std::clamp<int64_t>(
        (static_cast<int64_t>(speed_q12)*0x1000LL)/contact_scale,0x100LL,0x800LL));
    const int32_t applied_delta=std::clamp(yaw_delta,-int32_t(std::min<int64_t>(0x8000,int64_t(derived_step)*step_ticks)),int32_t(std::min<int64_t>(0x8000,int64_t(derived_step)*step_ticks)));
    if(applied_delta!=0){
        actor.body_yaw_turn16=wrap_turn16(
            static_cast<int64_t>(actor.body_yaw_turn16)+applied_delta);
        actor.body_turn_velocity_turn16=applied_delta;
        actor.articulated_turning=true;
    }else{
        actor.body_turn_velocity_turn16=0;
        actor.articulated_turning=false;
    }

    const int32_t remaining=std::abs(shortest_turn16_delta(actor.body_yaw_turn16,target_yaw));
    if(remaining<=0x2000){
        const float step=std::min(distance,speed);
        actor.position.x+=std::cos(turn16_to_radians(actor.body_yaw_turn16))*step;
        actor.position.y+=std::sin(turn16_to_radians(actor.body_yaw_turn16))*step;
    }
    return applied_delta!=0||remaining<=0x2000;
}

namespace {
int32_t wrap_model_texture_offset(int32_t previous, int32_t distance,
                                     int32_t scale) noexcept {
    // Native arithmetic is modulo 2^32 followed by ONE signed wrap correction.
    // Repeated normalization changes large imported travel/scales and is costly.
    int32_t value = std::bit_cast<int32_t>(static_cast<uint32_t>(previous) +
        static_cast<uint32_t>(distance) * static_cast<uint32_t>(scale));
    constexpr int32_t period = 0x100000;
    if (value > period) value -= period;
    else if (value < -period) value += period;
    return value;
}

int32_t signed_track_chord_q12(Vec3f previous_position, uint32_t previous_yaw_turn16,
                               Vec3f current_position, uint32_t current_yaw_turn16,
                               float local_bounds_y0, bool opposite_side) noexcept {
    const uint32_t side_bit = opposite_side ? 0x8000U : 0U;
    const float previous_angle = turn16_to_radians(
        wrap_turn16(static_cast<int64_t>(previous_yaw_turn16) - 0x4000 + side_bit));
    const float current_angle = turn16_to_radians(
        wrap_turn16(static_cast<int64_t>(current_yaw_turn16) - 0x4000 + side_bit));
    const Vec3f previous_side{
        previous_position.x + std::cos(previous_angle) * local_bounds_y0,
        previous_position.y + std::sin(previous_angle) * local_bounds_y0,
        previous_position.z};
    const Vec3f current_side{
        current_position.x + std::cos(current_angle) * local_bounds_y0,
        current_position.y + std::sin(current_angle) * local_bounds_y0,
        current_position.z};
    const float dx = current_side.x - previous_side.x;
    const float dy = current_side.y - previous_side.y;
    const float distance = std::hypot(dx, dy);
    if (distance <= 1.0e-9F) return 0;
    const uint32_t chord_heading = radians_to_turn16(std::atan2(dy, dx));
    const uint32_t relative = wrap_turn16(
        static_cast<int64_t>(chord_heading) - current_yaw_turn16);
    const int32_t distance_q12 = static_cast<int32_t>(
        std::lround(distance * 4096.0F));
    return relative > 0x4000U && relative < 0xC000U ? -distance_q12 : distance_q12;
}
} // namespace

bool simulation_model_node_is_weapon_aim_axis(
    std::span<const SimulationWeaponProfile> profiles,uint32_t model_node_index) noexcept {
    return std::any_of(profiles.begin(),profiles.end(),[&](const SimulationWeaponProfile& profile){
        return profile.yaw_model_node_index==model_node_index ||
               profile.pitch_model_node_index==model_node_index;
    });
}

void simulation_update_tank_track_offsets(SimulationActorState& actor,
                                          Vec3f previous_position,
                                          uint32_t previous_yaw_turn16,
                                          Vec3f current_position,
                                          uint32_t current_yaw_turn16,
                                          float local_bounds_y0,
                                          int32_t texture_travel_scale) noexcept {
    if (texture_travel_scale == 0) return;
    const int32_t primary_distance = signed_track_chord_q12(
        previous_position, previous_yaw_turn16, current_position, current_yaw_turn16,
        local_bounds_y0, false);
    const int32_t secondary_distance = signed_track_chord_q12(
        previous_position, previous_yaw_turn16, current_position, current_yaw_turn16,
        local_bounds_y0, true);
    actor.primary_track_texture_offset_u = wrap_model_texture_offset(
        actor.primary_track_texture_offset_u, primary_distance, texture_travel_scale);
    actor.secondary_track_texture_offset_u = wrap_model_texture_offset(
        actor.secondary_track_texture_offset_u, secondary_distance, texture_travel_scale);
}

bool simulation_tick_movement_ground_effect_countdown(
    SimulationActorState& actor, bool root_moved_or_turned,
    uint32_t reset_ticks, uint32_t simulation_step_ticks) noexcept {
    if (!root_moved_or_turned || simulation_step_ticks == 0U) return false;
    const int64_t next = static_cast<int64_t>(actor.movement_ground_effect_countdown_ticks) -
                         static_cast<int64_t>(simulation_step_ticks);
    actor.movement_ground_effect_countdown_ticks = static_cast<int32_t>(
        std::clamp<int64_t>(next, std::numeric_limits<int32_t>::min(),
                           std::numeric_limits<int32_t>::max()));
    // The executable emits both when the subtraction reaches exactly zero and when a zero initial
    // countdown crosses negative. This <=0 form is equivalent for the non-negative MDL intervals
    // used by the movement-ground effect owners.
    if (actor.movement_ground_effect_countdown_ticks > 0) return false;
    actor.movement_ground_effect_countdown_ticks = static_cast<int32_t>(std::min<uint32_t>(
        reset_ticks, static_cast<uint32_t>(std::numeric_limits<int32_t>::max())));
    return true;
}

Vec3f simulation_articulated_step_target(Vec3f root, Vec3f opposite_target,
    Vec3f side_offset, uint32_t heading_turn16, int32_t stride_q12,
    Vec3f route_point) noexcept {
    const float reach = std::hypot(opposite_target.x + side_offset.x - root.x,
                                   opposite_target.y + side_offset.y - root.y) +
                        q12_to_float(std::max(0, stride_q12));
    const float route_distance = std::hypot(route_point.x - root.x,
                                           route_point.y - root.y);
    Vec3f ahead = route_point;
    if (reach < route_distance) {
        const float heading = turn16_to_radians(heading_turn16);
        ahead.x = root.x + std::cos(heading) * reach;
        ahead.y = root.y + std::sin(heading) * reach;
    }
    return {ahead.x + side_offset.x, ahead.y + side_offset.y, root.z};
}

int32_t simulation_articulated_contact_step_q12(
    int32_t contact_interpolation_numerator_q12,int32_t contact_lift_q12,
    int32_t contact_distance_q12) noexcept {
    const int64_t denominator=static_cast<int64_t>(contact_distance_q12)+
        static_cast<int64_t>(contact_lift_q12)*2LL;
    if(denominator<=0LL)return 0x2000;
    const int64_t numerator=static_cast<int64_t>(contact_interpolation_numerator_q12)<<13;
    if(numerator<=0LL)return 0x2000;
    return static_cast<int32_t>(std::clamp<int64_t>(numerator/denominator,1LL,0x7FFFFFFFLL));
}

void simulation_update_articulated_contact_transition(
    SimulationActorState& actor,Vec3f previous_position,uint32_t previous_yaw_turn16,
    Vec3f current_position,uint32_t current_yaw_turn16,
    int32_t contact_interpolation_numerator_q12,int32_t movement_advance_q12) noexcept {
    const float distance=std::hypot(current_position.x-previous_position.x,
                                    current_position.y-previous_position.y);
    const uint32_t yaw_work=static_cast<uint32_t>(std::abs(
        shortest_turn16_delta(previous_yaw_turn16,current_yaw_turn16)));

    const auto publish_contact_completion=[&](){
        actor.articulated_last_completed_side=actor.articulated_swing_side&1U;
        ++actor.articulated_contact_event_serial;
        actor.articulated_swing_side^=1U;
    };
    const auto write_phase=[&](uint32_t progress){
        actor.articulated_contact_progress_q12=progress;
        actor.articulated_gait_phase_turn16=(progress<<4U)|
            ((actor.articulated_swing_side&1U)!=0U?0x8000U:0U);
    };

    if(distance<=1.0e-8F&&yaw_work==0U){
        actor.articulated_turning=false;
        actor.articulated_was_moving=false;
        // In ArmyRuntimeClass_UpdateArticulatedMovement the active contact bits are handled at
        // 0x005212D0..0x00521433 before the <=0x40 waypoint-stop path at 0x00521443..0x005214C8.
        // Therefore a partial foot transition finishes even after the root reaches its destination.
        // The old editor cleared the transition immediately and left Gorilla/Emu/Orang feet
        // suspended or planted at a stale mid-step position.
        if(actor.articulated_contact_transition_active&&
           (actor.articulated_contact_progress_q12!=0U||actor.articulated_contact_settling)){
            actor.articulated_contact_settling=true;
            const uint32_t remaining=0x1000U-actor.articulated_contact_progress_q12;
            const uint32_t advance=std::min<uint32_t>(remaining,
                std::max<uint32_t>(1U,actor.articulated_last_contact_advance_q12));
            uint32_t progress=actor.articulated_contact_progress_q12+advance;
            if(progress>=0x1000U){
                progress=0U;
                publish_contact_completion();
                actor.articulated_contact_settling=false;
                actor.articulated_contact_transition_active=false;
            }
            write_phase(progress);
            return;
        }
        actor.articulated_contact_settling=false;
        actor.articulated_contact_transition_active=false;
        return;
    }

    actor.articulated_contact_settling=false;
    const int32_t distance_q12=static_cast<int32_t>(std::lround(distance*4096.0F));
    // Retail advances contact progress as movementAdvancePerTickQ12 * derivedContactStepQ12.
    // The old editor divided by +0xC0 and therefore inverted the gait-speed relationship.
    const int32_t step_q12=actor.articulated_contact_step_q12>0
        ?actor.articulated_contact_step_q12
        :std::max(std::abs(contact_interpolation_numerator_q12),0x2000);
    const uint32_t travel_advance=static_cast<uint32_t>(std::max<int64_t>(0,
        (static_cast<int64_t>(distance_q12)*static_cast<int64_t>(step_q12))>>12));
    // A turn requests a contact; it does not complete one. Native AdvanceRunningStep advances
    // pivot contacts by movementAdvancePerTickQ12 * stepRate, just like walking contacts.
    // Converting 0x800 heading work into a whole step made the feet teleport on sharp turns.
    const int32_t pivot_travel=movement_advance_q12>0?movement_advance_q12:
        std::max(1,std::abs(contact_interpolation_numerator_q12)/16);
    const uint32_t turn_advance=yaw_work==0U?0U:static_cast<uint32_t>(
        (static_cast<int64_t>(pivot_travel)*step_q12)>>12);
    const uint32_t advance=std::max<uint32_t>(1U,std::max(travel_advance,turn_advance));
    actor.articulated_last_contact_advance_q12=advance;
    uint32_t progress=actor.articulated_contact_progress_q12+advance;
    while(progress>=0x1000U){
        progress-=0x1000U;
        publish_contact_completion();
    }
    write_phase(progress);
    actor.articulated_contact_transition_active=true;
    actor.articulated_was_moving=true;
    actor.articulated_turning=yaw_work!=0U;
}

uint32_t simulation_articulated_support_heading(const SimulationActorState& actor) noexcept {
    if(!actor.articulated_contacts_initialized)return actor.body_yaw_turn16;
    auto headings=actor.articulated_contact_heading_turn16;
    if(actor.articulated_contact_transition_active){
        const uint32_t side=actor.articulated_swing_side&1U;
        const auto delta=shortest_turn16_delta(headings[side],
            actor.articulated_contact_target_heading_turn16[side]);
        headings[side]=wrap_turn16(static_cast<int64_t>(headings[side])+
            ((static_cast<int64_t>(delta)*std::min(actor.articulated_contact_progress_q12,4096U))>>12));
    }
    // Native stores half the planned turn in stepEndHeading; the first foot supplies the first
    // half and the closing foot supplies the second. Average in the wrapped angle domain.
    return wrap_turn16(static_cast<int64_t>(headings[0])+
        (shortest_turn16_delta(headings[0],headings[1])>>1));
}

void simulation_update_articulated_gait_phase(
    SimulationActorState& actor,Vec3f previous_position,Vec3f current_position,
    int32_t contact_interpolation_rate_q12) noexcept {
    simulation_update_articulated_contact_transition(
        actor,previous_position,actor.body_yaw_turn16,current_position,actor.body_yaw_turn16,
        contact_interpolation_rate_q12);
}

void simulation_initialize_class17_deployment(
    SimulationActorState& actor,bool freshly_placed,bool deployment_tilt_enabled) noexcept {
    actor.class17_bank_angle_turn16=0x4000U;
    actor.class17_bank_heading_turn16=actor.body_yaw_turn16;
    if(!deployment_tilt_enabled){
        actor.class17_rotor_deploy_ticks_remaining=0U;
        actor.class17_rotor_tilt_turn16=0U;
        return;
    }
    actor.class17_rotor_deploy_ticks_remaining=freshly_placed?24U:0U;
    actor.class17_rotor_tilt_turn16=freshly_placed?0U:0x3FF0U;
}

void simulation_tick_class17_deployment(SimulationActorState& actor) noexcept {
    if(actor.class17_rotor_deploy_ticks_remaining==0U)return;
    actor.class17_rotor_tilt_turn16=std::min(0x3FF0U,
        actor.class17_rotor_tilt_turn16+0x2AAU);
    --actor.class17_rotor_deploy_ticks_remaining;
    if(actor.class17_rotor_deploy_ticks_remaining==0U)
        actor.class17_rotor_tilt_turn16=0x3FF0U;
}

void simulation_tick_class17_banking(SimulationActorState& actor,bool moved) noexcept {
    // The movement callback moves classState60 by -0x100 while advancing and +0x40 while stopped,
    // clamped to [0x3800,0x4000]. ownerValue64 follows heading by at most 0x400 per tick.
    if(moved){
        actor.class17_bank_angle_turn16=std::max(0x3800U,
            actor.class17_bank_angle_turn16>=0x100U
                ?actor.class17_bank_angle_turn16-0x100U:0U);
        const int32_t delta=shortest_turn16_delta(
            actor.class17_bank_heading_turn16,actor.body_yaw_turn16);
        actor.class17_bank_heading_turn16=wrap_turn16(
            static_cast<int64_t>(actor.class17_bank_heading_turn16)+
            std::clamp(delta,-0x400,0x400));
    }else{
        actor.class17_bank_angle_turn16=std::min(0x4000U,
            actor.class17_bank_angle_turn16+0x40U);
    }
}

void simulation_tick_class17_pose(SimulationActorState& actor,bool moved) noexcept {
    simulation_tick_class17_deployment(actor);
    simulation_tick_class17_banking(actor,moved);
}


SimulationWeaponTickResult simulation_tick_weapon(
    SimulationActorState& actor,SimulationWeaponState& state,
    const SimulationWeaponProfile& weapon,const ShotDefinitionRecord& shot,
    Vec3f aim_origin,const Vec3f* target,bool line_of_fire_clear,
    uint32_t available_launch_attachment_selector_mask,
    std::span<const float> yaw_world_basis,bool command_aim,bool native_turret,
    std::optional<std::array<uint32_t,3>> native_yaw_angles) noexcept {
    SimulationWeaponTickResult result;
    const bool native_barrel_spin=native_turret&&(weapon.runtime_class_id==7U||weapon.runtime_class_id==8U);
    // Class 7/8 rotate barrel 0 while the reload counter runs. This angle is
    // cumulative and survives reload completion; the recoil clock is separate.
    if(native_barrel_spin&&state.cooldown_ticks>0U)
        state.recoil_angle_offset_turn16=wrap_turn16(int64_t(state.recoil_angle_offset_turn16)+weapon.recoil_rotation_step_turn16);
    if(native_turret&&weapon.runtime_class_id==8U)
        for(auto& remaining:state.alternate_barrel_recoil_ticks)if(remaining)--remaining;
    if(state.cooldown_ticks>0U)--state.cooldown_ticks;
    if(state.inter_shot_ticks>0U)--state.inter_shot_ticks;
    for(uint32_t selector=0U;selector<state.attachment_reload_ticks.size();++selector){
        auto& timer=state.attachment_reload_ticks[selector];
        // ArmyRuntimeWeapon_UpdateTargetAimAndFireAttachments subtracts the simulation step
        // unconditionally, tests the signed result, then stores zero and restores the selector.
        // With a one-tick editor step, reload N remains hidden when it reaches zero and returns on
        // the next maintenance tick. Initial zero timers also reassert their visible rack bits.
        if(timer>0){
            --timer;
        }else{
            timer=0;
            state.launch_mesh_group_mask|=1U<<selector;
        }
    }
    if(state.recoil_rotation_ticks>0U){
        if(!native_barrel_spin)state.recoil_angle_offset_turn16+=weapon.recoil_rotation_step_turn16;
        --state.recoil_rotation_ticks;
        if(!native_barrel_spin&&state.recoil_rotation_ticks==0U)state.recoil_angle_offset_turn16=0;
    }
    if(state.recoil_translation_ticks>0U){
        state.recoil_translation_offset_q12=-weapon.recoil_translation_q12;
        --state.recoil_translation_ticks;
        if(state.recoil_translation_ticks==0U)state.recoil_translation_offset_q12=0;
    }
    state.target_in_range=false;state.target_aligned=false;state.line_of_fire_clear=line_of_fire_clear;
    state.last_fired_attachment_selector=0xFFFFFFFFU;
    if(!weapon.valid())return result;
    if(!target){
        // Native turrets recenter both axes while following a route or while
        // either axis is still turning. Stationary turrets retain their pose.
        const bool recenter=native_turret &&
            (actor.move_target.has_value() || state.yaw.velocity_turn16!=0 ||
             state.pitch.velocity_turn16!=0);
        const uint32_t old_yaw=state.yaw.angle_turn16;
        const int32_t old_yaw_velocity=state.yaw.velocity_turn16;
        const uint32_t old_pitch=state.pitch.angle_turn16;
        const int32_t old_pitch_velocity=state.pitch.velocity_turn16;
        if(recenter||state.yaw.velocity_turn16!=0)
            (void)simulation_step_yaw(state.yaw,0U,
                weapon.yaw_max_velocity_turn16,weapon.yaw_acceleration_turn16);
        if(recenter||state.pitch.velocity_turn16!=0)
            (void)simulation_step_pitch(state.pitch,0U,
                weapon.pitch_max_velocity_turn16,weapon.pitch_acceleration_turn16,
                weapon.pitch_min_turn16,weapon.pitch_max_turn16);
        result.turned=state.yaw.angle_turn16!=old_yaw||
            state.yaw.velocity_turn16!=old_yaw_velocity||
            state.pitch.angle_turn16!=old_pitch||
            state.pitch.velocity_turn16!=old_pitch_velocity;
        return result;
    }
    state.target_in_range=simulation_target_within_range(aim_origin,*target,weapon);
    result.in_range=state.target_in_range;result.line_of_fire_clear=line_of_fire_clear;
    if(!state.target_in_range&&!command_aim)return result;
    const auto aim=native_turret?native_shot_launch_angles(aim_origin,*target,shot)
        :simulation_shot_launch_angles(aim_origin,*target,shot);
    if(!aim.valid)return result;
    uint32_t relative_yaw=yaw_world_basis.size()>=9U
        ?simulation_relative_yaw_from_world_basis(
             yaw_world_basis,state.yaw.angle_turn16,aim_origin,*target)
        :wrap_turn16(static_cast<int64_t>(aim.yaw_turn16)-actor.body_yaw_turn16);
    uint32_t relative_pitch=aim.pitch_turn16;
    if(native_turret&&yaw_world_basis.size()>=9U){
        // The native yaw and pitch requests both come from the direction
        // transformed into turret space, including the hull's contact tilt.
        if(!native_yaw_angles){
            std::array<int32_t,9> basis{};
            for(std::size_t i=0;i<basis.size();++i)
                basis[i]=static_cast<int32_t>(std::lround(double(yaw_world_basis[i])*268435456.0));
            native_yaw_angles=retail_extract_model_angles(basis);
        }
        const auto [roll,elevation,azimuth]=*native_yaw_angles;
        const auto inverse=retail_model_basis_q28(0U-roll,elevation,azimuth+0x8000U-roll);
        auto heading=aim.yaw_turn16;
        auto pitch=aim.pitch_turn16;
        if(shot.trajectory_mode!=1U&&shot.trajectory_mode!=2U){
            const auto direct=native_shot_launch_angles(aim_origin,*target,shot);
            heading=direct.yaw_turn16;
            pitch=direct.pitch_turn16;
        }
        const auto world=retail_direction_scaled(pitch,heading,4096);
        std::array<int32_t,3> local{};
        for(std::size_t i=0;i<local.size();++i)
            local[i]=static_cast<int32_t>(
                (int64_t(inverse[3*i])*world[0]+int64_t(inverse[3*i+1])*world[1]+
                 int64_t(inverse[3*i+2])*world[2])>>28);
        const auto horizontal=retail_uint64_sqrt(
            uint64_t(int64_t(local[0])*local[0])+uint64_t(int64_t(local[1])*local[1]));
        relative_yaw=(state.yaw.angle_turn16+fixed_atan2_angle16_compat(local[1],local[0]))&0xFFFFU;
        relative_pitch=fixed_atan2_angle16_compat(local[2],static_cast<int32_t>(horizontal))&0xFFFFU;
    }
    const bool yaw_aligned=simulation_step_yaw(state.yaw,relative_yaw,
        weapon.yaw_max_velocity_turn16,weapon.yaw_acceleration_turn16,
        native_turret?0x3FFU:kSimulationAimToleranceTurn16);
    bool pitch_aligned=simulation_step_pitch(state.pitch,relative_pitch,
        weapon.pitch_max_velocity_turn16,weapon.pitch_acceleration_turn16,
        weapon.pitch_min_turn16,weapon.pitch_max_turn16,native_turret?1U:kSimulationAimToleranceTurn16);
    if(native_turret){
        const int32_t requested=signed_pitch(relative_pitch);
        const int32_t clamped=std::clamp(requested,weapon.pitch_min_turn16,weapon.pitch_max_turn16);
        // Native callers compare the pitch smoother's return value to the request.
        // While still turning it returns the remaining difference, which can itself
        // equal a positive request and permit firing before the pitch reaches it.
        const uint32_t returned=pitch_aligned?static_cast<uint32_t>(clamped):
            (state.pitch.angle_turn16-static_cast<uint32_t>(clamped))&0xFFFFU;
        pitch_aligned=returned==static_cast<uint32_t>(requested);
    }
    const auto trajectory_mode=shot.trajectory_kind();
    const bool enforce_retail_pitch_window = trajectory_mode==ShotTrajectoryMode::Ballistic ||
        ((trajectory_mode==ShotTrajectoryMode::DirectLine ||
          trajectory_mode==ShotTrajectoryMode::LeadAdjusted) && shot.mode3_lead_disabled==0U);
    const int32_t requested_pitch=signed_pitch(relative_pitch);
    const bool pitch_in_retail_window = !enforce_retail_pitch_window ||
        (requested_pitch>=weapon.pitch_min_turn16 && requested_pitch<=weapon.pitch_max_turn16);
    // V530's line-of-fire test rejects these trajectories outside the authored elevation window.
    // Keep driving the visual pitch axis toward its clamped limit, but never report lock/fire merely
    // because the smoother itself reached that clamp.
    state.target_aligned=yaw_aligned&&pitch_aligned&&pitch_in_retail_window;
    result.aligned=state.target_aligned;result.turned=true;
    result.ready_to_fire=state.target_aligned&&
        (weapon.runtime_class_id==9U?state.inter_shot_ticks==0U:state.cooldown_ticks==0U);
    if(!state.target_aligned||!line_of_fire_clear)return result;

    if(weapon.runtime_class_id==9U){
        if(state.inter_shot_ticks!=0U)return result;
        // The original callback scans all eight class-state timer slots from zero on
        // every attempt. Only selectors backed by a kind-2 attachment can succeed;
        // masks such as 0x50 therefore fire selectors 4 and 6, not synthetic slots 0/1.
        for(uint32_t selector=0U;selector<state.attachment_reload_ticks.size();++selector){
            if((weapon.launch_attachment_selector_mask&(1U<<selector))==0U)continue;
            const uint32_t selector_bit=1U<<selector;
            if(state.attachment_reload_ticks[selector]!=0U)continue;
            // ArmyRuntime_ResolveShotLaunchFromModelAttachmentCf searches the exact local-point
            // key (selector<<4)|2 and reports failure through carry. The class-9 callback writes
            // -1 to that selector and continues scanning the remaining slots in the same tick;
            // it does not consume the inter-shot gate or hide any rack geometry on failure.
            if((available_launch_attachment_selector_mask&selector_bit)==0U){
                state.attachment_reload_ticks[selector]=-1;
                continue;
            }
            state.attachment_reload_ticks[selector]=static_cast<int32_t>(std::max(1U,weapon.reload_ticks));
            state.inter_shot_ticks=std::max(1U,weapon.inter_shot_ticks);
            state.launch_mesh_group_mask=simulation_class9_mesh_mask_after_fire(
                state.launch_mesh_group_mask,selector);
            state.last_fired_attachment_selector=selector;
            result.fired_attachment_selector=selector;
            result.fired=true;
            return result;
        }
        return result;
    }

    if(state.cooldown_ticks==0U){
        state.cooldown_ticks=std::max(1U,weapon.reload_ticks);
        state.recoil_rotation_ticks=static_cast<uint32_t>(std::max(0,weapon.recoil_duration_ticks));
        if(native_turret&&weapon.runtime_class_id==7U)state.recoil_translation_ticks+=state.recoil_rotation_ticks;
        else state.recoil_translation_ticks=state.recoil_rotation_ticks;
        if(weapon.launch_policy==SimulationLaunchPolicy::AlternateLaunchNodes){
            const auto count=std::max<size_t>(1U,weapon.launch_model_node_indices.size());
            // Native class 8 increments its sequence before selecting: barrel 1 fires first.
            const auto selector=uint32_t(++state.next_launch_attachment%count);
            state.last_fired_attachment_selector=selector;result.fired_attachment_selector=selector;
            if(native_turret&&selector<state.alternate_barrel_recoil_ticks.size())
                state.alternate_barrel_recoil_ticks[selector]+=state.recoil_rotation_ticks;
        }
        if(weapon.launch_policy==SimulationLaunchPolicy::SequentialKind2Attachments){
            const uint32_t count=std::max(1U,weapon.launch_attachment_count);
            const uint32_t selector=state.next_launch_attachment%count;
            state.last_fired_attachment_selector=selector;
            state.next_launch_attachment=(selector+1U)%count;
            result.fired_attachment_selector=selector;
        }
        result.fired=true;
    }
    return result;
}

SimulationActorTickResult simulation_tick_actor(
    SimulationActorState& actor,const SimulationMotionProfile& motion,
    const SimulationWeaponProfile* weapon,const Vec3f* target,uint32_t tick_count) noexcept {
    SimulationActorTickResult aggregate;
    tick_count=std::max(1U,tick_count);
    if(weapon&&actor.weapons.empty())actor.weapons.emplace_back();
    for(uint32_t tick=0;tick<tick_count;++tick){
        aggregate.moved|=simulation_tick_actor_motion(actor,motion);
        if(weapon&&weapon->valid()){
            ShotDefinitionRecord synthetic;
            synthetic.trajectory_mode=0;
            synthetic.elevation_offset_angle16=0;
            const auto result=simulation_tick_weapon(actor,actor.weapons.front(),*weapon,synthetic,
                                                      actor.position,target,true);
            aggregate.turned|=result.turned;aggregate.fired|=result.fired;
            aggregate.in_range=result.in_range;aggregate.aligned=result.aligned;
            if(result.fired)aggregate.fired_weapon_indices.push_back(0U);
        }
    }
    actor.target_in_range=aggregate.in_range;actor.target_aligned=aggregate.aligned;
    return aggregate;
}

namespace {

double cubic_ramp_distance_ticks(uint32_t age_ticks, uint32_t ramp_ticks) noexcept {
    if (age_ticks == 0U) return 0.0;
    if (ramp_ticks == 0U) return static_cast<double>(age_ticks);

    // Creation stores zero current speed when +0x270 is nonzero. Maintenance moves with the
    // previous speed first, then installs min(age,ramp)^3/ramp^3 * launchSpeed for mode 2.
    const uint32_t last_ramp_tick = std::min(age_ticks - 1U, ramp_ticks);
    const double n = static_cast<double>(last_ramp_tick);
    const double triangular = n * (n + 1.0) * 0.5;
    const double sum_cubes = triangular * triangular;
    const double ramp = static_cast<double>(ramp_ticks);
    const double ramp_contribution = sum_cubes / (ramp * ramp * ramp);
    const uint32_t saturated_ticks = age_ticks > ramp_ticks + 1U
        ? age_ticks - (ramp_ticks + 1U) : 0U;
    return ramp_contribution + static_cast<double>(saturated_ticks);
}

uint32_t fixed_range_transition_age(const ShotDefinitionRecord& shot) noexcept {
    // The comparison happens after the runtime age has been incremented for a processed tick.
    // Even a serialized threshold of zero therefore cannot transition at presentation age zero.
    return std::max(1U, shot.fixed_range_transition_age_threshold_ticks);
}

Vec3f fixed_range_projectile_position(const SimulationProjectileState& projectile,
                                      const ShotDefinitionRecord& shot,
                                      uint32_t age_ticks,
                                      const Vec3f& target_position) noexcept {
    const uint32_t ramp_ticks = shot.trajectory_ramp_duration_ticks;
    const uint32_t transition_age = fixed_range_transition_age(shot);
    const double distance_at_age = cubic_ramp_distance_ticks(age_ticks, ramp_ticks);
    const double distance_at_transition =
        cubic_ramp_distance_ticks(std::min(age_ticks, transition_age), ramp_ticks);
    const double signed_distance_ticks = age_ticks <= transition_age
        ? distance_at_age
        : distance_at_transition - (distance_at_age - distance_at_transition);
    const float vertical_speed = std::abs(projectile.launch_velocity.z);

    Vec3f position = projectile.launch_position;
    position.z += vertical_speed * static_cast<float>(signed_distance_ticks);
    if (age_ticks >= transition_age) {
        // Retail mode 2 snaps/follows only X/Y and then negates its pitch; Z keeps integrating.
        position.x = target_position.x;
        position.y = target_position.y;
    }
    return position;
}

uint32_t fixed_range_target_reach_tick(const SimulationProjectileState& projectile,
                                       const ShotDefinitionRecord& shot,
                                       Vec3f target_position) noexcept {
    const uint32_t lifetime = std::max(1U, projectile.lifetime_ticks);
    const uint32_t transition_age = fixed_range_transition_age(shot);
    for (uint32_t age = transition_age; age <= lifetime; ++age) {
        const Vec3f position = fixed_range_projectile_position(projectile, shot, age, target_position);
        if (position.z <= target_position.z) return age;
    }
    return lifetime;
}

} // namespace

SimulationLinkedEffectShotGeometry simulation_linked_effect_shot_geometry(
    Vec3f effect_root_position, Vec3f attachment_world_position) noexcept {
    const Vec3f delta{attachment_world_position.x-effect_root_position.x,
                      attachment_world_position.y-effect_root_position.y,
                      attachment_world_position.z-effect_root_position.z};
    return {attachment_world_position,
            {effect_root_position.x+delta.x*2.0F,
             effect_root_position.y+delta.y*2.0F,
             effect_root_position.z+delta.z*2.0F}};
}

std::vector<SimulationProjectileState> simulation_create_volley(
    std::size_t owner_placement_index, std::size_t weapon_profile_index,
    const SimulationWeaponProfile& weapon, std::span<const Vec3f> launch_positions,
    Vec3f volley_target_position, const ShotDefinitionRecord& shot, uint32_t spawn_tick,
    uint64_t& next_serial, uint32_t fired_attachment_selector,
    std::optional<std::size_t> live_target_placement_index,
    SimulationProjectileOwnerKind owner_kind,uint64_t owner_runtime_serial,
    std::span<const Vec3f> target_offsets,bool native_launch_angles) {
    std::vector<SimulationProjectileState> result;
    result.reserve(launch_positions.size());

    const float speed = std::max(1.0F / 4096.0F,
                                 std::abs(q12_to_float(shot.launch_speed_q12)));
    for (std::size_t index=0;index<launch_positions.size();++index) {
        const auto launch=launch_positions[index];
        auto target_position=volley_target_position;
        if(index<target_offsets.size()){
            target_position.x+=target_offsets[index].x;
            target_position.y+=target_offsets[index].y;
        }
        const auto aim=native_launch_angles
            ?native_shot_launch_angles(launch,target_position,shot)
            :simulation_shot_launch_angles(launch,target_position,shot);
        if (!aim.valid) continue;

        const uint32_t movement_pitch_turn16 = shot.trajectory_mode == 3
            ? wrap_turn16(static_cast<int64_t>(aim.pitch_turn16) - shot.elevation_offset_angle16)
            : aim.pitch_turn16;
        const float yaw = turn16_to_radians(aim.yaw_turn16);
        const float movement_pitch = turn16_to_radians(movement_pitch_turn16);
        const Vec3f velocity{
            std::cos(yaw) * std::cos(movement_pitch) * speed,
            std::sin(yaw) * std::cos(movement_pitch) * speed,
            std::sin(movement_pitch) * speed,
        };

        const float direct_distance = distance3(launch, target_position);
        uint32_t travel_ticks = 1U;
        if (shot.trajectory_mode == 0) {
            // Mode zero ray-tests from the launch node in its first maintenance update.
            travel_ticks = 1U;
        } else if (shot.trajectory_mode == 1) {
            // Ballistic creation uses the supplied target only to solve the initial launch angles.
            // Retail then advances velocity/gravity and collision for the full serialized SHT
            // lifetime; it never snaps a ballistic projectile to the aim point. Surface/model
            // collision below shortens this to the first actual contact.
            travel_ticks = std::max(1U, shot.projectile_lifetime_ticks);
        } else if (shot.trajectory_mode == 2) {
            // Mode 2 rises vertically, optionally ramps its speed cubically, snaps to the target
            // X/Y at +0x274, then descends. Start with the complete lifetime; target/surface
            // resolution below shortens this to the first actual contact.
            travel_ticks = std::max(1U, shot.projectile_lifetime_ticks);
        } else if (shot.trajectory_mode == 3 &&
                   shot.trajectory_ramp_duration_ticks != 0U && speed > 1.0e-8F) {
            const uint32_t ramp = shot.trajectory_ramp_duration_ticks;
            double travelled = 0.0;
            const uint32_t cap = std::max(1U, shot.projectile_lifetime_ticks);
            for (travel_ticks = 1U; travel_ticks < cap && travelled < direct_distance;
                 ++travel_ticks) {
                const uint32_t previous_age = travel_ticks - 1U;
                const uint32_t clamped = std::min(previous_age, ramp);
                travelled += static_cast<double>(speed) * clamped * clamped /
                    static_cast<double>(ramp) / static_cast<double>(ramp);
            }
        } else {
            travel_ticks = std::max(1U,
                static_cast<uint32_t>(std::ceil(direct_distance / speed)));
        }

        SimulationProjectileState projectile;
        projectile.serial = next_serial++;
        projectile.owner_placement_index = owner_placement_index;
        projectile.owner_kind = owner_kind;
        projectile.owner_runtime_serial = owner_runtime_serial;
        projectile.weapon_profile_index = weapon_profile_index;
        projectile.shot_definition_id = weapon.shot_definition_id;
        projectile.launch_position = launch;
        projectile.target_position = target_position;
        projectile.live_target_placement_index = live_target_placement_index;
        projectile.launch_velocity = velocity;
        projectile.spawn_tick = spawn_tick;
        projectile.travel_ticks = travel_ticks;
        projectile.lifetime_ticks = std::max(travel_ticks,
            std::max(1U, shot.projectile_lifetime_ticks));
        projectile.launch_yaw_turn16 = aim.yaw_turn16;
        projectile.launch_pitch_turn16 = aim.pitch_turn16;
        projectile.fired_attachment_selector = fired_attachment_selector;

        if (shot.trajectory_mode == 2 && live_target_placement_index) {
            projectile.travel_ticks = fixed_range_target_reach_tick(projectile, shot, target_position);
        }
        result.push_back(projectile);
    }
    return result;
}

std::vector<SimulationProjectileState> simulation_create_volley(
    std::size_t owner_placement_index, const SimulationWeaponProfile& weapon,
    std::span<const Vec3f> launch_positions, Vec3f target_position,
    const ShotDefinitionRecord& shot, uint32_t spawn_tick, uint64_t& next_serial) {
    return simulation_create_volley(owner_placement_index, 0U, weapon, launch_positions,
                                    target_position, shot, spawn_tick, next_serial,
                                    0xFFFFFFFFU, std::nullopt,
                                    SimulationProjectileOwnerKind::Placement,0U);
}

uint32_t simulation_shot_frame_index(const ShotDefinitionRecord& shot,
                                     uint32_t age_ticks) noexcept {
    const uint32_t threshold = std::max(1U, shot.animation_threshold_q4);
    const uint32_t frames = std::max(1U, shot.animation_frame_count);
    return ((age_ticks * 0x10U) / threshold) % frames;
}

uint32_t simulation_mode3_elevation_offset_turn16(const ShotDefinitionRecord& shot,
                                                  uint32_t age_ticks) noexcept {
    if (shot.trajectory_mode != 3 || shot.elevation_offset_angle16 == 0 ||
        shot.trajectory_ramp_duration_ticks == 0U) return 0U;
    const uint32_t ramp = shot.trajectory_ramp_duration_ticks;
    const uint32_t remaining = age_ticks >= ramp * 2U ? 0U : ramp * 2U - age_ticks;
    const int64_t scaled =
        (static_cast<int64_t>(remaining) * shot.elevation_offset_angle16) / ramp;
    return wrap_turn16(scaled / 2);
}

uint32_t simulation_mode3_model_pitch_turn16(const SimulationProjectileState& projectile,
                                              const ShotDefinitionRecord& shot,
                                              uint32_t age_ticks) noexcept {
    // Retail keeps movement pitch and presentation/model pitch as separate state. The model pitch
    // eases the serialized elevation offset back toward the movement direction over the ramp.
    if (projectile.runtime_flight_initialized) return uint32_t(projectile.runtime_flight_pitch)&65535U;
    if (shot.trajectory_mode != 3) return projectile.launch_pitch_turn16;
    const uint32_t movement_pitch = wrap_turn16(
        static_cast<int64_t>(projectile.launch_pitch_turn16) - shot.elevation_offset_angle16);
    return wrap_turn16(static_cast<int64_t>(movement_pitch) +
        static_cast<int32_t>(simulation_mode3_elevation_offset_turn16(shot, age_ticks)));
}

Vec3f simulation_projectile_position(const SimulationProjectileState& projectile,
                                     const ShotDefinitionRecord& shot,
                                     uint32_t current_tick,
                                     const Vec3f* live_target_position) noexcept {
    if(projectile.runtime_flight_initialized){
        if(current_tick<projectile.runtime_flight_tick)return projectile.runtime_flight_previous;
        return projectile.runtime_flight_position;
    }
    const uint32_t age = current_tick - projectile.spawn_tick;
    if (shot.trajectory_mode == 0) {
        // Mode zero is a stationary launch-node model stretched to the ray hit by render state.
        return projectile.launch_position;
    }

    const Vec3f target = live_target_position ? *live_target_position : projectile.target_position;
    if (shot.trajectory_mode == 2) {
        return fixed_range_projectile_position(projectile, shot, age, target);
    }
    if (age >= projectile.travel_ticks) {
        if (shot.trajectory_mode != 1) return target;
        // A ballistic aim point is only an initial launch-angle input. At lifetime
        // expiry it must not create a final segment back to that point. Real model
        // or surface contacts retain their resolved position for presentation.
        if (projectile.impact_kind != SimulationImpactKind::None)
            return projectile.impact_position;
    }

    float distance_ticks = static_cast<float>(age);
    if (shot.trajectory_mode == 3 && shot.trajectory_ramp_duration_ticks != 0U) {
        // Retail mode 3 accelerates along the direct launch vector. Its path is accelerated but
        // not a fabricated sine arc. The presentation/model pitch is handled separately below.
        const uint32_t ramp = shot.trajectory_ramp_duration_ticks;
        double accumulated = 0.0;
        for (uint32_t tick = 0U; tick < age; ++tick) {
            const uint32_t clamped = std::min(tick, ramp);
            accumulated += static_cast<double>(clamped) * clamped /
                static_cast<double>(ramp) / static_cast<double>(ramp);
        }
        distance_ticks = static_cast<float>(accumulated);
    }

    Vec3f position{
        projectile.launch_position.x + projectile.launch_velocity.x * distance_ticks,
        projectile.launch_position.y + projectile.launch_velocity.y * distance_ticks,
        projectile.launch_position.z + projectile.launch_velocity.z * distance_ticks,
    };
    if (shot.trajectory_mode == 1 && shot.ballistic_divisor_q12 != 0) {
        const float gravity = std::abs(q12_to_float(shot.ballistic_divisor_q12));
        position.z -= 0.5F * gravity * distance_ticks * distance_ticks;
    }
    return position;
}

void simulation_update_projectile_live_target(SimulationProjectileState& projectile,
                                              const ShotDefinitionRecord& shot,
                                              Vec3f target_position) noexcept {
    // Only SHT trajectory mode 2 owns a live entity target after creation. Retail mode 0
    // direct-line shots keep the launch orientation fixed and raycast that same world-space ray
    // on maintenance ticks; moving the selected target must not bend or resize the beam toward
    // the target's new position. Modes 1/3 likewise advance from their stored launch state.
    if (shot.trajectory_mode != 2) return;
    if (projectile.runtime_flight_initialized) return;
    projectile.target_position = target_position;
    if (projectile.impact_kind == SimulationImpactKind::TargetModelImpactClass) {
        projectile.impact_position = target_position;
    }
    projectile.travel_ticks = fixed_range_target_reach_tick(projectile, shot, target_position);
}

bool simulation_projectile_impacted(const SimulationProjectileState& projectile,
                                    uint32_t current_tick) noexcept {
    return projectile.impact_kind!=SimulationImpactKind::None&&
        current_tick-projectile.spawn_tick>=projectile.travel_ticks;
}

bool simulation_projectile_expired(const SimulationProjectileState& projectile,
                                   uint32_t current_tick) noexcept {
    if(projectile.runtime_flight_initialized&&projectile.runtime_flight_expired)return true;
    const uint32_t age=current_tick-projectile.spawn_tick;
    // A mode-zero beam model remains linked for its serialized SHT lifetime while the one-shot
    // impact EFF can already be active. Retaining every projectile until the later of the model
    // lifetime and impact-EFF lifetime also avoids truncating this exact overlap.
    const uint32_t impact_end=projectile.impact_kind!=SimulationImpactKind::None
        ?projectile.travel_ticks+projectile.impact_visual_lifetime_ticks:0U;
    return age>=std::max(projectile.lifetime_ticks,impact_end);
}

// Resolve actual swept segments against the same bounded FLD surfaces used by
// live beams. Expiry/aim endpoints are not contacts, and absent impact EFF
// slots make that surface ineligible in ShotModel_CastHitRays.
void simulation_resolve_projectile_surface_impact(
    SimulationProjectileState& projectile,const ShotDefinitionRecord& shot,
    const FieldGrid& field) noexcept {
    projectile.impact_kind=SimulationImpactKind::None;
    projectile.impact_position=projectile.target_position;
    projectile.impact_material_index=0;
    projectile.impact_effect_id=0;
    const uint32_t travel=std::max(1U,projectile.travel_ticks);
    const auto start=projectile.launch_position,aim=projectile.target_position;
    Vec3f previous=start;
    // Bound authoring-preview work for custom lifetimes. The catalog-backed
    // runtime casts its current interval independently on every update.
    const uint32_t segments=shot.trajectory_mode==0?1U:std::min(travel,16384U);
    for(uint32_t step=1;step<=segments;++step){
        const Vec3f end=shot.trajectory_mode==0?aim:
            simulation_projectile_position(projectile,shot,projectile.spawn_tick+step);
        const Vec3f delta{end.x-previous.x,end.y-previous.y,end.z-previous.z};
        const float distance=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
        if(std::isfinite(distance)&&distance>0.F){
            const Vec3f direction{delta.x/distance,delta.y/distance,delta.z/distance};
            std::optional<FieldRaycastHit> nearest;
            uint32_t effect=0,material=0;
            if(const auto terrain=raycast_field_surface_compatibility(field,previous,direction,distance,FieldRaycastSurface::Terrain)){
                material=field.at(terrain->nearest_vertex_x,terrain->nearest_vertex_y).material();
                effect=shot.terrain_impact_effect_id(material);
                if(effect)nearest=terrain;
            }
            if(shot.primary_effect_id){
                if(const auto water=raycast_field_surface_compatibility(field,previous,direction,distance,FieldRaycastSurface::Secondary);
                   water&&(!nearest||water->distance<nearest->distance)){
                    nearest=water;effect=shot.primary_effect_id;material=0;
                }
            }
            if(nearest){
                projectile.impact_kind=nearest->secondary_surface?SimulationImpactKind::SecondarySurface:SimulationImpactKind::TerrainSurface;
                projectile.impact_position=nearest->position;projectile.impact_material_index=material;
                projectile.impact_effect_id=effect;
                projectile.travel_ticks=shot.trajectory_mode==0?travel:step;
                projectile.target_position=nearest->position;
                return;
            }
        }
        previous=end;
    }
}


bool simulation_class13_candidate_compatible(uint32_t producer_mask,uint32_t candidate_flags) noexcept {
    return (producer_mask & candidate_flags) != 0U;
}

void simulation_class13_begin_production(SimulationClass13ProductionState& state,
    uint32_t produced_army_asset_id,uint32_t build_duration_ticks,int32_t energy_demand_q4) noexcept {
    state.phase=SimulationClass13ProductionPhase::Building;
    state.produced_army_asset_id=produced_army_asset_id;
    state.build_elapsed_ticks=0U;
    state.build_duration_ticks=build_duration_ticks;
    state.active_energy_demand_q4=energy_demand_q4;
    state.primary_texture_offset_v=0;
    state.produced_entity_serial=0U;
}

bool simulation_class13_tick_build(SimulationClass13ProductionState& state,
    uint32_t simulation_step_ticks) noexcept {
    if(state.phase!=SimulationClass13ProductionPhase::Building)return false;
    const uint64_t next=static_cast<uint64_t>(state.build_elapsed_ticks)+simulation_step_ticks;
    state.build_elapsed_ticks=static_cast<uint32_t>(std::min<uint64_t>(next,
        std::numeric_limits<uint32_t>::max()));
    const uint64_t animation_next=static_cast<uint64_t>(state.animated_subnode_ticks)+
        simulation_step_ticks;
    state.animated_subnode_ticks=static_cast<uint32_t>(std::min<uint64_t>(animation_next,
        std::numeric_limits<uint32_t>::max()));
    return state.build_elapsed_ticks>=state.build_duration_ticks;
}

void simulation_class13_mark_spawned(SimulationClass13ProductionState& state,
    uint64_t produced_entity_serial) noexcept {
    if(state.phase!=SimulationClass13ProductionPhase::Building)return;
    state.produced_entity_serial=produced_entity_serial;
    state.active_energy_demand_q4=0;
    state.phase=SimulationClass13ProductionPhase::Opening;
}

bool simulation_class13_tick_open(SimulationClass13ProductionState& state,
    int32_t class_step_q12,uint32_t simulation_step_ticks) noexcept {
    if(state.phase!=SimulationClass13ProductionPhase::Opening)return false;
    const int64_t delta=static_cast<int64_t>(class_step_q12)*simulation_step_ticks;
    const int64_t next=static_cast<int64_t>(state.primary_texture_offset_v)+delta;
    if(next>0x7ffffLL){
        state.primary_texture_offset_v=0x80000;
        state.phase=SimulationClass13ProductionPhase::WaitingForProducedUnitClear;
        return true;
    }
    state.primary_texture_offset_v=static_cast<int32_t>(std::clamp<int64_t>(next,
        std::numeric_limits<int32_t>::min(),std::numeric_limits<int32_t>::max()));
    return false;
}

bool simulation_class13_observe_produced_unit_link(SimulationClass13ProductionState& state,
    bool still_linked_to_producer) noexcept {
    if(state.phase!=SimulationClass13ProductionPhase::WaitingForProducedUnitClear||
       still_linked_to_producer)return false;
    state.produced_entity_serial=0U;
    state.phase=SimulationClass13ProductionPhase::Closing;
    return true;
}

bool simulation_class13_tick_close(SimulationClass13ProductionState& state,
    int32_t class_step_q12,uint32_t simulation_step_ticks) noexcept {
    if(state.phase!=SimulationClass13ProductionPhase::Closing)return false;
    const int64_t delta=static_cast<int64_t>(class_step_q12)*simulation_step_ticks;
    const int64_t next=static_cast<int64_t>(state.primary_texture_offset_v)-delta;
    if(next<1LL){
        state.primary_texture_offset_v=0;
        state.phase=SimulationClass13ProductionPhase::Idle;
        state.produced_army_asset_id=0U;
        state.build_elapsed_ticks=0U;
        state.build_duration_ticks=0U;
        state.active_energy_demand_q4=0;
        return true;
    }
    state.primary_texture_offset_v=static_cast<int32_t>(std::clamp<int64_t>(next,
        std::numeric_limits<int32_t>::min(),std::numeric_limits<int32_t>::max()));
    return false;
}


SimulationClass22LaunchCommand simulation_class22_world_launch_command(
    uint32_t heading_turn16,int32_t mission_world_x_q12,int32_t mission_world_y_q12) noexcept {
    return {static_cast<int32_t>(heading_turn16&0xFFFFU),mission_world_x_q12,mission_world_y_q12};
}

bool simulation_class22_candidate_compatible(uint32_t candidate_flags) noexcept {
    return (candidate_flags & 8U) != 0U;
}

void simulation_class22_configure_storage(SimulationClass22AirportState& state,
    uint32_t storage_capacity,int32_t platform_baseline_q12) noexcept {
    state.storage_capacity=std::min<uint32_t>(storage_capacity,
        static_cast<uint32_t>(kSimulationClass22StoredSlotCount));
    state.platform_local_z_q12=platform_baseline_q12;
    state.stored_slot_count=static_cast<uint32_t>(std::count_if(
        state.stored_army_asset_ids.begin(),state.stored_army_asset_ids.begin()+state.storage_capacity,
        [](uint32_t id){return id!=0U;}));
    const uint16_t capacity_mask=state.storage_capacity==0U ? 0U :
        static_cast<uint16_t>((1U<<state.storage_capacity)-1U);
    state.occupied_slot_mask=static_cast<uint16_t>(state.occupied_slot_mask&capacity_mask);
    const uint32_t occupied_count=static_cast<uint32_t>(std::popcount(
        static_cast<unsigned int>(state.occupied_slot_mask)));
    // Keep malformed/restored editor state from wrapping the availability counter. Retail state
    // cannot legitimately occupy more slots than contain persistent aircraft ids, but editor
    // reconfiguration and old snapshots can present that transiently.
    state.available_stored_count=state.stored_slot_count-
        std::min(state.stored_slot_count,occupied_count);
}

bool simulation_class22_begin_storage_build(SimulationClass22AirportState& state,
    uint32_t army_asset_id,uint32_t build_duration_ticks,int32_t energy_demand_q4) noexcept {
    if(state.build_phase!=SimulationClass22BuildPhase::Idle||army_asset_id==0U||
       state.stored_slot_count>=state.storage_capacity)return false;
    state.build_phase=SimulationClass22BuildPhase::Building;
    state.building_army_asset_id=army_asset_id;
    state.build_elapsed_ticks=0U;
    state.build_duration_ticks=build_duration_ticks;
    state.active_energy_demand_q4=energy_demand_q4;
    return true;
}

bool simulation_class22_tick_storage_build(SimulationClass22AirportState& state,
    uint32_t simulation_step_ticks) noexcept {
    if(state.build_phase!=SimulationClass22BuildPhase::Building)return false;
    state.build_elapsed_ticks=static_cast<uint32_t>(std::min<uint64_t>(
        static_cast<uint64_t>(state.build_elapsed_ticks)+simulation_step_ticks,
        std::numeric_limits<uint32_t>::max()));
    state.animated_subnode_ticks=static_cast<uint32_t>(std::min<uint64_t>(
        static_cast<uint64_t>(state.animated_subnode_ticks)+simulation_step_ticks,
        std::numeric_limits<uint32_t>::max()));
    return state.build_elapsed_ticks>=state.build_duration_ticks;
}

bool simulation_class22_complete_storage_build(SimulationClass22AirportState& state,
    bool energy_satisfied) noexcept {
    if(state.build_phase!=SimulationClass22BuildPhase::Building||
       state.build_elapsed_ticks<state.build_duration_ticks)return false;
    const uint32_t completed_id=state.building_army_asset_id;
    state.build_phase=SimulationClass22BuildPhase::Idle;
    state.building_army_asset_id=0U;
    state.build_elapsed_ticks=0U;
    state.build_duration_ticks=0U;
    state.active_energy_demand_q4=0;
    if(!energy_satisfied||completed_id==0U||state.storage_capacity==0U)return false;
    std::size_t slot=0U;
    bool found=false;
    for(std::size_t index=state.storage_capacity;index>0U;--index){
        if(state.stored_army_asset_ids[index-1U]==0U){slot=index-1U;found=true;break;}
    }
    if(!found)slot=0; // Native completion overwrites slot zero if restored capacity filled mid-build.
    state.stored_army_asset_ids[slot]=completed_id;
    ++state.stored_slot_count;
    ++state.available_stored_count;
    return true;
}

void simulation_class22_queue_launch_mask(SimulationClass22AirportState& state,uint32_t lane_mask,
    int32_t value_a,int32_t value_b,int32_t value_c) noexcept {
    std::array<uint32_t,3> counts{};
    for(const uint32_t id:state.stored_army_asset_ids){
        for(std::size_t lane=0;lane<kSimulationClass22LinkedArmyAssetIds.size();++lane)
            if(id==kSimulationClass22LinkedArmyAssetIds[lane])++counts[lane];
    }
    for(std::size_t lane=0;lane<counts.size();++lane){
        if((lane_mask&(1U<<lane))==0U)continue;
        state.pending_launch_counts[lane]=static_cast<uint8_t>(std::min<uint32_t>(counts[lane],0xFFU));
        state.launch_commands[lane]={value_a,value_b,value_c};
    }
}

std::optional<SimulationClass22LaunchRequest>
simulation_class22_take_next_pending_launch(SimulationClass22AirportState& state) noexcept {
    if(state.dispatch_phase==SimulationClass22DispatchPhase::Reset)
        state.dispatch_phase=SimulationClass22DispatchPhase::Idle;
    if(state.dispatch_phase!=SimulationClass22DispatchPhase::Idle)return std::nullopt;
    for(std::size_t lane=0;lane<state.pending_launch_counts.size();++lane){
        if(state.pending_launch_counts[lane]==0U)continue;
        --state.pending_launch_counts[lane];
        return SimulationClass22LaunchRequest{static_cast<uint32_t>(lane),
            kSimulationClass22LinkedArmyAssetIds[lane],state.launch_commands[lane]};
    }
    return std::nullopt;
}

std::optional<std::size_t> simulation_class22_find_free_stored_slot(
    const SimulationClass22AirportState& state,uint32_t army_asset_id) noexcept {
    const std::size_t limit=std::min<std::size_t>(state.storage_capacity,kSimulationClass22StoredSlotCount);
    for(std::size_t slot=0;slot<limit;++slot){
        const uint16_t bit=static_cast<uint16_t>(1U<<slot);
        if(state.stored_army_asset_ids[slot]==army_asset_id&&
           (state.occupied_slot_mask&bit)==0U)return slot;
    }
    return std::nullopt;
}

bool simulation_class22_mark_linked_child_spawned(SimulationClass22AirportState& state,
    std::size_t storage_slot) noexcept {
    if(storage_slot>=state.storage_capacity||storage_slot>=kSimulationClass22StoredSlotCount||
       state.dispatch_phase!=SimulationClass22DispatchPhase::Idle)return false;
    const uint16_t bit=static_cast<uint16_t>(1U<<storage_slot);
    if(state.stored_army_asset_ids[storage_slot]==0U||(state.occupied_slot_mask&bit)!=0U)return false;
    state.occupied_slot_mask|=bit;
    if(state.available_stored_count!=0U)--state.available_stored_count;
    state.dispatch_phase=SimulationClass22DispatchPhase::Opening;
    return true;
}

void simulation_class22_release_linked_child_slot(SimulationClass22AirportState& state,
    std::size_t storage_slot) noexcept {
    if(storage_slot>=kSimulationClass22StoredSlotCount)return;
    const uint16_t bit=static_cast<uint16_t>(1U<<storage_slot);
    if((state.occupied_slot_mask&bit)==0U)return;
    state.occupied_slot_mask=static_cast<uint16_t>(state.occupied_slot_mask&~bit);
    if(state.stored_army_asset_ids[storage_slot]!=0U&&
       state.available_stored_count<state.stored_slot_count)++state.available_stored_count;
}

bool simulation_class22_tick_open(SimulationClass22AirportState& state,int32_t door_step_q12,
    uint32_t simulation_step_ticks) noexcept {
    if(state.dispatch_phase!=SimulationClass22DispatchPhase::Opening)return false;
    const int64_t next=static_cast<int64_t>(state.primary_texture_offset_v)+
        static_cast<int64_t>(door_step_q12)*simulation_step_ticks;
    if(next>0x7ffffLL){
        state.primary_texture_offset_v=0x80000;
        state.dispatch_phase=SimulationClass22DispatchPhase::RaisingPlatform;
        return true;
    }
    state.primary_texture_offset_v=static_cast<int32_t>(std::clamp<int64_t>(next,
        std::numeric_limits<int32_t>::min(),std::numeric_limits<int32_t>::max()));
    return false;
}

bool simulation_class22_tick_raise_platform(SimulationClass22AirportState& state,
    int32_t platform_step_q12,int32_t platform_ceiling_q12,uint32_t simulation_step_ticks) noexcept {
    if(state.dispatch_phase!=SimulationClass22DispatchPhase::RaisingPlatform)return false;
    const int64_t next=static_cast<int64_t>(state.platform_local_z_q12)+
        static_cast<int64_t>(platform_step_q12)*simulation_step_ticks;
    if(next>platform_ceiling_q12){
        state.platform_local_z_q12=platform_ceiling_q12;
        state.dispatch_phase=SimulationClass22DispatchPhase::WaitingForChildRelease;
        return true;
    }
    state.platform_local_z_q12=static_cast<int32_t>(std::clamp<int64_t>(next,
        std::numeric_limits<int32_t>::min(),std::numeric_limits<int32_t>::max()));
    return false;
}

bool simulation_class22_signal_child_release(SimulationClass22AirportState& state) noexcept {
    if(state.dispatch_phase!=SimulationClass22DispatchPhase::WaitingForChildRelease)return false;
    state.dispatch_phase=SimulationClass22DispatchPhase::LoweringPlatform;
    return true;
}

bool simulation_class22_tick_lower_platform(SimulationClass22AirportState& state,
    int32_t platform_step_q12,int32_t platform_baseline_q12,uint32_t simulation_step_ticks) noexcept {
    if(state.dispatch_phase!=SimulationClass22DispatchPhase::LoweringPlatform)return false;
    const int64_t next=static_cast<int64_t>(state.platform_local_z_q12)-
        static_cast<int64_t>(platform_step_q12)*simulation_step_ticks;
    if(next<platform_baseline_q12){
        state.platform_local_z_q12=platform_baseline_q12;
        state.dispatch_phase=SimulationClass22DispatchPhase::Closing;
        return true;
    }
    state.platform_local_z_q12=static_cast<int32_t>(std::clamp<int64_t>(next,
        std::numeric_limits<int32_t>::min(),std::numeric_limits<int32_t>::max()));
    return false;
}

bool simulation_class22_tick_close(SimulationClass22AirportState& state,int32_t door_step_q12,
    uint32_t simulation_step_ticks) noexcept {
    if(state.dispatch_phase!=SimulationClass22DispatchPhase::Closing)return false;
    const int64_t next=static_cast<int64_t>(state.primary_texture_offset_v)-
        static_cast<int64_t>(door_step_q12)*simulation_step_ticks;
    if(next<1LL){
        state.primary_texture_offset_v=0;
        state.dispatch_phase=SimulationClass22DispatchPhase::Reset;
        return true;
    }
    state.primary_texture_offset_v=static_cast<int32_t>(std::clamp<int64_t>(next,
        std::numeric_limits<int32_t>::min(),std::numeric_limits<int32_t>::max()));
    return false;
}

bool simulation_class22_reset_dispatch_cycle(SimulationClass22AirportState& state) noexcept {
    if(state.dispatch_phase!=SimulationClass22DispatchPhase::Reset)return false;
    state.dispatch_phase=SimulationClass22DispatchPhase::Idle;
    return true;
}

bool simulation_class21_begin_outbound_when_platform_raised(
    SimulationClass21LinkedChildState& child,SimulationClass22DispatchPhase parent_phase,
    int32_t release_countdown,int32_t outbound_countdown) noexcept {
    if(child.phase!=SimulationClass21LinkedChildPhase::Attached||
       parent_phase!=SimulationClass22DispatchPhase::WaitingForChildRelease)return false;
    child.phase=SimulationClass21LinkedChildPhase::OutboundRelease;
    child.release_countdown=release_countdown;
    child.outbound_countdown=outbound_countdown;
    return true;
}

bool simulation_class21_tick_outbound_release(SimulationClass21LinkedChildState& child,
    uint32_t simulation_step_ticks) noexcept {
    if(child.phase!=SimulationClass21LinkedChildPhase::OutboundRelease)return false;
    // Native take-off decrements each countdown once per callback, not per tick in the batch.
    (void)simulation_step_ticks;
    child.release_countdown=std::bit_cast<int32_t>(uint32_t(child.release_countdown)-1U);
    const bool edge=child.release_countdown==0;
    child.outbound_countdown=std::bit_cast<int32_t>(uint32_t(child.outbound_countdown)-1U);
    if(child.outbound_countdown<0)child.phase=SimulationClass21LinkedChildPhase::OffmapOutbound;
    return edge;
}

bool simulation_class21_begin_mission_pass(SimulationClass21LinkedChildState& child,
    int32_t mission_countdown) noexcept {
    if(child.phase!=SimulationClass21LinkedChildPhase::OffmapOutbound)return false;
    child.phase=SimulationClass21LinkedChildPhase::MissionPass;
    child.mission_countdown=mission_countdown;
    // State 4 stores MDL +0xC8 in ArmyRuntime +0x6C and twice that value in ownerValue68.
    // The editor call site supplies the latter (2N), so recover the exact +0x6C seed here.
    child.mission_emission_counter=mission_countdown/2;
    return true;
}

SimulationClass21MissionTickResult simulation_class21_tick_mission_pass_events(
    SimulationClass21LinkedChildState& child,int32_t model_point_step30,
    uint32_t simulation_step_ticks) noexcept {
    SimulationClass21MissionTickResult result;
    if(child.phase!=SimulationClass21LinkedChildPhase::MissionPass)return result;
    const int64_t step=static_cast<int64_t>(model_point_step30);
    constexpr std::array<uint8_t,7> kOrdinals{{7U,6U,5U,4U,3U,2U,1U}};
    for(uint32_t tick=0;tick<simulation_step_ticks;++tick){
        // Raw 0x00525E73/0x00525E79 order: +0x6C and ownerValue68 are decremented before
        // the mapped-terrain call and before the seven threshold comparisons.
        --child.mission_emission_counter;
        --child.mission_countdown;
        if(child.mission_countdown<0){
            child.phase=SimulationClass21LinkedChildPhase::OffmapReturn;
            result.completed=true;
        }
        ++result.mapped_terrain_sound_attempts;
        if(model_point_step30!=0){
            const int64_t counter=child.mission_emission_counter;
            for(std::size_t i=0;i<kOrdinals.size();++i){
                const int64_t multiplier=static_cast<int64_t>(i)-3LL; // -3 .. +3
                if(counter==multiplier*step&&result.emission_count<result.model_point_ordinals.size())
                    result.model_point_ordinals[result.emission_count++]=kOrdinals[i];
            }
        }
        // The retail switch entered state 5 once and finishes the current simulation-step batch
        // even if ownerValue68 crossed below zero. Preserve that for batched callers.
    }
    return result;
}

bool simulation_class21_tick_mission_pass(SimulationClass21LinkedChildState& child,
    uint32_t simulation_step_ticks) noexcept {
    return simulation_class21_tick_mission_pass_events(child,0,simulation_step_ticks).completed;
}

namespace {
constexpr std::array<int32_t,24> kAiCombatTargetClassBaseScoreTable{{
    0x000,0x800,0x1000,0x1000,0x200,0x000,0x000,0x000,
    0x000,0x000,0x180,0x100,0x000,0x080,0x400,0x380,
    0x300,0x1000,0x1800,0x1000,0x100,0x1000,0x0C0,0x040,
}};
constexpr int64_t kAiRadialClearanceWeight=0x600;
constexpr int64_t kAiCandidateCounterWeight=0x12000;
constexpr int64_t kAiSourceCounterWeight=0x20000;
constexpr int64_t kAiClassBaseScoreMultiplier=0x800;

uint32_t integer_sqrt_u64(uint64_t value) noexcept {
    // FixedMath_UInt64Sqrt returns the floor of the unsigned square root.
    uint64_t bit=uint64_t{1}<<62U;
    while(bit>value)bit>>=2U;
    uint64_t result=0U;
    while(bit!=0U){
        if(value>=result+bit){
            value-=result+bit;
            result=(result>>1U)+bit;
        }else result>>=1U;
        bit>>=2U;
    }
    return static_cast<uint32_t>(result);
}
}

uint32_t simulation_class21_candidate_score_full_integrity(
    const SimulationClass21CandidateScoreInput& input,uint32_t current_best_score) noexcept {
    (void)current_best_score;
    if(input.candidate_collision_radius_q12<=0||
       input.source_impact_class_index>=8U||input.candidate_impact_class_index>=8U||
       input.candidate_runtime_class_id>=kAiCombatTargetClassBaseScoreTable.size())return 0U;

    const int64_t range=static_cast<int64_t>(input.candidate_collision_radius_q12)+0x2000LL;
    const int64_t dx=static_cast<int64_t>(input.source_world_x_q12)-input.candidate_world_x_q12;
    const int64_t dy=static_cast<int64_t>(input.source_world_y_q12)-input.candidate_world_y_q12;
    int64_t remaining=range*range-dx*dx;
    if(remaining<0)return 0U;
    remaining-=dy*dy;
    if(remaining<0)return 0U;
    const uint32_t radial_clearance=integer_sqrt_u64(static_cast<uint64_t>(remaining));

    int64_t source_counter=input.candidate_target_class_damage_q12[input.source_impact_class_index];
    int64_t candidate_counter=input.source_target_class_damage_q12[input.candidate_impact_class_index];
    if(candidate_counter==0)return 0U;
    if(candidate_counter<0){source_counter=0;candidate_counter=-candidate_counter;}

    int64_t score=(static_cast<int64_t>(radial_clearance)*kAiRadialClearanceWeight)/
        input.candidate_collision_radius_q12;
    score+=static_cast<int64_t>(kAiCombatTargetClassBaseScoreTable[input.candidate_runtime_class_id])*
        kAiClassBaseScoreMultiplier;
    // Full-integrity ModelRuntimeHierarchy_QueryScaleRatio returns EAX=EDX=0x1000.
    score+=(kAiSourceCounterWeight*source_counter)/0x1000LL;
    score+=(kAiCandidateCounterWeight*candidate_counter)/0x1000LL;
    if(source_counter==0)score>>=2;
    if(score<=0)return 0U;
    return static_cast<uint32_t>(std::min<int64_t>(score,std::numeric_limits<uint32_t>::max()));
}

uint32_t simulation_class21_apply_candidate_line_of_fire_result(uint32_t raw_score,
    uint32_t current_best_score,bool line_of_fire_clear,bool source_runtime_field18_nonzero) noexcept {
    if(raw_score<=current_best_score||line_of_fire_clear)return raw_score;
    const uint32_t obstructed_score=raw_score>>2U;
    return source_runtime_field18_nonzero?obstructed_score:0U;
}

bool simulation_class21_candidate_within_projectile_lock(
    int32_t source_world_x_q12,int32_t source_world_y_q12,
    int32_t candidate_world_x_q12,int32_t candidate_world_y_q12,
    int32_t candidate_collision_radius_q12) noexcept {
    const int64_t dx=static_cast<int64_t>(candidate_world_x_q12)-source_world_x_q12;
    const int64_t dy=static_cast<int64_t>(candidate_world_y_q12)-source_world_y_q12;
    const uint64_t squared=static_cast<uint64_t>(dx*dx+dy*dy);
    const int64_t distance=static_cast<int64_t>(integer_sqrt_u64(squared));
    return distance-static_cast<int64_t>(candidate_collision_radius_q12)<0x1001LL;
}

bool simulation_class21_mapped_terrain_sound_gate(uint32_t source_faction_index,
    uint32_t active_faction_index,bool capability_bit_clear,uint8_t active_faction_occupancy_byte,
    uint32_t relation_transition_tick) noexcept {
    return source_faction_index!=active_faction_index&&capability_bit_clear&&
        (active_faction_occupancy_byte&0x10U)!=0U&&relation_transition_tick>0x10U;
}

bool simulation_class21_begin_return_when_parent_idle(SimulationClass21LinkedChildState& child,
    SimulationClass22AirportState& parent,int32_t return_countdown) noexcept {
    if(child.phase!=SimulationClass21LinkedChildPhase::OffmapReturn||
       parent.dispatch_phase!=SimulationClass22DispatchPhase::Idle)return false;
    parent.dispatch_phase=SimulationClass22DispatchPhase::Opening;
    child.phase=SimulationClass21LinkedChildPhase::ReturningToPlatform;
    child.return_countdown=return_countdown;
    return true;
}

bool simulation_class21_tick_return_to_platform(SimulationClass21LinkedChildState& child,
    SimulationClass22AirportState& parent,uint32_t simulation_step_ticks) noexcept {
    if(child.phase!=SimulationClass21LinkedChildPhase::ReturningToPlatform)return false;
    (void)simulation_step_ticks;
    child.return_countdown=std::bit_cast<int32_t>(uint32_t(child.return_countdown)-1U);
    if(child.return_countdown<0){
        child.phase=SimulationClass21LinkedChildPhase::Attached;
        parent.dispatch_phase=SimulationClass22DispatchPhase::LoweringPlatform;
        return true;
    }
    return false;
}

uint32_t fixed_atan2_angle16_compat(int32_t y,int32_t x) noexcept {
    // Exact integer polynomial from 0x00484BA0 FixedMath_Atan2Angle16.  All primitive
    // add/sub/neg operations are expressed through uint32 so malformed editor inputs retain
    // x86 two's-complement wrap instead of invoking C++ signed-overflow undefined behaviour.
    const auto add=[](int32_t a,int32_t b)->int32_t{
        return static_cast<int32_t>(static_cast<uint32_t>(a)+static_cast<uint32_t>(b));
    };
    const auto sub=[](int32_t a,int32_t b)->int32_t{
        return static_cast<int32_t>(static_cast<uint32_t>(a)-static_cast<uint32_t>(b));
    };
    const auto neg=[](int32_t a)->int32_t{
        return static_cast<int32_t>(0U-static_cast<uint32_t>(a));
    };
    const auto twice=[&](int32_t a)->int32_t{return add(a,a);};
    int32_t base=0;
    int32_t denominator=twice(x);
    const int32_t twice_y=twice(y);
    uint32_t numerator=0U;
    if(sub(twice_y,x)==0||twice_y<x){
        if(denominator<neg(y)){
            if(y<denominator){
                base=-0x4000; denominator=neg(y); numerator=static_cast<uint32_t>(x);
            }else{
                numerator=static_cast<uint32_t>(sub(x,y)); base=-0x6000;
                denominator=neg(add(x,y));
            }
        }else{
            denominator=x; numerator=static_cast<uint32_t>(y);
            if(twice_y<neg(x)){
                numerator=static_cast<uint32_t>(add(x,y)); base=-0x2000;
                denominator=sub(x,y);
            }
        }
    }else if(denominator<neg(y)){
        if(twice_y<neg(x)){
            base=0x8000; denominator=neg(x); numerator=static_cast<uint32_t>(neg(y));
            if(static_cast<int32_t>(numerator)>0)base=-0x8000;
        }else{
            denominator=sub(y,x); base=0x6000; numerator=static_cast<uint32_t>(neg(add(x,y)));
        }
    }else if(y<denominator){
        denominator=add(x,y); base=0x2000; numerator=static_cast<uint32_t>(sub(y,x));
    }else{
        base=0x4000; numerator=static_cast<uint32_t>(neg(x)); denominator=y;
    }
    const int32_t twice_denominator=twice(denominator);
    if(twice_denominator==0)return static_cast<uint32_t>(base);
    const int64_t scaled_numerator=static_cast<int64_t>(static_cast<int32_t>(numerator))*
        (int64_t{1}<<32);
    const int32_t ratio=static_cast<int32_t>(scaled_numerator/twice_denominator);
    const auto mul_hi=[](int32_t a,int32_t b)->int32_t{
        return static_cast<int32_t>((static_cast<int64_t>(a)*static_cast<int64_t>(b))>>32);
    };
    const int32_t ratio_sq_hi=mul_hi(ratio,ratio);
    int32_t poly=add(mul_hi(ratio_sq_hi,0x104c2),-0x6ca6);
    poly=add(mul_hi(ratio_sq_hi,poly),0x517d);
    return static_cast<uint32_t>(add(base,mul_hi(ratio,poly)));
}

uint32_t simulation_class21_flight_pitch_turn16(int32_t motion_phase_q12,
    int32_t arc_coefficient_q12,uint32_t simulation_step_ticks) noexcept {
    // 0x005263xx class-21 states 2/3/5 form a signed 64-bit product and feed product>>11
    // to FixedMath_Atan2Angle16 with X=0x1000. Saturation affects malformed editor state only.
    const int64_t product=static_cast<int64_t>(motion_phase_q12)*
        static_cast<int64_t>(simulation_step_ticks)*static_cast<int64_t>(arc_coefficient_q12);
    const int32_t slope_q12=static_cast<int32_t>(std::clamp<int64_t>(product>>11,
        std::numeric_limits<int32_t>::min(),std::numeric_limits<int32_t>::max()));
    const uint32_t angle=fixed_atan2_angle16_compat(slope_q12,0x1000);
    return wrap_turn16(0x4000LL-static_cast<int64_t>(static_cast<int32_t>(angle)));
}

uint32_t simulation_validate_texture_layer_capacity(
    std::size_t requested_layers,uint32_t device_max_layers) {
    if(device_max_layers==0U)throw std::runtime_error("Vulkan device reports zero image-array layers");
    if(requested_layers>device_max_layers){
        throw std::runtime_error("object/effect texture array requires "+
            std::to_string(requested_layers)+" layers but the selected Vulkan device supports "+
            std::to_string(device_max_layers));
    }
    if(requested_layers>std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("object/effect texture layer count exceeds uint32 range");
    return static_cast<uint32_t>(requested_layers);
}

} // namespace thandor
