#include "thandor/retail_placement.hpp"
#include "thandor/simulation_lab.hpp"
#include <thandor/world/pathing/scratch_grid.h>
#include <random>
#include <iostream>
#include <cstdlib>
void Thandor_Ptr32Overflow(uintptr_t){std::abort();}
int main(){std::mt19937 rng(0x775);size_t compared=0;
 for(unsigned trial=0;trial<180;++trial){const unsigned w=39,h=43;std::vector<GridScratchCell> native(w*h);thandor::SimulationLabRetailScratchImage lab;lab.width=w;lab.height=h;lab.state.resize(w*h);
  uint32_t wall=0x80000001U|((trial%2)?0x70000000U:0x0e000000U);std::vector<size_t> q;
  for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){uint32_t v=(x==0||y==0||x+1==w||y+1==h)?0x80000000U:((rng()%100)<47?wall&~1U:0U);native[y*w+x].stateMask=int32_t(v);lab.state[y*w+x]=v;}
  for(unsigned seed=0;seed<12;++seed){size_t i=(1+rng()%(h-2))*w+(1+rng()%(w-2));GridScratch_FloodFillConnectedCells(int32_t(wall),w*sizeof(GridScratchCell),native.data()+i);thandor::retail_flood_reachable_scratch(lab,i,wall,q);for(size_t j=0;j<lab.state.size();++j){++compared;if(uint32_t(native[j].stateMask)!=lab.state[j]){std::cerr<<"Flood mismatch trial="<<trial<<" seed="<<seed<<" cell="<<j<<'\n';return 1;}}}
 }
 std::cout<<"NATIVE_REACHABILITY PASS compared_cells="<<compared<<" cases=2160\n";
}
