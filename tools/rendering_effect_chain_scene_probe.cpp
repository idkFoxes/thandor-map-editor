// Stock EFF/SHT scene expansion and recoverable budget regression fixtures.
// Explicit GPU/window boundary stubs: this checks CPU geometry/material submission,
// not Vulkan pixels or a replay of the uploaded simulations.
// Executes the real editor CPU scene builder with only the GPU/window boundary stubbed.
// This is an explicit developer probe, not Vulkan playback validation.
#include "editor_app.hpp"
#include <imgui.h>
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/simulation_lab_environment.hpp"
#include "thandor/simulation_lab_catalog.hpp"
#include <algorithm>
using namespace thandor;
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <iostream>
namespace editor {
static unsigned object_uploads;
static size_t static_caster_vertices;
static std::vector<bool> texture_has_alpha;
static std::vector<thandor::RgbaImage> texture_images;
VulkanContext::VulkanContext(GLFWwindow* w):window_(w){max_image_array_layers_=std::getenv("THANDOR_TEST_IMAGE_LIMIT")?uint32_t(std::atoi(std::getenv("THANDOR_TEST_IMAGE_LIMIT"))):4096;}
VulkanContext::~VulkanContext()=default;
void VulkanContext::wait_idle()const{}
TerrainRenderer::TerrainRenderer(VulkanContext& c):context_(c){}
TerrainRenderer::~TerrainRenderer()=default;
void TerrainRenderer::update_mesh(const thandor::FieldGrid&,const thandor::TerrainLighting&,const thandor::TerrainTextureLayout*,const thandor::TerrainSurfaceTextureLayout*,bool,uint32_t,uint32_t){}
void TerrainRenderer::update_materials(std::span<const thandor::RgbaImage>){}
void TerrainRenderer::update_surface_materials(std::span<const thandor::RgbaImage>){}
void TerrainRenderer::update_objects(std::span<const ObjectVertex> v,std::span<const uint32_t> i,std::span<const ObjectDrawRun>r,std::span<const ObjectVertex>,std::span<const uint32_t>si,std::span<const thandor::RgbaImage>t){static_caster_vertices=std::count_if(v.begin(),v.end(),[](const auto& x){return x.casts_shadow!=0;});++object_uploads;shadow_index_count_=si.size();texture_images.assign(t.begin(),t.end());texture_has_alpha.clear();for(const auto&im:t){bool visible=false;for(size_t a=3;a<im.pixels.size();a+=4)visible|=im.pixels[a]>=7;texture_has_alpha.push_back(visible);}
object_index_count_=i.size();object_draw_runs_.assign(r.begin(),r.end());object_texture_layer_count_=t.size();}
struct EditorUiValidation {
 static void run(EditorApp& app){
  ImGui::GetIO().DisplaySize={1600,1000};app.sound_playback_enabled_=false;
  std::vector<std::string> maps{"s20_posten","s21_marsch","hargonatoll"};
  if(std::getenv("THANDOR_EFFECT_ALL_MAPS")){
   maps.clear();for(const auto& g:app.catalog_.map_groups())maps.push_back(g.level_path);
  }
  for(const auto& name:maps){
   std::cout<<"LOAD "<<name<<std::endl;
   auto g=std::find_if(app.catalog_.map_groups().begin(),app.catalog_.map_groups().end(),[&](const auto& g){return g.level_path==name||g.level_path.ends_with(std::string("/")+name+".lev");});
   if(g==app.catalog_.map_groups().end())throw std::runtime_error("map missing");
   app.load_map(g-app.catalog_.map_groups().begin());if(!app.document_)throw std::runtime_error(app.status_);
   app.center_camera();app.camera_.distance*=.8F;app.animate_timed_effects_=true;app.animate_resource_fields_=true;app.animate_buildings_=true;app.animate_units_=true;
   for(uint32_t tick:{0U,1U,20U,100U,500U,1000U,2048U,10000U,75000U}){
    std::cout<<"BUILD "<<name<<" tick="<<tick<<std::endl;
    app.current_presentation_tick_=tick;app.current_building_animation_tick_=tick;app.rebuild_dynamic_scene_cpu();
    if(!app.asset_load_failures_.empty())throw std::runtime_error("stock effects required fallback: "+app.asset_load_failures_.begin()->first);
    const auto expected=app.dynamic_scene_cpu_.indices.size();
    app.rebuild_dynamic_scene_cpu(true);
    if(app.dynamic_scene_cpu_.indices.size()!=expected)throw std::runtime_error("cached camera rebuild changed effect geometry");
    if(!app.asset_load_failures_.empty())throw std::runtime_error("cached effects required fallback");
    std::cout<<"PASS triangles="<<expected/3<<" cached_replay=PASS"<<std::endl;
   }
  }
  // Exercise pathological optional data without touching the game installation.
  auto g=std::find_if(app.catalog_.map_groups().begin(),app.catalog_.map_groups().end(),[](const auto& g){return g.level_path.ends_with("/s20_posten.lev");});
  app.load_map(g-app.catalog_.map_groups().begin());app.center_camera();
  auto* effect=const_cast<EffectDefinitionRecord*>(app.catalog_.effect_definition(25));
  const auto saved=*effect;bool found_attachment=false;
  for(uint32_t id=1;id<512&&!found_attachment;++id){
   const auto* other=app.catalog_.effect_definition(id);if(!other)continue;
   const auto& sprite=app.cached_sprite(other->sprite_path);
   for(const auto& attachment:sprite.lookup_records())if(attachment.key_class().value()==3U){
    effect->sprite_path=other->sprite_path;found_attachment=true;break;
   }
  }
  if(!found_attachment)throw std::runtime_error("no class-3 test emitter");
  auto& indices=const_cast<std::vector<uint32_t>&>(app.cached_sprite(effect->sprite_path).mesh().indices);
  const auto saved_indices=indices;indices.clear();
  effect->runtime_value_0c=32;effect->frame_advance_threshold_q4=16;
  effect->periodic_effect_id=25;effect->periodic_effect_interval_ticks=1;
  effect->linked_effect_present=0;effect->linked_shot_present=0;
  app.current_presentation_tick_=100;app.rebuild_dynamic_scene_cpu();
  if(!app.asset_load_failures_.contains("effect-preview/25"))throw std::runtime_error("pathological chain did not trigger recoverable limit");
  const auto warnings=app.asset_load_failures_.size();app.rebuild_dynamic_scene_cpu(true);
  if(app.asset_load_failures_.size()!=warnings)throw std::runtime_error("repeated chain warning was not deduplicated");
  *effect=saved;indices=saved_indices;app.asset_load_failures_.clear();app.rebuild_dynamic_scene_cpu();
  if(!app.asset_load_failures_.empty())throw std::runtime_error("stock scene did not recover after pathological fixture");
  std::cout<<"PASS pathological chain bounded, warning deduplicated, stock scene recovered"<<std::endl;
 }
};
}
int main(int argc,char** argv)try{if(argc!=2)return 2;editor::VulkanContext vk(nullptr);editor::EditorApp app(nullptr,vk,argv[1]);editor::EditorUiValidation::run(app);}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
