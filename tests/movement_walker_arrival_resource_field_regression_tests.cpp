#include "thandor/catalog.hpp"
#include "thandor/effect.hpp"
#include "thandor/field.hpp"
#include "thandor/simulation.hpp"
#include "articulated_walker_pose.hpp"
#include "model_transform.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif
namespace {
void req(bool v,const char* m){if(!v)throw std::runtime_error(m);} 
std::string text(const std::filesystem::path&p){std::ifstream f(p,std::ios::binary);std::ostringstream s;s<<f.rdbuf();return s.str();}
std::vector<thandor::ModelVisualNode> hierarchy(){std::vector<thandor::ModelVisualNode> h(9);h[0].parent_index=std::numeric_limits<uint32_t>::max();for(uint32_t side=0;side<2;++side){uint32_t b=1+side*4;h[b].parent_index=0;h[b].child_slot=side;for(uint32_t d=1;d<4;++d){h[b+d].parent_index=b+d-1;h[b+d].child_slot=0;}}return h;}
std::vector<editor::ModelHierarchyTransform> transforms(float x){std::vector<editor::ModelHierarchyTransform> t(9);t[0]=editor::placement_root_transform(x,0,0,0);for(uint32_t side=0;side<2;++side){uint32_t b=1+side*4;float y=side?-.3f:.3f;t[b]=editor::placement_root_transform(x,y,0,0);t[b+1]=editor::placement_root_transform(x,y,-.15f,0);t[b+2]=editor::placement_root_transform(x+.2f,y,-.4f,0);t[b+3]=editor::placement_root_transform(x+.35f,y,-.65f,0);}return t;}
}
int main(int argc,char**argv)try{
  auto h=hierarchy(); thandor::SimulationActorState a; a.body_yaw_turn16=0; a.move_target=thandor::Vec3f{1.0f,0,0};
  auto seed=transforms(0); auto ps=editor::apply_articulated_walker_support_pose(h,seed,1100,1400,250,&a,[](float,float){return 0.f;},[](uint32_t){return 0.f;}); req(ps.applied,"seed pose"); a.position=ps.root_position;
  thandor::SimulationMotionProfile m; m.speed_q12_per_tick=410; // ~0.10 world/tick
  float max_post_target_delta=0.f; bool cleared=false; float prior=a.position.x;
  for(int tick=0;tick<80;++tick){
    auto prev=a.position; auto prevYaw=a.body_yaw_turn16;
    thandor::simulation_tick_articulated_actor_motion(a,m,1400);
    thandor::simulation_update_articulated_contact_transition(a,prev,prevYaw,a.position,a.body_yaw_turn16,1400);
    auto tr=transforms(a.position.x); auto pose=editor::apply_articulated_walker_support_pose(h,tr,1100,1400,250,&a,[](float,float){return 0.f;},[](uint32_t){return 0.f;}); req(pose.applied,"live pose"); a.position=pose.root_position;
    float delta=std::abs(a.position.x-prior); prior=a.position.x;
    if(!a.move_target){cleared=true;max_post_target_delta=std::max(max_post_target_delta,delta);}
    if(cleared&&!a.articulated_contact_transition_active)break;
  }
  req(cleared,"walker never reached target");
  req(max_post_target_delta<0.20f,"walker arrival root discontinuity is too large");

  const auto root=std::filesystem::path(THANDOR_SOURCE_DIR); const auto src=text(root/"src/editor/editor_app.cpp"); const auto hdr=text(root/"src/editor/editor_app.hpp");
  req(src.find("actor.position=articulated_pose.root_position")!=std::string::npos,"class-3 solved root is not persisted");
  req(src.find("Animate Xenite / Tritium resource fields")!=std::string::npos,"resource-field animation control missing");
  req(src.find("controller.owner_is_resource_field")!=std::string::npos && src.find("source.owner_is_resource_field")!=std::string::npos,"resource timed effects still share only generic gate");
  req(hdr.find("bool animate_resource_fields_{true}")!=std::string::npos,"resource animation is not default-on");

  if(argc>=2){auto c=thandor::AssetCatalog::scan_game_root(argv[1]);size_t count=0;for(auto const&p:c.army_presets())if(p.visual_kind==thandor::PlacementVisualKind::PowerResource||p.visual_kind==thandor::PlacementVisualKind::BuildingResource){++count;req(p.runtime_model_class_id==4,"resource runtime class drift");req((p.timed_effect_id==114||p.timed_effect_id==116),"resource timed EFF drift");req(p.timed_effect_interval_ticks==80&&p.timed_effect_random_ticks==10,"resource cadence drift");}req(count==4,"resource preset census drift");std::cout<<" stock_resource_presets="<<count;}
  std::cout<<" V451_WALKER_ARRIVAL_RESOURCE_FIELD PASS\n";return 0;
}catch(const std::exception&e){std::cerr<<"V451 walker/resource regression failed: "<<e.what()<<"\n";return 1;}
