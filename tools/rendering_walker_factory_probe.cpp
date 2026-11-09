// Factory-completion fixtures through the real editor scene builder.
// Explicit GPU/window boundary stubs: this checks CPU geometry/material submission,
// not Vulkan pixels or a replay of the uploaded simulations.
// Executes the real editor CPU scene builder with only the GPU/window boundary stubbed.
// This is an explicit developer probe, not Vulkan playback validation.
#include "editor_app.hpp"
#include <imgui.h>
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/simulation_lab_environment.hpp"
#include "thandor/simulation_lab_catalog.hpp"
#define main stock_replay_probe_main
#include "simlab_stock_replay.cpp"
#undef main
using namespace thandor;
#include <fstream>
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
  auto runtime=simulation_lab_prepare_runtime_catalog(app.catalog_);
  std::ifstream input(case_file);if(!input)throw std::runtime_error("case file absent");
  std::stringstream case_text;case_text<<input.rdbuf();
  for(const auto& p:app.catalog_.army_presets())if(p.runtime_model_class_id==3U&&p.browser_kind==PlacementBrowserKind::Unit)case_text<<"niflheim "<<p.registry_id<<" 1 19.4741 -19.9405 43395 0\n";
  auto& cases=case_text;
  std::string map,current;uint32_t arm,faction,yaw,expected_root;float x,y;size_t checked=0;
  while(cases>>map>>arm>>faction>>x>>y>>yaw>>expected_root){
   if(map!=current){
    auto group=std::find_if(app.catalog_.map_groups().begin(),app.catalog_.map_groups().end(),[&](const auto& g){return g.level_path.ends_with("/"+map+".lev")||g.display_name==map;});
    if(group==app.catalog_.map_groups().end())throw std::runtime_error("map absent: "+map);
    app.clear_simulation_lab();app.load_map(group-app.catalog_.map_groups().begin());current=map;
   }
   app.animate_buildings_=app.animate_units_=app.animate_timed_effects_=false;
   const auto& field=app.document_->field();const auto* preset=app.catalog_.army_preset(arm);
   const auto* factory=app.catalog_.army_preset(303);if(!preset||!factory)throw std::runtime_error("walker/factory missing");
   const auto& center=field.at(field.width()/2,field.height()/2);
   const float cx=x==0&&y==0?q12_to_float(center.world_x_q12):x;
   const float cy=x==0&&y==0?q12_to_float(center.world_y_q12):y;
   for(uint32_t tier:{0U,1U,2U}){
    std::array<FactionTechnologyMask256,8> masks{};if(tier==2)masks[faction].fill(UINT32_MAX);
    if(tier==1)for(uint32_t tech:{1U,30U,31U,33U,34U,36U,37U})masks[faction][tech/32U]|=1U<<(tech%32U);
    auto parent=seed_for(*factory,app.catalog_,masks[faction]);parent.faction_index=faction;
    parent.source_placement_index=SIZE_MAX;parent.actor.position={cx,cy,field.sample_terrain(cx,cy).height};parent.actor.body_yaw_turn16=yaw;
    SimulationLabProductionAsset product;product.army_asset_id=arm;product.entity_seed=seed_for(*preset,app.catalog_,masks[faction]);
    product.army_runtime_flags14_raw=preset->army_runtime_flags14_raw;product.build_duration_ticks=1;
    SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=std::max(4U,faction);
    scenario.ai_planning_enabled=false;scenario.runtime_catalog=runtime;
    SimulationLabConfig config;config.run_count=1;config.duration_ticks=600;std::array<uint32_t,AiKnowledgeData::kParameterCount>ki{};
    auto lab=std::make_shared<SimulationLabExperiment>();lab->reset(config,ki,std::span(&parent,1),{},std::span(&product,1),{},masks,{},scenario);
    auto& producer=lab->runs()[0].entities[0];producer.production_behavior_state=1;producer.production_building=true;
    producer.production_asset_id=arm;producer.production_duration_ticks=1;producer.production_elapsed_ticks=0;
    for(uint32_t target_tick:{1U,32U,96U,512U}){
     lab->step(field,target_tick-lab->runs()[0].tick);
     auto& run=lab->runs()[0];if(run.entities.size()!=2||run.metrics.class13_units_spawned!=1)throw std::runtime_error("factory did not spawn requested walker");
     const auto& child=run.entities[1];if(child.army_asset_id!=arm||!child.synthetic)throw std::runtime_error("unexpected factory child");
     std::vector<Vec3f> fog_off_positions;
     for(int fog:{0,2}){
     app.fog_preview_mode_=fog;app.simulation_lab_snapshot_=lab;
     app.camera_.target=child.actor.position;app.camera_.distance=12;
     app.sync_selected_simulation_lab_run();app.rebuild_objects();
     if(app.simulation_lab_render_receipts_.size()!=1)throw std::runtime_error("missing/duplicate child receipt");
     const auto& receipt=app.simulation_lab_render_receipts_[0];
     if(std::string_view(receipt.result)!="submitted"||!receipt.bounds_valid||receipt.active_model_nodes==0||receipt.vertex_count!=receipt.triangles*3U)
      throw std::runtime_error("walker primary model unavailable: "+std::string(receipt.result));
     float minz=1e9,maxz=-1e9;size_t above=0,alpha=0,invalid=0,visible_samples=0;
     const auto sample_alpha=[&](uint32_t layer,float u,float v){
      if(layer>=texture_images.size())throw std::runtime_error("missing material layer");
      const auto& im=texture_images[layer];
      auto coord=[](float f,uint32_t n){return std::min(n-1U,uint32_t((f-std::floor(f))*n));};
      return im.pixels[(size_t(coord(v,im.height))*im.width+coord(u,im.width))*4U+3U];
     };
     for(size_t index=receipt.first_vertex;index<receipt.first_vertex+receipt.vertex_count;++index){
      const auto& v=app.dynamic_scene_cpu_.vertices.at(index);
      if(fog==0)fog_off_positions.push_back(v.position);
      else {const auto a=fog_off_positions.at(index-receipt.first_vertex);if(std::abs(a.x-v.position.x)>1e-5F||std::abs(a.y-v.position.y)>1e-5F||std::abs(a.z-v.position.z)>1e-5F)throw std::runtime_error("fog changed walker geometry");}
      const auto dz=v.position.z-field.sample_terrain(v.position.x,v.position.y).height;
      minz=std::min(minz,dz);maxz=std::max(maxz,dz);above+=dz>0.01F;
      invalid+=!std::isfinite(v.position.x)||!std::isfinite(v.position.y)||!std::isfinite(v.position.z);
      if(v.direct_tint_bypass>>1U)throw std::runtime_error("dynamic walker tagged for static suppression");
      if(v.texture_layer<texture_has_alpha.size()&&texture_has_alpha[v.texture_layer])++alpha;
      visible_samples+=sample_alpha(v.texture_layer,v.u,v.v)>=7U;
     }
     for(size_t index=receipt.first_vertex;index<receipt.first_vertex+receipt.vertex_count;index+=3U){
      const auto& a=app.dynamic_scene_cpu_.vertices.at(index);
      const auto& b=app.dynamic_scene_cpu_.vertices.at(index+1);
      const auto& c=app.dynamic_scene_cpu_.vertices.at(index+2);
      visible_samples+=sample_alpha(a.texture_layer,(a.u+b.u+c.u)/3,(a.v+b.v+c.v)/3)>=7U;
     }
     const auto root=child.model_components[child.root_model_component_index].model_definition_id;
     size_t child_shadow_vertices=0;
     for(const auto& shadow:app.dynamic_shadow_templates_)
      if(shadow.owner.source==RuntimeShadowSource::LabEntity&&shadow.owner.run==0&&shadow.owner.id==1)
       child_shadow_vertices+=shadow.receiver_vertices.size();
     std::cout<<"FACTORY_DRAW map="<<map<<" arm="<<arm<<" tier="<<tier<<" fog="<<fog<<" root="<<root
       <<" logged_root="<<expected_root<<" tick="<<run.tick<<" linked="<<child.production_linked_to_parent
       <<" contacts="<<child.actor.articulated_contacts_initialized<<" vertices="<<app.dynamic_scene_cpu_.vertices.size()
       <<" first_vertex="<<receipt.first_vertex<<" model_vertices="<<receipt.vertex_count<<" triangles="<<receipt.triangles<<" active_nodes="<<receipt.active_model_nodes<<" texel_samples="<<visible_samples<<" above="<<above<<" min_height="<<minz<<" max_height="<<maxz<<" alpha="<<alpha<<" invalid="<<invalid
       <<" shadow_vertices="<<child_shadow_vertices<<" error="<<app.first_object_build_error_<<'\n';
     if(app.dynamic_scene_cpu_.indices.empty()||alpha==0||visible_samples==0||above==0||invalid||!app.first_object_build_error_.empty())throw std::runtime_error("walker geometry missing, transparent, invalid or buried");
     if(app.simulation_lab_render_receipts_.size()!=1||std::string_view(app.simulation_lab_render_receipts_[0].result)!="submitted"||app.simulation_lab_render_receipts_[0].triangles==0)throw std::runtime_error("walker not submitted through primary renderer");
     ++checked;
     }
    }
   }
  }
  if(!cases.eof()||!checked)throw std::runtime_error("invalid/empty cases");
  std::cout<<"WALKER_FACTORY_SCENE PASS cases="<<checked<<" GPU_boundary_stubbed=1\n";
 }
};
}
int main(int argc,char**argv)try{if(argc!=3)throw std::runtime_error("Usage: rendering_walker_factory_probe GAME_ROOT CASES.tsv");editor::VulkanContext vk(nullptr);editor::EditorApp app(nullptr,vk,argv[1]);editor::EditorUiValidation::run(app,argv[2]);}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
