#include "thandor/retail_placement.hpp"
#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_site_placement.hpp"
#include "thandor/simulation_lab_environment.hpp"
#include "thandor/simulation_placement.hpp"
#include "thandor/catalog.hpp"
#include "placement_native_reference.hpp"
#include <iostream>
#include <stdexcept>
#include <fstream>
using namespace thandor;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
void placement_controls(){
 auto field=FieldGrid::create(80,80,0,0);const auto& c=field.at(40,40);const MovementPoint point{c.world_x_q12,c.world_y_q12};
 SimulationLabRun run;auto scratch=std::make_shared<SimulationLabRetailScratchImage>();scratch->field_occupancy_masks.assign(field.cells().size(),uint64_t{1}<<8);run.retail_scratch=scratch;
 SimulationLabSiteModel factory{301,13,4096,0,0,0,0,MovementPoint{0,12288}};
 std::vector<SimulationLabSiteOwner> owners{{{{point.x-20000,point.y},1,11,0,100000,true},1000,{}}};
 const auto query=[&](uint32_t heading=0,uint32_t mode=1){return simulation_lab_query_site_placement(run,field,owners,factory,point,1,mode,0,heading);};
 check(query().accepted,"clear factory rejected");
 const auto exit=retail_placement_anchor(point,0,*factory.factory_exit_local);
 owners.push_back({{exit,2,1,0,0,true},1000,{}});
 check(query().reason==PioneerPlacementReason::FactoryExitRuntimeCollision,"factory root admitted a blocked exit area");
 check(query(0x8000).accepted,"rotated clear factory direction was not admitted");
 check(query(0,4).accepted,"native structures-only query incorrectly blocked by a unit");
 owners.back().support.runtime_class=11;
 check(query(0,4).reason==PioneerPlacementReason::FactoryExitRuntimeCollision,"structure at factory exit ignored");
 owners.pop_back();auto cell=retail_fog_world_to_cell(field,exit.x,exit.y);check(bool(cell),"exit outside fixture");
 field.at(cell->x,cell->y).terrain_height_q12=8192;
 check(query().reason==PioneerPlacementReason::FactoryExitTerrainRejected,"factory allowed exit onto incompatible terrain");
 field.at(cell->x,cell->y).terrain_height_q12=0;
 factory.factory_exit_local.reset();check(query().reason==PioneerPlacementReason::MissingFactoryAnchor,"missing stock exit metadata silently accepted");
 factory.factory_exit_local=MovementPoint{0,12288};
 owners.push_back({{{point.x+30000,point.y},2,13,0,0,true},1000,point});
 check(query().reason==PioneerPlacementReason::RuntimeCollision,"existing factory's reserved second footprint ignored");owners.pop_back();
 scratch->field_occupancy_masks.assign(field.cells().size(),0);
 check(query().reason==PioneerPlacementReason::FieldBlocked,"generic factory ignored native faction field admission");
 scratch->field_occupancy_masks.assign(field.cells().size(),uint64_t{1}<<8);
 owners[0].support.runtime_class=18;
 check(!query().accepted&&query(0,0).accepted,"mode1 did not exclude Pioneer support");owners[0].support.runtime_class=11;
 // The auxiliary test uses ground normal at the center, secondary normal on sectors.
 for(auto& c:field.cells()){c.terrain_height_q12=0;c.water_delta_q12=200000;}
 field.at(41,40).terrain_height_q12=8192;field.at(41,40).water_delta_q12=200000-8192;
 const auto ground=retail_placement_normal_angles(field,40,40,false)>>16;
 const auto water=retail_placement_normal_angles(field,40,40,true)>>16;
 check(ground<0x3000&&water>=0x3000,"center-normal fixture did not separate ground/water");
 check(!simulation_placement_terrain_clear(field,{q12_to_float(point.x),q12_to_float(point.y),q12_to_float(200000)},200000,0xC00,1),"water placement used secondary rather than native center ground normal");
 std::cout<<"factory root/rotated exit/terrain/second footprint/field/mode/Pioneer checks PASS\n";
}
void stock_controls(const char* root){
 const auto catalog=AssetCatalog::scan_game_root(root);const auto runtime=simulation_lab_prepare_runtime_catalog(catalog);
 for(auto id:{301U,302U,303U,304U,305U}){
  const auto model=simulation_lab_first_site_model(*runtime,id);check(model&&model->runtime_class==13&&model->factory_exit_local.has_value(),"first stock root factory/exit metadata missing");
  std::cout<<"stock factory="<<id<<" radius="<<model->radius_q12<<" contact="<<model->contact_kind<<" local_exit="<<model->factory_exit_local->x<<','<<model->factory_exit_local->y<<'\n';
  auto field=FieldGrid::create(100,100,0,0);for(auto& cell:field.cells())cell.water_delta_q12=model->contact_kind==1?4096:0;
  const auto& cell=field.at(50,50);const MovementPoint p{cell.world_x_q12,cell.world_y_q12};
  SimulationLabRun run;auto scratch=std::make_shared<SimulationLabRetailScratchImage>();scratch->field_occupancy_masks.assign(field.cells().size(),uint64_t{1}<<8);run.retail_scratch=scratch;
  const std::vector<SimulationLabSiteOwner> support{{{{p.x-40000,p.y},1,11,0,100000,true},1000,{}}};
  for(uint32_t yaw:{0U,0x4000U,0x8000U,0xC000U,12345U}){
   check(simulation_lab_query_site_placement(run,field,support,*model,p,1,1,0,yaw).accepted,"stock factory with clear root/exit was rejected");
   auto owners=support;const auto anchor=retail_placement_anchor(p,yaw,*model->factory_exit_local);
   owners.push_back({{anchor,2,11,0,0,true},1000,{}});
   check(simulation_lab_query_site_placement(run,field,owners,*model,p,1,1,0,yaw).reason==PioneerPlacementReason::FactoryExitRuntimeCollision,"stock factory ignored blocked rotated drive-out area");
  }
 }
 std::cout<<"stock factories=5 headings=5 clear/blocked exit controls PASS (declared field/support input)\n";
}
int main(int argc,char** argv)try{
 placement_controls();
 uint32_t rng=0x12345678;const auto random=[&](){rng=rng*1664525U+1013904223U;return rng;};
 for(unsigned trial=0;trial<240;++trial){
  SimulationLabRetailScratchImage a;a.width=a.height=128;a.state.resize(128*128);
  for(uint32_t y=0;y<128;++y)for(uint32_t x=0;x<128;++x){
   auto& s=a.state[y*128+x];s=x<4||y<4||x>=124||y>=124?0x80000000U:0;
   if(x>25&&x<100&&y>25&&y<100&&(random()%13==0))s|=1U<<(8+random()%7);
  }
  if(trial%3==0)for(uint32_t y=4;y<124;++y)a.state[y*128+64]|=0x100;
  const uint32_t radius=trial%4==0?5920:random()%4000;
  const auto p=v519_path_scratch_center_to_world_q12({48+int(random()%32),48+int(random()%32)});
  std::vector<retail_reference_v624::GridScratchCell> ref(a.state.size());
  for(size_t i=0;i<a.state.size();++i)ref[i]={a.state[i],0};
  retail_reference_v624::g_GridScratchPrimary=ref.data();retail_reference_v624::g_GridScratchWidth=retail_reference_v624::g_GridScratchHeight=128;
  const bool expected=retail_reference_v624::GridReachability_RebuildConnectedRegionAroundWorldPoint(radius,p.y_q12,p.x_q12);
  const bool actual=retail_placement_region_split(a,radius,{p.x_q12,p.y_q12});
  if(actual!=expected)std::cerr<<"trial="<<trial<<" expected="<<expected<<" actual="<<actual<<'\n';
  check(actual==expected,"placement ring decision differs from unchanged native reference body");
  for(size_t i=0;i<ref.size();++i)if(a.state[i]!=ref[i].stateMask||a.placement_path_cost[i]!=ref[i].pathCost){
   std::cerr<<"trial="<<trial<<" cell="<<i<<" state="<<a.state[i]<<'/'<<ref[i].stateMask<<" cost="<<a.placement_path_cost[i]<<'/'<<ref[i].pathCost<<'\n';
   throw std::runtime_error("placement scratch side effects differ from native reference");}
 }
 std::cout<<"native connected-ring differential cases=240 state/pathCost/decision=identical\n";
 const auto anchor=retail_placement_anchor({10000,-20000},0,{4096,0});
 check(anchor.x==14096&&anchor.y==-20000,"zero-heading factory point changed axes");
 const auto turned=retail_placement_anchor({10000,-20000},0x4000,{4096,0});
 check(turned.x==10000&&turned.y==-15904,"quarter-turn factory point changed axes");
 if(argc>1){
  std::ifstream f(argv[1],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});check(!bytes.empty(),"EXE absent");
  const auto u16=[&](size_t o){return uint32_t(uint8_t(bytes.at(o)))|(uint32_t(uint8_t(bytes.at(o+1)))<<8);};
  const auto u32=[&](size_t o){return u16(o)|(u16(o+2)<<16);};
  const auto pe=u32(0x3c),sections=u16(pe+6),opt=u16(pe+20),base=u32(pe+52);size_t raw=0;
  for(unsigned i=0;i<sections;++i){const size_t off=pe+24+opt+i*40;const auto rva=u32(off+12);if(rva<=0x4246A0-base&&0x4246A0-base<rva+std::max(u32(off+8),u32(off+16)))raw=u32(off+20)+0x4246A0-base-rva;}
  check(raw!=0,"original sine table unmapped");
  for(uint32_t i=0;i<98304;++i)check(uint32_t(retail_placement_sine_table_entry(i))==u32(raw+i*4),"native Q28 table reconstruction differs from original executable");
  std::cout<<"original executable Q28 sine table entries=98304 all identical\n";
 }
 if(argc>2)stock_controls(argv[2]);
 std::cout<<"V624_RETAIL_PLACEMENT PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
