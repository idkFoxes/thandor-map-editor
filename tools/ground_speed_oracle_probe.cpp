#include "thandor/simulation.hpp"
#include <bit>
#include <iostream>
int main(){
 uint32_t speed,acceleration,current,elevation,azimuth,heading,low,high;
 while(std::cin>>speed>>acceleration>>current>>elevation>>azimuth>>heading>>low>>high){
  thandor::SimulationMotionProfile p;p.speed_q12_per_tick=std::bit_cast<int32_t>(speed);
  p.ground_acceleration_q12=acceleration;p.ground_slope_threshold_turn16=std::bit_cast<int32_t>(low);
  p.ground_secondary_slope_threshold_turn16=std::bit_cast<int32_t>(high);
  thandor::SimulationActorState a;a.ground_advance_q12=current;
  a.ground_normal_elevation_turn16=std::bit_cast<int32_t>(elevation);
  a.ground_normal_azimuth_turn16=azimuth;a.body_yaw_turn16=heading;
  std::cout<<thandor::simulation_ground_advance_q12(a,p)<<'\n';
 }
 return std::cin.eof()?0:1;
}
