#include "thandor/field.hpp"
#include "thandor/retail_placement.hpp"
#include "thandor/simulation_lab.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <stdexcept>
using namespace thandor;
static void check(bool v,const char* s){if(!v)throw std::runtime_error(s);}
static void unreachable_retry_uses_native_origin(){
 // Captured native traversal bits for a disconnected large region. The retry
 // reaches the row above the source at a queue boundary before reaching the
 // source itself. Native still backtracks and returns the nearby waypoint.
 std::ifstream input(std::filesystem::path(__FILE__).parent_path()/"fixtures/unreachable_retry_scratch.txt");
 uint32_t width{},height{};input>>width>>height;
 check(width==460&&height==460,"retry scratch fixture unavailable");
 auto states=std::make_shared<std::vector<uint32_t>>();
 uint32_t count{},value{};
 while(input>>std::dec>>count>>std::hex>>value)states->insert(states->end(),count,value);
 check(states->size()==size_t(width)*height,"retry scratch fixture truncated");
 auto field=FieldGrid::create(width/4,height/4,0,0);
 V565PathTerrainCache cache;cache.field=&field;cache.runtime_states=states;
 for(bool adapter:{false,true}){
  const auto r=adapter?v565_resolve_route_fast_for_simulation_lab(field,114011,-62056,233928,-117941,8,1,5,{},1250,&cache):
      v528_resolve_route(field,114011,-62056,233928,-117941,8,1,5,{},1250,&cache);
  check(r.usable()&&r.primary_world_q12.has_value(),"bounded retry discarded native backtracking");
  check(r.primary_world_q12->x_q12==113953&&r.primary_world_q12->y_q12==-62219,
      "bounded retry used the source row instead of the native preceding row");
  check(r.resolved_world_q12.x_q12==230932&&r.resolved_world_q12.y_q12==-127187,
      "bounded retry changed the retargeted destination");
 }
}
static void propagation_crosses_open_flat_row_edges(){
 auto field=FieldGrid::create(11,11,0,0);
 constexpr unsigned width=44;
 auto states=std::make_shared<std::vector<uint32_t>>(width*width,0U);
 for(unsigned x=0;x<width;++x){
  (*states)[x]=0x80000000U;
  (*states)[(width-1)*width+x]=0x80000000U;
 }
 for(unsigned y=1;y<width-1;++y)(*states)[y*width+22]=0x80000000U;
 V565PathTerrainCache cache;cache.field=&field;cache.runtime_states=states;
 const auto from=v519_path_scratch_center_to_world_q12({11,22});
 const auto to=v519_path_scratch_center_to_world_q12({32,22});
 const auto route=v528_resolve_route(field,from.x_q12,from.y_q12,to.x_q12,to.y_q12,8,2,6,{},0,&cache);
 check(route.disposition==V528RouteDisposition::Routed &&
       route.primary_cell==V519PathScratchCoordinate{10,22},
       "native flat scratch edge did not connect the two sides of the wall");
}
int main()try{
 unreachable_retry_uses_native_origin();
 propagation_crosses_open_flat_row_edges();
 auto f=FieldGrid::create(11,11,0,0);auto bytes=f.serialize();
 // Imported FLDs need not carry the native save seed at their untouched border.
 write_u32(bytes,0x200U+0x08,0x12340022U);
 write_u32(bytes,0x200U+0x78,0x00001234U);
 auto loaded=FieldGrid::parse(bytes);
 check(retail_placement_normal_angles(loaded,0,0,false)==0x12340022U,"imported ground border normal replaced");
 check(retail_placement_normal_angles(loaded,0,0,true)==0x1234U,"imported water border normal replaced");
 // Interior normals stay derived from editable heights, not a loaded seed.
 loaded.at(5,5).loaded_terrain_normal_angles=0;
 check(retail_placement_normal_angles(loaded,5,5,false)==0x40000000U,"interior used stale seed");
 const auto before=retail_placement_normal_angles(loaded,5,5,false);
 loaded.at(6,5).terrain_height_q12=12000;
 check(retail_placement_normal_angles(loaded,5,5,false)!=before,"normal ignored edited terrain");
 auto exported=FieldGrid::parse(loaded.serialize());
 check(retail_placement_normal_angles(exported,0,0,false)==FieldGrid::kRetailSerializedTriangle0NormalSeed,"save stopped normalizing ground seed");
 // Native asymmetric span endpoints connect this two-cell diagonal corridor.
 SimulationLabRetailScratchImage s;s.width=9;s.height=9;s.state.assign(81,0x81000000U);
 const auto at=[](int x,int y){return size_t(y*9+x);};
 s.state[at(3,5)]=s.state[at(4,4)]=s.state[at(5,3)]=0;
 s.state[at(2,4)]=0; // upper-left is not a neighbour of (3,5).
 std::vector<size_t> queue;
 retail_flood_reachable_scratch(s,at(3,5),0x81000001U,queue);
 check(s.state[at(5,3)]&1U,"upper-right diagonal corridor split");
 check(!(s.state[at(2,4)]&1U),"non-native upper-left diagonal admitted");
 for(auto& v:s.state)v&=~1U;
 retail_flood_reachable_scratch(s,at(5,3),0x81000001U,queue);
 check(s.state[at(3,5)]&1U,"lower-left diagonal corridor split");
 // Start class walls can seed a native span; hard-blocked starts cannot.
 s.state.assign(81,0x81000000U);s.state[at(4,4)]=0x01000000U;s.state[at(5,4)]=0;
 retail_flood_reachable_scratch(s,at(4,4),0x81000001U,queue);
 check(s.state[at(5,4)]&1U,"class-wall seed rejected");
 s.state.assign(81,0x81000000U);s.state[at(5,4)]=0;
 retail_flood_reachable_scratch(s,at(4,4),0x81000001U,queue);
 check(!(s.state[at(5,4)]&1U),"blocked seed admitted");
 // Runtime queries must not manufacture the owning faction's presence bit.
 auto terrain=FieldGrid::create(35,35,0,0);
 auto snapshot=std::make_shared<std::vector<uint32_t>>(retail_build_terrain_scratch(terrain).state);
 V565PathTerrainCache cache;cache.field=&terrain;cache.runtime_states=snapshot;
 const V519PathScratchCoordinate start{60,60},end{61,60};
 (*snapshot)[size_t(60)*140+60]|=2U;
 check(!v565_path_segment_clear_static_cached(terrain,start,end,2,0,0x02000000U,{},1000,cache),"runtime query fabricated faction presence");
 auto visible=std::make_shared<std::vector<uint32_t>>(*snapshot);
 (*visible)[size_t(60)*140+61]|=2U;
 V565PathTerrainCache other;other.field=&terrain;other.runtime_states=visible;
 check(v565_path_segment_clear_static_cached(terrain,start,end,2,0,0x02000000U,{},1000,other),"runtime query rejected present open cell");
 check(!v565_path_segment_clear_static_cached(terrain,start,end,2,0,0x02000000U,{},1000,cache),"one run changed another run's snapshot");
 V565PathTerrainCache legacy;legacy.prepare(terrain);
 check(v565_path_segment_clear_static_cached(terrain,start,end,2,0,0x02000000U,{},1000,legacy),"static/synthetic query compatibility changed");
 // The catalog-backed adapter must retain FIFO expansion/tie order.
 for(auto& state:*visible)if(!(state&0x80000000U))state|=2U;
 for(unsigned y=35;y<100;++y)(*visible)[size_t(y)*140+70]|=0x02000000U;
 const auto from=v519_path_scratch_center_to_world_q12({60,60});
 const auto to=v519_path_scratch_center_to_world_q12({85,65});
 const auto native_order=v528_resolve_route(terrain,from.x_q12,from.y_q12,to.x_q12,to.y_q12,2,1,1,{},0,&other);
 const auto lab_order=v565_resolve_route_fast_for_simulation_lab(terrain,from.x_q12,from.y_q12,to.x_q12,to.y_q12,2,1,1,{},0,&other,true);
 check(native_order.disposition==lab_order.disposition && native_order.primary_cell==lab_order.primary_cell &&
       native_order.expanded_nodes==lab_order.expanded_nodes,"runtime adapter changed native FIFO route order");
 std::cout<<"BORDER_REACHABILITY_V775 PASS imported border, edited interior, save seed, asymmetric corridors\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
