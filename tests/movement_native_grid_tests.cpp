#include "thandor/retail_placement.hpp"
#include "thandor/simulation_lab_geometry.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void terrain(){
 auto f=FieldGrid::create(35,35,0,0);
 f.at(17,17).water_delta_q12=600;
 V565PathTerrainCache cache;cache.prepare(f);
 const auto native=retail_build_terrain_scratch(f);
 check(cache.states==native.state,"movement did not use the expanded terrain lattice");
 // A neighbouring scratch record receives the bad cell's halo even though
 // inverse world sampling puts it on dry FLD (16,16).
 const V519PathScratchCoordinate end{17*4+2,17*4-2},from{end.column-1,end.row};
 check((cache.states[size_t(end.row)*native.width+end.column]&0x10000000U)!=0,
       "water/slope halo missing from movement cache");
 check(!v565_path_segment_clear_static_cached(f,from,end,2,0,0x10000000U,{},1000,cache),
       "movement accepted the native terrain halo");
 check((cache.states[0]&0x80000000U)!=0,"unexported editable grid lost runtime map boundary");
}
void influence(){
 auto f=FieldGrid::create(35,35,0,0);V565PathTerrainCache cache;cache.prepare(f);
 auto image=retail_build_terrain_scratch(f);
 const auto p=v519_path_scratch_center_to_world_q12({68,68});
 for(uint32_t cls=0;cls<24;++cls){
  const V528PathRuntimeBlocker b{p.x_q12,p.y_q12,1500,cls};
  auto expected=image;retail_placement_stamp_influence(expected,{p.x_q12,p.y_q12},1500,cls);
  const bool low=cls==4||(cls>=10&&cls<=16)||cls==20||cls==22;
  const bool high=cls<=3||(cls>=17&&cls<=19)||cls==23;
  for(uint32_t band=0;band<8;++band){
   const uint32_t low_mask=0x100U<<band,high_mask=0x10000U<<band;
   bool saw_margin=false;
   for(int row=48;row<88;++row)for(int col=48;col<88;++col){
    const V519PathScratchCoordinate from{col-1,row},to{col,row};
    const auto state=expected.read(uint32_t(col),uint32_t(row));
    const bool clear=v565_path_segment_clear_static_cached(f,from,to,2,0,low_mask,std::span(&b,1),1000,cache);
    check(clear==((state&low_mask)==0),"route admission disagrees with class-specific native influence");
    const bool cheap=v565_path_segment_clear_static_cached(f,from,to,2,high_mask,0,std::span(&b,1),1000,cache);
    check(cheap==((state&high_mask)==0),"mobile influence did not reach the route cost mask");
    const auto world=v519_path_scratch_center_to_world_q12(to);
    const int64_t dx=int64_t(world.x_q12)-p.x_q12,dy=int64_t(world.y_q12)-p.y_q12;
    if((state&low_mask)&&dx*dx+dy*dy>2500LL*2500)saw_margin=true;
   }
   if(low)check(saw_margin,"building clearance omitted native radius margin");
   if(high)check((expected.read(68,68)&low_mask)==0,"mobile unit stamped structure blocker bands");
  }
 }
}
void unfinished_owner(){
 auto f=FieldGrid::create(59,59,0,0);
 SimulationLabSeedEntity s;s.army_asset_id=1;s.faction_index=1;s.runtime_model_class_id=1;
 s.root_model_maximum_integrity=1000;s.actor.position={20,-10,0};
 s.motion.speed_q12_per_tick=64;s.motion.collision_radius_q12=1000;
 s.motion.yaw_max_velocity_turn16=s.motion.yaw_acceleration_turn16=0x4000;
 SimulationLabSeedEntity b;b.army_asset_id=300;b.faction_index=1;b.runtime_model_class_id=11;
 b.root_model_maximum_integrity=1000;b.actor.position={21,-10,0};b.motion.collision_radius_q12=2000;b.building=true;
 const std::array seeds{s,b};SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=512;
 SimulationLabScenarioRuntimeSeed scene;scene.enabled=true;scene.active_faction_count=1;scene.ai_planning_enabled=false;
 SimulationLabExperiment lab;std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
 lab.reset(cfg,ki,seeds,{},{},{},{},{},scene);auto& run=lab.runs()[0];
 run.entities[1].construction_complete=false;run.entities[1].construction_complete_tick=10000;
 run.entities[0].actor.move_target=Vec3f{24,-10,0};
 const V528PathRuntimeBlocker blocker{float_to_q12(21),float_to_q12(-10),2000,11};
 for(int tick=0;tick<300;++tick){auto before=run.entities[0].actor.position;lab.step(f,1);
  check(!simulation_lab_step_hits_blocker(before,run.entities[0].actor.position,1000,blocker),
        "unit drove through an unfinished linked building owner");}
}
}
int main()try{terrain();influence();unfinished_owner();std::cout<<"MOVEMENT_NATIVE_GRID PASS terrain, 24 classes x 8 bands, unfinished owner\n";}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
