#include "editor_text_color.hpp"
#include "thandor/catalog.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
int main()try{
 const std::array<std::array<float,3>,4> backgrounds{{{.075F,.095F,.13F},{.10F,.13F,.18F},{.13F,.27F,.43F},{.15F,.30F,.46F}}};
 for(uint32_t family=0;family<9;++family){
  const auto color=editor::readable_editor_text_rgb(thandor::graphics_family_single_player_text_rgb24(family));
  for(const auto& background:backgrounds){
   const float ratio=(editor::text_relative_luminance(color)+.05F)/(editor::text_relative_luminance(background)+.05F);
   if(ratio<4.5F)throw std::runtime_error("faction text lacks contrast on editor theme");
  }
 }
 const auto blue=editor::readable_editor_text_rgb(0x0000DD);
 if(blue[2]<=blue[0]||blue[0]<=0)throw std::runtime_error("blue faction identity/readability lost");
 if(thandor::graphics_family_single_player_text_rgb24(1)!=0x0000DD)throw std::runtime_error("stock faction colours modified");
 std::cout<<"PASS all nine UI faction colours readable on panel/popup/frame/hover backgrounds; stock colours unchanged\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
