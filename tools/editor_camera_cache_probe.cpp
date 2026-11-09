// Compare camera-only cached CPU scenes with complete rebuilds across pan, zoom,
// pitch, rotation and viewport changes. GPU/window SDK boundaries are stubbed.
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
 static void run(EditorApp&app,const char* mapname){
  ImGui::GetIO().DisplaySize={1600,1000};app.sound_playback_enabled_=false;
  const std::string map=mapname;
  const auto group=std::find_if(app.catalog_.map_groups().begin(),app.catalog_.map_groups().end(),[&](const auto&g){return g.level_path.ends_with("/"+map+".lev");});
  if(group==app.catalog_.map_groups().end())throw std::runtime_error("map absent");
  app.load_map(group-app.catalog_.map_groups().begin());
  app.simulation_lab_config_=simulation_lab_loaded_baseline_config(app.ai_knowledge_->parameters());
  app.simulation_lab_config_.run_count=std::getenv("THANDOR_CACHE_RUNS")?std::stoul(std::getenv("THANDOR_CACHE_RUNS")):1;app.simulation_lab_config_.duration_ticks=64000;
  app.start_simulation_lab();
  const uint32_t ticks=std::getenv("THANDOR_TRACE_TICKS")?std::stoul(std::getenv("THANDOR_TRACE_TICKS")):28000;
  for(uint32_t tick=0;tick<ticks;tick+=256){
   const auto target=std::min(tick+256,ticks);app.simulation_lab_worker_.request_advance(target-tick);
   const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
   while(app.simulation_lab_worker_.snapshot()->runs()[0].tick<target){
    if(auto error=app.simulation_lab_worker_.error())throw std::runtime_error(*error);
    if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("worker timeout");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
   }
  }
  app.simulation_lab_snapshot_=app.simulation_lab_worker_.snapshot();app.simulation_lab_selected_run_=0;
  app.sync_selected_simulation_lab_run();app.rebuild_objects();
  std::cout<<"PROFILE_WORLD map="<<map<<" tick="<<ticks<<" entities="<<app.simulation_lab_snapshot_->runs()[0].entities.size()<<" templates="<<app.dynamic_shadow_templates_.size()<<std::endl;
  const auto digest=[&](){
   std::vector<std::byte> out;
   const auto bytes=[&](const auto& v){auto b=std::as_bytes(std::span(&v,1));out.insert(out.end(),b.begin(),b.end());};
   const auto vertices=[&](const auto& vs){bytes(vs.size());for(const auto&v:vs){bytes(v.position.x);bytes(v.position.y);bytes(v.position.z);bytes(v.normal.x);bytes(v.normal.y);bytes(v.normal.z);bytes(v.u);bytes(v.v);bytes(v.texture_layer);bytes(v.packed_color_argb);bytes(v.direct_tint_bypass);bytes(v.animation_v_amplitude);bytes(v.animation_cycle_ticks);}};
   const auto& scene=app.dynamic_scene_cpu_;vertices(scene.vertices);vertices(scene.shadow_vertices);
   bytes(scene.indices.size());bytes(scene.shadow_indices.size());bytes(scene.draw_runs.size());bytes(scene.shading_emitters.size());
   bytes(app.fog_preview_occupancy_history_.size());bytes(app.fog_preview_runtime_cells_.size());
   bytes(app.visible_animated_buildings_.size());bytes(app.visible_timed_effect_controllers_.size());bytes(app.simulation_lab_render_receipts_.size());
   for(auto i:scene.indices)bytes(i);for(auto i:scene.shadow_indices)bytes(i);
   for(const auto&v:scene.draw_runs){bytes(v.first_index);bytes(v.index_count);bytes(v.blend);bytes(v.two_sided);bytes(v.sort_group);bytes(v.retail_world_pass);}
   for(const auto&v:scene.shading_emitters){bytes(v.position.x);bytes(v.position.y);bytes(v.position.z);bytes(v.target_radius);bytes(v.color_r);bytes(v.color_g);bytes(v.color_b);bytes(v.cycle_ticks);bytes(v.spawn_tick);bytes(v.grow_ticks);bytes(v.stop_ticks);bytes(v.release_ticks);}
   for(auto i:app.fog_preview_occupancy_history_)bytes(i);for(auto i:app.fog_preview_runtime_cells_)bytes(i);
   for(auto i:app.visible_animated_buildings_)bytes(i);for(auto i:app.visible_timed_effect_controllers_)bytes(i);
   bytes(app.fog_preview_visible_cell_count_);bytes(app.fog_preview_explored_cell_count_);bytes(app.fog_preview_observer_count_);
   bytes(app.timed_effect_sprite_triangle_count_);bytes(app.lava_linked_shot_particle_count_);
   for(const auto&v:app.simulation_lab_render_receipts_){bytes(v.entity);bytes(v.run);bytes(v.tick);bytes(v.army);bytes(v.faction);bytes(v.root_model_definition);bytes(v.triangles);bytes(v.first_vertex);bytes(v.vertex_count);bytes(v.bounds_valid);bytes(v.bounds_min.x);bytes(v.bounds_min.y);bytes(v.bounds_min.z);bytes(v.bounds_max.x);bytes(v.bounds_max.y);bytes(v.bounds_max.z);for(auto c:std::string_view(v.result))bytes(c);}
   return out;
  };
  const auto base=app.camera_;
  std::size_t checked=0;double cached_ms=0,full_ms=0;std::size_t inline_effect_cases=0;
  const auto check=[&](const char* path,int sample){
   app.rebuild_dynamic_scene_cpu(true);auto cached=digest();const auto cm=app.dynamic_scene_build_milliseconds_;
   inline_effect_cases+=app.dynamic_model_cache_.steps.size()>1;
   app.rebuild_dynamic_scene_cpu();auto full=digest();const auto fm=app.dynamic_scene_build_milliseconds_;
   if(cached!=full){
    std::size_t k=0;while(k<std::min(cached.size(),full.size())&&cached[k]==full[k])++k;
    throw std::runtime_error(std::string("cached scene differs path=")+path+" sample="+std::to_string(sample)+" byte="+std::to_string(k)+" cached_size="+std::to_string(cached.size())+" full_size="+std::to_string(full.size()));
   }
   cached_ms+=cm;full_ms+=fm;++checked;
   std::cout<<"CAMERA path="<<path<<" sample="<<sample<<" cached_ms="<<cm<<" full_ms="<<fm<<" vertices="<<app.dynamic_scene_cpu_.vertices.size()<<" shadows="<<app.dynamic_scene_cpu_.shadow_vertices.size()<<" hits="<<app.dynamic_model_cache_hits_<<" view_misses="<<app.dynamic_view_cache_misses_<<'\n';
  };
  for(int fog:{0,2}){
   app.fog_preview_mode_=fog;app.camera_=base;app.rebuild_dynamic_scene_cpu();
   for(int path=0;path<6;++path){
    app.camera_=base;app.rebuild_dynamic_scene_cpu();
    for(int i=0;i<48;++i){
     const float t=float(i)/47.F;
     if(path==0){app.camera_.target.x=base.target.x+30.F*std::sin(t*6.2831853F);app.camera_.target.y=base.target.y+30.F*std::cos(t*6.2831853F);}
     if(path==1)app.camera_.distance=2.F+180.F*t;
     if(path==2)app.camera_.pitch=0.15F+1.25F*t;
     if(path==3)app.camera_.yaw=base.yaw+6.2831853F*t;
     if(path==4){app.camera_.yaw=base.yaw+6.2831853F*t;app.camera_.pitch=0.2F+1.F*t;app.camera_.distance=10.F+100.F*t;}
     if(path==5)ImGui::GetIO().DisplaySize={800.F+24.F*i,600.F+10.F*i};
     check((std::string("fog")+std::to_string(fog)+"_"+std::to_string(path)).c_str(),i);
    }
    ImGui::GetIO().DisplaySize={1600,1000};
   }
  }
  const auto invalidation=[&](const char* name){
   const auto misses=app.dynamic_model_cache_misses_;
   check(name,0);
   if(app.dynamic_model_cache_misses_<misses+2)throw std::runtime_error(std::string("stale state cache accepted: ")+name);
  };
  app.fog_preview_mode_=1;invalidation("fog_mode");
  app.fog_preview_faction_=2;invalidation("fog_faction");
  app.fog_preview_all_explored_=!app.fog_preview_all_explored_;invalidation("fog_explored");
  app.animate_units_=!app.animate_units_;invalidation("unit_animation");
  app.animate_buildings_=!app.animate_buildings_;invalidation("building_animation");
  app.animate_timed_effects_=!app.animate_timed_effects_;invalidation("effect_animation");
  app.animate_resource_fields_=!app.animate_resource_fields_;invalidation("resource_animation");
  app.simulation_lab_show_all_runs_=!app.simulation_lab_show_all_runs_;invalidation("run_overlay");
  app.lighting_cycle_preview_enabled_=!app.lighting_cycle_preview_enabled_;invalidation("lighting_mode");
  ++app.lighting_cycle_preview_tick_;invalidation("lighting_tick");
  app.simulation_lab_worker_.request_advance(32);
  const auto target=app.simulation_lab_snapshot_->runs()[0].tick+32;
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
  while(app.simulation_lab_worker_.snapshot()->runs()[0].tick<target){
   if(std::chrono::steady_clock::now()>deadline)throw std::runtime_error("snapshot advance timeout");
   std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  app.poll_simulation_lab_snapshot();app.sync_selected_simulation_lab_run();
  invalidation("new_snapshot");
  if(app.simulation_lab_runs().size()>1){
   app.simulation_lab_selected_run_=1;app.sync_selected_simulation_lab_run();invalidation("selected_run");
   app.simulation_lab_selected_run_=0;app.sync_selected_simulation_lab_run();invalidation("selected_run_restore");
  }
  // Own a mutable copy for a damaged synthetic-unit fixture; never mutate worker snapshots.
  auto damaged=std::make_shared<SimulationLabExperiment>(*app.simulation_lab_snapshot_);
  std::size_t damage_candidates=0;
  for(auto& entity:damaged->runs()[0].entities){
   if(!entity.synthetic||entity.building||entity.runtime_removed)continue;
   entity.root_model_maximum_integrity=100;entity.root_model_integrity=1;++damage_candidates;
  }
  app.simulation_lab_snapshot_=damaged;app.animate_timed_effects_=true;
  app.animate_units_=app.animate_buildings_=true;app.fog_preview_mode_=0;
  app.sync_selected_simulation_lab_run();app.camera_=base;app.rebuild_objects();
  bool damage_effect_seen=false;
  for(int i=0;i<64;++i){
   damaged->runs()[0].tick+=7;app.current_building_animation_tick_=damaged->runs()[0].tick;
   app.rebuild_dynamic_scene_cpu();
   if(app.dynamic_model_cache_.steps.size()>1){
    damage_effect_seen=true;
    for(int j=0;j<24;++j){app.camera_.yaw=base.yaw+float(j)*0.02F;check("damaged_unit_billboard",j);}
    break;
   }
  }
  std::cout<<"DAMAGE_FIXTURE candidates="<<damage_candidates<<" effect_seen="<<damage_effect_seen<<'\n';
  app.clear_simulation_lab();app.animate_units_=app.animate_buildings_=true;
  app.animate_timed_effects_=app.animate_resource_fields_=true;
  app.fog_preview_all_explored_=false;app.lighting_cycle_preview_enabled_=false;
  app.rebuild_objects();app.camera_=base;
  for(int fog:{0,2}){
   app.fog_preview_mode_=fog;app.rebuild_objects();app.rebuild_dynamic_scene_cpu();
   for(int i=0;i<48;++i){app.camera_.yaw=base.yaw+6.2831853F*float(i)/47.F;check("normal_editor_rotation",i);}
   ++app.current_presentation_tick_;invalidation("editor_tick");
  }
  std::cout<<"CACHE_PARITY PASS cases="<<checked<<" cached_ms="<<cached_ms<<" full_ms="<<full_ms<<" model_hits="<<app.dynamic_model_cache_hits_<<" view_misses="<<app.dynamic_view_cache_misses_<<" model_misses="<<app.dynamic_model_cache_misses_<<" fog_hits="<<app.dynamic_fog_cache_hits_<<" shadow_hits="<<app.dynamic_shadow_cache_hits_<<" inline_effect_cases="<<inline_effect_cases<<std::endl;

 }
};}
int main(int argc,char**argv)try{if(argc!=3)throw std::runtime_error("Usage: editor_camera_cache_probe GAME_ROOT MAP (optional THANDOR_TRACE_TICKS / THANDOR_TRACE_ENTITY)");editor::VulkanContext vk(nullptr);editor::EditorApp app(nullptr,vk,argv[1]);editor::EditorUiValidation::run(app,argv[2]);}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
