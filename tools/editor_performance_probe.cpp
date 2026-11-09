// Normal editor CPU instrumentation probe; no Vulkan frames are presented.
#include "editor_app.hpp"
#include "thandor/editor_version.hpp"
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
 static void run(EditorApp& app,const char* path){
  ImGui::GetIO().DisplaySize={1600,1000};app.sound_playback_enabled_=false;
  if(app.simulation_mode_||app.simulation_lab_active())throw std::runtime_error("idle fixture started simulation");
  if(!app.editor_performance_log_.start(path,thandor::kEditorVersion))throw std::runtime_error("logger failed");
  for(const char* map:{"asgard","lavam"}){
   const auto group=std::find_if(app.catalog_.map_groups().begin(),app.catalog_.map_groups().end(),[&](const auto& g){return g.level_path.ends_with(std::string("/")+map+".lev");});
   if(group==app.catalog_.map_groups().end())throw std::runtime_error("map absent");
   const auto started=EditorPerformanceLog::Clock::now();
   app.load_map(group-app.catalog_.map_groups().begin());
   app.rebuild_mesh();app.rebuild_dynamic_scene_cpu();
   app.finish_editor_performance_frame(false,started);
   const auto context=app.editor_performance_context();
   if(context.simulation||context.lab||!context.field_width||!context.placements)throw std::runtime_error("invalid normal-editor context");
   app.editor_performance_log_.sample(context,true);
  }
  app.editor_performance_log_.finish(app.editor_performance_context(),"idle_cpu_probe");
  std::cout<<"EDITOR_IDLE_CPU_PROBE PASS maps=2 simulation=0 lab=0 presented=0 GPU_boundary_stubbed=1\n";
 }
};}
int main(int argc,char**argv)try{if(argc!=3)throw std::runtime_error("Usage: editor_performance_probe GAME_ROOT LOG_PATH");editor::VulkanContext vk(nullptr);editor::EditorApp app(nullptr,vk,argv[1]);editor::EditorUiValidation::run(app,argv[2]);}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
