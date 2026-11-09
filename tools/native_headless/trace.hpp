#pragma once
#include <array>
#include <chrono>
#include <thread>
#include <ctime>
#include <stdexcept>
struct NativeTrace {
 FILE *ticks,*armies,*factions;uint64_t hash=14695981039346656037ULL;
 std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();std::clock_t cpuStart=std::clock();
 NativeTrace(const std::string& prefix){
  ticks=std::fopen((prefix+"_ticks.csv.part").c_str(),"w");armies=std::fopen((prefix+"_armies.csv.part").c_str(),"w");factions=std::fopen((prefix+"_factions.csv.part").c_str(),"w");if(!ticks||!armies||!factions)throw std::runtime_error("cannot open trace outputs");
  std::fputs("tick,hash,rng,armies,objects,lifecycle0,lifecycle1,lifecycle2,lifecycle3,lifecycle4,lifecycle5,lifecycle6,lifecycle7\n",ticks);
  std::fputs("tick,slot,faction,arm,class,building,x_q12,y_q12,z_q12,hp,max_hp,move_flags,command_mode,command_generation,ai_state,target_slot,move_target_x_q12,move_target_y_q12,model_flags,production_arm,production_elapsed,production_duration,research_tec,research_elapsed,research_duration,route_x_q12,route_y_q12,production_active\n",armies);
  std::fputs("tick,faction,lifecycle,buildings,units,primary_queue,secondary_queue,xenite_q4,tritium_q4,xenite_capacity_q4,tritium_capacity_q4,energy_capacity_q4,energy_supplied_q4,energy_unpowered_q4,xenite_rate_q4,tritium_rate_q4,technology0,technology1,technology2,technology3,technology4,technology5,technology6,technology7,rng\n",factions);
 }
 static int slot(const ArmyRuntimeSlot *a){return a?int(a-g_ArmyRuntimeSlots):-1;}
 void word(uint32_t n){for(int b=0;b<4;++b){hash^=(n>>(8*b))&255;hash*=1099511628211ULL;}}
 void capture(bool detailed){
  hash=14695981039346656037ULL;const auto t=g_GameFactionRuntimeImage.tail.simulationTick;word(t);word(Random_GetSecondarySeed());unsigned objects=0,count=0;std::array<unsigned,8> buildings{},units{};
  for(WorldOwnerListNode *n=root.worldRuntime.ownerListHead;n;n=n->nextNode){word(uint32_t(n->ownerClassId));word(n->worldXQ12);word(n->worldYQ12);word(n->worldZQ12);++objects;}
  for(unsigned i=0;i<ARMY_RUNTIME_SLOT_COUNT;++i){auto &a=g_ArmyRuntimeSlots[i];if(a.modelNodeRuntime==nullptr)continue;++count;auto *m=a.modelRuntimeOrSavedOffset.modelRuntime.get();auto &pos=a.modelNodeRuntime->worldTransform.translation;const auto cl=m->definitionOrSavedId.runtimeDefinition->runtimeClassId;const bool building=a.depthBinClass==144;
   if(a.factionIndex>=0&&a.factionIndex<8)(building?buildings:units)[a.factionIndex]++;
   const bool producing=(cl==MODEL_RUNTIME_CLASS_11||cl==MODEL_RUNTIME_CLASS_13)&&m->classState.behaviorState==1;const unsigned production=producing?m->classLinkState.modelLinkOrState.classState:0;
   word(i);word(a.armyAssetId);word(a.factionIndex);word(pos.x);word(pos.y);word(pos.z);word(m->health);word(ToBits(a.movementStateFlags));word(ToBits(a.commandModeFlags));word(a.commandGeneration);word(a.aiUnitState);word(slot(a.commandTargetArmyRuntime));word(a.movementTarget0Q12);word(a.movementTarget1Q12);word(ToBits(m->classState.stateFlags));word(production);word(m->classLinkState.classState64);word(m->classLinkState.classState68);word(m->researchTechnologyId);word(m->researchElapsedTicks);word(m->researchDurationTicks);
   if(detailed||a.factionIndex>0)std::fprintf(armies,"%u,%u,%d,%d,%u,%u,%d,%d,%d,%u,%u,%u,%u,%u,%u,%d,%d,%d,%u,%u,%u,%u,%u,%d,%d,%d,%d,%u\n",t,i,a.factionIndex,a.armyAssetId,unsigned(cl),building,pos.x,pos.y,pos.z,m->health,m->definitionOrSavedId.runtimeDefinition->maximumHealth,ToBits(a.movementStateFlags),ToBits(a.commandModeFlags),a.commandGeneration,a.aiUnitState,slot(a.commandTargetArmyRuntime),a.movementTarget0Q12,a.movementTarget1Q12,ToBits(m->classState.stateFlags),production,m->classLinkState.classState64,m->classLinkState.classState68,m->researchTechnologyId,m->researchElapsedTicks,m->researchDurationTicks,a.movementPosition0Q12,a.movementPosition1Q12,producing);
  }
  for(unsigned f=0;f<8;++f){auto &r=g_GameFactionRuntimeImage.records[f];word(g_GameFactionRuntimeImage.tail.factionLifecycleStates[f]);word(r.primaryArmyAssetCount);word(r.secondaryArmyAssetCount);word(r.xeniteCurrentQ4);word(r.tritiumCurrentQ4);word(r.xeniteStorageLimitQ4);word(r.tritiumStorageLimitQ4);word(r.energyGenerationCapacityQ4);word(r.suppliedEnergyDemandQ4);word(r.unpoweredEnergyDemandQ4);word(r.xeniteExtractionRateQ4PerTick);word(r.tritiumExtractionRateQ4PerTick);for(auto v:r.technologyMasks256Bits)word(v);
   if(f>0&&f<=g_GameFactionRuntimeImage.tail.activeFactionCount){std::fprintf(factions,"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",t,f,g_GameFactionRuntimeImage.tail.factionLifecycleStates[f],buildings[f],units[f],r.primaryArmyAssetCount,r.secondaryArmyAssetCount,r.xeniteCurrentQ4,r.tritiumCurrentQ4,r.xeniteStorageLimitQ4,r.tritiumStorageLimitQ4,r.energyGenerationCapacityQ4,r.suppliedEnergyDemandQ4,r.unpoweredEnergyDemandQ4,r.xeniteExtractionRateQ4PerTick,r.tritiumExtractionRateQ4PerTick);for(auto v:r.technologyMasks256Bits)std::fprintf(factions,",%u",v);std::fprintf(factions,",%u\n",Random_GetSecondarySeed());}
  }
  std::fprintf(ticks,"%u,%016llx,%u,%u,%u",t,(unsigned long long)hash,Random_GetSecondarySeed(),count,objects);for(unsigned f=0;f<8;++f)std::fprintf(ticks,",%u",unsigned(g_GameFactionRuntimeImage.tail.factionLifecycleStates[f]));std::fputc(10,ticks);
  if(t&&t%1200==0)std::fprintf(stderr,"native progress tick=%u armies=%u objects=%u hash=%016llx\n",t,count,objects,(unsigned long long)hash);
 }
 void throttle(){double cpu=double(std::clock()-cpuStart)/CLOCKS_PER_SEC;auto target=start+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(cpu*2));if(target>std::chrono::steady_clock::now())std::this_thread::sleep_until(target);}
 void finish(const std::string &prefix){for(auto *f:{ticks,armies,factions})if(std::fflush(f)||std::ferror(f))throw std::runtime_error("trace write failure");for(auto *f:{ticks,armies,factions})if(std::fclose(f))throw std::runtime_error("trace close failure");for(auto s:{"_ticks.csv","_armies.csv","_factions.csv"})if(std::rename((prefix+s+".part").c_str(),(prefix+s).c_str()))throw std::runtime_error("trace publish failure");std::fprintf(stderr,"trace complete cpu_seconds=%.3f wall_seconds=%.3f\n",double(std::clock()-cpuStart)/CLOCKS_PER_SEC,std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count());}
};
