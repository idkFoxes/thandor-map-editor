#include THANDOR_NATIVE_WATER_RELAXATION
#include "thandor/retail_water_relaxation.hpp"
#include <iostream>
#include <random>
#include <vector>
void Thandor_Ptr32Overflow(uintptr_t){std::abort();}
int main(){std::mt19937 random(0x55ca9701U);size_t cases=0,checks=0;
 for(unsigned trial=0;trial<256;++trial){const unsigned w=3U+random()%20U,h=3U+random()%20U;
  std::vector<uint64_t> memory((offsetof(FieldGridAsset,cells)+size_t(w)*h*sizeof(FieldGridCell)+7U)/8U);
  auto* native=reinterpret_cast<FieldGridAsset*>(memory.data());native->gridWidth=w;native->gridHeight=h;
  auto editor=thandor::FieldGrid::create(w,h,0,0);
  for(size_t i=0;i<size_t(w)*h;++i){auto& n=native->cells[i];auto&e=editor.cells()[i];
   n.terrainHeight=int32_t(random()%60000)-30000;n.waterSurfaceDelta=int32_t(random()%20000)-10000;
   n.flagsAndMaterial=static_cast<decltype(n.flagsAndMaterial)>((random()%5U==0?0x20000000U:0U)|(random()%7U==0?0x40000000U:0U));
   e.terrain_height_q12=n.terrainHeight;e.water_delta_q12=n.waterSurfaceDelta;e.flags_and_material=uint32_t(n.flagsAndMaterial);
  }
  for(unsigned pass=0;pass<20;++pass){const bool reverse=(pass&1U)!=0U;
   if(reverse)TerrainGrid_RelaxNeighborHeightsReverseWithSignGate(native);else TerrainGrid_RelaxNeighborHeightsForwardWithSignGate(native);
   thandor::retail_relax_water_surface(editor,reverse);++cases;
   for(size_t i=0;i<size_t(w)*h;++i){++checks;if(editor.cells()[i].water_delta_q12!=native->cells[i].waterSurfaceDelta){std::cerr<<"water mismatch trial="<<trial<<" pass="<<pass<<" cell="<<i<<'\n';return 1;}}
  }
 }
 std::cout<<"NATIVE_WATER_RELAXATION PASS cases="<<cases<<" cell_checks="<<checks<<'\n';
}
