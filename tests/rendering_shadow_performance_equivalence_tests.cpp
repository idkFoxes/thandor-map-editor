#include "thandor/terrain_shadow.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void require(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int32_t original_index(Vec3f n,Vec3f ray){
 const float length=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
 if(length<1.e-8F)n={0,0,1};else n={n.x/length,n.y/length,n.z/length};
 const float d=std::clamp(n.x*ray.x+n.y*ray.y+n.z*ray.z,-1.F,1.F);
 return std::clamp(int32_t(std::floor(d*256.F)),-256,256);
}
void lighting(){
 uint32_t rng=12345;size_t cases=0;
 for(uint32_t angle=0;angle<1024;angle++){
  TerrainLighting l;l.azimuth=uint16_t(angle*625U);l.elevation=uint16_t(angle*991U);
  l.ramp_124=0xFF123456;l.ramp_12c=0xFFCC7722;l.color_130=0xFF123451;
  l.color_134=0xFF18A19C;l.color_138=0xFF734922;l.color_13c=0xFFABCDEF;
  const auto ramp=l.build_game_ramp();const auto p=l.prepare_directional_light();const auto ray=l.shadow_ray_direction();
  require(p.ray_direction.x==ray.x&&p.ray_direction.y==ray.y&&p.ray_direction.z==ray.z,"prepared light direction changed");
  for(uint32_t j=0;j<128;j++){
   const auto component=[&]{rng=rng*1664525U+1013904223U;return (float(int32_t(rng>>8))-8388608.F)/131072.F;};
   Vec3f n{component(),component(),component()};if(j==0)n={0,0,0};if(j==1)n={0,0,1};if(j==2)n=ray;
   const auto index=original_index(n,ray);const auto expected=index>=0?l.ramp_124:ramp[256+index];
   require(p.signed_lut_index(n)==index&&l.signed_directional_lut_index(n)==index,"prepared light changes signed LUT rounding");
   require(p.color_for_upward_normal(n,ramp)==expected&&l.directional_color_for_upward_normal(n,ramp)==expected,"prepared light changes vertex color");cases++;
  }
  // A prepared value owns its state. The editor prepares a new value per build,
  // including a new sample of an active lighting cycle.
  const auto held=p;l.azimuth+=0x1000;l.elevation+=0x2000;l.ramp_124^=0x00FFFFFF;
  const auto changed=l.prepare_directional_light();require(changed.base_color==l.ramp_124&&held.base_color==0xFF123456,"prepared light retained a mutable reference");
 }
 std::cout<<"PREPARED_LIGHT cases="<<cases<<" signed_index_and_color_exact=1 changed_light=1\n";
}
void masks(){
 const int kernel[5][5]={{0,1,1,1,0},{1,1,3,1,1},{1,3,4,3,1},{1,1,3,1,1},{0,1,1,1,0}};
 unsigned cases=0;
 for(bool slope:{false,true}){
  auto field=FieldGrid::create(32,32,0,0);
  if(slope)for(uint32_t y=0;y<field.height();y++)for(uint32_t x=0;x<field.width();x++)field.at(x,y).terrain_height_q12=int32_t(x*128+y*64);
  const auto&c=field.at(16,16);const float x=q12_to_float(c.world_x_q12),y=q12_to_float(c.world_y_q12),z=c.terrain_height();
  for(uint32_t n:{16U,17U,31U,32U,33U,64U,127U,128U})for(unsigned angle=0;angle<8;angle++){
   const float a=angle*6.283185307179586F/8;const Vec3f light{.32F*std::cos(a),.32F*std::sin(a),-.916F};
   const Vec3f p0{x-1.3F,y-.4F,z+.15F},p1{x+1.3F,y+.4F,z+.15F},p2{x+1.1F,y+.5F,z+3.2F},p3{x-1.2F,y-.3F,z+3.2F};
   std::array<GeneratedShadowTriangle,2> triangles{{{{p0,p1,p2},false},{{p0,p2,p3},false}}};
   GeneratedShadowSettings cfg;cfg.grid_half_size=n;cfg.texture_dimension=n*2;
   const auto hard=build_game_generated_object_shadow(field,triangles,light,cfg);for(auto&t:triangles)t.blurred_group=true;
   const auto soft=build_game_generated_object_shadow(field,triangles,light,cfg);
   require(hard&&soft,"filter equivalence fixture has no receiver");
   require(hard->receiver_indices==soft->receiver_indices&&hard->receiver_vertices.size()==soft->receiver_vertices.size(),"filter changed receiver topology");
   for(size_t i=0;i<soft->receiver_vertices.size();i++){
    const auto&a=hard->receiver_vertices[i];const auto&b=soft->receiver_vertices[i];
    require(a.position.x==b.position.x&&a.position.y==b.position.y&&a.position.z==b.position.z&&a.u==b.u&&a.v==b.v&&a.distance_attenuation==b.distance_attenuation&&a.contact_path==b.contact_path,"filter changed receiver geometry or fade");
   }
   for(int py=0;py<int(n);py++)for(int px=0;px<int(n);px++){
    unsigned sum=0;for(int ky=-2;ky<=2;ky++)for(int kx=-2;kx<=2;kx++){
     const int sx=px+kx,sy=py+ky;if(sx<0||sy<0||sx>=int(n)||sy>=int(n))continue;
     sum+=kernel[ky+2][kx+2]*((hard->mask.pixels[(sy*n+sx)*4+3]>>5)&7);
    }
    require(soft->mask.pixels[(py*n+px)*4+3]==std::min(sum,255U),"SIMD filter changed the scalar mask bytes");
   }
   cases++;
  }
 }
 std::cout<<"SHADOW_MASK cases="<<cases<<" scalar_reference_exact=1 receiver_unchanged=1 odd_width_tail=1\n";
}
}
int main()try{lighting();masks();std::cout<<"SHADOW_PERFORMANCE_EQUIVALENCE PASS\n";}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
