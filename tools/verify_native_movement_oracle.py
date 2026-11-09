#!/usr/bin/env python3
"""Compile selected, unmodified open-thandor grid routines against a small data adapter.
Usage: python tools/verify_native_movement_oracle.py OPEN_THANDOR_ROOT BUILD_DIR
Requires the editor's libthandor_map_core.a. No game data or display is used.
The terrain rebuild is intentionally stopped before its world-owner flood fills;
this tests classification/expansion/dilation and influence, not whole-game parity.
"""
from pathlib import Path
import re
import subprocess
import sys

reference, build = map(lambda p: Path(p).resolve(), sys.argv[1:])
editor = Path(__file__).resolve().parents[1]

def function(text, name):
    # Anchor to the definition line, avoiding earlier comments and callsites.
    match = re.search(r'^(?:static )?(?:void|bool|int|uint32_t|GridScratchStateMask) '+re.escape(name)+r'\b', text, re.M)
    if not match:
        raise ValueError(name)
    start = match.start()
    brace = text.index('{', match.end())
    depth = 1
    end = brace+1
    # The selected functions contain no braces in strings/comments.
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

scratch = (reference/'src/world/pathing/scratch_grid.cpp').read_text()
influence = (reference/'src/world/pathing/influence.cpp').read_text()
grid_header = (reference/'include/thandor/world/pathing/grid.h').read_text()
constants = '\n'.join(line for line in grid_header.splitlines() if line.startswith('inline constexpr') and ';' in line)
# Keep the original classification, stamping and dilation statements exactly.
rebuild = function(scratch, 'GridScratch_RebuildTerrainAndRuntimeClassificationMasks')
rebuild = rebuild[:rebuild.index('  ownerNode = worldRuntime->ownerListHead;')]+'\n}'
thresholds = 'const int32_t g_GridTerrainClassThresholds[] = {0,11500,10500,10500,10500,14000,15000,15500,500,500,500,10500,10500,10500,12500,13500,14500};'
threshold_names = re.findall(r'GRID_TERRAIN_THRESHOLD_[A-Z0-9_]+', function(scratch,'GridScratch_ClassifyFieldCell'))
# Values come from the reference enum, not guessed name ordering.
threshold_header=(reference/'include/thandor/world/pathing/grid.h').read_text()
other_headers='\n'.join(p.read_text() for p in (reference/'include/thandor/world/pathing').glob('*.h'))
enum_body=re.search(r'enum GridTerrainClassThresholdIndex[^\{]*\{(.*?)\};', other_headers,re.S)
if enum_body:
    threshold_enum='enum GridTerrainClassThresholdIndex {'+enum_body.group(1)+'};'
else:
    # Current reference has an anonymous enum; capture the table's first marker.
    begin=other_headers.rfind('enum',0,other_headers.index('GRID_TERRAIN_THRESHOLD_BIT24_MAX_WATER_SURFACE_DELTA'))
    end=other_headers.index('};',begin)+2
    threshold_enum=other_headers[begin:end]

prefix=r'''
#include "thandor/simulation_lab.hpp"
#include "thandor/retail_placement.hpp"
#include <cassert>
#include <iostream>
#include <random>
#include <stdexcept>
namespace native {
using Q12=int32_t; using FieldGridRadiusUnits=int32_t; using FieldGridCellCoordinate=int32_t;
using FieldGridDimension=uint32_t; using FieldCellPackedFlagsAndMaterial=uint32_t;
using GridScratchStateMask=uint32_t;
constexpr uint32_t FIELD_CELL_FIRST_COLUMN_BOUNDARY=0x2000, FIELD_CELL_FIRST_ROW_BOUNDARY=0x4000;
constexpr uint32_t FIELD_CELL_LAST_COLUMN_BOUNDARY=0x08000000, FIELD_CELL_LAST_ROW_BOUNDARY=0x80000000;
constexpr uint32_t FIELD_CELL_GRID_EDGE_MASK=0x88006000;
constexpr int FIELD_GRID_WORLD_X_TO_COLUMN_Q20=0x1C6E9C, FIELD_GRID_WORLD_Y_TO_ROW_Q20=-0x20C8CC;
constexpr int FIELD_GRID_WORLD_COLUMN_STEP_X=0x901, FIELD_GRID_WORLD_ROW_STEP_Y=-1999;
constexpr uint32_t GRID_SCRATCH_TRAVERSAL_VISITED=1;
constexpr uint32_t GRID_SCRATCH_TERRAIN_CLASS_BIT24=1U<<24, GRID_SCRATCH_TERRAIN_CLASS_BIT25=1U<<25;
constexpr uint32_t GRID_SCRATCH_TERRAIN_CLASS_BIT26=1U<<26, GRID_SCRATCH_TERRAIN_CLASS_BIT27=1U<<27;
constexpr uint32_t GRID_SCRATCH_TERRAIN_CLASS_BIT28=1U<<28, GRID_SCRATCH_TERRAIN_CLASS_BIT29=1U<<29, GRID_SCRATCH_TERRAIN_CLASS_BIT30=1U<<30;
constexpr uint64_t FIELD_CELL_OCCUPANCY_PRESENCE_BITS=0x88;
#define FIELD_CELL_OCCUPANCY_SLOT_MASK(bits,slot) (uint64_t(bits) << (8*(slot)))
bool Any(uint32_t value){return value!=0;}
struct FieldGridCell {uint32_t flagsAndMaterial{},triangle0NormalAngles{},triangle1NormalAngles{}; int32_t waterSurfaceDelta{}; uint64_t occupancyMask{};};
struct FieldGridAsset {uint32_t gridWidth{},gridHeight{}; FieldGridCell* cells{};};
struct WorldOwnerListNode {};
struct WorldRuntimeContext {FieldGridAsset* fieldGrid{}; WorldOwnerListNode* ownerListHead{};};
struct GridScratchCell {uint32_t stateMask{},pathCost{};};
uint32_t* GridScratchCell_StateMaskBits(GridScratchCell* p){return &p->stateMask;}
uint32_t g_GridScratchWidth{},g_GridScratchHeight{};
GridScratchCell* g_GridScratchPrimary{};
const uint32_t g_GridInfluenceRadiusOffset[8]={1000,1250,1500,1600,1920,2240,2600,4100};
uint32_t g_GridInfluenceSquaredThreshold[8]{};
using GridInfluenceBandWalker=int(FieldGridCellCoordinate,FieldGridCellCoordinate,FieldGridCellCoordinate,FieldGridCellCoordinate,uint32_t*);
'''
native = prefix+'\n'+constants+'\n'+threshold_enum+'\n'+thresholds+'\n'
native += function(scratch,'GridScratch_ClassifyFieldCell')+'\n'+rebuild+'\n'
native += function(influence,'GridInfluence_RingBandMask')+'\n'
for band in ('Low','High'):
    for direction in ('Negative','Positive'):
        native += function(influence,f'GridInfluence_Set{band}DistanceBandsDiagonal{direction}')+'\n'
native += function(influence,'GridInfluence_WalkDistanceBandsAroundWorldPoint')+'\n}\n'
main=r'''
int main(){
 using namespace thandor;
 std::mt19937 random(0x78656373);
 size_t compared=0,influence_cases=0,terrain_cases=0;
 for(int scenario=0;scenario<12;++scenario){
  auto field=FieldGrid::create(67,53,0,0);
  for(uint32_t y=1;y+1<field.height();++y)for(uint32_t x=1;x+1<field.width();++x){
   auto& c=field.at(x,y);
   c.terrain_height_q12=scenario==0?0:int32_t(random()%14001)-7000;
   c.water_delta_q12=scenario<2?0:int32_t(random()%1601)-800;
  }
  for(uint32_t y=0;y<field.height();++y)for(uint32_t x=0;x<field.width();++x){
   auto& flags=field.at(x,y).flags_and_material;
   if(!x)flags|=0x2000;if(!y)flags|=0x4000;
   if(x+1==field.width())flags|=0x08000000;if(y+1==field.height())flags|=0x80000000;
  }
  auto actual=retail_build_terrain_scratch(field);
  std::vector<native::FieldGridCell> cells(field.cells().size());
  for(uint32_t y=0;y<field.height();++y)for(uint32_t x=0;x<field.width();++x){
   const auto& c=field.at(x,y); auto& n=cells[size_t(y)*field.width()+x];
   n.flagsAndMaterial=c.flags_and_material;n.waterSurfaceDelta=c.water_delta_q12;
   n.triangle0NormalAngles=retail_placement_normal_angles(field,x,y,false);
   n.triangle1NormalAngles=retail_placement_normal_angles(field,x,y,true);
  }
  native::FieldGridAsset grid{field.width(),field.height(),cells.data()};
  native::WorldRuntimeContext world{&grid,nullptr};
  native::g_GridScratchWidth=actual.width;native::g_GridScratchHeight=actual.height;
  std::vector<native::GridScratchCell> expected(actual.state.size());
  native::g_GridScratchPrimary=expected.data();
  native::GridScratch_RebuildTerrainAndRuntimeClassificationMasks(&world);
  for(size_t i=0;i<actual.state.size();++i){
   if(expected[i].stateMask!=actual.state[i]){
    std::cerr<<"terrain mismatch scenario="<<scenario<<" cell="<<i<<" native="<<std::hex<<expected[i].stateMask<<" editor="<<actual.state[i]<<'\n';return 1;
   }++compared;
  }++terrain_cases;
  // The native hexscan selftest uses seeded synthetic positions and boundaries.
  // Exercise all 24 class callbacks, all eight mover-radius classes and edges.
  for(uint32_t cls=0;cls<24;++cls)for(int trial=0;trial<12;++trial){
   const uint32_t x=1+random()%(field.width()-2), y=1+random()%(field.height()-2);
   MovementPoint p{field.at(x,y).world_x_q12+int32_t(random()%401)-200,
                   field.at(x,y).world_y_q12+int32_t(random()%401)-200};
   const int32_t radius=native::g_GridInfluenceRadiusOffset[trial%8];
   auto stamped=actual;
   for(size_t i=0;i<expected.size();++i)expected[i].stateMask=actual.state[i];
   const bool low=cls==4||(cls>=10&&cls<=16)||cls==20||cls==22;
   const bool high=cls<=3||(cls>=17&&cls<=19)||cls==23;
   if(low||high)native::GridInfluence_WalkDistanceBandsAroundWorldPoint(radius,p.y,p.x,
      low?native::GridInfluence_SetLowDistanceBandsDiagonalPositive:native::GridInfluence_SetHighDistanceBandsDiagonalPositive,
      low?native::GridInfluence_SetLowDistanceBandsDiagonalNegative:native::GridInfluence_SetHighDistanceBandsDiagonalNegative);
   retail_placement_stamp_influence(stamped,p,radius,cls);
   for(size_t i=0;i<expected.size();++i){
    if(expected[i].stateMask!=stamped.state[i]){
     std::cerr<<"influence mismatch class="<<cls<<" trial="<<trial<<" cell="<<i<<'\n';return 1;
    }++compared;
   }++influence_cases;
  }
 }
 std::cout<<"NATIVE_MOVEMENT_ORACLE PASS terrain_cases="<<terrain_cases<<" influence_cases="<<influence_cases<<" compared_cells="<<compared<<" classes=24\n";
}
'''
source=build/'native_movement_oracle.cpp'
source.write_text(native+main)
exe=build/'native_movement_oracle'
subprocess.run(['g++','-std=c++20','-O2','-fwrapv','-I'+str(editor/'include'),str(source),str(build/'libthandor_map_core.a'),'-pthread','-ldl','-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
