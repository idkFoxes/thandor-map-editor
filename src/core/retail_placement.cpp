#include "thandor/retail_placement.hpp"
#include "thandor/retail_contact.hpp"
#include "thandor/simulation_lab.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace thandor {
namespace {
int32_t wrap(uint32_t v){return std::bit_cast<int32_t>(v);}
int32_t add(int32_t a,int32_t b){return wrap(uint32_t(a)+uint32_t(b));}
int32_t sub(int32_t a,int32_t b){return wrap(uint32_t(a)-uint32_t(b));}
int32_t mul(int32_t a,int32_t b){return wrap(uint32_t(uint64_t(uint32_t(a))*uint32_t(b)));}
}
int32_t retail_placement_sine_table_entry(uint32_t index){
 if(index>=98304)throw std::out_of_range("retail sine table index");
 static const auto table=[](){std::array<int32_t,98304> a{};
  for(int i=0;i<98304;++i)a[i]=int32_t(std::floor(std::sin((i-16384)*(3.141592654/32768.0))*268435456.0+.5));
  return a;}();
 return table[index];
}
MovementPoint retail_placement_anchor(MovementPoint p,uint32_t heading,MovementPoint local){
 const auto length=retail_uint64_sqrt(uint64_t(int64_t(local.x)*local.x)+uint64_t(int64_t(local.y)*local.y));
 const auto angle=(fixed_atan2_angle16_compat(local.y,local.x)+heading)&0xFFFFU;
 const auto dx=int32_t((int64_t(retail_placement_sine_table_entry(32768+angle))*length)>>28);
 const auto dy=int32_t((int64_t(retail_placement_sine_table_entry(16384+angle))*length)>>28);
 return {add(p.x,dx),add(p.y,dy)};
}
namespace {
uint32_t compute_interior_normal(const FieldGrid& f,uint32_t x,uint32_t y,bool secondary){
 const auto& center=f.at(x,y);const auto height=[&](const FieldCell& c){return secondary?add(c.terrain_height_q12,c.water_delta_q12):c.terrain_height_q12;};
 int32_t nx=0,ny=0;const std::array<const FieldCell*,6> neighbors{&f.at(x+1,y),&f.at(x,y+1),&f.at(x,y-1),&f.at(x-1,y),&f.at(x-1,y+1),&f.at(x+1,y-1)};
 for(const auto* n:neighbors){const auto dh=sub(height(*n),height(center));nx=sub(nx,mul(sub(n->world_x_q12,center.world_x_q12),dh));ny=sub(ny,mul(sub(n->world_y_q12,center.world_y_q12),dh));}
 const auto horizontal=retail_uint64_sqrt(uint64_t(int64_t(nx)*nx)+uint64_t(int64_t(ny)*ny));
 return (fixed_atan2_angle16_compat(ny,nx)&0xFFFFU)|(fixed_atan2_angle16_compat(0xC00000,wrap(horizontal))<<16);
}
}

uint32_t retail_placement_normal_angles(const FieldGrid& f,uint32_t x,uint32_t y,bool secondary){
 if(x>=f.width()||y>=f.height())return 0x40000000U;
 if(f.has_runtime_normals()||!x||!y||x+1>=f.width()||y+1>=f.height()){
  const auto& cell=f.at(x,y);
  return secondary?cell.loaded_water_normal_angles:cell.loaded_terrain_normal_angles;
 }
 return compute_interior_normal(f,x,y,secondary);
}
void FieldGrid::capture_runtime_normals() {
    if(!runtime_normals_captured_)refresh_runtime_normals();
}

void FieldGrid::refresh_runtime_normals() {
    for(uint32_t y=1;y+1<height();++y)
        for(uint32_t x=1;x+1<width();++x){
            auto& cell=at(x,y);
            cell.loaded_terrain_normal_angles=compute_interior_normal(*this,x,y,false);
            cell.loaded_water_normal_angles=compute_interior_normal(*this,x,y,true);
        }
    runtime_normals_captured_=true;
}

void FieldGrid::apply_runtime_terrain_impact(int32_t x_q12, int32_t y_q12, int32_t radius_q12,
                                            int32_t height_amplitude_q12, int32_t material) {
    if(radius_q12<=0)return;
    capture_runtime_normals();
    const auto grid_cell=[](int64_t x,int64_t y){
        const int64_t half_row=(y*-0x20C8CC)>>21;
        return std::pair{(((x*0x1C6E9C)>>20)-half_row)>>12,(half_row*2)>>12};
    };
    const int64_t horizontal_radius=(int64_t(radius_q12)*0x1BB6)>>12;
    const auto [left,top]=grid_cell(x_q12-horizontal_radius,int64_t(y_q12)+radius_q12);
    const auto [right,bottom]=grid_cell(x_q12+horizontal_radius,int64_t(y_q12)-radius_q12);
    const auto first_x=std::max<int64_t>(1,left);
    const auto first_y=std::max<int64_t>(1,top);
    const auto end_x=right+1<width()?right+1:int64_t(width())-1;
    const auto end_y=bottom+1<height()?bottom+1:int64_t(height())-1;
    const int64_t radius_squared=int64_t(radius_q12)*radius_q12;
    const int32_t radius_squared_q12=wrap(uint32_t(radius_squared>>12));
    for(auto y=first_y;y<end_y;++y)
        for(auto x=first_x;x<end_x;++x){
            auto& cell=at(uint32_t(x),uint32_t(y));
            const int64_t dx=int64_t(x_q12)-cell.world_x_q12;
            const int64_t dy=int64_t(y_q12)-cell.world_y_q12;
            const int64_t distance_squared=dx*dx+dy*dy;
            const auto radius_high=radius_squared>>32;
            const auto distance_high=distance_squared>>32;
            if(distance_high>radius_high||
               (distance_high==radius_high&&wrap(uint32_t(distance_squared))>=wrap(uint32_t(radius_squared)))||
               radius_squared_q12==0)continue;
            const auto delta=wrap(uint32_t(((distance_squared/radius_squared_q12-4096)*
                height_amplitude_q12)>>12));
            cell.terrain_height_q12=add(cell.terrain_height_q12,delta);
            if(cell.water_delta_q12>=0)cell.water_delta_q12=sub(cell.water_delta_q12,delta);
            if(material>=0)cell.set_material(static_cast<uint8_t>(material));
        }
    // The native effect refreshes the whole rectangle, including corners
    // outside the circle and cells whose height amplitude was zero.
    for(auto y=first_y;y<end_y;++y)
        for(auto x=first_x;x<end_x;++x){
            auto& cell=at(uint32_t(x),uint32_t(y));
            cell.loaded_terrain_normal_angles=compute_interior_normal(*this,uint32_t(x),uint32_t(y),false);
            cell.loaded_water_normal_angles=compute_interior_normal(*this,uint32_t(x),uint32_t(y),true);
        }
}
int32_t retail_placement_height_q12(const FieldGrid& f,MovementPoint p,int32_t contact){
 const uint32_t half=uint32_t((int64_t(p.y)*-0x20C8CC)>>21);
 const uint32_t col=uint32_t((int64_t(p.x)*0x1C6E9C)>>20)-half,row=half*2;
 const int x=wrap(col)>>12,y=wrap(row)>>12;
 if(x<0||y<0||x+1>=int(f.width())||y+1>=int(f.height()))return 0;
 const auto& a=f.at(x,y);const auto& b=f.at(x+1,y);const auto& c=f.at(x,y+1);const auto& d=f.at(x+1,y+1);
 if((a.flags_and_material|d.flags_and_material)&0x88006000U)return 0;
 const int fx=col&4095,fy=row&4095,diagonal=fx+fy-4096;
 const auto sample=[&](const auto& value){
  const int64_t sum=fx+fy<4096
   ?int64_t(value(b))*fx+int64_t(value(c))*fy-int64_t(value(a))*diagonal
   :int64_t(value(d))*diagonal-int64_t(value(c))*(fx-4096)-int64_t(value(b))*(fy-4096);
  return wrap(uint32_t(sum>>12));};
 const auto terrain=sample([](const auto& n){return n.terrain_height_q12;});
 if(contact==1)return sample([&](const auto& n){return add(n.terrain_height_q12,n.water_delta_q12);});
 if(contact==4)return add(terrain,std::max(0,sample([](const auto& n){return n.water_delta_q12;})));
 return terrain;
}

bool retail_placement_region_split(SimulationLabRetailScratchImage& g,uint32_t radius,MovementPoint point){
 const int64_t width=g.width,height=g.height,count=width*height;
 if(width<4||height<=8||count<=0||uint64_t(count)!=g.state.size())return true;
 g.placement_path_cost.assign(g.state.size(),0);
 auto& state=g.state;auto& cost=g.placement_path_cost;
 for(auto& s:state)s|=1U;
 bool escaped=false;
 const auto valid=[&](int64_t i){return i>=0&&i<count;};
 const auto footprint=[&](uint32_t r){
  const auto center=v519_path_world_to_scratch(point.x,point.y);
  if(center.column<0||center.row<0||center.column>=width||center.row>=height)return;
  const int64_t origin=int64_t(center.row)*width+center.column;
  if(state[origin]&0x80000000U)return;
  const auto world=v519_path_scratch_center_to_world_q12(center);
  const uint32_t reach=r+2600U+499U,threshold=reach*reach;
  const auto walk=[&](int64_t at,int32_t wx,int32_t wy,bool up){
   int next=0;for(;;){const int visited=next;
    if(!valid(at)){escaped=true;return visited;}
    const auto dx=sub(wx,point.x),dy=sub(wy,point.y);
    if(threshold<uint32_t(uint64_t(uint32_t(dx))*uint32_t(dx)+uint64_t(uint32_t(dy))*uint32_t(dy)))return visited;
    state[at]&=~0x80000001U;++cost[at];
    wy=add(wy,up?999:-999);at+=up?-width*2+1:width*2-1;
    if(!valid(at)){escaped=true;return visited;}
    ++next;if(state[at]&0x80000000U)return visited;
    if(next>count){escaped=true;return visited;}
   }
  };
  auto at=origin;auto wx=world.x_q12;
  while(walk(at,wx,world.y_q12,true)&&walk(at,wx,world.y_q12,false)){--at;wx=sub(wx,0x240);if(!valid(at)){escaped=true;break;}}
  at=origin;wx=world.x_q12;
  while(walk(at,wx,world.y_q12,true)&&walk(at,wx,world.y_q12,false)){++at;wx=add(wx,0x240);if(!valid(at)){escaped=true;break;}}
  at=origin-width;wx=sub(world.x_q12,0x120);
  while(walk(at,wx,add(world.y_q12,499),true)){--at;wx=sub(wx,0x240);if(!valid(at)){escaped=true;break;}}
  at=origin-width+1;wx=add(world.x_q12,0x120);
  while(walk(at,wx,add(world.y_q12,499),true)){++at;wx=add(wx,0x240);if(!valid(at)){escaped=true;break;}}
  at=origin+width;wx=add(world.x_q12,0x120);
  while(walk(at,wx,sub(world.y_q12,500),false)){++at;wx=add(wx,0x240);if(!valid(at)){escaped=true;break;}}
  at=origin+width-1;wx=sub(world.x_q12,0x120);
  while(walk(at,wx,sub(world.y_q12,500),false)){--at;wx=sub(wx,0x240);if(!valid(at)){escaped=true;break;}}
 };
 const auto fill=[&](int64_t start,bool clear){
  struct Frame{int64_t left,right,cursor;bool below;};std::vector<Frame> stack;
  const auto open=[&](int64_t i){return valid(i)&&(clear?bool((state[i]&1U)&&cost[i]):(state[i]&0xF0007F01U)==0);};
  const auto mark=[&](int64_t i){if(clear)state[i]&=~1U;else state[i]|=1U;};
  const auto span=[&](int64_t at){mark(at);auto left=at-1,right=at+1;while(open(left)){mark(left);--left;}while(open(right)){mark(right);++right;}return Frame{left,right,left+1-width,false};};
  if(!valid(start)){escaped=true;return;}stack.push_back(span(start));
  while(!stack.empty()){auto& top=stack.back();
   if(!top.below&&top.right-width<top.cursor){top.below=true;top.cursor=top.left+width;}
   if(top.below&&top.right+width<=top.cursor){stack.pop_back();continue;}
   const auto cursor=top.cursor++;if(open(cursor))stack.push_back(span(cursor));
  }
 };
 footprint(radius*3U);
 const auto center=v519_path_world_to_scratch(point.x,point.y);
 if(center.column<0||center.row<0||center.column>=width||center.row>=height)return true;
 fill(int64_t(center.row)*width+center.column,false);footprint(radius);
 const auto ring_edge=[&](int64_t cursor){
  const auto i=cursor+width;if(!valid(i-width)||!valid(i+width))return false;
  return !(state[i]&0x80000000U)&&cost[i]&&(state[i]&1U)&&
   (!cost[i-width]||!cost[i-width+1]||!cost[i-1]||!cost[i+1]||!cost[i+width-1]||!cost[i+width]);
 };
 const int64_t begin=width*3,end=begin+(height-8)*width;
 auto first=begin;while(first<end&&!ring_edge(first))++first;
 if(first==end)return escaped;
 fill(first+width,true);
 for(auto i=begin;i<end;++i)if(ring_edge(i))return true;
 return escaped;
}
}
