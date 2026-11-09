#pragma once
#include "thandor/gfx.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <optional>

namespace thandor {
struct RetailSelectionFrameCell {uint32_t flags{},subresource{};int32_t x{},y{};};
struct RetailSelectionFrameAssets {
 std::array<RetailSelectionFrameCell,26> cells{};
 std::map<uint32_t,RgbaImage> images;
 static RetailSelectionFrameAssets parse(std::span<const std::byte> dat,const GfxAsset& gfx){
  if(dat.size()<0x1A4)throw FormatError("selection frame DAT is truncated");
  RetailSelectionFrameAssets result;
  for(size_t i=0;i<result.cells.size();++i){const size_t at=i*16;
   result.cells[i]={read_u32(dat,at+4),read_u32(dat,at+8),read_i32(dat,at+12),read_i32(dat,at+16)};
  }
  for(unsigned cell:{0U,1U,2U,3U,4U,5U,6U,18U,22U,25U}){
   const auto base=result.cells[cell].subresource;const unsigned count=cell==18?18:(cell==22||cell==25)?10:cell>=4?3:1;
   for(unsigned i=0;i<count;++i)result.images.try_emplace(base+i,gfx.decode_logical(GraphicsSubresourceIndex{base+i},false));
  }
  return result;
 }
};
struct RetailSelectionFrameBlit {uint32_t subresource{};float x{},y{},width{},height{};bool tile_x{},tile_y{};bool faction_tint{true};};
struct RetailSelectionFrameMetrics {
 std::optional<std::array<int32_t,2>> top_bar; // maximum, current
 uint32_t energy_total_q4{},energy_active_q4{};
 bool force_energy_meter{};
};
inline uint32_t retail_selection_meter_frame(uint32_t total,uint32_t active) noexcept {
 if(!total)return 17U;
 return uint32_t((uint64_t(std::min(active,total))*32U+total)/total/2U)+1U;
}
inline uint32_t retail_selection_pixel_argb(uint32_t pixel,std::optional<uint32_t> faction_rgb) noexcept {
 if(!faction_rgb)return pixel;
 const auto value=std::max({(pixel>>16U)&255U,(pixel>>8U)&255U,pixel&255U});
 const auto r=(((*faction_rgb>>16U)&255U)*value+127U)/255U;
 const auto g=(((*faction_rgb>>8U)&255U)*value+127U)/255U;
 const auto b=((*faction_rgb&255U)*value+127U)/255U;
 return (pixel&0xFF000000U)|(r<<16U)|(g<<8U)|b;
}
inline int32_t retail_selection_round(int64_t n,int32_t d){if(d<=0)return 0;return int32_t(n/d+(n%d*2>d));}
// SelectionPanel_RenderArmyRuntimeMetrics generic frame: exact select.dat offsets,
// native corner images, capped top/sides, and rounded six-level condition fill.
inline std::vector<RetailSelectionFrameBlit> retail_selection_frame_blits(
 const RetailSelectionFrameAssets& assets,float left,float top,float right,float bottom,int32_t condition_q12,
 const RetailSelectionFrameMetrics& metrics={}){
 std::vector<RetailSelectionFrameBlit> out;if(right<=left||bottom<=top)return out;
 const auto image=[&](uint32_t id)->const RgbaImage&{return assets.images.at(id);};
 const auto push=[&](uint32_t id,float x,float y,float w=0,float h=0,bool tx=false,bool ty=false,bool tint=true){
  const auto& i=image(id);out.push_back({id,x,y,w>0?w:float(i.width),h>0?h:float(i.height),tx,ty,tint});
 };
 std::array<std::array<float,2>,4> next{};
 for(unsigned i=0;i<4;++i){
  const bool energy_corner=i==0&&(metrics.force_energy_meter||metrics.energy_total_q4);
  const auto& cell=assets.cells[energy_corner?18:i];const auto& im=image(cell.subresource);
  const float x=(i%2?right:left)+cell.x,y=(i>=2?bottom:top)+cell.y;
  // The base and the selected state image together form the energy icon.
  // Preserve both; every ordinary frame/bar piece uses the faction tint.
  push(cell.subresource,x,y,0,0,false,false,!energy_corner);
  if(energy_corner)
   push(cell.subresource+retail_selection_meter_frame(metrics.energy_total_q4,metrics.energy_active_q4),x,y,0,0,false,false,false);
  next[i]={x+((cell.flags&4)?0:im.width),y+((cell.flags&8)?0:im.height)};
 }
 const auto horizontal=[&](unsigned cell_id,float fixed,float start,float end,std::optional<std::array<int32_t,2>> value){
  const auto& cell=assets.cells[cell_id];const uint32_t base=cell.subresource;
  const float x0=start+image(base).width,x1=end-image(base+2).width,y=fixed+cell.y;
  push(base,start,y);push(base+2,x1,y);if(x1<=x0)return;
  if(!value){push(base+1,x0,y,x1-x0,0,true);return;}
  const auto maximum=std::max(0,(*value)[0]),current=std::clamp((*value)[1],0,maximum);
  const float filled=maximum?float(retail_selection_round(int64_t(int(x1-x0))*current,maximum)):x1-x0;
  const uint32_t frame=maximum?uint32_t(retail_selection_round(int64_t(current)*6,maximum)):6U;
  if(filled>0)push(base+3+frame,x0,y,filled,0,true);
  if(x1>x0+filled)push(base+1,x0+filled,y,x1-x0-filled,0,true);
 };
 horizontal(metrics.top_bar?22:4,top,next[0][0],next[1][0],metrics.top_bar);
 horizontal(25,bottom,next[2][0],next[3][0],std::array<int32_t,2>{4096,condition_q12});
 for(unsigned i=0;i<2;++i){const auto& cell=assets.cells[5+i];const uint32_t base=cell.subresource;
  const float x=(i?right:left)+cell.x,y0=next[i][1],y1=next[i+2][1]-image(base+2).height;
  push(base,x,y0);push(base+2,x,y1);if(y1>y0+image(base).height)
   push(base+1,x,y0+image(base).height,0,y1-y0-image(base).height,false,true);
 }
 return out;
}
inline bool retail_selection_show_damage_frame(int32_t current,int32_t maximum)noexcept{
 return maximum>0&&current<maximum;
}
} // namespace thandor
