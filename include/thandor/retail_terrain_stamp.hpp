#pragma once
#include "thandor/field.hpp"
#include <algorithm>
#include <bit>
namespace thandor {
// FieldGrid_ApplyHeightAtWorldPointAndRefreshNeighbors: native nearest-vertex
// selection and six directional scans. Water surface stays fixed.
inline bool retail_stamp_structure_height(FieldGrid& field,int32_t x,int32_t y,int32_t z,uint32_t radius){
 const auto half=uint32_t((int64_t(y)*-0x20C8CC)>>21);
 const auto col=uint32_t((int64_t(x)*0x1C6E9C)>>20)-half,row=half*2;
 int cx=std::bit_cast<int32_t>(col)>>12,cy=std::bit_cast<int32_t>(row)>>12;
 const auto fx=col&4095,fy=row&4095,sum=fy+2*fx;
 if(sum<4096){if(fx+2*fy>4095)++cy;}
 else if(sum<=8192){++cx;if(fx<fy){++cy;--cx;}}
 else {++cx;if(fx+2*fy>8191)++cy;}
 constexpr uint32_t edge=0x88006000U;
 auto valid=[&](int a,int b){return a>=0&&b>=0&&a<int(field.width())&&b<int(field.height());};
 if(!valid(cx,cy))return false;
 const auto& center=field.at(cx,cy);
 if((center.flags_and_material&edge)||center.water_delta_q12>0)return false;
 bool changed=false;
 auto visit=[&](int a,int b){
  if(!valid(a,b)||(field.at(a,b).flags_and_material&edge))return false;
  auto& c=field.at(a,b);const uint32_t delta=uint32_t(z)-uint32_t(c.terrain_height_q12);
  c.water_delta_q12=std::bit_cast<int32_t>(uint32_t(c.water_delta_q12)-delta);
  changed|=c.terrain_height_q12!=z;c.terrain_height_q12=z;return true;
 };
 visit(cx,cy);
 const int limit=int(std::clamp(radius/0x240U,1U,255U));
 const int dx[]={1,1,0,-1,-1,0},dy[]={0,-1,-1,0,1,1};
 auto leg=[&](int step,int a,int b,int d){while(step<limit){if(!visit(a,b))return;step+=4;a+=dx[d];b+=dy[d];}};
 for(int a=0;a<6;++a){const int b=(a+1)%6;int sx=cx+dx[a],sy=cy+dy[a],step=0;
  while(step<limit){
   if(!visit(sx,sy))break;
   leg(step+4,sx+dx[a],sy+dy[a],a);
   if(limit<=step+4)break;
   const int bx=sx+dx[b],by=sy+dy[b];if(!visit(bx,by))break;
   step+=7;leg(step,bx+dx[b],by+dy[b],b);sx=bx+dx[a];sy=by+dy[a];
  }
 }
 return changed;
}
}
