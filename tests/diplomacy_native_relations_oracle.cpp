#include THANDOR_NATIVE_RELATIONS
#include THANDOR_NATIVE_FACTION_RUNTIME
#include THANDOR_NATIVE_RANDOM
#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include <random>
#include <iostream>
#include <cstring>
#include <bit>
GameFactionRuntimeImage g_GameFactionRuntimeImage{};
GameDataAuxState g_GameDataAuxState{};
InGameLevelRuntimeGlobalBlock20 g_InGameLevelRuntimeGlobalBlock{};
static InGameRuntimeRoot root{};
InGameRuntimeRoot* g_InGameRuntimeRoot=&root;
static FrontendPlayerRuntimeRecord players[8]{};
FrontendPlayerRuntimeRecord* g_FrontendPlayerRuntimeBlocks=players;
FrontendPlayerRuntimeBlockCount g_FrontendPlayerRuntimeBlockCount{};
static SelectionPlayerRuntimeBlock selections[8]{};
SelectionPlayerRuntimeBlock* g_SelectionPlayerRuntimeBlockPointers[256]{};
ArmyGraphicsBinding g_ArmyGraphicsBindings[8]{};
void Thandor_Log(const char*,...){}
void Thandor_Ptr32Overflow(uintptr_t){std::abort();}
void InGameNotificationQueue_InsertPriorityRecord(InGameNotificationPayloadKind,uint32_t,uint32_t,AngleTurn32,Q12,Q12,InGameNotificationPriority,InGameNotificationMovieId){}
void InGameBuildCatalog_RebuildGrid(UiNodeBase*){}
void InGameSpecialBuildCatalog_RebuildGrid(UiNodeBase*){}
void InGameArmyStock_RebuildGrid(UiNodeBase*){}
void InGameOtherPlayerCommand_RebuildTargetEntries(UiNodeBase*){}
void ModelRuntimeHierarchy_SetPaletteAndTextureSetNonNullRecursive(GraphicsPaletteAsset*,GraphicsTextureSet*,ModelRuntimeNode*){}
static InGameLevelConditionStorage storage{};
alignas(FieldGridAsset) static unsigned char field_image[sizeof(FieldGridAsset)+63*sizeof(FieldGridCell)]{};
static auto& field=*reinterpret_cast<FieldGridAsset*>(field_image);
static ModelRuntimeSlot models[6]{};
static ModelRuntimeNode nodes[6]{};
static ArmyRuntimeSlot armies[6]{};
void check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
int main()try {
 using namespace thandor;std::mt19937 rng(0x656);g_InGameLevelRuntimeGlobalBlock.conditionStorage=&storage;
 field.gridWidth=field.gridHeight=8;root.worldRuntime.fieldGrid=&field;
 uint32_t passes=0,merges=0,transitions=0;
 for(uint32_t n=0;n<24000;++n) {
  SimulationLabRun r;r.tick=rng();r.rng_state=rng();r.active_view_faction_index=rng()%8;
  g_GameFactionRuntimeImage={};g_GameDataAuxState={};g_GameFactionRuntimeImage.tail.simulationTick=r.tick;root.worldRuntime.activeFactionRuntimeIndex=r.active_view_faction_index;g_RandomGeneratorState.primarySeed=r.rng_state;
  r.relation_ui_flags=rng()%8;g_GameFactionRuntimeImage.tail.relationUiFlags=static_cast<GameRelationUiFlags>(r.relation_ui_flags);
  g_FrontendPlayerRuntimeBlockCount=rng()%9;players[0]={};
  for(uint32_t i=0;i<g_FrontendPlayerRuntimeBlockCount;++i){players[i]={};players[i].playerRuntimeId=i;players[i].factionAssignment.factionAssignmentIndex=1+rng()%7;++r.faction_frontend_player_counts[players[i].factionAssignment.factionAssignmentIndex];g_SelectionPlayerRuntimeBlockPointers[i]=&selections[i];selections[i].factionIndex=players[i].factionAssignment.factionAssignmentIndex;}
  for(uint32_t f=0;f<8;++f) {
   auto& a=g_GameFactionRuntimeImage.records[f];auto& e=r.faction_economy[f];auto& score=r.faction_score[f];
   r.faction_lifecycle_states[f]=n%2?1:rng()%4;g_GameFactionRuntimeImage.tail.factionLifecycleStates[f]=static_cast<FactionRuntimeLifecycleObservedState>(r.faction_lifecycle_states[f]);
   r.faction_packed_relation_states[f]=a.packedRelationStates=rng();r.faction_capability_flags[f]=a.capabilityFlags=rng();
   for(uint32_t other=0;other<8;++other){r.faction_pair_change_ticks[f][other]=a.relationStateTicks[other]=rng();r.faction_pair_pressure[f][other]=g_GameDataAuxState.pairPressureMatrix8x8[f*8+other]=n%2?0:rng()%40;}
   e.xenite_current_q4=a.xeniteCurrentQ4=rng();e.tritium_current_q4=a.tritiumCurrentQ4=rng();e.xenite_storage_limit_q4=a.xeniteStorageLimitQ4=rng();e.tritium_storage_limit_q4=a.tritiumStorageLimitQ4=rng();e.baseline_energy_supply_q4=a.baselineEnergySupplyQ4=rng();r.faction_energy_generation_capacity_q4[f]=a.energyGenerationCapacityQ4=rng();
   score.xenite_extracted_total_q4=a.xeniteExtractedTotalQ4=rng();score.tritium_extracted_total_q4=a.tritiumExtractedTotalQ4=rng();
   score.relation_counter_a=a.relationCounterA=std::bit_cast<int32_t>(uint32_t(rng()));score.relation_counter_b=a.relationCounterB=std::bit_cast<int32_t>(uint32_t(rng()));score.relation_counter_c=a.relationCounterC=std::bit_cast<int32_t>(uint32_t(rng()));score.relation_counter_d=a.relationCounterD=std::bit_cast<int32_t>(uint32_t(rng()));score.relation_counter_e=a.relationCounterE=std::bit_cast<int32_t>(uint32_t(rng()));score.relation_counter_f=a.relationCounterF=std::bit_cast<int32_t>(uint32_t(rng()));
   for(uint32_t w=0;w<8;++w)r.faction_technology_masks[f][w]=a.technologyMasks256Bits[w]=rng();
   a.primaryArmyAssetCount=rng()%65;a.secondaryArmyAssetCount=rng()%65;
   for(uint32_t i=0;i<a.primaryArmyAssetCount;++i){a.primaryArmyAssetPointersOrIds[i]=300+rng()%32;r.pending_asset_requests.push_back({a.primaryArmyAssetPointersOrIds[i],f,0,0});}
   for(uint32_t i=0;i<a.secondaryArmyAssetCount;++i){a.secondaryArmyAssetPointersOrIds[i]=rng()%256;r.secondary_purchase_requests.push_back({a.secondaryArmyAssetPointersOrIds[i],f,0,0});}
  }
  for(auto& c:r.scheduled_conditions){c.status_and_kind=uint8_t(2*(rng()%14)+(rng()&1));c.set_operand(0,rng()%8);c.set_operand(1,rng()%3);if(c.kind()==LevelScheduledConditionKind::BooleanPostfixExpression){c.payload_bytes.fill(0xFC);for(size_t i=0;i<10;++i)c.payload_bytes[i]=rng()%64;}}
  for(auto& t:r.end_condition_triggers){t.state_flags=rng()%4;t.faction_runtime_index=rng()%8;t.condition_index=rng()%64;t.movie_variant_selector=rng()%4;}
  std::memcpy(storage.schedule.conditions,r.scheduled_conditions.data(),sizeof(storage.schedule.conditions));std::memcpy(storage.schedule.triggers,r.end_condition_triggers.data(),sizeof(storage.schedule.triggers));
  auto scratch=std::make_shared<SimulationLabRetailScratchImage>();scratch->field_occupancy_masks.resize(64);for(uint32_t i=0;i<64;++i)field.cells[i].occupancyMask=scratch->field_occupancy_masks[i]=(uint64_t(rng())<<32)|rng();r.retail_scratch=scratch;
  for(uint32_t i=0;i<6;++i){SimulationLabEntity e;e.faction_index=1+rng()%7;r.entities.push_back(e);models[i]={};nodes[i]={};armies[i]={};models[i].ownerArmyRuntimeOrSavedOffset.armyRuntime=&armies[i];armies[i].factionIndex=e.faction_index;armies[i].modelNodeRuntime=&nodes[i];auto* node=reinterpret_cast<WorldOwnerListNode*>(&nodes[i]);node->ownerClassId=WORLD_OWNER_RUNTIME_MODEL;node->runtimePayload=&models[i];node->nextNode=i?reinterpret_cast<WorldOwnerListNode*>(&nodes[i-1]):nullptr;}
  root.worldRuntime.ownerListHead=reinterpret_cast<WorldOwnerListNode*>(&nodes[5]);
  if(n<20000) {const uint32_t f=1+rng()%7;GameFactionRelations_UpdateAllPairsForFaction(f,&root.worldRuntime);simulation_lab_update_faction_relations(r,f);}
  else {const uint32_t first=1+rng()%7,second=1+(first+rng()%6)%7,a=rng()%16,b=n%3?uint32_t(rng()%16):11U;GameFactionRuntime_ApplyPairwiseRelationTransition(0,0,a,b,first,second);simulation_lab_apply_relation_transition(r,first,second,a,b);}
  check(r.rng_state==g_RandomGeneratorState.primarySeed,"relation RNG differs");check(r.active_view_faction_index==root.worldRuntime.activeFactionRuntimeIndex,"merge active view differs");
  check(std::memcmp(storage.schedule.conditions,r.scheduled_conditions.data(),sizeof(storage.schedule.conditions))==0,"prediction side effects differ");
  std::array<uint32_t,8> counts{};for(uint32_t i=0;i<g_FrontendPlayerRuntimeBlockCount;++i){++counts[players[i].factionAssignment.factionAssignmentIndex];check(selections[i].factionIndex==players[i].factionAssignment.factionAssignmentIndex,"native selection reownership missing");}check(counts==r.faction_frontend_player_counts,"merged player counts differ");
  for(uint32_t f=0;f<8;++f) {
   const auto& a=g_GameFactionRuntimeImage.records[f];const auto& e=r.faction_economy[f];const auto& score=r.faction_score[f];
   check(r.faction_packed_relation_states[f]==a.packedRelationStates && r.faction_capability_flags[f]==a.capabilityFlags && r.faction_lifecycle_states[f]==uint32_t(g_GameFactionRuntimeImage.tail.factionLifecycleStates[f]),"directed relation/capability/lifecycle differs");
   for(uint32_t other=0;other<8;++other)check(r.faction_pair_change_ticks[f][other]==a.relationStateTicks[other],"pair timestamp differs");
   check(e.xenite_current_q4==a.xeniteCurrentQ4 && e.tritium_current_q4==a.tritiumCurrentQ4 && e.xenite_storage_limit_q4==a.xeniteStorageLimitQ4 && e.tritium_storage_limit_q4==a.tritiumStorageLimitQ4 && e.baseline_energy_supply_q4==a.baselineEnergySupplyQ4 && r.faction_energy_generation_capacity_q4[f]==a.energyGenerationCapacityQ4,"merged resource/energy differs");
   check(score.xenite_extracted_total_q4==a.xeniteExtractedTotalQ4 && score.tritium_extracted_total_q4==a.tritiumExtractedTotalQ4 && score.relation_counter_a==a.relationCounterA && score.relation_counter_b==a.relationCounterB && score.relation_counter_c==a.relationCounterC && score.relation_counter_d==a.relationCounterD && score.relation_counter_e==a.relationCounterE && score.relation_counter_f==a.relationCounterF,"merged score differs");
   for(uint32_t w=0;w<8;++w)check(r.faction_technology_masks[f][w]==a.technologyMasks256Bits[w],"merged masks differ");
   std::vector<uint32_t> primary,secondary;for(const auto& q:r.pending_asset_requests)if(q.faction_index==f)primary.push_back(q.army_asset_id);for(const auto& q:r.secondary_purchase_requests)if(q.faction_index==f)secondary.push_back(q.army_asset_id);
   check(primary.size()==a.primaryArmyAssetCount && secondary.size()==a.secondaryArmyAssetCount,"merged queue counts differ");for(size_t i=0;i<primary.size();++i)check(primary[i]==a.primaryArmyAssetPointersOrIds[i],"primary queue order differs");for(size_t i=0;i<secondary.size();++i)check(secondary[i]==a.secondaryArmyAssetPointersOrIds[i],"secondary queue order differs");
  }
  for(uint32_t i=0;i<6;++i)check(r.entities[i].faction_index==armies[i].factionIndex,"merged model ownership differs");for(uint32_t i=0;i<64;++i)check(r.retail_scratch->field_occupancy_masks[i]==field.cells[i].occupancyMask,"merged occupancy differs");
  merges+=r.relation_trace.merges;transitions+=r.relation_trace.transitions;++passes;
 }
 std::cout<<"V656_NATIVE_RELATIONS PASS passes="<<passes<<" full_dispatch=20000 direct_transitions=4000 transitions="<<transitions<<" merges="<<merges<<"; original RNG/guards/drift/advance/reset/apply/merge, controlled world/frontend and UI/palette no-ops\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
