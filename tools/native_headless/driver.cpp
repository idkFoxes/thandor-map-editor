// Offline bootstrap and original world tick, without SDL, Wine, graphics frames or sockets.
#include <thandor/thandor.h>
#include <thandor/platform/bootstrap/image.h>
#include "native_simulation_tick.cpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#ifndef THANDOR_WINDOWS_HEADLESS
#include <execinfo.h>
#include <unistd.h>
#endif
#include <string>
#include <memory>
#include <filesystem>
#include <fstream>
static InGameRuntimeRoot root;
#include "trace.hpp"
#ifdef THANDOR_JANDRENATOR_HEADLESS
#include <thandor/gameplay/ai/improved.h>
#include <thandor/graphics/render/light_transitions.h>
#include <thandor/gameplay/army/evade.h>
#include <thandor/gameplay/session/player_record.h>
#include <thandor/ui/frontend/session.h>
#include "jandrenator_trace.hpp"

// Use gameplay state, since animation, particles and RNG keep changing in an idle world.
static uint64_t jandrenator_activity() {
    uint64_t activity = 14695981039346656037ULL;
    const auto add = [&](uint32_t value) {
        activity = (activity ^ value) * 1099511628211ULL;
    };
    const auto& game = g_GameFactionRuntimeImage;
    const auto human = root.worldRuntime.activeFactionRuntimeIndex;
    const bool human_active = AiImproved_IsHumanFaction(human) &&
        game.tail.factionLifecycleStates[human] == FACTION_RUNTIME_LIFECYCLE_ACTIVE;
    for (unsigned faction = 1; faction <= game.tail.activeFactionCount && faction < 8; ++faction) {
        const auto& record = game.records[faction];
        add(game.tail.factionLifecycleStates[faction]);
        add(record.packedRelationStates);
        add(record.xeniteCurrentQ4);
        add(record.tritiumCurrentQ4);
        add(record.primaryArmyAssetCount);
        add(record.secondaryArmyAssetCount);
        for (auto mask : record.technologyMasks256Bits) add(mask);
        if (human_active && faction != human &&
            game.tail.factionLifecycleStates[faction] == FACTION_RUNTIME_LIFECYCLE_ACTIVE &&
            ((record.packedRelationStates >> (human * 4)) & 0xFU) < FACTION_RELATION_STATE_ALLIED &&
            AiImproved_GraceActive(faction))
            add(game.tail.simulationTick);
    }
    for (unsigned slot = 0; slot < ARMY_RUNTIME_SLOT_COUNT; ++slot) {
        const auto& army = g_ArmyRuntimeSlots[slot];
        if (army.factionIndex <= 0 || !army.modelNodeRuntime) continue;
        const auto* model = army.modelRuntimeOrSavedOffset.modelRuntime.get();
        if (!model || !model->definitionOrSavedId.runtimeDefinition) continue;
        const auto& position = army.modelNodeRuntime->worldTransform.translation;
        add(slot);
        add(army.factionIndex);
        add(army.armyAssetId);
        add(position.x);
        add(position.y);
        add(model->health);
        add(model->researchElapsedTicks);
        const auto model_class = model->definitionOrSavedId.runtimeDefinition->runtimeClassId;
        if ((model_class == MODEL_RUNTIME_CLASS_11 || model_class == MODEL_RUNTIME_CLASS_13) &&
            model->classState.behaviorState == 1)
            add(model->classLinkState.classState64);
    }
    if (const auto* storage = g_InGameLevelRuntimeGlobalBlock.conditionStorage.get()) {
        for (const auto& condition : storage->schedule.conditions) {
            if ((condition.statusAndKind.kind & INGAME_SCHEDULED_CONDITION_KIND_MASK) ==
                INGAME_SCHEDULED_CONDITION_COUNTDOWN_ELAPSED)
                add(condition.payload.operands[1]);
        }
    }
    return activity;
}
#endif
#ifdef THANDOR_JANDRENATOR_EMBEDDED
int jandrenator_worker_main(int argc,char **argv) try {
#else
int main(int argc,char **argv) try {
#endif
 const unsigned ticks=argc>1?std::stoul(argv[1]):20;
#ifndef THANDOR_WINDOWS_HEADLESS
 std::signal(SIGSEGV,+[](int sig){void *b[40];int n=backtrace(b,40);backtrace_symbols_fd(b,n,2);_exit(128+sig);});
 const int niceResult=nice(10);(void)niceResult;
#endif
 const std::string prefix=argc>2?argv[2]:"native_run";
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
#ifdef THANDOR_JANDRENATOR_HEADLESS
 uint16_t mapPackage[]{'m','a','p','.','p','c','k',0};
 if(!Package_Mount(mapPackage,nullptr))throw std::runtime_error("cannot mount authored map");
 AiImproved_LoadSettings();
#endif
 for(const char* name:{"ENGINE.PCK","DATEN.PCK","MODELLE.PCK","GRAPHIK.PCK","SOUND.PCK","LEVEL.PCK"}){
  uint16_t path[256]={};for(unsigned i=0;name[i];++i)path[i]=name[i];uintptr_t result=0;
  if(!Package_Mount(path,&result)){std::fprintf(stderr,"mount failed %s: %lx\n",name,result);return 2;}
  if(std::strcmp(name,"SOUND.PCK")==0)g_SoundPackageHandle=result;
 }
 if(!AiRuntime_InitWorkspace(&err))FatalError_ShowAndExit(err);
 err=GameRuntime_InitializeSpatialAudioAndRendering();if(err)FatalError_ShowAndExit(err);SoftwareRenderer_BuildFactorTables();
 if(!SoftwarePixelFormat_BaseDisplayModeHook(0,32,0,0,&err))FatalError_ShowAndExit(err);
 const uint32_t seed=argc>3?std::stoul(argv[3]):12345U;Random_SetBothSeeds(seed);Random_SelectSecondaryStream();
 if((err=GameData_ResetDefaults()))FatalError_ShowAndExit(err);
#ifdef THANDOR_JANDRENATOR_HEADLESS
 const auto opponentPercent=argc>6?std::stoul(argv[6]):100U;
 if(opponentPercent<80||opponentPercent>120)throw std::runtime_error("opponent strength must be 80 to 120 percent");
 // The integrated game uses an exact neutral base for Jandrenator.
 g_GameFactionRuntimeImage.tail.gameSpeedQ8=opponentPercent==100 ? Q8_ONE :
    (opponentPercent*FRONTEND_GAME_SPEED_PERCENT_TO_Q8_Q16)>>16;
 std::fprintf(stderr,"seed: %u; opponent strength: %lu%% (Q8 %u)\n",seed,opponentPercent,g_GameFactionRuntimeImage.tail.gameSpeedQ8);
#else
 g_GameFactionRuntimeImage.tail.gameSpeedQ8=256;
#endif
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
#ifdef THANDOR_JANDRENATOR_HEADLESS
 const unsigned human=argc>5?std::stoul(argv[5]):1;
 if(human==0||human>lev->worldSettings.activeFactionCount)throw std::runtime_error("invalid human faction");
 FrontendPlayerRuntime_InitializeFactionAssignments();
 g_FrontendPlayerRuntimeBlocks[0].factionAssignment.factionAssignmentIndex=human;
 world->activeFactionRuntimeIndex=human;
#else
 const auto observer_count=g_FrontendPlayerRuntimeBlockCount;
 g_FrontendPlayerRuntimeBlockCount=0;
 FrontendPlayerRuntime_InitializeFactionAssignments();
 g_FrontendPlayerRuntimeBlockCount=observer_count;
#endif
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
 if(!InGameSession_AllocateGridScratchAndRebuildDerived(world,&err))FatalError_ShowAndExit(err);WorldLightingRuntime_UpdateInterpolatedTerrainLighting();
#ifdef THANDOR_JANDRENATOR_HEADLESS
 g_SelectionPlayerRuntimeBlockPointers[0]->factionIndex=human;
 AiImproved_OnSessionStart(false);
 ArmyEvade_OnSessionStart();
 PlayerRecord_OnSessionStart(false);
 const auto tick_ms=argc>7?std::stoul(argv[7]):0U;
 if(tick_ms>5000)throw std::runtime_error("invalid presentation tick interval");
 JandrenatorTrace(prefix);
 JandrenatorScene(prefix);
 std::unique_ptr<NativeTrace> validation;
 if(std::getenv("THANDOR_JANDRENATOR_VALIDATION")){
  validation=std::make_unique<NativeTrace>("validation");
  validation->capture(true);
 }
 const auto pause_file = argc > 8 ? std::filesystem::path(argv[8]) : std::filesystem::path{};
 // Fixed-tick validation callers omit the experiment's early-stop policy.
 const bool experiment_run = argc > 9;
 const auto idle_minutes = experiment_run ? std::stoul(argv[9]) : 0;
 if (idle_minutes > 90) throw std::runtime_error("invalid idle limit");
 constexpr unsigned ticks_per_second = 20;
 const auto idle_limit = idle_minutes * 60 * ticks_per_second;
 unsigned last_activity_tick = 0;
 uint64_t activity = idle_limit ? jandrenator_activity() : 0;
 using Clock = std::chrono::steady_clock;
 constexpr auto poll_interval = std::chrono::milliseconds(25);
 auto start = Clock::now();
 const auto scene_interval=tick_ms ? std::max(1UL, (50 + tick_ms - 1) / tick_ms) : 100UL;
 for(unsigned tick=0;tick<ticks;++tick){
  auto deadline = start + std::chrono::milliseconds((tick + 1) * tick_ms);
  if (pause_file.empty()) {
   if (tick_ms) std::this_thread::sleep_until(deadline);
  } else {
   for (;;) {
    if (std::filesystem::exists(pause_file)) {
     const auto paused_at = Clock::now();
     do { std::this_thread::sleep_for(poll_interval); } while (std::filesystem::exists(pause_file));
     const auto elapsed = Clock::now() - paused_at;
     start += elapsed;
     deadline += elapsed;
    }
    const auto now = Clock::now();
    if (now >= deadline) break;
    std::this_thread::sleep_until(std::min(deadline, now + poll_interval));
   }
  }
  InGameTick_RunSimulationStep(&root);
  InterpolationStateTable_Advance256ByTicks(g_InGameSimulationStepTicks);
  if(validation)validation->capture((tick+1)%20==0);
  const unsigned elapsed = tick + 1;
  const char* result = nullptr;
  if (experiment_run && Any(g_UiCommandRuntimeFlags & UI_COMMAND_RUNTIME_FLAG_END_MOVIE_PENDING)) {
   result = "Match ended";
  } else if (idle_limit && elapsed % ticks_per_second == 0) {
   const auto current_activity = jandrenator_activity();
   if (current_activity != activity) {
    activity = current_activity;
    last_activity_tick = elapsed;
   } else if (elapsed - last_activity_tick >= idle_limit) {
    result = "Stopped after inactivity";
   }
  }
  if (elapsed % 100 == 0 || elapsed == ticks || result) JandrenatorTrace(prefix);
  if (elapsed % scene_interval == 0 || elapsed == ticks || result) JandrenatorScene(prefix);
  if (result) {
   std::ofstream result_file("result");
   result_file << result << '\n';
   break;
  }
 }
 if(validation)validation->finish("validation");
 return 0;
#else
 g_FrontendPlayerRuntimeBlockCount=0;
 NativeTrace trace(prefix);trace.capture(true);
 for(unsigned tick=0;tick<ticks;++tick){InGameTick_RunSimulationStep(&root);trace.capture((tick+1)%20==0||tick<20);if((tick+1)%20==0)trace.throttle();}
 trace.finish(prefix);std::fprintf(stderr,"completed native ticks=%u\n",g_GameFactionRuntimeImage.tail.simulationTick);return 0;
#endif
} catch(const std::exception& e){std::fprintf(stderr,"headless error: %s\n",e.what());return 4;}
