#define main NativeHeadlessMain
#include THANDOR_NATIVE_DRIVER_INCLUDE
#undef main
#include "thandor/retail_terrain_stamp.hpp"
#include <iostream>
int main(int argc,char**argv){if(NativeHeadlessMain(argc,argv))return 1;auto*f=root.worldRuntime.fieldGrid.get();auto lab=thandor::FieldGrid::create(f->gridWidth,f->gridHeight,0,0);for(unsigned i=0;i<lab.cells().size();++i){lab.cells()[i].terrain_height_q12=f->cells[i].terrainHeight;lab.cells()[i].water_delta_q12=f->cells[i].waterSurfaceDelta;lab.cells()[i].flags_and_material=uint32_t(f->cells[i].flagsAndMaterial);}
unsigned cases=0;for(int cy=1;cy<int(f->gridHeight)-1;cy+=7)for(int cx=1;cx<int(f->gridWidth)-1;cx+=7)for(unsigned radius:{0U,576U,2304U,7000U,15000U}){const auto c=lab.at(cx,cy);int x=c.world_x_q12+71,y=c.world_y_q12-193,z=c.terrain_height_q12+137;FieldGrid_ApplyHeightAtWorldPointAndRefreshNeighbors(radius,z,y,x,f);thandor::retail_stamp_structure_height(lab,x,y,z,radius);for(unsigned i=0;i<lab.cells().size();++i)if(lab.cells()[i].terrain_height_q12!=f->cells[i].terrainHeight||lab.cells()[i].water_delta_q12!=f->cells[i].waterSurfaceDelta){std::cerr<<"STAMP MISMATCH case="<<cases<<" index="<<i<<"\n";return 2;}++cases;}std::cout<<"NATIVE_STAMP PASS cases="<<cases<<"\n";}
