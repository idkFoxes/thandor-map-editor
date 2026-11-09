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
 static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
 static size_t casters(std::span<const ObjectVertex> v){return std::count_if(v.begin(),v.end(),[](const auto& x){return x.casts_shadow!=0;});}
 static void run(EditorApp& app){
  ImGui::GetIO().DisplaySize={1600,1000};app.sound_playback_enabled_=false;
  if(std::filesystem::exists(app.catalog_.game_root()/"PATCH_stability_fixture.pck")){
   require(app.startup_validation_.issues.size()==3,"corrupt fixture was not validated at startup");
   require(app.asset_load_failures_.size()==3,"corrupt startup assets were not reported");
   app.asset_load_failures_.clear();
  }
  const auto group=std::find_if(app.catalog_.map_groups().begin(),app.catalog_.map_groups().end(),[](const auto& g){return g.level_path.ends_with("/lavam.lev");});
  require(group!=app.catalog_.map_groups().end(),"fixture map absent");app.load_map(group-app.catalog_.map_groups().begin());
  while(!app.document_->level().placements().empty())app.document_->level().erase_placement(0);
  app.document_->field()=FieldGrid::create(129,129,0,0);
  app.document_->level().lighting().azimuth=0x1000;app.document_->level().lighting().elevation=0xE000;
  auto runtime=simulation_lab_prepare_runtime_catalog(app.catalog_);
  std::vector<uint32_t> assets{137,1,187,242};size_t buildings=0,pumps=0;
  for(const auto& p:app.catalog_.army_presets()){
   if(p.browser_kind!=PlacementBrowserKind::Building||!p.casts_generated_shadow)continue;
   auto name=p.display_name;for(auto& c:name)c=std::tolower(static_cast<unsigned char>(c));
   const bool pump=name.find("pump")!=std::string::npos;
   if(pump||buildings<4){assets.push_back(p.registry_id);++buildings;pumps+=pump;}
  }
  require(pumps>0,"stock pump fixture not found");
  size_t cases=0;
  for(const auto arm:assets){
   const auto* preset=app.catalog_.army_preset(arm);require(preset!=nullptr,"missing preset");
   app.clear_simulation_lab();app.simulation_mode_=false;app.fog_preview_mode_=0;app.projected_object_shadows_=true;app.animate_buildings_=app.animate_units_=false;
   const auto& cell=app.document_->field().at(64,64);
   LevelPlacement placement;placement.army_asset_id=arm;placement.faction_index=1;placement.set_render_position_q12(cell.world_x_q12,cell.world_y_q12);
   app.document_->level().add_placement(placement);app.rebuild_objects();
   require(static_caster_vertices>0,"placed stock model has no depth caster vertices");
   require(app.renderer_.shadow_index_count()==0,"legacy raised receiver was generated");
   app.document_->level().erase_placement(0);
   std::array<FactionTechnologyMask256,8> masks{};auto seed=seed_for(*preset,app.catalog_,masks[1]);seed.faction_index=1;seed.source_placement_index=SIZE_MAX;
   seed.actor.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
   SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=4;scenario.ai_planning_enabled=false;scenario.runtime_catalog=runtime;
   SimulationLabConfig config;config.run_count=1;config.duration_ticks=100;std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
   auto lab=std::make_shared<SimulationLabExperiment>();lab->reset(config,ki,std::span(&seed,1),{}, {},{},masks,{},scenario);lab->step(app.document_->field(),1);
   lab->runs()[0].entities[0].synthetic=true;
   app.animate_buildings_=app.animate_units_=true;
   app.simulation_lab_selected_run_=0;app.simulation_lab_snapshot_=lab;app.camera_.target=seed.actor.position;app.camera_.distance=25;
   app.sync_selected_simulation_lab_run();app.rebuild_objects();
   const auto caster_count=casters(app.dynamic_scene_cpu_.vertices);require(caster_count>0,"live stock model has no depth caster vertices");
   require(app.dynamic_scene_cpu_.shadow_indices.empty()&&app.dynamic_shadow_templates_.empty(),"live stock model still builds stale shadow receivers");
   if(arm==332){
    const auto pose=app.dynamic_scene_cpu_.vertices;lab->runs()[0].tick+=9;app.rebuild_dynamic_scene_cpu();
    require(pose.size()!=app.dynamic_scene_cpu_.vertices.size()||std::memcmp(pose.data(),app.dynamic_scene_cpu_.vertices.data(),pose.size()*sizeof(ObjectVertex))!=0,"pump animation reuses stale caster geometry");
    std::cout<<"PUMP_ANIMATION_PASS consecutive_pose_ticks=1,10\n";
   }
   if((arm==1||arm==332)&&std::getenv("THANDOR_SHADOW_SCENE_DIR")){
    std::ofstream stream(std::filesystem::path(std::getenv("THANDOR_SHADOW_SCENE_DIR"))/("stock_"+std::to_string(arm)+".bin"),std::ios::binary);
    auto write=[&](const auto& value){stream.write(reinterpret_cast<const char*>(&value),sizeof(value));};
    auto vector=[&](const auto& values){write(uint64_t(values.size()));if(!values.empty())stream.write(reinterpret_cast<const char*>(values.data()),values.size()*sizeof(values[0]));};
    auto camera=app.camera_;camera.distance=8;write(camera);write(app.document_->level().lighting().shadow_ray_direction());
    vector(app.dynamic_scene_cpu_.vertices);vector(app.dynamic_scene_cpu_.indices);vector(app.dynamic_scene_cpu_.draw_runs);
    write(uint64_t(texture_images.size()));for(const auto& im:texture_images){write(im.width);write(im.height);vector(im.pixels);}
    require(bool(stream),"stock GPU scene export failed");
   }
   auto duplicate=lab->runs()[0].entities[0];lab->runs()[0].entities.push_back(duplicate);
   app.simulation_static_replacement_dirty_=false;app.sync_selected_simulation_lab_run();
   require(!app.simulation_static_replacement_dirty_,"same material instance unnecessarily rebuilds the atlas");
   lab->runs()[0].entities.pop_back();
   const auto before=app.dynamic_scene_cpu_.vertices;
   auto& actor=lab->runs()[0].entities[0].actor;actor.position.x+=2;actor.body_yaw_turn16+=0x4000;
   for(auto& foot:actor.articulated_foot_anchor_world)foot.x+=2;
   for(auto& foot:actor.articulated_foot_target_world)foot.x+=2;
   app.rebuild_dynamic_scene_cpu();require(casters(app.dynamic_scene_cpu_.vertices)>0,"moving/rotating caster disappeared");
   require(before.size()!=app.dynamic_scene_cpu_.vertices.size()||std::memcmp(before.data(),app.dynamic_scene_cpu_.vertices.data(),before.size()*sizeof(ObjectVertex))!=0,"moved actor reuses stale caster geometry");
   lab->runs()[0].entities[0].runtime_removed=true;app.rebuild_dynamic_scene_cpu();
   require(casters(app.dynamic_scene_cpu_.vertices)==0,"removed stock actor left caster geometry");
   std::cout<<"STOCK_DIRECTIONAL_CASTER arm="<<arm<<" name="<<preset->display_name<<" live_vertices="<<caster_count<<" moving_pose=1 removal=1 legacy_receivers=0\n";++cases;
  }
  require(app.asset_load_failures_.empty(),"stock scene required an asset fallback");
  app.clear_simulation_lab();
  const auto flight=std::find_if(app.catalog_.map_groups().begin(),app.catalog_.map_groups().end(),[](const auto& g){return g.level_path.ends_with("/s10_flug.lev");});
  require(flight!=app.catalog_.map_groups().end(),"performance log fixture map missing");
  app.load_map(flight-app.catalog_.map_groups().begin());app.simulation_lab_config_.run_count=1;app.simulation_lab_config_.duration_ticks=120;
  const auto now=[](){return std::chrono::steady_clock::now();};
  auto begin=now();app.start_simulation_lab();const auto cold=std::chrono::duration<double,std::milli>(now()-begin).count();
  require(app.simulation_lab_level_catalog_misses_==1,"cold level catalog not cached");
  const auto key=app.simulation_lab_level_catalog_key_;begin=now();app.start_simulation_lab();const auto warm=std::chrono::duration<double,std::milli>(now()-begin).count();
  require(app.simulation_lab_level_catalog_misses_==1&&app.simulation_lab_level_catalog_hits_==1,"warm restart rescans archives");
  auto patch=app.catalog_.game_root()/"PATCH_stability_fixture.pck";
  if(std::filesystem::exists(patch)){
   const auto stamp=std::filesystem::last_write_time(patch);std::filesystem::last_write_time(patch,stamp+std::chrono::seconds(1));
   app.start_simulation_lab();require(app.simulation_lab_level_catalog_misses_==2&&app.simulation_lab_level_catalog_key_!=key,"changed archive did not invalidate catalog cache");
  }
  app.clear_simulation_lab();
  require(app.asset_load_failures_.empty(),"flight map or Lab restart required an asset fallback");
  for(const std::string path:{"missing/absent.spr","test/broken.spr"}){
   const auto& sprite=app.cached_sprite(path);require(sprite.group_count()==1&&sprite.mesh().indices.empty(),"failed sprite has unsafe or drawable fallback");
   require(sprite.raycast_mesh().indices.empty()&&sprite.lod_mesh(0).indices.empty(),"fallback LOD/raycast access failed");
   const auto count=app.asset_load_failures_.size();app.cached_sprite(path);require(app.asset_load_failures_.size()==count,"bad sprite retried or reported every frame");
  }
  for(const auto& pair:std::array<std::pair<std::string,std::string>,2>{{{"missing/absent.gfx","missing/absent.pal"},{"test/broken.gfx","test/broken.pal"}}}){
   auto family=app.load_texture_family(pair.first,pair.second);require(family->graphics.subresources().size()==1,"failed GFX has no valid fallback");
   require(family->square_canvas(0).pixels.size()>0,"failed texture fallback cannot decode");
  }
  setenv("THANDOR_TEST_IMAGE_LIMIT","4",1);
  {VulkanContext limited_vk(nullptr);EditorApp limited(nullptr,limited_vk,app.catalog_.game_root());
   limited.load_map(flight-app.catalog_.map_groups().begin());
   require(limited.document_&&limited.renderer_.object_index_count()>0,"capacity fallback left no loaded map/model geometry");
   require(limited.renderer_.object_texture_layer_count()<=4,"texture fallback exceeds device capacity");
   require(std::any_of(limited.asset_load_failures_.begin(),limited.asset_load_failures_.end(),[](const auto& error){return error.second.find("capacity")!=std::string::npos;}),"capacity failure was not reported");
   limited.rebuild_dynamic_scene_cpu();
   require(!limited.asset_load_failures_.empty(),"capacity/fixture warnings are not visible");
  }
  unsetenv("THANDOR_TEST_IMAGE_LIMIT");
  std::cout<<"STABILITY_SCENE PASS same_material_duplicate_instances=9 stock_fallbacks=0 missing_corrupt_assets_recovered="<<app.asset_load_failures_.size()<<" catalog_cold_ms="<<cold<<" catalog_warm_ms="<<warm<<" cache_hit=1 archive_change_invalidates=1 low_texture_capacity_recovered=1 low_capacity_geometry_retained=1\n";
  std::cout<<"DIRECTIONAL_SHADOW_SCENE PASS cases="<<cases<<" pumps="<<pumps<<" GPU_verified=0\n";
 }
};}
int main(int argc,char** argv)try{if(argc!=2)throw std::runtime_error("Usage: rendering_directional_shadow_scene_probe GAME_ROOT");editor::VulkanContext vk(nullptr);editor::EditorApp app(nullptr,vk,argv[1]);editor::EditorUiValidation::run(app);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
