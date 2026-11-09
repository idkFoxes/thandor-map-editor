#!/usr/bin/env python3
"""Compare all three native water-damage arithmetic blocks with the shared helper.
Usage: verify_native_water_damage.py OPEN_THANDOR_ROOT BUILD_DIR
Only the extracted damage block is executed; this is not a full-game replay.
"""
from pathlib import Path
import subprocess,sys,tempfile
reference,build=map(lambda p:Path(p).resolve(),sys.argv[1:])
root=Path(__file__).resolve().parents[1]
blocks=[]
for name,count in [('drive_ground.cpp',2),('walker.cpp',1)]:
    source=(reference/'src/gameplay/army'/name).read_text();start=0
    for _ in range(count):
        a=source.index('if (movementDefinition->waterDamageThreshold < waterDelta)',start)
        brace=source.index('{',a);end=brace+1;depth=1
        while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
        blocks.append(source[a:end]);start=end
prefix=r'''
#include "thandor/simulation_terrain_damage.hpp"
#include <random>
#include <iostream>
namespace native {
struct ModelRuntimeSlot{};struct Definition{int32_t waterDamageThreshold,waterDamageMultiplier;};
std::optional<int32_t> result;
template<class T> T* ModelView_Cast(ModelRuntimeSlot* p){return p;}
void ArmyRuntime_ApplyDamageAndPropagateToParent(int32_t value,ModelRuntimeSlot*){result=value;}
'''
for i,block in enumerate(blocks):
    prefix+='std::optional<int32_t> apply'+str(i)+'(int32_t waterDelta,int32_t threshold,int32_t multiplier){\nDefinition definition{threshold,multiplier};auto* movementDefinition=&definition;ModelRuntimeSlot model;auto* modelRuntime=&model;int waterDamage;result.reset();\n'+block+'\nreturn result;}\n'
prefix+=r'''}
int main(){std::mt19937 rng(12345);size_t count=0;
 for(size_t i=0;i<100000;++i){int32_t depth=int32_t(rng()),threshold=int32_t(rng()),multiplier=int32_t(rng());
  for(auto fn:{native::apply0,native::apply1,native::apply2}){
   if(fn(depth,threshold,multiplier)!=thandor::simulation_water_damage_amount(depth,threshold,multiplier))return 1;++count;}}
 std::cout<<"NATIVE_WATER_DAMAGE PASS comparisons="<<count<<" native_blocks=3 signed_wrap threshold zero negative\n";}
'''
with tempfile.TemporaryDirectory(prefix='thandor-water-oracle-') as tmp:
    source=Path(tmp)/'oracle.cpp';exe=Path(tmp)/'oracle';source.write_text(prefix)
    subprocess.run(['g++','-std=c++20','-O2','-fwrapv','-I'+str(root/'include'),str(source),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
