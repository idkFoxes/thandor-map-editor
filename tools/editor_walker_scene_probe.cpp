// Audit actual worker snapshots and the final CPU walker mesh bounds after long playback.
// Usage follows editor_worker_motion_probe; GPU/window SDK boundaries are stubbed.
// GPU/window SDK boundary stubs; this is a CPU simulation diagnostic.
#include "editor_app.hpp"
#include "simulation_lab_diagnostic_writer.hpp"
#include <imgui.h>
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/simulation_lab_geometry.hpp"
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
  size_t support_samples=0,support_mismatches=0;float max_support_error=0;
  for(uint32_t tick=0;tick<ticks;tick+=32){
   app.simulation_lab_worker_.request_advance(std::min(32U,ticks-tick));
   auto snapshot=app.simulation_lab_worker_.snapshot();
   const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
   while(snapshot->runs()[0].tick<std::min(tick+32,ticks)){
    if(auto error=app.simulation_lab_worker_.error())throw std::runtime_error(*error);
    if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("worker timeout");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));snapshot=app.simulation_lab_worker_.snapshot();
   }
   const auto& run=snapshot->runs()[0];
   for(const auto& e:run.entities){if(e.runtime_model_class_id!=3||!e.articulated_contact_geometry_enabled||!e.actor.articulated_contacts_initialized)continue;
    const auto&a=e.actor;auto p=a.articulated_foot_anchor_world;
    if(a.articulated_contact_transition_active&&(a.articulated_was_moving||a.articulated_contact_settling)){auto side=a.articulated_swing_side&1U;float t=static_cast<float>(a.articulated_contact_progress_q12)/4096.F;p[side].x+=(a.articulated_foot_target_world[side].x-p[side].x)*t;p[side].y+=(a.articulated_foot_target_world[side].y-p[side].y)*t;}
    float error=std::hypot(a.position.x-(p[0].x+p[1].x)*.5F,a.position.y-(p[0].y+p[1].y)*.5F);++support_samples;max_support_error=std::max(error,max_support_error);support_mismatches+=error>1.F/4096.F;
   }
   if(entity>=run.entities.size())continue;
   const auto& e=run.entities.at(entity);
   auto t=e.actor.move_target.value_or(e.actor.position),w=e.actor.move_waypoint.value_or(e.actor.position);
   std::cout<<"M tick="<<run.tick<<" x="<<e.actor.position.x<<" y="<<e.actor.position.y<<" yaw="<<e.actor.body_yaw_turn16<<" target="<<bool(e.actor.move_target)<<" tx="<<t.x<<" ty="<<t.y<<" waypoint="<<bool(e.actor.move_waypoint)<<" wx="<<w.x<<" wy="<<w.y<<" choice="<<e.military_ai_last_choice.kind<<" cooldown="<<e.military_ai_busy_cooldown<<" orders="<<run.metrics.military_ai_site_orders<<" blocks="<<run.metrics.navigation_actual_step_blocks<<" score="<<e.military_ai_last_choice.score<<" site="<<e.military_ai_last_choice.site_index<<" follow="<<e.combat_follow_owned<<" cmdmode="<<e.combat_command_mode_flags<<" route="<<run.metrics.navigation_route_resolves<<'\n';
  }
  const auto final=app.simulation_lab_worker_.snapshot();const auto& run=final->runs()[0];
  std::cout<<"SUPPORT_SWEEP samples="<<support_samples<<" mismatches="<<support_mismatches<<" max_error="<<max_support_error<<" ticks="<<run.tick<<" stride=32 gpu_verified=0\n";
  app.simulation_lab_snapshot_=final;app.simulation_lab_selected_run_=0;app.fog_preview_mode_=0;
  app.sync_selected_simulation_lab_run();app.rebuild_objects();
  for(const auto& receipt:app.simulation_lab_render_receipts_){
    if(receipt.entity>=run.entities.size())continue;const auto&e=run.entities[receipt.entity];if(e.runtime_model_class_id!=3)continue;
    const auto p=e.actor.position;const auto lo=receipt.bounds_min,hi=receipt.bounds_max;
    const float dx=std::max({lo.x-p.x,0.F,p.x-hi.x}),dy=std::max({lo.y-p.y,0.F,p.y-hi.y});
    std::cout<<"WALKER_DRAW entity="<<receipt.entity<<" arm="<<e.army_asset_id<<" result="<<receipt.result<<" valid="<<receipt.bounds_valid<<" distance_to_bounds="<<std::hypot(dx,dy)<<" triangles="<<receipt.triangles<<" root_x="<<p.x<<" root_y="<<p.y<<" min_x="<<lo.x<<" min_y="<<lo.y<<" max_x="<<hi.x<<" max_y="<<hi.y<<" gpu_verified=0\n";
    write_walker_contact_diagnostics(std::cout,0,run.tick,receipt.entity,e);
  }
  auto statics=simulation_lab_prepare_static_navigation_blockers(app.catalog_,app.document_->level());
  for(size_t i=0;i<run.entities.size();++i){const auto& e=run.entities[i];
   if(!e.actor.move_target||e.runtime_removed||e.root_model_terminal)continue;
   std::cout<<"DETAIL entity="<<i<<" cls="<<e.runtime_model_class_id<<" yaw="<<e.actor.body_yaw_turn16<<" speed="<<e.motion.speed_q12_per_tick<<" yawmax="<<e.motion.yaw_max_velocity_turn16<<" accel="<<e.motion.yaw_acceleration_turn16<<" low="<<e.traversal_low_band_index<<" high="<<e.traversal_high_band_index<<"\n";
   if(e.runtime_model_class_id==3){
   const auto&a=e.actor;
   std::cout<<"CONTACT entity="<<i<<" active="<<a.articulated_contact_transition_active<<" progress="<<a.articulated_contact_progress_q12<<" side="<<a.articulated_swing_side<<" event="<<a.articulated_contact_event_serial<<" moving="<<a.articulated_was_moving<<" settling="<<a.articulated_contact_settling<<" pose="<<a.articulated_pose_transition_active<<'\n';
   for(unsigned side=0;side<2;++side){const auto& f=a.articulated_foot_anchor_world[side];const auto& t=a.articulated_foot_target_world[side];std::cout<<"FOOT entity="<<i<<" side="<<side<<" ax="<<f.x<<" ay="<<f.y<<" tx="<<t.x<<" ty="<<t.y<<" heading="<<a.articulated_contact_heading_turn16[side]<<" target_heading="<<a.articulated_contact_target_heading_turn16[side]<<'\n';}}
   const auto target=*e.actor.move_target;const auto w=e.actor.move_waypoint.value_or(e.actor.position);
   std::cout<<"FINAL entity="<<i<<" arm="<<e.army_asset_id<<" x="<<e.actor.position.x<<" y="<<e.actor.position.y<<" tx="<<target.x<<" ty="<<target.y<<" waypoint="<<bool(e.actor.move_waypoint)<<" wx="<<w.x<<" wy="<<w.y<<" radius="<<e.motion.collision_radius_q12<<" follow="<<e.combat_follow_owned<<" flags="<<e.combat_movement.flags<<" linked="<<e.production_linked_to_parent<<'\n';
   auto report=[&](int kind,size_t j,V528PathRuntimeBlocker b){
    const int64_t dx=int64_t(float_to_q12(target.x))-b.world_x_q12,dy=int64_t(float_to_q12(target.y))-b.world_y_q12;
    const int64_t radius=int64_t(e.motion.collision_radius_q12)+b.collision_radius_q12;
    if(e.motion.collision_radius_q12>0&&b.collision_radius_q12>0&&dx*dx+dy*dy<=radius*radius)
     std::cout<<"TARGET_OVERLAP entity="<<i<<" blocker_kind="<<kind<<" blocker="<<j<<" bx="<<q12_to_float(b.world_x_q12)<<" by="<<q12_to_float(b.world_y_q12)<<" br="<<b.collision_radius_q12<<'\n';
   };
   auto a=e.actor;if(e.runtime_model_class_id==3)simulation_tick_articulated_actor_motion(a,e.motion,e.articulated_contact_interpolation_rate_q12);else simulation_tick_actor_motion(a,e.motion);
   auto hit=[&](int kind,size_t j,V528PathRuntimeBlocker b){if(simulation_lab_step_hits_blocker(e.actor.position,a.position,e.motion.collision_radius_q12,b))std::cout<<"STEP_HIT entity="<<i<<" kind="<<kind<<" other="<<j<<" x="<<q12_to_float(b.world_x_q12)<<" y="<<q12_to_float(b.world_y_q12)<<" radius="<<b.collision_radius_q12<<" step_x="<<a.position.x<<" step_y="<<a.position.y<<'\n';};
   for(size_t j=0;j<statics.size();++j){report(0,j,statics[j]);hit(0,j,statics[j]);}
   for(size_t j=0;j<run.entities.size();++j){const auto& other=run.entities[j];if(i==j||other.runtime_removed||!other.construction_complete)continue;
    const V528PathRuntimeBlocker b{float_to_q12(other.actor.position.x),float_to_q12(other.actor.position.y),other.motion.collision_radius_q12};report(1,j,b);hit(1,j,b);
   }
  }

 }
};
}
int main(int argc,char**argv)try{if(argc!=3)throw std::runtime_error("Usage: editor_walker_scene_probe GAME_ROOT MAP (optional THANDOR_TRACE_TICKS / THANDOR_TRACE_ENTITY)");editor::VulkanContext vk(nullptr);editor::EditorApp app(nullptr,vk,argv[1]);editor::EditorUiValidation::run(app,argv[2]);}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
