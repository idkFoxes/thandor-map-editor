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
 static void run(EditorApp& app,const char* mapname){
  ImGui::GetIO().DisplaySize={1600,1000};
  const std::string map=mapname;
  const auto group=std::find_if(app.catalog_.map_groups().begin(),app.catalog_.map_groups().end(),[&](const auto& g){return g.level_path.ends_with("/"+map+".lev");});
  if(group==app.catalog_.map_groups().end())throw std::runtime_error("map absent");
  app.load_map(group-app.catalog_.map_groups().begin());
  app.simulation_lab_config_=simulation_lab_loaded_baseline_config(app.ai_knowledge_->parameters());
  app.simulation_lab_semantic_row_=1;app.simulation_lab_config_.run_count=1;
  app.start_simulation_lab();const auto initial=app.simulation_lab_worker_.snapshot();
  if(!initial)throw std::runtime_error(app.status_);
  const auto runtime=simulation_lab_prepare_runtime_catalog(app.catalog_);
  std::vector<uint32_t> ids;for(const auto& [id,shot]:runtime->shots){(void)shot;ids.push_back(id);}std::sort(ids.begin(),ids.end());
  for(bool all:{false,true}){
   auto frame=std::make_shared<SimulationLabExperiment>(initial->snapshot_copy());auto& run=frame->runs()[0];run.projectiles.clear();
   const auto cell=app.document_->field().at(app.document_->field().width()/2,app.document_->field().height()/2);
   Vec3f origin{q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),q12_to_float(cell.terrain_height_q12)+5};
   const Vec3f target{origin.x+2,origin.y,origin.z};uint64_t serial=1;
   for(uint32_t id:ids){if(!all&&id!=0)continue;SimulationWeaponProfile weapon;weapon.shot_definition_id=id;weapon.reload_ticks=1;weapon.launch_attachment_count=1;
    const std::array<Vec3f,1> launch{origin};auto volley=simulation_create_volley(0,weapon,launch,target,runtime->shots.at(id),0,serial);
    run.projectiles.insert(run.projectiles.end(),volley.begin(),volley.end());}
   app.simulation_lab_snapshot_=frame;app.simulation_lab_selected_run_=0;app.fog_preview_mode_=0;
   app.sync_selected_simulation_lab_run();app.rebuild_objects();
   constexpr uint64_t key=uint64_t{1}<<63U;
   const auto layer=app.object_texture_layer_lookup_.find(key);
   if(layer==app.object_texture_layer_lookup_.end()||!app.first_object_build_error_.empty())throw std::runtime_error("reported SHT0 texture was not resident in actual scene");
   size_t vertices=0;for(const auto& v:app.dynamic_scene_cpu_.vertices)vertices+=v.texture_layer==layer->second;
   if(vertices==0||run.projectiles.empty())throw std::runtime_error("reported layer was not used by a live projectile CPU scene");
   app.rebuild_dynamic_scene_cpu();
   std::cout<<"RUNTIME_TEXTURE map="<<map<<" mode="<<(all?"all_shots":"laser")<<" projectiles="<<run.projectiles.size()<<" reported_key="<<key<<" resident=1 vertices="<<vertices<<" layers="<<app.object_texture_layer_lookup_.size()<<" dynamic_rebuild=1 gpu_verified=0\n";
  }
 }
};
}
int main(int argc,char**argv)try{if(argc!=3)throw std::runtime_error("Usage: editor_runtime_texture_probe GAME_ROOT MAP");editor::VulkanContext vk(nullptr);editor::EditorApp app(nullptr,vk,argv[1]);editor::EditorUiValidation::run(app,argv[2]);}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
