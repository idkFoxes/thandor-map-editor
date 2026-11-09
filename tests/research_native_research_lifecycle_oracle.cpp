#include THANDOR_NATIVE_TECHNOLOGY
#include THANDOR_NATIVE_RELATIONS
#include THANDOR_NATIVE_PURCHASING
#include THANDOR_NATIVE_WORKSPACES
#include THANDOR_NATIVE_ARMY_STOCK
#include <thandor/gameplay/army/model_views.h>
#include THANDOR_NATIVE_CALLBACK
#include "thandor/simulation_lab.hpp"
#include <random>
#include <iostream>
#include <bit>

static InGameRuntimeRoot root{};
InGameRuntimeRoot* g_InGameRuntimeRoot=&root;
UiCommandRuntimeFlagMask g_UiCommandRuntimeFlags{};
ArmyRuntimeOrderHandlerMatrix11x24 g_ArmyRuntimeOrderHandlerMatrix11Columns24Classes{};
decltype(g_InGameSimulationStepTicks) g_InGameSimulationStepTicks=1;
static struct {TechnologyAssetHeader header;TechnologyRecord records[256];} tech_image{};
static ModelRuntimeSlot models[140]{};
static ModelRuntimeNode nodes[140]{};
static ArmyRuntimeSlot armies[140]{};
static ModelDefinition definitions[140]{};
static AiStructureWorkspaceEntry structures[128]{};
static AiCandidateWorkspaceEntry candidates[128]{};
void Thandor_Log(const char*,...){}
void Thandor_Ptr32Overflow(uintptr_t){std::abort();}
void InGameBuildCatalog_RebuildGrid(UiNodeBase*){}
void InGameSpecialBuildCatalog_RebuildGrid(UiNodeBase*){}
void InGameOtherPlayerCommand_RebuildTargetEntries(UiNodeBase*){}
void InGameNotificationQueue_InsertPriorityRecord(InGameNotificationPayloadKind,uint32_t,uint32_t,AngleTurn32,Q12,Q12,InGameNotificationPriority,InGameNotificationMovieId){}
void ModelRuntimeHierarchy_ApplyFactionTechnologyVariants(FactionRuntimeIndex,ArmyRuntimeSlot*){}
void ArmyRuntime_ProcessReadyAttachmentChannels(WorldRuntimeContext*,ModelRuntimeSlot*){throw std::runtime_error("unexpected attachment callback");}
uint32_t ArmyRuntimeSpawner_ComputeRemainingLinkedAssetMetric(ArmyRuntimeLinkedChildMaskSlotView*){throw std::runtime_error("unexpected dismantle callback");}
EffectRuntimeSlot* EffectRuntimePool_CreateInstanceFromDefinition(EffectRuntimeCompletionAction,EffectRuntimeOwnerReference,AngleTurn32,AngleTurn32,AngleTurn32,Q12,Q12,Q12,EffectDefinition*,WorldRuntimeContext*){throw std::runtime_error("unexpected effect callback");}
uint32_t ArmyAssetRegistry_FindById(PckArmyAssetIdCatalog,ArmyAssetRecordPrefix**){return 1;}
void no_class_update(WorldRuntimeContext*,ModelRuntimeSlot*){}
void check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
uint32_t flags(const thandor::SimulationLabEntity& e){return e.runtime_flags | (e.model_components.empty()?0:e.model_components[e.root_model_component_index].runtime_flags_ec) | (e.research_active?0x40:0) | (e.research_pending_payment?0x80:0);}
void native_owner(uint32_t i,uint32_t faction,uint32_t raw) {
 models[i]={};nodes[i]={};armies[i]={};definitions[i]={};
 definitions[i].runtimeClassId=static_cast<ModelRuntimeClassId>(13);definitions[i].maximumHealth=1000;
 models[i].definitionOrSavedId.runtimeDefinition=&definitions[i];models[i].ownerArmyRuntimeOrSavedOffset.armyRuntime=&armies[i];models[i].rootModelNodeOrSavedOffset.modelNode=&nodes[i];
 models[i].health=1000;models[i].classState.stateFlags=static_cast<ArmyRuntimeFlags>(raw);
 armies[i].factionIndex=faction;
 auto* node=reinterpret_cast<WorldOwnerListNode*>(&nodes[i]);node->ownerClassId=WORLD_OWNER_RUNTIME_MODEL;node->runtimePayload=&models[i];node->nextNode=i?reinterpret_cast<WorldOwnerListNode*>(&nodes[i-1]):nullptr;
}
int main(int argc,char** argv)try {
 using namespace thandor;std::mt19937 rng(0x655);
 const auto field=thandor::FieldGrid::create(16,16,0,0);
 g_TechnologyAsset=reinterpret_cast<::TechnologyAsset*>(&tech_image);
 g_ArmyRuntimeOrderHandlerMatrix11Columns24Classes.runtimeUpdate[13]=no_class_update;
 g_AiWorkspace00Structures=structures;g_AiWorkspace13Candidates=candidates;
 std::array<SimulationLabTechnologyAsset,32> techs{};
 for(uint32_t i=0;i<32;++i){techs[i].technology_id=i;techs[i].dependency_technology_id=i/2;tech_image.records[i].dependencyTechnologyIndex=i/2;}
 SimulationLabSeedEntity seed;seed.army_asset_id=330;seed.faction_index=1;seed.building=true;seed.root_model_maximum_integrity=1000;seed.runtime_model_class_id=13;seed.research_technology_ids={7};
 SimulationLabModelComponentSeed component;component.runtime_class_id=13;component.maximum_integrity=1000;seed.model_components.push_back(component);
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
 SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=2;scenario.ai_planning_enabled=false;
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=200;config.parameter_index=100;
 uint32_t lifecycle_checks=0;
 constexpr uint32_t edge[]{0,1,2,3,4,5,6,0x7ffffffe,0x7fffffff,0x80000000,0xfffffffe,0xffffffff};
 for(uint32_t n=0;n<(argc>1?0U:6000U);++n) {
  const uint32_t faction=(n/1200)%5;
  config.runtime_callback_step_ticks=1+n%5;g_InGameSimulationStepTicks=config.runtime_callback_step_ticks;
  const uint32_t job[]{0,0x40,0x80,0xc0};const uint32_t raw=job[n%4] | ((n/4)%3==1?1U:0U) | ((n/12)%3==1?0x100U:0U);
  const uint32_t elapsed=n%2?uint32_t(rng()):edge[(n/2)%12];const uint32_t duration=edge[(n/24)%12];
  const uint32_t cost=n%3?uint32_t(rng()):10U,stock=n%3?uint32_t(rng()):20U,energy=rng();
  g_GameFactionRuntimeImage={};g_GameFactionRuntimeImage.records[faction].xeniteCurrentQ4=stock;g_GameFactionRuntimeImage.records[faction].technologyMasks256Bits[0]=1;
  native_owner(0,faction,raw);root.worldRuntime.ownerListHead=reinterpret_cast<WorldOwnerListNode*>(&nodes[0]);
  auto& model=models[0];model.researchTechnologyId=7;model.researchElapsedTicks=std::bit_cast<int32_t>(elapsed);model.researchDurationTicks=std::bit_cast<int32_t>(duration);model.researchXeniteCostQ4=std::bit_cast<int32_t>(cost);model.researchEnergyLoadQ4=std::bit_cast<int32_t>(energy);model.classState.energyLoadQ4=(raw&0x40)?energy:0;
  std::array<FactionTechnologyMask256,8> masks{};set_faction_technology_unlocked(masks[faction],0,true);
  std::array<SimulationLabFactionEconomySeed,8> economy{};economy[faction].xenite_current_q4=stock;
  auto lifecycle_seed=seed;lifecycle_seed.faction_index=faction;
  SimulationLabExperiment lab;lab.reset(config,ki,std::span(&lifecycle_seed,1),economy,{},techs,masks,{},scenario);auto& run=lab.runs()[0];auto& e=run.entities[0];
  e.runtime_flags=raw&~0xc0U;
  if(n%3==0)e.runtime_flags|=raw&0xc0U;
  if(n%3==1)e.model_components[0].runtime_flags_ec|=raw&0xc0U;
  if(n%3==2){e.research_active=(raw&0x40)!=0;e.research_pending_payment=(raw&0x80)!=0;}
  e.research_technology_id=7;e.research_elapsed_ticks=elapsed;e.research_duration_ticks=duration;e.research_xenite_due_q4=cost;e.research_energy_demand_q4=energy;
  run.faction_active_research_energy_q4[faction]=(raw&0x40)?energy:0;
  ArmyRuntimeHierarchy_UpdateProgressAndClassCallbacksRecursive(&root.worldRuntime,&model);lab.step(field,1);
  const bool same=(flags(e)&0xc0)==(uint32_t(model.classState.stateFlags)&0xc0) && e.research_active==bool(uint32_t(model.classState.stateFlags)&0x40) && e.research_pending_payment==bool(uint32_t(model.classState.stateFlags)&0x80) && e.research_elapsed_ticks==uint32_t(model.researchElapsedTicks) && e.research_xenite_due_q4==uint32_t(model.researchXeniteCostQ4) && run.faction_economy[faction].xenite_current_q4==g_GameFactionRuntimeImage.records[faction].xeniteCurrentQ4 && uint32_t(run.faction_active_research_energy_q4[faction])==uint32_t(model.classState.energyLoadQ4);
  if(!same){std::cerr<<"lifecycle case="<<n<<" flags="<<raw<<" elapsed="<<elapsed<<" duration="<<duration<<" native_flags="<<uint32_t(model.classState.stateFlags)<<" lab_flags="<<flags(e)<<" native_elapsed="<<model.researchElapsedTicks<<" lab_elapsed="<<e.research_elapsed_ticks<<'\n';return 1;}
  for(size_t w=0;w<8;++w)check(run.faction_technology_masks[faction][w]==g_GameFactionRuntimeImage.records[faction].technologyMasks256Bits[w],"unlock/dependency mask mismatch");
  ++lifecycle_checks;
 }
 std::cout<<"V655_NATIVE_RESEARCH_LIFECYCLE PASS callbacks="<<lifecycle_checks<<" batches=1..5; raw/boolean flags, signed overflow, payment, completion, energy and unlock recursion\n";
 uint32_t assignment_checks=0;
 techs[7].xenite_cost_q4=tech_image.records[7].xeniteCostQ4=10;
 techs[7].energy_demand_q4=tech_image.records[7].energyCostQ4=3;
 techs[7].research_duration_ticks=tech_image.records[7].researchDurationQ5=10000;
 for(uint32_t n=0;n<(argc>1?0U:1200U);++n) {
  config.runtime_callback_step_ticks=g_InGameSimulationStepTicks=1;
  config.fast_build_cheat=n&1;g_UiCommandRuntimeFlags=config.fast_build_cheat?UI_COMMAND_RUNTIME_FLAG_CHEAT_FAST_BUILD:UiCommandRuntimeFlagMask{};
  const uint32_t count=n%6==5?130:2,mode=(n/6)%10;
  std::vector<SimulationLabSeedEntity> seeds(count,seed);
  std::array<SimulationLabFactionEconomySeed,8> economy{};economy[1].xenite_current_q4=100;
  std::array<FactionTechnologyMask256,8> masks{};set_faction_technology_unlocked(masks[1],0,true);
  scenario.ai_planning_enabled=true;
  SimulationLabExperiment lab;lab.reset(config,ki,seeds,economy,{},techs,masks,{},scenario);auto& run=lab.runs()[0];run.tick=73;
  g_GameFactionRuntimeImage={};g_GameFactionRuntimeImage.records[1].xeniteCurrentQ4=100;g_GameFactionRuntimeImage.records[1].technologyMasks256Bits[0]=1;
  for(uint32_t i=0;i<count;++i) {
   uint32_t raw=0;
   if(i==count-1){constexpr uint32_t source_flags[]{0,0,0,1,8,0x80,0x40,0x40,0,0};raw=source_flags[mode];}
   if(i==0 && mode==8)raw=0x40;
   if(i==0 && mode==9)raw=0x80;
   native_owner(i,1,raw);definitions[i].researchTechnologyIds[1]=7;
   models[i].researchTechnologyId=mode==7 || mode==8?7:6;models[i].researchDurationTicks=10000;models[i].researchElapsedTicks=4;models[i].researchXeniteCostQ4=200;
   auto& e=run.entities[i];e.runtime_flags=raw;e.research_technology_id=models[i].researchTechnologyId;e.research_duration_ticks=10000;e.research_elapsed_ticks=4;e.research_xenite_due_q4=200;
   if(i==count-1 && mode==1)e.construction_complete=false;
   if(i==count-1 && mode==2)e.root_model_terminal=true;
  }
  root.worldRuntime.ownerListHead=reinterpret_cast<WorldOwnerListNode*>(&nodes[count-1]);
  for(uint32_t i=count;i-->0;)ArmyRuntimeHierarchy_UpdateProgressAndClassCallbacksRecursive(&root.worldRuntime,&models[i]);
  g_AiWorkspace00Count=std::min(count,128U);for(uint32_t i=0;i<g_AiWorkspace00Count;++i){structures[i].runtimeSlotAddressOrZero=&models[count-1-i];structures[i].armyAssetId=330;}
  g_AiCandidateWorkspaceEntryCount=1;candidates[0].entityIdAndMultiplicity=7;candidates[0].weightedScoreAndKind=0x3002;
  const bool no_applied=AiPurchasePlanner_ExecuteAffordableCandidates(1);
  run.ai_candidate_cache_reuse_state[1]=16;run.ai_candidate_cache_count[1]=1;run.ai_candidate_cache[1][0]={7,0x3002,2};
  lab.step(field,1);
  check((run.ai_candidate_cache_reuse_state[1]!=0)==no_applied,"research execution availability differs");
  for(uint32_t i=0;i<count;++i) {
   const auto& e=run.entities[i];const auto& m=models[i];
   if((flags(e)&0xc0)!=(uint32_t(m.classState.stateFlags)&0xc0) || e.research_technology_id!=m.researchTechnologyId || e.research_elapsed_ticks!=uint32_t(m.researchElapsedTicks) || e.research_duration_ticks!=uint32_t(m.researchDurationTicks) || e.research_xenite_due_q4!=uint32_t(m.researchXeniteCostQ4)) {
    std::cerr<<"assignment case="<<n<<" mode="<<mode<<" owner="<<i<<" native_flags="<<uint32_t(m.classState.stateFlags)<<" lab_flags="<<flags(e)<<" native_id="<<m.researchTechnologyId<<" lab_id="<<e.research_technology_id<<'\n';return 1;
   }
  }
  ++assignment_checks;
 }
 std::cout<<"V655_NATIVE_RESEARCH_ASSIGNMENT PASS passes="<<assignment_checks<<"; original availability/execute/assignment, live-root gates, raw jobs, workspace cap and fast-build duration\n";
 scenario.ai_planning_enabled=false;g_UiCommandRuntimeFlags={};
 uint32_t exchange_checks=0;
 for(uint32_t n=0;n<1200;++n) {
  g_GameFactionRuntimeImage={};root.worldRuntime.ownerListHead=nullptr;
  std::array<FactionTechnologyMask256,8> masks{};
  for(uint32_t f=0;f<8;++f)for(uint32_t w=0;w<8;++w){masks[f][w]=w?uint32_t(rng()):(rng()|1U);g_GameFactionRuntimeImage.records[f].technologyMasks256Bits[w]=masks[f][w];}
  SimulationLabExperiment lab;lab.reset(config,ki,{}, {},{},techs,masks,{},scenario);auto& run=lab.runs()[0];run.tick=7;
  for(uint32_t f=0;f<8;++f){const uint32_t row=rng();run.faction_packed_relation_states[f]=row;g_GameFactionRuntimeImage.records[f].packedRelationStates=row;}
  GameFactionRuntime_SynchronizeTechnologiesForRelationStates8To10();lab.step(field,1);
  for(uint32_t f=0;f<8;++f)for(uint32_t w=0;w<8;++w)if(run.faction_technology_masks[f][w]!=g_GameFactionRuntimeImage.records[f].technologyMasks256Bits[w]){std::cerr<<"exchange case="<<n<<" faction="<<f<<" word="<<w<<'\n';return 1;}
  ++exchange_checks;
 }
 std::cout<<"V655_NATIVE_RESEARCH_EXCHANGE PASS passes="<<exchange_checks<<"; ordered one-for-one exchange with dependency recursion including inactive factions\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
