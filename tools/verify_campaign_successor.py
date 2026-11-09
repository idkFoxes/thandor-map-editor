#!/usr/bin/env python3
"""Compare the CGN resolver against the unchanged supplied next-level function.
Usage: verify_campaign_successor.py OPEN_THANDOR_ROOT EDITOR_BUILD OUTPUT_DIRECTORY
The source function uses controlled resource/path stubs; this is not a game.exe run.
"""
from pathlib import Path
import hashlib,json,subprocess,sys

def function(source,signature):
    start=source.index(signature)
    opening=source.index('{',start)
    depth=0
    for end in range(opening,len(source)):
        if source[end]=='{':depth+=1
        elif source[end]=='}':
            depth-=1
            if depth==0:return source[start:end+1]
    raise ValueError('unterminated function')

def main():
    native,build,out=map(lambda p:Path(p).resolve(),sys.argv[1:])
    out.mkdir(parents=True,exist_ok=True)
    editor=Path(__file__).resolve().parents[1]
    path=native/'src/ui/frontend/main_loop.cpp'
    source=path.read_text()
    bodies=[function(source,'static inline CampaignAsset *FrontendMainLoop_NextLevelRecordView'),
            function(source,'static bool FrontendMainLoop_SelectCampaignSuccessorLevel')]
    harness=r'''
#include "thandor/campaign.hpp"
#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <random>
#include <stdexcept>
struct CampaignLevelRecord {
 int32_t successorLevelIds[8],endMovieNumbersVariant[8],endMovieNumbers[8];
 int32_t exitZoneCenterX[8],exitZoneCenterY[8],exitZoneRadius[8],exitZoneDestinationX[8],exitZoneDestinationY[8];
 int32_t levelId;uint32_t carryOverMask,skipMask;uint16_t levelFileName[58];
};
struct CampaignAsset {
 uint32_t magic,decodedSizeBytes;uint8_t reserved08_B3[0xAC];
 int32_t firstLevelId,levelRecordCount;uint8_t reservedBC_C3[8];
 int32_t currentLevelId;uint8_t reservedC8_1FF[0x138];CampaignLevelRecord levels[1];
};
static_assert(sizeof(CampaignLevelRecord)==0x180&&offsetof(CampaignLevelRecord,levelId)==0x100);
static_assert(offsetof(CampaignAsset,levels)==0x200&&offsetof(CampaignAsset,currentLevelId)==0xC4);
CampaignAsset* g_FrontendLoadedCampaignAsset=nullptr;
uint32_t g_EndMovieSelectionIndex=0,g_FrontendScenarioInitializationCount=9;
uint16_t g_FrontendScenarioPathScratchUtf16[256]{};
uint16_t g_ScenarioLevelDirectoryUtf16[]={'l','e','v','e','l',0};
constexpr int WIDE_PATH_EXTENSION_LEV=1;
bool released=false;
void Resource_Release(void*){released=true;}
template<class... T>void Thandor_Log(const char*,T...){}
void WidePath_CombineDirectoryAndLeaf(uint16_t* out,const uint16_t* leaf,const uint16_t* dir){
 while(*dir)*out++=*dir++;*out++='\\';while(*leaf)*out++=*leaf++;*out=0;
}
void WidePath_SetExtensionCode(int,uint16_t* out){
 while(*out)++out;for(char c:std::string(".lev"))*out++=uint16_t(c);*out=0;
}
'''
    tests=r'''
int main(){
 std::mt19937 rng(760);unsigned cases=0;
 for(unsigned n=0;n<10000;++n){
  std::vector<std::byte> b(0x200+4*0x180);
  thandor::write_u32(b,0,thandor::fourcc("cgn"));thandor::write_u32(b,4,uint32_t(b.size()));
  thandor::write_i32(b,0xB4,10);thandor::write_i32(b,0xB8,4);
  const int32_t ids[]={10,20,42,99};
  for(unsigned i=0;i<4;++i){
   auto r=thandor::CampaignLevelRecord::minimal(ids[i],"map"+std::to_string(i));
   for(unsigned o=0;o<8;++o){const auto choice=rng()%7;
    r.set_successor(o,choice<4?ids[choice]:choice==4?0:choice==5?-1:777);}
   std::memcpy(b.data()+0x200+i*0x180,r.bytes().data(),0x180);
  }
  const int32_t current=n%17==0?888:ids[rng()%4];
  thandor::write_i32(b,0xC4,current);
  const auto c=thandor::CampaignAsset::parse(b);const auto outcome=rng()%8;
  const auto expected=c.continuation(current,outcome);
  g_FrontendLoadedCampaignAsset=reinterpret_cast<CampaignAsset*>(b.data());
  g_EndMovieSelectionIndex=outcome;released=false;g_FrontendScenarioInitializationCount=9;
  std::memset(g_FrontendScenarioPathScratchUtf16,0,sizeof g_FrontendScenarioPathScratchUtf16);
  const bool advanced=FrontendMainLoop_SelectCampaignSuccessorLevel();
  using K=thandor::CampaignContinuationKind;
  if(advanced!=(expected.kind==K::NextLevel)||released!=(expected.kind==K::CampaignFinished))
   throw std::runtime_error("campaign continuation/release differs from supplied source");
  if(expected.kind==K::NextLevel){
   if(g_FrontendLoadedCampaignAsset->currentLevelId!=expected.successor_id)throw std::runtime_error("wrong successor id");
   std::string path;for(auto* p=g_FrontendScenarioPathScratchUtf16;*p;++p)path.push_back(char(*p));
   if(path!="level\\"+expected.level_path+".lev")throw std::runtime_error("wrong successor path");
  }else if(expected.kind==K::NoTransition){
   if(!g_FrontendLoadedCampaignAsset||g_FrontendLoadedCampaignAsset->currentLevelId!=current)
    throw std::runtime_error("negative/missing-current path changed campaign state");
  }else if(g_FrontendLoadedCampaignAsset||g_FrontendScenarioInitializationCount!=0)
   throw std::runtime_error("finished campaign was not released/reset");
  ++cases;
 }
 std::cout<<"Native campaign successor comparison PASS cases="<<cases<<"; branch ids, replay, missing id termination, negative id retain and unknown current\n";
}
'''
    cpp=out/'native_campaign_successor.cpp';cpp.write_text(harness+'\n'+'\n'.join(bodies)+'\n'+tests)
    binary=out/'native_campaign_successor'
    command=['g++','-std=c++20','-O2','-I'+str(editor/'include'),str(cpp),str(build/'libthandor_map_core.a'),'-pthread','-ldl','-o',str(binary)]
    results=[]
    for name,args in [('compile',command),('run',[str(binary)])]:
        r=subprocess.run(args,text=True,capture_output=True,timeout=60)
        (out/(name+'.log')).write_text(r.stdout+r.stderr);results.append({'name':name,'returncode':r.returncode})
        if r.returncode:raise RuntimeError(r.stdout+r.stderr)
        if name=='run':print(r.stdout.strip())
    (out/'results.json').write_text(json.dumps({'source':str(path.relative_to(native)),
        'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'function_sha256':[hashlib.sha256(b.encode()).hexdigest() for b in bodies],
        'scope':'Unchanged supplied successor body and cursor helper; controlled path/resource functions; not original binary execution.',
        'results':results},indent=2)+'\n')
if __name__=='__main__':main()
