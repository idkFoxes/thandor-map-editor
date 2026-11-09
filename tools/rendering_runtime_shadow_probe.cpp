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
static unsigned object_uploads;
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
void TerrainRenderer::update_objects(std::span<const ObjectVertex>,std::span<const uint32_t> i,std::span<const ObjectDrawRun>r,std::span<const ObjectVertex>,std::span<const uint32_t>si,std::span<const thandor::RgbaImage>t){++object_uploads;shadow_index_count_=si.size();texture_images.assign(t.begin(),t.end());texture_has_alpha.clear();for(const auto&im:t){bool visible=false;for(size_t a=3;a<im.pixels.size();a+=4)visible|=im.pixels[a]>=7;texture_has_alpha.push_back(visible);}
object_index_count_=i.size();object_draw_runs_.assign(r.begin(),r.end());object_texture_layer_count_=t.size();}
struct EditorUiValidation {
 static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
 static void run(EditorApp& app){
  ImGui::GetIO().DisplaySize={1600,1000};app.sound_playback_enabled_=false;
  const auto group=std::find_if(app.catalog_.map_groups().begin(),app.catalog_.map_groups().end(),
    [](const auto& g){return g.level_path.ends_with("/lavam.lev");});
  require(group!=app.catalog_.map_groups().end(),"fixture map absent");
  app.load_map(group-app.catalog_.map_groups().begin());
  app.document_->field()=FieldGrid::create(129,129,0,0);
  app.document_->level().lighting().azimuth=0x1000;app.document_->level().lighting().elevation=0xE000;
  auto runtime=simulation_lab_prepare_runtime_catalog(app.catalog_);
  std::vector<uint32_t> assets{137};
  for(const auto& preset:app.catalog_.army_presets()){
   if(preset.browser_kind!=PlacementBrowserKind::Building||!preset.casts_generated_shadow)continue;
   const auto& hierarchy=preset.resolved_model_hierarchy.empty()?preset.model_hierarchy:preset.resolved_model_hierarchy;
   bool has=false;for(const auto& node:hierarchy)if(!node.sprite_path.empty())has|=app.cached_sprite(node.sprite_path).has_generated_shadow_mesh();
   if(has)assets.push_back(preset.registry_id);
   if(assets.size()==5)break;
  }
  require(assets.size()==5,"four caster buildings unavailable");size_t cases=0;bool missing=false;
  for(const uint32_t id:{1U,187U,242U}){
   const auto* preset=app.catalog_.army_preset(id);
   require(preset&&preset->casts_generated_shadow,"additional unit caster fixture missing");
   assets.push_back(id);
  }
  for(const auto arm:assets){
   const auto* preset=app.catalog_.army_preset(arm);std::array<FactionTechnologyMask256,8> masks{};
   auto seed=seed_for(*preset,app.catalog_,masks[1]);seed.faction_index=1;seed.source_placement_index=SIZE_MAX;
   const auto& cell=app.document_->field().at(64,64);seed.actor.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
   app.clear_simulation_lab();app.simulation_mode_=false;app.fog_preview_mode_=0;
   app.projected_object_shadows_=true;app.animate_buildings_=app.animate_units_=false;
   app.rebuild_objects();const auto static_before=app.renderer_.shadow_index_count();
   LevelPlacement placement;placement.army_asset_id=arm;placement.faction_index=1;
   placement.set_render_position_q12(cell.world_x_q12,cell.world_y_q12);
   app.document_->level().add_placement(placement);app.rebuild_objects();
   const auto static_after=app.renderer_.shadow_index_count();
   std::cout<<"PLACED_SHADOW arm="<<arm<<" added_indices="<<(static_after-static_before)<<std::endl;
   require(static_after==static_before+42,"existing LEV caster shadow path failed");
   app.document_->level().erase_placement(app.document_->level().placements().size()-1);app.rebuild_objects();
   SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=4;
   scenario.ai_planning_enabled=false;scenario.runtime_catalog=runtime;
   SimulationLabConfig config;config.run_count=2;config.duration_ticks=100;
   std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
   auto lab=std::make_shared<SimulationLabExperiment>();lab->reset(config,ki,std::span(&seed,1),{}, {},{},masks,{},scenario);
   lab->step(app.document_->field(),1);
   for(auto& run:lab->runs())run.entities[0].synthetic=true;
   app.projected_object_shadows_=true;app.fog_preview_mode_=0;app.simulation_lab_selected_run_=0;
   app.simulation_lab_snapshot_=lab;app.camera_.target=seed.actor.position;app.camera_.distance=25;
   app.sync_selected_simulation_lab_run();app.rebuild_objects();
   std::cout<<"RUNTIME_SHADOW arm="<<arm<<" building="<<(preset->browser_kind==PlacementBrowserKind::Building)
     <<" dynamic_vertices="<<app.dynamic_scene_cpu_.shadow_vertices.size()<<" indices="<<app.dynamic_scene_cpu_.shadow_indices.size()<<std::endl;
#ifdef THANDOR_SHADOW_BASELINE
   missing|=app.dynamic_scene_cpu_.shadow_indices.empty();
#else
   require(!app.dynamic_scene_cpu_.shadow_indices.empty(),"runtime unit/building has no submitted shadow");
   const auto owned=[&](uint64_t id){return std::find_if(app.dynamic_shadow_templates_.begin(),app.dynamic_shadow_templates_.end(),
     [&](const auto& t){return t.owner.source==RuntimeShadowSource::LabEntity&&t.owner.run==0&&t.owner.id==id;});};
   auto shadow=owned(0);require(shadow!=app.dynamic_shadow_templates_.end(),"runtime shadow owner missing");
   require(shadow->receiver_vertices.size()==12&&shadow->receiver_indices.size()==42,"runtime receiver topology changed");
   size_t opaque=0;uint32_t alpha=0;
   for(const auto& v:shadow->receiver_vertices){require(v.texture_layer<texture_images.size(),"shadow atlas layer missing");alpha=std::max(alpha,v.packed_color_argb>>24);}
   const auto& image=texture_images[shadow->receiver_vertices[0].texture_layer];
   for(size_t i=3;i<image.pixels.size();i+=4)opaque+=image.pixels[i]>0;
   require(alpha>5&&opaque>0,"runtime shadow tint/mask is transparent");
   require(!app.runtime_shadow_collector_,"atlas collector outlived its CPU rebuild");
   const auto uploads=object_uploads;
   app.rebuild_dynamic_scene_cpu();app.rebuild_dynamic_scene_cpu();
   require(object_uploads==uploads,"normal dynamic frame reuploads the atlas");
   auto second=lab->runs()[0].entities[0];second.actor.position.x+=3;lab->runs()[0].entities.push_back(second);
   app.simulation_static_replacement_dirty_=false;app.sync_selected_simulation_lab_run();
   require(app.simulation_static_replacement_dirty_,"second resident-asset instance did not invalidate shadow membership");
   app.rebuild_objects();require(owned(0)!=app.dynamic_shadow_templates_.end()&&owned(1)!=app.dynamic_shadow_templates_.end(),"second instance did not get its own shadow");
   const auto old_receiver=owned(0)->receiver_vertices[0].position;
   const auto old_anchor=owned(0)->anchor_position;
   auto& first=lab->runs()[0].entities[0];first.actor.position.x+=2;first.actor.position.y-=1;
   first.actor.body_yaw_turn16=wrap_turn16(first.actor.body_yaw_turn16+0x4000);
   app.rebuild_dynamic_scene_cpu();require(app.dynamic_scene_cpu_.shadow_vertices.size()==24,"two current owners did not submit two receiver meshes");
   const auto moved=app.dynamic_scene_cpu_.shadow_vertices[0].position;
   const float expected_x=first.actor.position.x-(old_receiver.y-old_anchor.y);
   const float expected_y=first.actor.position.y+(old_receiver.x-old_anchor.x);
   require(std::abs(moved.x-expected_x)<1.e-4F&&std::abs(moved.y-expected_y)<1.e-4F,
       "cached runtime receiver did not follow the correct owner's position/yaw");
   // Membership/motion coverage above describes the existing cached-decal path.
   // It does NOT prove fixed-world-light projection. Compare that actual dynamic
   // frame with a fresh caster solve at the identical pose and terrain below.
   const auto cached_frame=app.dynamic_scene_cpu_.shadow_vertices;
   app.rebuild_objects();
   const auto& fresh_frame=app.dynamic_scene_cpu_.shadow_vertices;
   require(cached_frame.size()==fresh_frame.size(),"flat audit receiver topology changed");
   float maximum_xy_error=0;
   for(size_t i=0;i<cached_frame.size();++i)
    maximum_xy_error=std::max(maximum_xy_error,std::hypot(cached_frame[i].position.x-fresh_frame[i].position.x,
        cached_frame[i].position.y-fresh_frame[i].position.y));
   std::cout<<"FIXED_LIGHT_AUDIT arm="<<arm<<" building="<<(preset->browser_kind==PlacementBrowserKind::Building)
     <<" maximum_receiver_xy_error="<<maximum_xy_error
     <<" cached_matches_fresh="<<(maximum_xy_error<0.02F)<<" gpu_verified=0"<<std::endl;
   first.runtime_removed=true;app.rebuild_dynamic_scene_cpu();require(app.dynamic_scene_cpu_.shadow_vertices.size()==12,"removed owner left a ghost shadow");
   app.simulation_lab_selected_run_=1;app.sync_selected_simulation_lab_run();app.rebuild_objects();
   app.simulation_lab_selected_run_=0;app.rebuild_dynamic_scene_cpu();require(app.dynamic_scene_cpu_.shadow_indices.empty(),"another run's cached shadow leaked into the selected run");
   app.projected_object_shadows_=false;app.rebuild_objects();require(app.dynamic_scene_cpu_.shadow_indices.empty()&&app.renderer_.shadow_index_count()==0,"shadow toggle left submitted receivers");
#endif
   ++cases;
  }
#ifndef THANDOR_SHADOW_BASELINE
  app.clear_simulation_lab();app.simulation_mode_=true;app.projected_object_shadows_=true;app.fog_preview_mode_=0;
  const auto* walker=app.catalog_.army_preset(137);std::array<FactionTechnologyMask256,8> masks{};
  auto manual_seed=seed_for(*walker,app.catalog_,masks[1]);
  const auto& cell=app.document_->field().at(64,64);
  EditorApp::SimulationProducedEntity produced;produced.serial=71;produced.army_asset_id=137;
  produced.faction_index=1;produced.actor=manual_seed.actor;
  produced.actor.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
  app.simulation_produced_entities_[71]=produced;app.camera_.target=produced.actor.position;
  app.rebuild_objects();
  const auto has_produced=std::any_of(app.dynamic_shadow_templates_.begin(),app.dynamic_shadow_templates_.end(),
    [](const auto& shadow){return shadow.owner.source==RuntimeShadowSource::Produced&&shadow.owner.id==71;});
  require(has_produced&&!app.dynamic_scene_cpu_.shadow_indices.empty(),"manual factory child did not cast a shadow");
  app.simulation_produced_entities_.erase(71);app.rebuild_dynamic_scene_cpu();
  require(app.dynamic_scene_cpu_.shadow_indices.empty(),"erased manual factory child left a ghost shadow");
  std::cout<<"MANUAL_FACTORY_SHADOW PASS owner=71 erased_filtered=1\n";
#endif
  require(!missing,"runtime unit/building has no submitted shadow");
  std::cout<<"RUNTIME_SHADOW_COVERAGE PASS cases="<<cases<<" cached_motion_only=1 fixed_light_parity_verified=0 gpu_verified=0\n";
 }
};}
int main(int argc,char**argv)try{if(argc!=2)throw std::runtime_error("Usage: rendering_runtime_shadow_probe GAME_ROOT");editor::VulkanContext vk(nullptr);editor::EditorApp app(nullptr,vk,argv[1]);editor::EditorUiValidation::run(app);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
