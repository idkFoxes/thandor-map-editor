// Drive the actual GUI worker; published display snapshots cannot be stepped as full worlds.
// GPU/window SDK boundary stubs; this is a CPU simulation diagnostic.
#include "editor_app.hpp"
#include <imgui.h>
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/simulation_lab_environment.hpp"
#include "thandor/simulation_lab_catalog.hpp"
#include "thandor/simulation_lab_defaults.hpp"
using namespace thandor;
#include <fstream>
#include <thread>
#include <chrono>
#include <iostream>
namespace editor {
static std::vector<bool> texture_has_alpha;
static std::vector<thandor::RgbaImage> texture_images;
VulkanContext::VulkanContext(GLFWwindow* w):window_(w){max_image_array_layers_=4096;}
VulkanContext::~VulkanContext()=default;
void VulkanContext::wait_idle()const{}
TerrainRenderer::TerrainRenderer(VulkanContext& c):context_(c){}
TerrainRenderer::~TerrainRenderer()=default;
void TerrainRenderer::update_mesh(const thandor::FieldGrid&,const thandor::TerrainLighting&,const thandor::TerrainTextureLayout*,const thandor::TerrainSurfaceTextureLayout*,bool,uint32_t,uint32_t){}
void TerrainRenderer::update_materials(std::span<const thandor::RgbaImage>){}
void TerrainRenderer::update_surface_materials(std::span<const thandor::RgbaImage>){}
void TerrainRenderer::update_objects(std::span<const ObjectVertex>,std::span<const uint32_t> i,std::span<const ObjectDrawRun>r,std::span<const ObjectVertex>,std::span<const uint32_t>,std::span<const thandor::RgbaImage>t){texture_images.assign(t.begin(),t.end());texture_has_alpha.clear();for(const auto&im:t){bool visible=false;for(size_t a=3;a<im.pixels.size();a+=4)visible|=im.pixels[a]>=7;texture_has_alpha.push_back(visible);}
object_index_count_=i.size();object_draw_runs_.assign(r.begin(),r.end());object_texture_layer_count_=t.size();}
struct EditorUiValidation {
 static void run(EditorApp& app,const char* case_file){
  ImGui::GetIO().DisplaySize={1600,1000};
  const std::string map=case_file;
  const auto group=std::find_if(app.catalog_.map_groups().begin(),app.catalog_.map_groups().end(),[&](const auto& g){return g.level_path.ends_with("/"+map+".lev");});
  if(group==app.catalog_.map_groups().end())throw std::runtime_error("map absent");
  app.load_map(group-app.catalog_.map_groups().begin());
  app.simulation_lab_config_=simulation_lab_loaded_baseline_config(app.ai_knowledge_->parameters());
  app.simulation_lab_semantic_row_=1;
  app.simulation_lab_config_.run_count=1;app.simulation_lab_config_.duration_ticks=43080;
  app.start_simulation_lab();
  const auto initial=app.simulation_lab_worker_.snapshot();if(!initial)throw std::runtime_error(app.status_);

  std::cout<<"INIT parameter="<<initial->config().parameter_index<<" fixed="<<initial->config().fixed_value<<'\n';
  const uint32_t ticks=std::getenv("THANDOR_TRACE_TICKS")?std::stoul(std::getenv("THANDOR_TRACE_TICKS")):8000;
  const uint32_t entity=std::getenv("THANDOR_TRACE_ENTITY")?std::stoul(std::getenv("THANDOR_TRACE_ENTITY")):19;
  for(uint32_t tick=0;tick<ticks;++tick){
   app.simulation_lab_worker_.request_advance(1);
   auto snapshot=app.simulation_lab_worker_.snapshot();
   const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
   while(snapshot->runs()[0].tick<tick+1){
    if(auto error=app.simulation_lab_worker_.error())throw std::runtime_error(*error);
    if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("worker timeout");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));snapshot=app.simulation_lab_worker_.snapshot();
   }
   const auto& run=snapshot->runs()[0];if(entity>=run.entities.size())continue;
   const auto& e=run.entities.at(entity);
   auto t=e.actor.move_target.value_or(e.actor.position),w=e.actor.move_waypoint.value_or(e.actor.position);
   std::cout<<"M tick="<<run.tick<<" x="<<e.actor.position.x<<" y="<<e.actor.position.y<<" yaw="<<e.actor.body_yaw_turn16<<" target="<<bool(e.actor.move_target)<<" tx="<<t.x<<" ty="<<t.y<<" waypoint="<<bool(e.actor.move_waypoint)<<" wx="<<w.x<<" wy="<<w.y<<" choice="<<e.military_ai_last_choice.kind<<" cooldown="<<e.military_ai_busy_cooldown<<" orders="<<run.metrics.military_ai_site_orders<<" blocks="<<run.metrics.navigation_actual_step_blocks<<" score="<<e.military_ai_last_choice.score<<" site="<<e.military_ai_last_choice.site_index<<" follow="<<e.combat_follow_owned<<" cmdmode="<<e.combat_command_mode_flags<<" route="<<run.metrics.navigation_route_resolves<<'\n';
  }
  const auto final=app.simulation_lab_worker_.snapshot();const auto& run=final->runs()[0];
  auto statics=simulation_lab_prepare_static_navigation_blockers(app.catalog_,app.document_->level());
  for(size_t i=0;i<run.entities.size();++i){const auto& e=run.entities[i];
   if(!e.actor.move_target||e.runtime_removed||e.root_model_terminal)continue;
   const auto target=*e.actor.move_target;const auto w=e.actor.move_waypoint.value_or(e.actor.position);
   std::cout<<"FINAL entity="<<i<<" arm="<<e.army_asset_id<<" x="<<e.actor.position.x<<" y="<<e.actor.position.y<<" tx="<<target.x<<" ty="<<target.y<<" waypoint="<<bool(e.actor.move_waypoint)<<" wx="<<w.x<<" wy="<<w.y<<" radius="<<e.motion.collision_radius_q12<<" follow="<<e.combat_follow_owned<<" flags="<<e.combat_movement.flags<<" linked="<<e.production_linked_to_parent<<'\n';
   auto report=[&](int kind,size_t j,V528PathRuntimeBlocker b){
    const int64_t dx=int64_t(float_to_q12(target.x))-b.world_x_q12,dy=int64_t(float_to_q12(target.y))-b.world_y_q12;
    const int64_t radius=int64_t(e.motion.collision_radius_q12)+b.collision_radius_q12;
    if(e.motion.collision_radius_q12>0&&b.collision_radius_q12>0&&dx*dx+dy*dy<=radius*radius)
     std::cout<<"TARGET_OVERLAP entity="<<i<<" blocker_kind="<<kind<<" blocker="<<j<<" bx="<<q12_to_float(b.world_x_q12)<<" by="<<q12_to_float(b.world_y_q12)<<" br="<<b.collision_radius_q12<<'\n';
   };
   for(size_t j=0;j<statics.size();++j)report(0,j,statics[j]);
   for(size_t j=0;j<run.entities.size();++j){const auto& other=run.entities[j];if(i==j||other.runtime_removed||!other.construction_complete)continue;
    report(1,j,{float_to_q12(other.actor.position.x),float_to_q12(other.actor.position.y),other.motion.collision_radius_q12});
   }
  }

 }
};
}
int main(int argc,char**argv)try{if(argc!=3)throw std::runtime_error("Usage: editor_worker_motion_probe GAME_ROOT MAP (optional THANDOR_TRACE_TICKS / THANDOR_TRACE_ENTITY)");editor::VulkanContext vk(nullptr);editor::EditorApp app(nullptr,vk,argv[1]);editor::EditorUiValidation::run(app,argv[2]);}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
