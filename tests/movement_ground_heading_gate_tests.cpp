#include "thandor/catalog.hpp"
#include "thandor/binary.hpp"
#include "thandor/simulation.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
SimulationActorState actor(uint32_t target_yaw,float distance=100.0F){
 SimulationActorState a;
 a.move_target=Vec3f{distance*std::cos(turn16_to_radians(target_yaw)),distance*std::sin(turn16_to_radians(target_yaw)),0};
 a.move_waypoint=a.move_target;
 return a;
}
SimulationMotionProfile profile(){
 SimulationMotionProfile p;p.speed_q12_per_tick=32;p.yaw_max_velocity_turn16=768;p.yaw_acceleration_turn16=329;
 p.heading_interpolation_distance_q12=390;p.far_heading_error_limit_turn16=121;p.near_heading_error_limit_turn16=256;
 return p;
}
}
int main(int argc,char**argv)try{
 // Native Asgard MDL300: placement proximity uses +1A0=4000,
 // independently of movement/height-stamp +DC=5920.
 std::vector<std::byte> mdl(0x600);
 write_u32(mdl,0,fourcc("mdl"));write_u32(mdl,4,mdl.size());write_u32(mdl,0xB0,1);
 constexpr size_t record=0x200;
 write_u32(mdl,record,0x300);write_u32(mdl,record+4,500);
 write_u32(mdl,record+0x4C,13);write_u32(mdl,record+0x64,0x500);
 write_u32(mdl,record+0xDC,5920);write_u32(mdl,record+0x1A0,4000);
 write_u32(mdl,record+0x260,UINT32_MAX);write_u32(mdl,0x500,0x100);
 write_u32(mdl,0x50C,0x4000);
 size_t name_offset=0x538;for(char c:std::string("spr/test.spr")){write_u16(mdl,name_offset,uint16_t(c));name_offset+=2;}
 auto models=parse_model_visual_records(mdl,"radius_fixture.mdl");
 check(models.size()==1&&models[0].simulation.movement_collision_radius_q12==5920&&
       models[0].simulation.attachment_proximity_radius_q12==4000,"serialized placement radius was conflated with movement radius");
 write_u32(mdl,record+0x260,0);
 models=parse_model_visual_records(mdl,"radius_fixture.mdl");
 check(models[0].simulation.movement_collision_radius_q12==1000&&
       models[0].simulation.attachment_proximity_radius_q12==1000,"terrain class initializer did not resolve both radius fields");
 for(uint32_t target:{0x200U,0xfe00U})for(uint32_t batch:{1U,5U}){
  auto a=actor(target);const auto p=profile();
  check(!simulation_tick_actor_motion(a,p,batch)&&a.position.x==0&&a.position.y==0,
        "weapon aim tolerance bypassed native ground heading admission");
  check(a.move_target&&a.body_turn_velocity_turn16!=0,"blocked drive discarded command or stopped accelerated turning");
  for(unsigned tick=0;tick<8;++tick)simulation_tick_actor_motion(a,p,batch);
  check(a.position.x>0,"heading admission never allows aligned movement");
 }
 auto p=profile();auto far=actor(0x200);
 p.far_heading_error_limit_turn16=0x1000;
 check(simulation_tick_actor_motion(far,p)&&far.position.x>0,
       "legal broad heading limit was replaced by exact-alignment requirement");
 p=profile();p.heading_interpolation_distance_q12=4096;
 p.far_heading_error_limit_turn16=64;p.near_heading_error_limit_turn16=512;
 auto near=actor(0x200,0.5F);
 check(!simulation_tick_actor_motion(near,p)&&near.position.x==0,"near gate admitted excessive heading error");
 near.body_yaw_turn16=0x100;near.body_turn_velocity_turn16=0;
 check(simulation_tick_actor_motion(near,p)&&near.position.x>0,"interpolated near gate ignored a legal heading");
 auto compact=actor(0x200);p=profile();p.heading_interpolation_distance_q12=0;
 p.far_heading_error_limit_turn16=p.near_heading_error_limit_turn16=0;
 check(simulation_tick_actor_motion(compact,p)&&compact.position.x>0,"legacy compact-profile heading fallback changed");
 if(argc>1){
  const auto catalog=AssetCatalog::scan_archive_root(argv[1]);const auto* unit=catalog.army_preset(1);
  check(unit&&unit->runtime_model_class_id==1,"stock class1 unit absent");
  const auto& motion=unit->simulation_motion_profile;
  check(motion.heading_interpolation_distance_q12==6000&&motion.far_heading_error_limit_turn16==8192&&
        motion.near_heading_error_limit_turn16==256,"class1 native steering fields were omitted from the motion profile");
  const auto* water=catalog.army_preset(280);
  check(water&&water->runtime_model_class_id==19&&
        water->simulation_motion_profile.heading_interpolation_distance_q12==6000&&
        water->simulation_motion_profile.far_heading_error_limit_turn16==8192&&
        water->simulation_motion_profile.near_heading_error_limit_turn16==256,
        "class19 native steering fields were omitted from the motion profile");
 }
 std::cout<<"GROUND_HEADING_GATE PASS tight/wrapped/batched, near interpolation, broad and compact gates, stock class1\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
