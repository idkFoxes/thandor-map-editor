#include "thandor/projectile_runtime.hpp"
#include "thandor/retail_contact.hpp"
#include "thandor/retail_placement.hpp"
#include <algorithm>
#include <bit>
#include <limits>
namespace thandor {
namespace {
int32_t signed_word(uint32_t v){return std::bit_cast<int16_t>(uint16_t(v));}
int32_t wrap(uint32_t v){return std::bit_cast<int32_t>(v);}
int32_t add(int32_t a,int32_t b){return wrap(uint32_t(a)+uint32_t(b));}
int32_t subtract(int32_t a,int32_t b){return wrap(uint32_t(a)-uint32_t(b));}
uint64_t square(int32_t value){return uint64_t(int64_t(value)*value);}
uint32_t planar_length(int32_t x,int32_t y){return retail_uint64_sqrt(square(x)+square(y));}
SimulationGuidanceAngles angles(int32_t x,int32_t y,int32_t z){
 return {int32_t(fixed_atan2_angle16_compat(y,x)&65535U),
         signed_word(fixed_atan2_angle16_compat(z,int32_t(planar_length(x,y))))};
}
}
std::array<int32_t,3> simulation_fixed_direction_q12(int32_t pitch,int32_t yaw,int32_t speed) noexcept {
 const uint32_t e=uint32_t(pitch)&65535U,a=(uint32_t(yaw)+e)&65535U,b=(uint32_t(yaw)-e)&65535U;
 const int32_t x=(retail_placement_sine_table_entry(32768+a)+retail_placement_sine_table_entry(32768+b));
 const int32_t y=(retail_placement_sine_table_entry(16384+a)+retail_placement_sine_table_entry(16384+b));
 const int32_t z=retail_placement_sine_table_entry(16384+e);
 return {int32_t((int64_t(x)*speed)>>29),int32_t((int64_t(y)*speed)>>29),int32_t((int64_t(z)*speed)>>28)};
}
SimulationGuidanceAngles simulation_guidance_angles(SimulationGuidanceAngles current,
    SimulationGuidanceAngles target,int32_t maximum_turn) noexcept {
 // Native elevation subtraction is signed32; only the heading delta is wrapped to signed16.
 if(maximum_turn<=0)return current;
 const int32_t dyaw=signed_word(uint32_t(target.yaw)-uint32_t(current.yaw));
 const int32_t dpitch=subtract(target.pitch,current.pitch);
 return {int32_t(uint32_t(add(current.yaw,std::clamp(dyaw,-maximum_turn,maximum_turn)))&65535U),
     add(current.pitch,std::clamp(dpitch,-maximum_turn,maximum_turn))};
}
bool simulation_begin_projectile_runtime_step(SimulationProjectileState& p,
    const ShotDefinitionRecord& shot,uint32_t tick) noexcept {
 if(shot.trajectory_mode==0||p.runtime_flight_expired)return false;
 if(!p.runtime_flight_initialized){
  p.runtime_flight_initialized=true;p.runtime_flight_tick=p.spawn_tick;p.runtime_flight_age=0;
  p.runtime_flight_position=p.launch_position;p.runtime_flight_yaw=int32_t(p.launch_yaw_turn16);
  p.runtime_flight_pitch=signed_word(p.launch_pitch_turn16);p.runtime_elevation_offset=shot.elevation_offset_angle16;
  p.runtime_flight_speed_q12=shot.trajectory_ramp_duration_ticks?0:shot.launch_speed_q12;
  p.runtime_ballistic_direction=simulation_fixed_direction_q12(p.runtime_flight_pitch,p.runtime_flight_yaw,shot.launch_speed_q12);
  if(shot.trajectory_mode==1)p.runtime_ballistic_direction[2]=add(p.runtime_ballistic_direction[2],shot.ballistic_divisor_q12>>1);
  p.travel_ticks=std::max(1U,shot.projectile_lifetime_ticks);p.lifetime_ticks=p.travel_ticks;
  // The launch-time preview must not turn an aim point into a guaranteed runtime hit.
  p.impact_kind=SimulationImpactKind::None;p.impact_target_placement_index.reset();p.impact_target_model_component_index.reset();
 }
 p.runtime_flight_tick=tick;++p.runtime_flight_age;
 if(p.runtime_flight_age>=std::max(1U,shot.projectile_lifetime_ticks)){p.runtime_flight_expired=true;return false;}
 p.runtime_flight_previous=p.runtime_flight_position;
 const auto d=simulation_fixed_direction_q12(subtract(p.runtime_flight_pitch,p.runtime_elevation_offset),p.runtime_flight_yaw,p.runtime_flight_speed_q12);
 p.runtime_flight_ray_end={q12_to_float(add(float_to_q12(p.runtime_flight_position.x),d[0])),
     q12_to_float(add(float_to_q12(p.runtime_flight_position.y),d[1])),q12_to_float(add(float_to_q12(p.runtime_flight_position.z),d[2]))};
 return true;
}
void simulation_finish_projectile_runtime_step(SimulationProjectileState& p,
    const ShotDefinitionRecord& shot,const Vec3f* live_aim,const Vec3f* live_root) noexcept {
 p.runtime_flight_position=p.runtime_flight_ray_end;
 if(shot.mode3_lead_disabled!=0U&&live_aim){
  const auto target=angles(subtract(float_to_q12(live_aim->x),float_to_q12(p.runtime_flight_position.x)),
      subtract(float_to_q12(live_aim->y),float_to_q12(p.runtime_flight_position.y)),
      subtract(float_to_q12(live_aim->z),float_to_q12(p.runtime_flight_position.z)));
  const auto steered=simulation_guidance_angles({p.runtime_flight_yaw,p.runtime_flight_pitch},target,
      std::bit_cast<int32_t>(shot.mode3_lead_disabled));
  p.runtime_flight_yaw=steered.yaw;p.runtime_flight_pitch=steered.pitch;
 }
 const uint32_t age=p.runtime_flight_age,ramp=shot.trajectory_ramp_duration_ticks;
 if(shot.trajectory_mode==1){
  auto& d=p.runtime_ballistic_direction;d[2]=subtract(d[2],shot.ballistic_divisor_q12);
  const auto a=angles(d[0],d[1],d[2]);p.runtime_flight_yaw=a.yaw;p.runtime_flight_pitch=a.pitch;
  p.runtime_flight_speed_q12=int32_t(retail_uint64_sqrt(square(d[0])+square(d[1])+square(d[2])));
 } else if(shot.trajectory_mode==3&&ramp>0&&ramp<=INT32_MAX){
  const int64_t clamped=std::min(age,ramp);
  p.runtime_flight_speed_q12=int32_t(((clamped*clamped/int32_t(ramp))*shot.launch_speed_q12)/int32_t(ramp));
  if(shot.elevation_offset_angle16){
   const uint32_t window=ramp*2U;const int32_t left=age>window?0:int32_t(window-age);
   const int32_t next=int32_t((int64_t(left)*shot.elevation_offset_angle16)/int32_t(ramp))>>1;
   p.runtime_flight_pitch=add(p.runtime_flight_pitch,subtract(next,p.runtime_elevation_offset));p.runtime_elevation_offset=next;
  }
 } else if(shot.trajectory_mode==2){
  const auto shift=[&](){if(live_root){p.runtime_flight_position.x=live_root->x;p.runtime_flight_position.y=live_root->y;}};
  if(p.runtime_flight_pitch<0)shift();
  else {
   if(ramp>0&&ramp<=INT32_MAX){const int64_t clamped=std::min(age,ramp);
    p.runtime_flight_speed_q12=int32_t(((((clamped*clamped/int32_t(ramp))*clamped)/int32_t(ramp))*shot.launch_speed_q12)/int32_t(ramp));}
   if(age>=shot.fixed_range_transition_age_threshold_ticks){
    if(!live_root)p.runtime_flight_expired=true;
    else {shift();p.runtime_flight_pitch=wrap(0U-uint32_t(p.runtime_flight_pitch));}
   }
  }
 }
 p.launch_yaw_turn16=uint32_t(p.runtime_flight_yaw)&65535U;p.launch_pitch_turn16=uint32_t(p.runtime_flight_pitch)&65535U;
}
}
