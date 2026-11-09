#include "thandor/retail_presence.hpp"
#include "thandor/simulation_lab.hpp"
#include <thandor/world/pathing/scratch_grid.h>
#include <random>
#include <iostream>
#include <cstddef>
#include <cstdlib>
void Thandor_Ptr32Overflow(uintptr_t){std::abort();}
int main(){
 constexpr unsigned w=19,h=21;auto field=thandor::FieldGrid::create(w,h,0,0);
 const size_t bytes=offsetof(FieldGridAsset,cells)+w*h*sizeof(FieldGridCell);
 std::vector<uint64_t> image((bytes+7)/8);auto* native_field=reinterpret_cast<FieldGridAsset*>(image.data());
 native_field->gridWidth=w;native_field->gridHeight=h;
 std::vector<GridScratchCell> native(w*h*16);std::mt19937 rng(0x776);size_t checks=0;
 for(unsigned trial=0;trial<160;++trial){
  thandor::SimulationLabRetailScratchImage lab;lab.width=w*4;lab.height=h*4;lab.state.resize(native.size());lab.field_presence_by_faction.assign(w*h,0);
  for(size_t i=0;i<native.size();++i){const uint32_t state=(rng()&0xFFFFFF00U)|(rng()&1U);native[i].stateMask=int32_t(state);lab.state[i]=state;}
  for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
   auto& cell=native_field->cells[y*w+x];const bool edge=x==0||y==0||x+1==w||y+1==h;
   cell.flagsAndMaterial=static_cast<decltype(cell.flagsAndMaterial)>(edge?0x88006000U:0U);
   if(edge)field.at(x,y).flags_and_material|=0x88006000U;
   cell.occupancyMask=0;
  }
  for(unsigned sample=0;sample<1+trial%7;++sample){
   const auto x=1+rng()%(w-2),y=1+rng()%(h-2),faction=1+rng()%7;
   auto& cell=native_field->cells[y*w+x];cell.occupancyMask|=uint64_t(0x10)<<(faction*8);
   lab.field_presence_by_faction[y*w+x]|=uint8_t(1U<<faction);
  }
  g_GridScratchWidth=w*4;g_GridScratchHeight=h*4;g_GridScratchPrimary=native.data();
  GridScratch_PropagateFieldOccupancyMaskNeighborhood(native_field);
  thandor::retail_propagate_field_presence_into_scratch(lab,field);
  for(size_t i=0;i<native.size();++i){++checks;if(uint32_t(native[i].stateMask)!=lab.state[i]){
   std::cerr<<"Presence mismatch trial="<<trial<<" cell="<<i<<'\n';return 1;}}
 }
 std::cout<<"NATIVE_PRESENCE_V776 PASS compared_cells="<<checks<<" cases=160 native 42-cell dilation\n";
}
