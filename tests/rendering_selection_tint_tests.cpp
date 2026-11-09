#include "thandor/retail_selection_frame.hpp"
#include "thandor/catalog.hpp"
#include <iostream>
#include <stdexcept>
#include <set>
using namespace thandor;
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
RetailSelectionFrameAssets fixture(){
 RetailSelectionFrameAssets a;
 for(uint32_t i=0;i<26;++i){a.cells[i].subresource=i*32;
  for(uint32_t j=0;j<32;++j)a.images.emplace(i*32+j,RgbaImage{2,2,std::vector<uint8_t>{80,200,120,255,100,40,220,128,20,20,20,255,0,0,0,0}});
 }
 return a;
}
void control(const RetailSelectionFrameAssets& a){
 size_t checks=0;const auto base=a.cells[18].subresource;
 for(uint32_t faction:{0xFF3020U,0x2080FFU,0xFFE030U,0x40D080U})
  for(uint32_t active:{0U,50U,100U}){
   RetailSelectionFrameMetrics m;m.energy_total_q4=100;m.energy_active_q4=active;m.top_bar=std::array<int32_t,2>{100,25};
   const auto blits=retail_selection_frame_blits(a,30,30,130,110,2048,m);
   bool base_seen=false,state_seen=false,reload_seen=false,health_seen=false;
   for(const auto& b:blits){
    const bool energy=b.subresource==base||b.subresource==base+retail_selection_meter_frame(100,active);
    check(b.faction_tint!=energy,"energy base/state must retain native colors; other frame/bar images must use faction tint");
    base_seen|=b.subresource==base;state_seen|=b.subresource==base+retail_selection_meter_frame(100,active);
    reload_seen|=b.subresource==a.cells[22].subresource+4;health_seen|=b.subresource==a.cells[25].subresource+6;
    const auto& im=a.images.at(b.subresource);
    for(size_t off=0;off<im.pixels.size();off+=4){
     const uint32_t original=(uint32_t(im.pixels[off+3])<<24)|(uint32_t(im.pixels[off])<<16)|(uint32_t(im.pixels[off+1])<<8)|im.pixels[off+2];
     const auto output=retail_selection_pixel_argb(original,b.faction_tint?std::optional<uint32_t>{faction}:std::nullopt);
     check((output>>24)==(original>>24),"faction selection tint changed alpha");
     if(energy)check(output==original,"energy icon changed with faction color");
     else check(output==retail_selection_pixel_argb(original,faction),"health/reload/frame lost faction tint");
     ++checks;
    }
   }
   check(base_seen&&state_seen&&reload_seen&&health_seen,"selection metric images omitted");
  }
 // A producer may display energy even with no demand. The icon still stays native.
 RetailSelectionFrameMetrics producer;producer.force_energy_meter=true;
 const auto forced=retail_selection_frame_blits(a,30,30,130,110,4096,producer);
 check(std::count_if(forced.begin(),forced.end(),[](const auto& b){return !b.faction_tint;})==2,"forced zero-demand energy icon tint changed");
 const auto plain=retail_selection_frame_blits(a,30,30,130,110,4096);
 check(std::all_of(plain.begin(),plain.end(),[](const auto& b){return b.faction_tint;}),"ordinary selection lost faction-colored pieces");
 std::cout<<"selection color checks="<<checks<<" factions=4 power_states=3 native_energy_base_and_state=unchanged\n";
}
void preview(const RetailSelectionFrameAssets& a,const char* path){
 RgbaImage out{640,360,std::vector<uint8_t>(640*360*4)};
 for(size_t i=0;i<out.pixels.size();i+=4){out.pixels[i]=24;out.pixels[i+1]=29;out.pixels[i+2]=38;out.pixels[i+3]=255;}
 for(uint32_t row=0;row<3;++row)for(uint32_t f=0;f<4;++f){
  const std::array colors{0xFF3020U,0x2080FFU,0xFFE030U,0x40D080U};RetailSelectionFrameMetrics m;
  m.energy_total_q4=100;m.energy_active_q4=row*50;m.top_bar=std::array<int32_t,2>{100,25};
  const auto blits=retail_selection_frame_blits(a,25+f*160,25+row*120,130+f*160,95+row*120,2048,m);
  for(const auto& b:blits){const auto& im=a.images.at(b.subresource);
   for(int y=0;y<int(b.height);++y)for(int x=0;x<int(b.width);++x){
    const int px=int(b.x)+x,py=int(b.y)+y;if(px<0||py<0||px>=640||py>=360)continue;
    const auto ix=b.tile_x?uint32_t(x)%im.width:std::min(uint32_t(x),im.width-1);
    const auto iy=b.tile_y?uint32_t(y)%im.height:std::min(uint32_t(y),im.height-1);const auto off=(iy*im.width+ix)*4;
    const auto pixel=(uint32_t(im.pixels[off+3])<<24)|(uint32_t(im.pixels[off])<<16)|(uint32_t(im.pixels[off+1])<<8)|im.pixels[off+2];
    const auto c=retail_selection_pixel_argb(pixel,b.faction_tint?std::optional<uint32_t>{colors[f]}:std::nullopt);
    const auto to=(py*640+px)*4;const unsigned alpha=c>>24;
    for(unsigned k=0;k<3;++k)out.pixels[to+k]=uint8_t((((c>>(16-k*8))&255)*alpha+out.pixels[to+k]*(255-alpha))/255);
   }
  }
 }
 save_tga_rgba(path,out);
}
int main(int argc,char** argv)try{
 control(fixture());
 if(argc>1){const auto catalog=AssetCatalog::scan_game_root(argv[1]);
  const auto assets=RetailSelectionFrameAssets::parse(catalog.load("gfx/panel/select.dat"),GfxAsset::parse(catalog.load("gfx/panel/select.gfx")));
  control(assets);if(argc>2)preview(assets,argv[2]);std::cout<<"stock select.dat/select.gfx checked\n";
 }
 std::cout<<"V623_SELECTION_TINT PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
