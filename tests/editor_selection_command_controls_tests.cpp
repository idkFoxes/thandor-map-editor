#include "thandor/retail_selection_metrics.hpp"
#include "thandor/simulation_command_markers.hpp"
#include "thandor/catalog.hpp"
#include "game_cursor.hpp"
#include <iostream>
#include <fstream>
#include <cmath>
using namespace thandor;
static void check(bool v,const char* text){if(!v)throw std::runtime_error(text);}
int main(int argc,char** argv)try{
 check(retail_selection_meter_frame(100,0)==1,"no-power frame");
 check(retail_selection_meter_frame(100,50)==9,"half-power frame");
 check(retail_selection_meter_frame(100,100)==17&&retail_selection_meter_frame(0,0)==17,"full/no-demand meter");
 check(retail_selection_pixel_argb(0x80606080,std::nullopt)==0x80606080,"untinted authoring changes native pixels");
 check(retail_selection_pixel_argb(0x80FFFFFF,0x00FF0000)==0x80FF0000,"frame tint or alpha wrong");
 SimulationActorState actor;actor.weapons.resize(1);SimulationWeaponProfile weapon;weapon.runtime_class_id=5;weapon.reload_ticks=100;
 actor.weapons[0].cooldown_ticks=75;
 check(retail_selection_reload(std::span{&weapon,1},actor)==std::array<int32_t,2>{100,25},"reload progress is not duration minus countdown");
 weapon.runtime_class_id=9;actor.weapons[0].attachment_reload_ticks.fill(90);actor.weapons[0].attachment_reload_ticks[2]=20;actor.weapons[0].inter_shot_ticks=40;
 check(retail_selection_reload(std::span{&weapon,1},actor)==std::array<int32_t,2>{100,60},"class9 unsigned minimum/shared reload bound");
 actor.weapons[0].attachment_reload_ticks.fill(-1);actor.weapons[0].inter_shot_ticks=0;
 check(retail_selection_reload(std::span{&weapon,1},actor)==std::array<int32_t,2>{100,100},"negative class9 ready countdown");
 SimulationLabEntity e;e.model_components.resize(3);e.model_components[0].retail_energy_demand_q4=16;
 e.model_components[1].parent_component_index=0;e.model_components[1].retail_energy_demand_q4=32;e.model_components[1].runtime_flags_ec=1;
 e.model_components[2].parent_component_index=1;e.model_components[2].retail_energy_demand_q4=64;
 e.model_components[0].source_model_node_index=0;
 auto hierarchy=std::make_shared<std::vector<ModelVisualNode>>(3);(*hierarchy)[0].simulation.runtime_render_flags=0x80;e.runtime_model_hierarchy=hierarchy;
 auto metrics=retail_selection_metrics(e);check(metrics.energy_total_q4==48&&metrics.energy_active_q4==16,"energy must count root and direct children only, excluding switched-off active demand");
 (*hierarchy)[0].simulation.runtime_render_flags=0;
 check(retail_selection_metrics(e).energy_total_q4==16,"energy child-count flag ignored");
 e.runtime_model_class_id=13;e.production_building=true;e.production_duration_ticks=100;e.production_elapsed_ticks=30;
 check(retail_selection_metrics(e).top_bar==std::array<int32_t,2>{100,30},"factory progress precedence");
 SimulationLabRun run;run.entities.resize(4);run.entities[0].faction_index=2;run.entities[0].runtime_model_class_id=13;
 run.entities[0].production_exit_world=Vec3f{1,2,0};run.entities[1].faction_index=1;
 run.entities[1].target_entity_index=2;run.entities[2].actor.position={50,60,0};run.entities[2].faction_index=2;
 run.entities[3].construction_complete=false;run.entities[3].faction_index=1;run.entities[3].actor.position={3,4,0};
 auto markers=simulation_lab_command_markers(run);
 check(markers.size()==3,"factory/stationary attack/construction markers omitted");
 check(markers[0].effect_id==143&&markers[0].point.x==1&&markers[0].faction_index==2,"factory default/faction missing");
 check(markers[1].effect_id==149&&markers[1].point.x==50,"stationary attack target requires movement incorrectly");
 run.entities[0].production_drive_target_world=Vec3f{7,8,0};
 check(simulation_lab_command_markers(run)[0].point.x==7,"live custom factory target was not consumed");
 run.entities[1].target_entity_index.reset();run.entities[1].actor.move_target=Vec3f{20,30,0};run.entities[1].actor.move_waypoint=Vec3f{20,30,0};
 check(simulation_lab_command_markers(run).size()==3,"duplicate final/waypoint marker");
 check(std::abs(editor::game_camera_turn_radians_per_pixel()*65536.F/(2.F*std::numbers::pi_v<float>)-16.F)<0.001F,"camera uses placement turn scale");
 check(placement_heading_after_pointer_delta(65520,1)==48,"placement rotation wrap/64 scale");
 if(argc>=2){
  const auto catalog=AssetCatalog::scan_game_root(argv[1]);const auto assets=RetailSelectionFrameAssets::parse(catalog.load("gfx/panel/select.dat"),GfxAsset::parse(catalog.load("gfx/panel/select.gfx")));
  RetailSelectionFrameMetrics m;m.energy_total_q4=100;m.energy_active_q4=0;m.top_bar=std::array<int32_t,2>{100,25};
  const auto blits=retail_selection_frame_blits(assets,20,20,100,100,2048,m);
  check(std::any_of(blits.begin(),blits.end(),[&](auto b){return b.subresource==assets.cells[18].subresource+1&&!b.faction_tint;}),"stock empty power meter absent");
  check(std::any_of(blits.begin(),blits.end(),[&](auto b){return b.subresource==assets.cells[22].subresource+4&&b.faction_tint;}),"stock reload top bar frame wrong");
  std::cout<<"Stock selection assets: no-power meter, reload, native tint boundaries PASS\n";
  if(argc==3){
   std::ofstream out(argv[2],std::ios::binary);const unsigned w=420,h=150;std::vector<uint8_t> pixels(w*h*4,0);
   for(unsigned i=0;i<w*h;++i){pixels[i*4]=20;pixels[i*4+1]=27;pixels[i*4+2]=36;pixels[i*4+3]=255;}
   for(unsigned state=0;state<3;++state){
    m.energy_total_q4=state==0?0:100;m.energy_active_q4=state==1?0:m.energy_total_q4;
    m.top_bar=state==0?std::nullopt:std::optional<std::array<int32_t,2>>{{100,int32_t(state*40+10)}};
    auto b=retail_selection_frame_blits(assets,20+state*140,30,120+state*140,120,2048,m);
    for(const auto& blit:b){const auto& im=assets.images.at(blit.subresource);
     for(unsigned y=0;y<unsigned(blit.height);++y)for(unsigned x=0;x<unsigned(blit.width);++x){
      int px=int(blit.x)+x,py=int(blit.y)+y;if(px<0||py<0||px>=int(w)||py>=int(h))continue;
      auto iy=blit.tile_y?y%im.height:std::min(y,im.height-1),ix=blit.tile_x?x%im.width:std::min(x,im.width-1);size_t off=(iy*im.width+ix)*4;
      uint32_t argb=(uint32_t(im.pixels[off+3])<<24)|(uint32_t(im.pixels[off])<<16)|(uint32_t(im.pixels[off+1])<<8)|im.pixels[off+2];
      const auto c=retail_selection_pixel_argb(argb,state==0||!blit.faction_tint?std::nullopt:std::optional<uint32_t>{state==1?0xFF8020:0x30C0FF});
      auto dest=(py*w+px)*4;unsigned a=c>>24;
      for(unsigned k=0;k<3;++k){unsigned color=(c>>(16-8*k))&255;pixels[dest+k]=uint8_t((color*a+pixels[dest+k]*(255-a))/255);}
     }
    }
   }
   out.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
  }
 }
 std::cout<<"V612 selection metrics, command intent, and camera/placement scale PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
