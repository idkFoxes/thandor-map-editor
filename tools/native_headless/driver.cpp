// Offline bootstrap and original world tick, without SDL, Wine, graphics frames or sockets.
#include <thandor/thandor.h>
#include <thandor/platform/bootstrap/image.h>
#include "native_simulation_tick.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <execinfo.h>
#include <unistd.h>
#include <string>
static InGameRuntimeRoot root;
#include "trace.hpp"
int main(int argc,char **argv) try {
 const unsigned ticks=argc>1?std::stoul(argv[1]):20;
 std::signal(SIGSEGV,+[](int sig){void *b[40];int n=backtrace(b,40);backtrace_symbols_fd(b,n,2);_exit(128+sig);});
 const int niceResult=nice(10);(void)niceResult;const std::string prefix=argc>2?argv[2]:"native_run";
 // Required precomputed executable tables, initialized by stock WinMain.
 FixedMath_BuildSinCosTables();
 GraphicsLighting_BuildPackedLookupTable();
 SoftwareRenderer_BuildFactorTables();
 GraphicsShading_BuildIntensityScaleTable();
 // Fail immediately if a headless bootstrap ever omits the fixed trig tables.
 const auto projection=FixedTrig_ProjectPlanarPoint(0,0,4096,0);
 if(projection.xQ12!=4096||projection.yQ12!=0)throw std::runtime_error("fixed trig startup self-check failed");
 ArenaHeap_Init();uint32_t err=Graphics_AllocateTables();if(err)FatalError_ShowAndExit(err);
 err=g_MemoryApi.alloc(PACKAGE_SCRATCH_BUFFER_BYTES,reinterpret_cast<void**>(&g_PackageScratchBuffer));if(err)FatalError_ShowAndExit(err);
 for(const char* name:{"ENGINE.PCK","DATEN.PCK","MODELLE.PCK","GRAPHIK.PCK","SOUND.PCK","LEVEL.PCK"}){
  uint16_t path[256]={};for(unsigned i=0;name[i];++i)path[i]=name[i];uintptr_t result=0;
  if(!Package_Mount(path,&result)){std::fprintf(stderr,"mount failed %s: %lx\n",name,result);return 2;}
  if(std::strcmp(name,"SOUND.PCK")==0)g_SoundPackageHandle=result;
 }
 if(!AiRuntime_InitWorkspace(&err))FatalError_ShowAndExit(err);
 err=GameRuntime_InitializeSpatialAudioAndRendering();if(err)FatalError_ShowAndExit(err);SoftwareRenderer_BuildFactorTables();
 if(!SoftwarePixelFormat_BaseDisplayModeHook(0,32,0,0,&err))FatalError_ShowAndExit(err);
 const uint32_t seed=argc>3?std::stoul(argv[3]):12345U;Random_SetBothSeeds(seed);Random_SelectSecondaryStream();
 if((err=GameData_ResetDefaults()))FatalError_ShowAndExit(err);g_GameFactionRuntimeImage.tail.gameSpeedQ8=256;
 g_InGameRuntimeRoot=&root;auto *world=&root.worldRuntime;world->activeFactionRuntimeIndex=1;
 world->minimumCameraDistanceQ12=INGAME_CAMERA_MINIMUM_DISTANCE_Q12;world->maximumCameraDistanceQ12=INGAME_CAMERA_MAXIMUM_DISTANCE_Q12;
 world->motion.minimumPitchAngle=INGAME_CAMERA_MINIMUM_PITCH_ANGLE16;world->motion.maximumPitchAngle=INGAME_CAMERA_MAXIMUM_PITCH_ANGLE16;
 err=g_MemoryApi.alloc(8*sizeof(FrontendPlayerRuntimeRecord),reinterpret_cast<void**>(&g_FrontendPlayerRuntimeBlocks));if(err)FatalError_ShowAndExit(err);std::memset(g_FrontendPlayerRuntimeBlocks,0,8*sizeof(FrontendPlayerRuntimeRecord));
 // One empty observer during the original picture-load loop; no active faction assigned.
 g_InGameSimulationStepTicks=1;g_FrontendPlayerRuntimeBlockCount=1;
 void *pool=nullptr;err=g_MemoryApi.alloc(INGAME_WORLD_OBJECT_RECORD_COUNT*sizeof(WorldObjectRecord),&pool);if(err)FatalError_ShowAndExit(err);std::memset(pool,0,INGAME_WORLD_OBJECT_RECORD_COUNT*sizeof(WorldObjectRecord));
 g_InGameWorldObjectRecords=static_cast<WorldObjectRecord*>(pool);g_RuntimeObjectRebaseBaseMinusOne=static_cast<uint8_t*>(pool)-1;
 WorldRuntime_AttachObjectArray(INGAME_WORLD_OBJECT_RECORD_COUNT,g_InGameWorldObjectRecords,world);WorldRuntime_AttachAndClearDwordArray(256,g_InGameWorldRuntimeDwordArray256,world);
 std::string map=argc>4?argv[4]:"mittelpunkt";if(map.empty()||map.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos)throw std::runtime_error("map must be ASCII stock map leaf name");
 std::u16string levPath;for(char c:std::string("level\\")+map+".lev")levPath.push_back(c);
 auto *lev=static_cast<LevelAssetRuntimePrefix*>(Package_LoadEntry(reinterpret_cast<uint16_t*>(levPath.data()),&err));if(!lev)FatalError_ShowAndExit(err);
 auto *fldPath=reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(lev)+lev->header.pathOffsets.levelPathOffset);WidePath_SetExtensionCode(WIDE_PATH_EXTENSION_FLD,fldPath);
 auto *fld=static_cast<FieldGridAsset*>(Package_LoadEntry(fldPath,&err));if(!fld)FatalError_ShowAndExit(err);lev->header.pathOffsets.levelPathOffset=Thandor_PointerToU32(fld);
 // Match the stock frontend's lifecycle setup before offline world loading.
 // Otherwise the compiled defaults leave empty slots 5..7 active on four-faction maps.
 g_FrontendLoadedLevelAsset=reinterpret_cast<FrontendLoadedLevelAsset*>(lev);
 const auto observer_count=g_FrontendPlayerRuntimeBlockCount;
 g_FrontendPlayerRuntimeBlockCount=0;
 FrontendPlayerRuntime_InitializeFactionAssignments();
 g_FrontendPlayerRuntimeBlockCount=observer_count;
 for(unsigned f=1;f<8;++f){
  const auto actual=g_GameFactionRuntimeImage.tail.factionLifecycleStates[f];
  const auto count=lev->worldSettings.activeFactionCount;
  // Preserve the stock six-faction quirk: its initializer leaves slot 7 alone.
  if((f<=count&&actual!=FACTION_RUNTIME_LIFECYCLE_ACTIVE)||
     (count<6&&f>count&&actual!=FACTION_RUNTIME_LIFECYCLE_INACTIVE))
      throw std::runtime_error("stock faction lifecycle initialization failed");
 }
 std::fprintf(stderr,"startup faction slots:");for(unsigned f=0;f<8;++f)std::fprintf(stderr," %u",unsigned(g_GameFactionRuntimeImage.tail.factionLifecycleStates[f]));std::fputc(10,stderr);
 if(!InGameLevelRuntime_LoadResourcesAfterDefaultReset(lev,world,&err))FatalError_ShowAndExit(err);
 err=g_MemoryApi.alloc(OLD_UNIT_SECONDARY_TABLE_BYTES,reinterpret_cast<void**>(&g_OldUnitSecondaryTable));if(err)FatalError_ShowAndExit(err);
 err=g_MemoryApi.alloc(OLD_UNIT_PRIMARY_TABLE_BYTES,reinterpret_cast<void**>(&g_OldUnitPrimaryTable));if(err)FatalError_ShowAndExit(err);
 err=g_MemoryApi.alloc(SELECTION_PLAYER_BLOCK_COUNT*sizeof(SelectionPlayerRuntimeBlock),reinterpret_cast<void**>(&g_SelectionPlayerBlocks));if(err)FatalError_ShowAndExit(err);std::memset(g_SelectionPlayerBlocks,0,SELECTION_PLAYER_BLOCK_COUNT*sizeof(SelectionPlayerRuntimeBlock));for(unsigned i=0;i<SELECTION_PLAYER_BLOCK_COUNT;++i)g_SelectionPlayerRuntimeBlockPointers[i]=&g_SelectionPlayerBlocks[i];
 void *scratch=nullptr;err=g_MemoryApi.alloc(TERRAIN_REGION_COLLECTION_CAPACITY*8,&scratch);if(err)FatalError_ShowAndExit(err);g_TerrainRegionCollectionEntries=reinterpret_cast<uintptr_t>(scratch);OldUnitRuntime_ResetPendingTables();
 if(!InGameSession_AllocateGridScratchAndRebuildDerived(world,&err))FatalError_ShowAndExit(err);WorldLightingRuntime_UpdateInterpolatedTerrainLighting();g_FrontendPlayerRuntimeBlockCount=0;
 NativeTrace trace(prefix);trace.capture(true);
 for(unsigned tick=0;tick<ticks;++tick){InGameTick_RunSimulationStep(&root);trace.capture((tick+1)%20==0||tick<20);if((tick+1)%20==0)trace.throttle();}
 trace.finish(prefix);std::fprintf(stderr,"completed native ticks=%u\n",g_GameFactionRuntimeImage.tail.simulationTick);return 0;
} catch(const std::exception& e){std::fprintf(stderr,"headless error: %s\n",e.what());return 4;}
