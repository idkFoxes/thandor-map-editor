#include "../src/editor/directional_shadow.hpp"
#include <iostream>
#include <stdexcept>
using namespace editor;
static void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
static bool near(float a,float b){return std::abs(a-b)<0.00002F;}
int main()try{
 for(const Vec3f ray:std::array<Vec3f,5>{{{0.7F,0.35F,-1},{-1,0.2F,-0.1F},{0,0,-1},{0,1,0},{1,2,3}}}){
  const auto m=directional_shadow_matrix(ray,{20,-30,4},64,512,2048);
  const Vec3f caster{23,-32,8};const auto along=add(caster,mul(normalize(ray),12));
  const auto a=multiply(m,Vec4f{caster.x,caster.y,caster.z,1});const auto b=multiply(m,Vec4f{along.x,along.y,along.z,1});
  require(near(a.x,b.x)&&near(a.y,b.y),"ray changes shadow texel: wrong light direction");
  require(b.z>a.z,"Vulkan shadow depth must increase away from the sun");
  for(float f:m.m)require(std::isfinite(f),"vertical/horizontal light produced nonfinite matrix");
 }
 const auto a=directional_shadow_matrix({0,0,-1},{0,0,0},64,512,2048);
 const auto b=directional_shadow_matrix({0,0,-1},{0.001F,0.001F,0},64,512,2048);
 require(a.m==b.m,"sub-texel pan makes stationary shadows swim");
 const auto near_point=multiply(a,Vec4f{0,0,512,1}),far_point=multiply(a,Vec4f{0,0,-512,1});
 require(near(near_point.z,0)&&near(far_point.z,1),"shadow depth is not Vulkan zero-to-one");
 std::cout<<"DIRECTIONAL_SHADOW_MATH PASS ray_projection=5 stable_pan=1 depth_zero_to_one=1\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
