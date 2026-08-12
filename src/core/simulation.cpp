#include "thandor/simulation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <stdexcept>

namespace thandor {
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
        const float speed2=speed*speed;
        const float discriminant=speed2*speed2-gravity*(gravity*horizontal*horizontal+2.0F*dz*speed2);
        if(discriminant<0.0F){result.valid=false;return result;}
        // The recovered game path chooses the high ballistic solution.
        const float tangent=(speed2+std::sqrt(discriminant))/(gravity*horizontal);
        result.pitch_turn16=radians_to_turn16(std::atan(tangent));
        return result;
    }
    auto direct=simulation_target_angles(source,target,shot.elevation_offset_angle16);
    const int32_t clamped=std::clamp(signed_pitch(direct.pitch_turn16),-0x4000,0x4000);
    direct.pitch_turn16=wrap_turn16(clamped);
    return direct;
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

bool simulation_tick_actor_motion(SimulationActorState& actor,
                                  const SimulationMotionProfile& motion) noexcept {
    if(!actor.move_target||!motion.movable())return false;
    const Vec3f destination=*actor.move_target;
    const float dx=destination.x-actor.position.x,dy=destination.y-actor.position.y;
    const float distance=std::hypot(dx,dy);
    const float speed=std::max(q12_to_float(std::abs(motion.speed_q12_per_tick)),1.0F/4096.0F);
    if(distance<=speed){
        actor.position.x=destination.x;actor.position.y=destination.y;actor.move_target.reset();return true;
    }
    const uint32_t target_yaw=radians_to_turn16(std::atan2(dy,dx));
    SimulationAxisState body{actor.body_yaw_turn16,actor.body_turn_velocity_turn16};
    const bool aligned=simulation_step_yaw(body,target_yaw,motion.yaw_max_velocity_turn16,
                                           motion.yaw_acceleration_turn16);
    actor.body_yaw_turn16=body.angle_turn16;actor.body_turn_velocity_turn16=body.velocity_turn16;
    if(aligned||std::abs(shortest_turn16_delta(actor.body_yaw_turn16,target_yaw))<0x1800){
        const float step=std::min(distance,speed);
        actor.position.x+=std::cos(turn16_to_radians(actor.body_yaw_turn16))*step;
        actor.position.y+=std::sin(turn16_to_radians(actor.body_yaw_turn16))*step;
        return true;
    }
    return false;
}

bool simulation_tick_articulated_actor_motion(
    SimulationActorState& actor,const SimulationMotionProfile& motion,
    int32_t contact_interpolation_rate_q12) noexcept {
    if(!actor.move_target||!motion.movable())return false;
    const Vec3f destination=*actor.move_target;
    const float dx=destination.x-actor.position.x;
    const float dy=destination.y-actor.position.y;
    const float distance=std::hypot(dx,dy);
    const float speed=std::max(q12_to_float(std::abs(motion.speed_q12_per_tick)),1.0F/4096.0F);
    if(distance<=speed){
        actor.position.x=destination.x;
        actor.position.y=destination.y;
        actor.move_target.reset();
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
    const int32_t applied_delta=std::clamp(yaw_delta,-derived_step,derived_step);
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
int32_t wrap_model_texture_offset(int64_t value) noexcept {
    constexpr int64_t period = 0x100000;
    while (value > period) value -= period;
    while (value < -period) value += period;
    return static_cast<int32_t>(value);
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
        static_cast<int64_t>(actor.primary_track_texture_offset_u) +
        static_cast<int64_t>(primary_distance) * texture_travel_scale);
    actor.secondary_track_texture_offset_u = wrap_model_texture_offset(
        static_cast<int64_t>(actor.secondary_track_texture_offset_u) +
        static_cast<int64_t>(secondary_distance) * texture_travel_scale);
}

void simulation_update_articulated_contact_transition(
    SimulationActorState& actor,Vec3f previous_position,uint32_t previous_yaw_turn16,
    Vec3f current_position,uint32_t current_yaw_turn16,
    int32_t contact_interpolation_rate_q12) noexcept {
    const float distance=std::hypot(current_position.x-previous_position.x,
                                    current_position.y-previous_position.y);
    const uint32_t yaw_work=static_cast<uint32_t>(std::abs(
        shortest_turn16_delta(previous_yaw_turn16,current_yaw_turn16)));
    if(distance<=1.0e-8F&&yaw_work==0U){
        actor.articulated_was_moving=false;
        actor.articulated_turning=false;
        actor.articulated_contact_transition_active=false;
        return;
    }
    const int32_t distance_q12=static_cast<int32_t>(std::lround(distance*4096.0F));
    const int32_t rate=std::max(std::abs(contact_interpolation_rate_q12),1);
    const uint32_t travel_advance=static_cast<uint32_t>(std::max<int64_t>(0,
        (static_cast<int64_t>(distance_q12)*0x1000LL)/rate));
    // A 0x0800 body-heading change is enough to request a new articulated contact in the binary.
    // Convert heading work into the same 0x1000 interpolation domain so pivot turns alternate feet.
    const uint32_t turn_advance=static_cast<uint32_t>(
        (static_cast<uint64_t>(yaw_work)*0x1000ULL)/0x800ULL);
    uint32_t progress=actor.articulated_contact_progress_q12+
        std::max<uint32_t>(1U,travel_advance+turn_advance);
    while(progress>=0x1000U){
        progress-=0x1000U;
        actor.articulated_swing_side^=1U;
    }
    actor.articulated_contact_progress_q12=progress;
    actor.articulated_gait_phase_turn16=(progress<<4U)|
        ((actor.articulated_swing_side&1U)!=0U?0x8000U:0U);
    actor.articulated_contact_transition_active=true;
    actor.articulated_was_moving=true;
    actor.articulated_turning=yaw_work!=0U;
}


void simulation_update_articulated_gait_phase(
    SimulationActorState& actor,Vec3f previous_position,Vec3f current_position,
    int32_t contact_interpolation_rate_q12) noexcept {
    simulation_update_articulated_contact_transition(
        actor,previous_position,actor.body_yaw_turn16,current_position,actor.body_yaw_turn16,
        contact_interpolation_rate_q12);
}

void simulation_initialize_class17_deployment(
    SimulationActorState& actor,bool freshly_placed) noexcept {
    actor.class17_bank_angle_turn16=0x4000U;
    actor.class17_bank_heading_turn16=actor.body_yaw_turn16;
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
    uint32_t available_launch_attachment_selector_mask) noexcept {
    SimulationWeaponTickResult result;
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
        state.recoil_angle_offset_turn16+=weapon.recoil_rotation_step_turn16;
        --state.recoil_rotation_ticks;
        if(state.recoil_rotation_ticks==0U)state.recoil_angle_offset_turn16=0;
    }
    if(state.recoil_translation_ticks>0U){
        state.recoil_translation_offset_q12=-weapon.recoil_translation_q12;
        --state.recoil_translation_ticks;
        if(state.recoil_translation_ticks==0U)state.recoil_translation_offset_q12=0;
    }
    state.target_in_range=false;state.target_aligned=false;state.line_of_fire_clear=line_of_fire_clear;
    state.last_fired_attachment_selector=0xFFFFFFFFU;
    if(!target||!weapon.valid())return result;
    state.target_in_range=simulation_target_within_range(aim_origin,*target,weapon);
    result.in_range=state.target_in_range;result.line_of_fire_clear=line_of_fire_clear;
    if(!state.target_in_range)return result;
    const auto aim=simulation_shot_launch_angles(aim_origin,*target,shot);
    if(!aim.valid)return result;
    const uint32_t relative_yaw=wrap_turn16(static_cast<int64_t>(aim.yaw_turn16)-actor.body_yaw_turn16);
    const bool yaw_aligned=simulation_step_yaw(state.yaw,relative_yaw,
        weapon.yaw_max_velocity_turn16,weapon.yaw_acceleration_turn16);
    const bool pitch_aligned=simulation_step_pitch(state.pitch,aim.pitch_turn16,
        weapon.pitch_max_velocity_turn16,weapon.pitch_acceleration_turn16,
        weapon.pitch_min_turn16,weapon.pitch_max_turn16);
    state.target_aligned=yaw_aligned&&pitch_aligned;
    result.aligned=state.target_aligned;result.turned=true;
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
        state.recoil_translation_ticks=state.recoil_rotation_ticks;
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

std::vector<SimulationProjectileState> simulation_create_volley(
    std::size_t owner_placement_index, std::size_t weapon_profile_index,
    const SimulationWeaponProfile& weapon, std::span<const Vec3f> launch_positions,
    Vec3f target_position, const ShotDefinitionRecord& shot, uint32_t spawn_tick,
    uint64_t& next_serial, uint32_t fired_attachment_selector,
    std::optional<std::size_t> live_target_placement_index) {
    std::vector<SimulationProjectileState> result;
    result.reserve(launch_positions.size());

    const float speed = std::max(1.0F / 4096.0F,
                                 std::abs(q12_to_float(shot.launch_speed_q12)));
    for (const Vec3f launch : launch_positions) {
        const auto aim = simulation_shot_launch_angles(launch, target_position, shot);
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
                                    0xFFFFFFFFU, std::nullopt);
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
    const uint32_t age = current_tick - projectile.spawn_tick;
    if (shot.trajectory_mode == 0) {
        // Mode zero is a stationary launch-node model stretched to the ray hit by render state.
        return projectile.launch_position;
    }

    const Vec3f target = live_target_position ? *live_target_position : projectile.target_position;
    if (shot.trajectory_mode == 2) {
        return fixed_range_projectile_position(projectile, shot, age, target);
    }
    if (age >= projectile.travel_ticks) return target;

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
    projectile.target_position = target_position;
    if (projectile.impact_kind == SimulationImpactKind::TargetModelImpactClass) {
        projectile.impact_position = target_position;
    }
    if (shot.trajectory_mode == 2) {
        projectile.travel_ticks = fixed_range_target_reach_tick(projectile, shot, target_position);
    }
}

bool simulation_projectile_impacted(const SimulationProjectileState& projectile,
                                    uint32_t current_tick) noexcept {
    return current_tick-projectile.spawn_tick>=projectile.travel_ticks;
}

bool simulation_projectile_expired(const SimulationProjectileState& projectile,
                                   uint32_t current_tick) noexcept {
    const uint32_t age=current_tick-projectile.spawn_tick;
    // A mode-zero beam model remains linked for its serialized SHT lifetime while the one-shot
    // impact EFF can already be active. Retaining every projectile until the later of the model
    // lifetime and impact-EFF lifetime also avoids truncating this exact overlap.
    const uint32_t impact_end=projectile.impact_kind!=SimulationImpactKind::None
        ?projectile.travel_ticks+projectile.impact_visual_lifetime_ticks:0U;
    return age>=std::max(projectile.lifetime_ticks,impact_end);
}

namespace {
struct SurfaceProbe {
    bool inside{};
    float terrain_height{};
    float secondary_height{};
    bool secondary_present{};
    uint32_t material{};
};
SurfaceProbe probe_surface(const FieldGrid& field, Vec3f point) noexcept {
    const auto sample=field.sample_terrain(point.x,point.y);
    if(!sample.inside||field.width()==0U||field.height()==0U)return {};
    const auto gx=static_cast<uint32_t>(std::clamp(std::lround(sample.grid_x),0L,
        static_cast<long>(field.width()-1U)));
    const auto gy=static_cast<uint32_t>(std::clamp(std::lround(sample.grid_y),0L,
        static_cast<long>(field.height()-1U)));
    const auto& cell=field.at(gx,gy);
    return {true,sample.height,cell.water_height(),cell.water_delta_q12>=0,
            static_cast<uint32_t>(cell.material())};
}
}

void simulation_resolve_projectile_surface_impact(
    SimulationProjectileState& projectile,const ShotDefinitionRecord& shot,
    const FieldGrid& field) noexcept {
    projectile.impact_kind=SimulationImpactKind::None;
    projectile.impact_position=projectile.target_position;
    projectile.impact_material_index=0U;
    projectile.impact_effect_id=0U;
    const uint32_t original_travel=std::max(1U,projectile.travel_ticks);
    const auto path_point=[&](float age)->Vec3f{
        if(shot.trajectory_mode==0){
            const float t=std::clamp(age/static_cast<float>(original_travel),0.0F,1.0F);
            return {projectile.launch_position.x+(projectile.target_position.x-projectile.launch_position.x)*t,
                    projectile.launch_position.y+(projectile.target_position.y-projectile.launch_position.y)*t,
                    projectile.launch_position.z+(projectile.target_position.z-projectile.launch_position.z)*t};
        }
        const uint32_t whole=static_cast<uint32_t>(std::floor(std::max(0.0F,age)));
        const float fraction=std::clamp(age-static_cast<float>(whole),0.0F,1.0F);
        const Vec3f a=simulation_projectile_position(projectile,shot,projectile.spawn_tick+whole);
        const Vec3f b=simulation_projectile_position(projectile,shot,
            projectile.spawn_tick+std::min(whole+1U,original_travel));
        return {a.x+(b.x-a.x)*fraction,a.y+(b.y-a.y)*fraction,a.z+(b.z-a.z)*fraction};
    };
    const float horizontal=std::hypot(projectile.target_position.x-projectile.launch_position.x,
                                      projectile.target_position.y-projectile.launch_position.y);
    const uint32_t minimum_substeps=std::min(16384U,
        original_travel>4096U?16384U:original_travel*4U);
    const uint32_t requested_substeps=static_cast<uint32_t>(std::min(16384.0F,
        std::ceil(horizontal/0.0625F)));
    const uint32_t substeps=std::clamp(requested_substeps,minimum_substeps,16384U);
    float previous_age=0.0F;
    Vec3f previous=path_point(0.0F);
    for(uint32_t step=1U;step<=substeps;++step){
        const float age=static_cast<float>(original_travel)*static_cast<float>(step)/
                        static_cast<float>(substeps);
        const Vec3f current=path_point(age);
        const auto probe=probe_surface(field,current);
        const bool hits_secondary=probe.inside&&probe.secondary_present&&
                                  previous.z>probe.secondary_height&&current.z<=probe.secondary_height;
        const bool hits_terrain=probe.inside&&previous.z>probe.terrain_height&&current.z<=probe.terrain_height;
        if(hits_secondary||hits_terrain){
            const float surface=hits_secondary?probe.secondary_height:probe.terrain_height;
            const float denominator=previous.z-current.z;
            const float local=std::abs(denominator)>1.0e-7F
                ?std::clamp((previous.z-surface)/denominator,0.0F,1.0F):1.0F;
            projectile.impact_position={previous.x+(current.x-previous.x)*local,
                                        previous.y+(current.y-previous.y)*local,surface};
            projectile.impact_material_index=probe.material;
            projectile.impact_kind=hits_secondary?SimulationImpactKind::SecondarySurface:
                                                       SimulationImpactKind::TerrainSurface;
            projectile.impact_effect_id=hits_secondary?shot.primary_effect_id:
                shot.terrain_impact_effect_id(probe.material);
            projectile.travel_ticks=std::max(1U,static_cast<uint32_t>(std::ceil(
                previous_age+(age-previous_age)*local)));
            projectile.target_position=projectile.impact_position;
            return;
        }
        previous=current; previous_age=age;
    }
    const auto endpoint=probe_surface(field,projectile.target_position);
    if(endpoint.inside){
        const bool secondary=endpoint.secondary_present&&
            projectile.target_position.z>=endpoint.terrain_height&&
            projectile.target_position.z<=endpoint.secondary_height+0.03125F;
        projectile.impact_kind=secondary?SimulationImpactKind::SecondarySurface:
                                          SimulationImpactKind::TerrainSurface;
        projectile.impact_material_index=endpoint.material;
        projectile.impact_effect_id=secondary?shot.primary_effect_id:
            shot.terrain_impact_effect_id(endpoint.material);
        projectile.impact_position.z=secondary?endpoint.secondary_height:endpoint.terrain_height;
        projectile.target_position=projectile.impact_position;
    }
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
