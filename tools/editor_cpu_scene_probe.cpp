// Executes the real editor CPU scene builder with only the GPU/window boundary stubbed.
// This is an explicit developer probe, not Vulkan playback validation.
#include "editor_app.hpp"
#include <imgui.h>
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/simulation_lab_environment.hpp"
#include "thandor/simulation_lab_catalog.hpp"
using namespace thandor;
#include <iostream>
namespace editor {
static std::vector<bool> texture_has_alpha;
VulkanContext::VulkanContext(GLFWwindow* w):window_(w){max_image_array_layers_=4096;}
VulkanContext::~VulkanContext()=default;
void VulkanContext::wait_idle()const{}
TerrainRenderer::TerrainRenderer(VulkanContext& c):context_(c){}
TerrainRenderer::~TerrainRenderer()=default;
void TerrainRenderer::update_mesh(const thandor::FieldGrid&,const thandor::TerrainLighting&,const thandor::TerrainTextureLayout*,const thandor::TerrainSurfaceTextureLayout*,bool,uint32_t,uint32_t){}
void TerrainRenderer::update_materials(std::span<const thandor::RgbaImage>){}
void TerrainRenderer::update_surface_materials(std::span<const thandor::RgbaImage>){}
void TerrainRenderer::update_objects(std::span<const ObjectVertex>,std::span<const uint32_t> i,std::span<const ObjectDrawRun>r,std::span<const ObjectVertex>,std::span<const uint32_t>,std::span<const thandor::RgbaImage>t){texture_has_alpha.clear();for(const auto&im:t){bool visible=false;for(size_t a=3;a<im.pixels.size();a+=4)visible|=im.pixels[a]>=7;texture_has_alpha.push_back(visible);}
object_index_count_=i.size();object_draw_runs_.assign(r.begin(),r.end());object_texture_layer_count_=t.size();}
struct EditorUiValidation {
 static void run(EditorApp&app){
  ImGui::GetIO().DisplaySize={1600,1000};
  auto map=std::find_if(app.catalog_.map_groups().begin(),app.catalog_.map_groups().end(),[](auto&g){return g.level_path=="level/asgard.lev";});
  if(map==app.catalog_.map_groups().end())throw std::runtime_error("asgard absent");
  app.load_map(map-app.catalog_.map_groups().begin());app.fog_preview_mode_=0;
  app.animate_buildings_=app.animate_units_=app.animate_timed_effects_=false;
  const auto& field=app.document_->field();auto runtime=simulation_lab_prepare_runtime_catalog(app.catalog_);
  size_t checked=0;for(int fog:{0,2})for(const auto& preset:app.catalog_.army_presets()){
   if(preset.browser_kind!=PlacementBrowserKind::Unit)continue;const unsigned id=preset.registry_id;app.fog_preview_mode_=fog;
   auto p=app.catalog_.army_preset(id);if(!p)continue;
   SimulationLabSeedEntity seed;seed.army_asset_id=id;seed.runtime_model_class_id=p->runtime_model_class_id;seed.motion=p->simulation_motion_profile;seed.root_model_maximum_integrity=p->root_maximum_integrity;seed.fog_uncover_radius_q12=p->fog_uncover_radius_q12;
   simulation_lab_prepare_model_technologies(*p,app.catalog_,seed);
   seed.faction_index=2;seed.source_placement_index=SIZE_MAX;
   const auto& cell=field.at(field.width()/2,field.height()/2);seed.actor.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),cell.terrain_height()};
   SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=4;scenario.ai_planning_enabled=false;scenario.runtime_catalog=runtime;
   SimulationLabConfig config;config.run_count=1;config.duration_ticks=100;std::array<uint32_t,AiKnowledgeData::kParameterCount>ki{};
   auto lab=std::make_shared<SimulationLabExperiment>();lab->reset(config,ki,std::span(&seed,1),{}, {}, {}, {}, {},scenario);
   lab->runs()[0].entities[0].synthetic=true;lab->step(field,1);
   app.simulation_lab_snapshot_=lab;app.camera_.target=seed.actor.position;app.camera_.distance=12;
   app.sync_selected_simulation_lab_run();app.rebuild_objects();
   float minz=1e9,maxz=-1e9;size_t above=0;for(const auto&v:app.dynamic_scene_cpu_.vertices){auto dz=v.position.z-field.sample_terrain(v.position.x,v.position.y).height;minz=std::min(minz,dz);maxz=std::max(maxz,dz);above+=dz>0.01F;}
   std::cout<<"HEIGHT min="<<minz<<" max="<<maxz<<" above="<<above<<" terrain="<<cell.terrain_height()<<"\n";
   size_t alpha_vertices=0,transparent_vertices=0;
   for(const auto&v:app.dynamic_scene_cpu_.vertices){if(v.texture_layer>=texture_has_alpha.size())throw std::runtime_error("missing texture layer");if(texture_has_alpha[v.texture_layer])++alpha_vertices;else ++transparent_vertices;}
   if(transparent_vertices)std::cout<<"TRANSPARENT_LAYER arm="<<id<<" visible_vertices="<<alpha_vertices<<" transparent_vertices="<<transparent_vertices<<'\n';
   if(alpha_vertices==0)throw std::runtime_error("entire unit selects transparent textures: ARM "+std::to_string(id));
   if(app.dynamic_scene_cpu_.indices.empty()||!app.first_object_build_error_.empty()||!std::isfinite(minz)||!std::isfinite(maxz))throw std::runtime_error("missing or invalid model geometry for ARM "+std::to_string(id));
   if(app.simulation_lab_render_receipts_.size()!=1||std::string_view(app.simulation_lab_render_receipts_[0].result)!="submitted"||app.simulation_lab_render_receipts_[0].triangles==0)throw std::runtime_error("unit used an unexpected skip/fallback path");
   size_t covered=0;for(auto&run:app.dynamic_scene_cpu_.draw_runs)covered+=run.index_count;if(covered!=app.dynamic_scene_cpu_.indices.size())throw std::runtime_error("unsubmitted triangles");++checked;
   std::cout<<"CPU_DRAW fog="<<fog<<" arm="<<id<<" vertices="<<app.dynamic_scene_cpu_.vertices.size()<<" indices="<<app.dynamic_scene_cpu_.indices.size()<<" error="<<app.first_object_build_error_<<'\n';
  }
  std::cout<<"EDITOR_CPU_SCENE PASS unit_cases="<<checked<<" (GPU boundary stubbed)\n";
 }
};
}
int main(int argc,char**argv)try{if(argc!=2)throw std::runtime_error("Usage: editor_cpu_scene_probe GAME_ROOT");editor::VulkanContext vk(nullptr);editor::EditorApp app(nullptr,vk,argv[1]);editor::EditorUiValidation::run(app);}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
