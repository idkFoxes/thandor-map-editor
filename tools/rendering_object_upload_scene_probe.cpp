// Actual CPU EditorApp and production renderer with software Vulkan API recording.
// Audit actual worker snapshots and the final CPU walker mesh bounds after long playback.
// Usage follows editor_worker_motion_probe; GPU/window SDK boundaries are stubbed.
// GPU/window SDK boundary stubs; this is a CPU simulation diagnostic.
#include <bits/stdc++.h>
#define private public
#include "editor_app.hpp"
#undef private
#include "simulation_lab_diagnostic_writer.hpp"
#include <imgui.h>
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/simulation_lab_geometry.hpp"
#include "thandor/simulation_lab_environment.hpp"
#include "thandor/simulation_lab_catalog.hpp"
#include "thandor/simulation_lab_defaults.hpp"
using namespace thandor;
#include <fstream>
#include <bit>
#include <thread>
#include <chrono>
#include <iostream>

namespace upload_probe {
extern uint64_t idle_waits,submits,copies,copied_bytes,images_created,buffers_created,descriptor_writes,barriers;
void shaders(const std::filesystem::path&);
std::vector<uint8_t> buffer_data(VkBuffer,size_t);std::vector<uint8_t> image_layer(VkImage,uint32_t);
}
namespace editor {struct EditorUiValidation {
 static void run(EditorApp& app){
  ImGui::GetIO().DisplaySize={1600,1000};app.sound_playback_enabled_=false;
  const auto group=std::find_if(app.catalog_.map_groups().begin(),app.catalog_.map_groups().end(),[](const auto&g){return g.level_path.ends_with("/asgard.lev");});
  if(group==app.catalog_.map_groups().end())throw std::runtime_error("map absent");
  app.load_map(group-app.catalog_.map_groups().begin());
  app.simulation_lab_config_=simulation_lab_loaded_baseline_config(app.ai_knowledge_->parameters());
  app.simulation_lab_config_.run_count=1;app.simulation_lab_config_.duration_ticks=36240;app.start_simulation_lab();
  uint64_t rebuilds=0;double rebuild_ms=0;
  const auto initial_bytes=upload_probe::copied_bytes,initial_images=upload_probe::images_created,initial_waits=upload_probe::idle_waits,initial_copies=upload_probe::copies;
  for(uint32_t tick=0;tick<36240;tick+=256){
   const auto target=std::min(tick+256,36240U);app.simulation_lab_worker_.request_advance(target-tick);
   const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
   while(app.simulation_lab_worker_.snapshot()->runs()[0].tick<target){
    if(auto error=app.simulation_lab_worker_.error())throw std::runtime_error(*error);
    if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("worker timeout");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
   }
   app.simulation_lab_snapshot_=app.simulation_lab_worker_.snapshot();app.sync_selected_simulation_lab_run();
   if(app.simulation_static_replacement_dirty_){
    app.simulation_static_replacement_dirty_=false;const auto start=std::chrono::steady_clock::now();app.rebuild_objects();
    rebuild_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();++rebuilds;
   }else app.rebuild_dynamic_scene_cpu();
   auto& renderer=app.renderer_;renderer.prepare_static_frame(0);uint64_t h=1469598103934665603ULL;
   const auto bytes=[&](std::span<const uint8_t> data){for(const auto b:data)h=(h^b)*1099511628211ULL;};
   const auto values=[&](const auto& data){bytes({reinterpret_cast<const uint8_t*>(data.data()),data.size()*sizeof(data[0])});};
   values(app.dynamic_scene_cpu_.vertices);values(app.dynamic_scene_cpu_.indices);
   values(app.dynamic_scene_cpu_.shadow_vertices);values(app.dynamic_scene_cpu_.shadow_indices);values(app.fog_preview_runtime_cells_);
#ifdef THANDOR_UPLOAD_BASELINE
   const auto vertex_bytes=renderer.object_vertex_buffers_[0].size,shadow_bytes=renderer.shadow_vertex_buffers_[0].size;
#else
   const auto vertex_bytes=renderer.retained_object_vertices_.size()*sizeof(ObjectVertex),shadow_bytes=renderer.retained_shadow_vertices_.size()*sizeof(ObjectVertex);
#endif
   if(vertex_bytes)bytes(upload_probe::buffer_data(renderer.object_vertex_buffers_[0].buffer,vertex_bytes));
   if(renderer.object_index_count_)bytes(upload_probe::buffer_data(renderer.object_index_buffers_[0].buffer,renderer.object_index_count_*4));
   if(shadow_bytes)bytes(upload_probe::buffer_data(renderer.shadow_vertex_buffers_[0].buffer,shadow_bytes));
   if(renderer.shadow_index_count_)bytes(upload_probe::buffer_data(renderer.shadow_index_buffers_[0].buffer,renderer.shadow_index_count_*4));
   for(uint32_t layer=0;layer<renderer.object_texture_layer_count();++layer)bytes(upload_probe::image_layer(
#if THANDOR_FRAME_VERSIONED_OBJECT_ATLAS
   renderer.object_texture_frame_images_[0].image,
#else
   renderer.object_textures_.image,
#endif
   layer));
   std::cout<<"SCENE tick="<<target<<" entities="<<app.simulation_lab_snapshot_->runs()[0].entities.size()<<" templates="<<app.dynamic_shadow_templates_.size()<<" layers="<<renderer.object_texture_layer_count()<<" digest="<<h<<'\n';
  }
  std::cout<<"UPLOAD_TOTAL rebuilds="<<rebuilds<<" rebuild_cpu_ms="<<rebuild_ms<<" image_creations="<<upload_probe::images_created-initial_images<<" idle_calls="<<upload_probe::idle_waits-initial_waits<<" copied_layers="<<upload_probe::copies-initial_copies<<" copied_bytes="<<upload_probe::copied_bytes-initial_bytes<<" GPU_verified=0\n";
#ifndef THANDOR_UPLOAD_BASELINE
  const auto& stats=app.renderer_.object_upload_stats();
  std::cout<<"UPLOAD_STATS skipped="<<stats.skipped<<" recreates="<<stats.array_recreates<<" partial_uploads="<<stats.partial_uploads<<" resampled="<<stats.resampled<<" reused="<<stats.reused<<" buffer_writes="<<stats.buffer_writes<<'\n';
#endif
 }
};}
int main(int argc,char**argv)try{
 if(argc!=2)throw std::runtime_error("Usage: rendering_object_upload_scene_probe GAME_ROOT");
 const auto dir=std::filesystem::temp_directory_path()/("thandor-scene-upload-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));upload_probe::shaders(dir);
 {editor::VulkanContext vk(nullptr);editor::EditorApp app(nullptr,vk,argv[1]);editor::EditorUiValidation::run(app);}
 std::filesystem::remove_all(dir);
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
