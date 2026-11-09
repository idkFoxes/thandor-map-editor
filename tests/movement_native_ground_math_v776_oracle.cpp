// Compiled with original drive_ground.cpp included by the oracle build driver.
#include "thandor/simulation.hpp"
#include <random>
#include <iostream>
#include <stdexcept>
InGameSimulationStepBatchTicks g_InGameSimulationStepTicks=1;
void Thandor_Ptr32Overflow(uintptr_t){std::abort();}
int main(){
 FixedMath_BuildSinCosTables();std::mt19937 rng(0x776);size_t checks=0;
 for(unsigned trial=0;trial<240;++trial)for(unsigned batch:{1U,5U}){
  g_InGameSimulationStepTicks=batch;
  ModelRuntimeGroundMovementSteeringView native{};ModelDefinitionGroundMovementSteeringView definition{};
  definition.turnRateLimitAnglePerTick=trial%17==0?0:768;
  definition.turnRateAccelerationAnglePerTick=trial%19==0?0:330;
  thandor::SimulationMotionProfile profile;profile.ground_speed_control=true;profile.speed_q12_per_tick=168;
  profile.ground_acceleration_q12=123;profile.yaw_max_velocity_turn16=definition.turnRateLimitAnglePerTick;
  profile.yaw_acceleration_turn16=definition.turnRateAccelerationAnglePerTick;
  profile.heading_interpolation_distance_q12=6000;profile.far_heading_error_limit_turn16=8192;profile.near_heading_error_limit_turn16=256;
  thandor::SimulationActorState actor;int32_t x=235785,y=-130466;
  int32_t tx=trial%23==0?x-200000:x+int32_t(rng()%600000)-300000;
  int32_t ty=trial%23==0?y:y+int32_t(rng()%600000)-300000;
  if(std::abs(tx-x)+std::abs(ty-y)<200000)tx+=(tx<x?-200000:200000);
  uint32_t yaw=trial%23==0?0:rng()&65535U,advance=0;
  actor.position={thandor::q12_to_float(x),thandor::q12_to_float(y),0};actor.body_yaw_turn16=yaw;
  actor.move_target=thandor::Vec3f{thandor::q12_to_float(tx),thandor::q12_to_float(ty),0};actor.move_waypoint=actor.move_target;
  for(unsigned tick=0;tick<80;++tick){
   const auto vector=FixedMath_Vector2AngleAndLength(ty-y,tx-x);
   yaw=ArmyGroundMovement_TurnTowardsHeading(&native,&definition,yaw,vector.angle)&65535U;
   const auto error=(yaw-vector.angle)&65535U;
   if(8192U<error&&error<65536U-8192U)advance=0;
   else {advance=std::min(168U,advance+123U);const auto p=FixedTrig_ProjectPlanarPoint(x,y,int32_t(advance*batch),yaw);x=p.xQ12;y=p.yQ12;}
   thandor::simulation_tick_actor_motion(actor,profile,batch);
   ++checks;
   if(thandor::float_to_q12(actor.position.x)!=x||thandor::float_to_q12(actor.position.y)!=y||actor.body_yaw_turn16!=yaw||actor.body_turn_velocity_turn16!=native.movementControl.turnVelocityAngle16){
    std::cerr<<"Ground math mismatch trial="<<trial<<" tick="<<tick<<" batch="<<batch<<" native="<<x<<","<<y<<","<<yaw<<","<<native.movementControl.turnVelocityAngle16<<" lab="<<thandor::float_to_q12(actor.position.x)<<","<<thandor::float_to_q12(actor.position.y)<<","<<actor.body_yaw_turn16<<","<<actor.body_turn_velocity_turn16<<" advance="<<advance<<","<<actor.ground_advance_q12<<" target="<<tx<<","<<ty<<'\n';return 1;}
  }
 }
 std::cout<<"NATIVE_GROUND_MATH_V776 PASS callback_states="<<checks<<" original turn, angle/length and Q28 projection\n";
}
