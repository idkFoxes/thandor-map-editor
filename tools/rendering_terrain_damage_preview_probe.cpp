// Real editor CPU checks for placed/produced depth damage and map-shot emission.
// GPU/window operations are stubbed; this does not verify Vulkan pixels.
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
  const auto group=std::find_if(app.catalog_.map_groups().begin(),app.catalog_.map_groups().end(),[](const auto& g){return g.level_path=="level/lavam.lev";});
  require(group!=app.catalog_.map_groups().end(),"LAVAM missing");app.load_map(group-app.catalog_.map_groups().begin());
  app.simulation_mode_=true;app.animate_timed_effects_=false;app.projected_object_shadows_=false;app.rebuild_objects();
  for(uint32_t tick=1;tick<=1024;++tick)app.spawn_environmental_linked_shots(tick);
  const auto map_shots=std::count_if(app.simulation_projectiles_.begin(),app.simulation_projectiles_.end(),
      [](const auto& p){return p.owner_kind==SimulationProjectileOwnerKind::Environmental;});
  require(map_shots>0,"visual animation toggle disabled damaging map shots");
  std::cout<<"PREVIEW_MAP_PHYSICS PASS animation_disabled=1 environmental_shots="<<map_shots<<" gpu_verified=0\n";
  auto& level=app.document_->level();while(!level.placements().empty())level.erase_placement(level.placements().size()-1);
  app.document_->field()=FieldGrid::create(64,64,0,0);
  const auto preset=std::find_if(app.catalog_.army_presets().begin(),app.catalog_.army_presets().end(),[](const auto& p){return p.browser_kind==PlacementBrowserKind::Unit&&p.runtime_model_class_id==1&&p.water_damage_multiplier>0&&p.root_maximum_integrity>0;});
  require(preset!=app.catalog_.army_presets().end(),"class-1 damage fixture absent");
  const int32_t depth=std::max(128,preset->water_damage_threshold_q12+128);
  for(auto& c:app.document_->field().cells())c.water_delta_q12=depth;
  const auto& cell=app.document_->field().at(32,32);
  LevelPlacement placement;placement.army_asset_id=preset->registry_id;placement.faction_index=1;placement.set_render_position_q12(cell.world_x_q12,cell.world_y_q12);level.add_placement(placement);
  app.reset_simulation(false);app.simulation_mode_=true;app.animate_units_=app.animate_buildings_=false;
  auto& actor=app.simulation_state_for(0);actor.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
  EditorApp::SimulationProducedEntity child;child.serial=71;child.army_asset_id=preset->registry_id;child.faction_index=1;child.actor=actor;
  child.maximum_integrity=child.integrity=preset->root_maximum_integrity;app.simulation_produced_entities_[71]=child;
  app.rebuild_objects();app.last_simulation_tick_=0;app.update_simulation(1);
  const int32_t expected=std::max(0,preset->root_maximum_integrity-int32_t((uint32_t(depth)*uint32_t(preset->water_damage_multiplier))>>7));
  require(app.simulation_integrity_states_.contains(0),"placed preview omitted stationary depth damage");
  require(app.simulation_integrity_states_.at(0).integrity==expected,"placed preview depth damage differs from MDL equation");
  require(app.simulation_produced_entities_.at(71).integrity==expected,"produced preview depth damage differs from placed unit");
  std::cout<<"PREVIEW_TERRAIN_DAMAGE PASS arm="<<preset->registry_id<<" class=1 animation_disabled=1 placed_health="<<expected<<" produced_health="<<expected<<" gpu_verified=0\n";
 }
};}
int main(int argc,char**argv)try{if(argc!=2)throw std::runtime_error("Usage: terrain_damage_preview PCK_ROOT");editor::VulkanContext vk(nullptr);editor::EditorApp app(nullptr,vk,argv[1]);editor::EditorUiValidation::run(app);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
