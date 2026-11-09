#include THANDOR_NATIVE_RELATIONS
#include THANDOR_NATIVE_FACTION_RUNTIME
#include THANDOR_NATIVE_LEVEL_SCRIPT
#include "thandor/retail_relation_prediction.hpp"
#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include <random>
#include <iostream>
#include <cstring>
GameFactionRuntimeImage g_GameFactionRuntimeImage{};
InGameLevelRuntimeGlobalBlock20 g_InGameLevelRuntimeGlobalBlock{};
static InGameRuntimeRoot root{};
InGameRuntimeRoot* g_InGameRuntimeRoot=&root;
ArmyRuntimeOrderHandlerMatrix11x24 g_ArmyRuntimeOrderHandlerMatrix11Columns24Classes{};
decltype(g_InGameSimulationStepTicks) g_InGameSimulationStepTicks=1;
void ArmyRuntime_ClassCommandHandlerGroupA(WorldRuntimeContext*,ModelRuntimeSlot*){}
void Thandor_Log(const char*,...){}
void Thandor_Ptr32Overflow(uintptr_t){std::abort();}
static InGameLevelConditionStorage storage{};
alignas(FieldGridAsset) static unsigned char field_image[sizeof(FieldGridAsset)+63*sizeof(FieldGridCell)]{};
static auto& native_field=*reinterpret_cast<FieldGridAsset*>(field_image);
static auto* cells=native_field.cells;
static ModelRuntimeSlot models[6]{};
static ModelRuntimeNode nodes[6]{};
static ArmyRuntimeSlot armies[6]{};
static ModelDefinition definitions[6]{};
void check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
void copy_conditions(const std::array<thandor::LevelScheduledCondition,64>& source) {
 static_assert(sizeof(source[0])==sizeof(storage.schedule.conditions[0]));
 std::memcpy(storage.schedule.conditions,source.data(),sizeof(storage.schedule.conditions));
}
int main()try {
 using namespace thandor;std::mt19937 rng(0x654);g_InGameLevelRuntimeGlobalBlock.conditionStorage=&storage;
 for(uint32_t cls=0;cls<24;++cls)if(simulation_lab_runtime_class_uses_group_a_command_handler(cls))
  g_ArmyRuntimeOrderHandlerMatrix11Columns24Classes.classCommand[cls]=ArmyRuntime_ClassCommandHandlerGroupA;
 uint32_t predictions=0,blocked=0;
 for(uint32_t n=0;n<10000;++n) {
  std::array<uint8_t,8> life{};std::array<uint32_t,8> rows{};
  for(uint32_t f=0;f<8;++f){life[f]=rng()%4;rows[f]=rng();g_GameFactionRuntimeImage.tail.factionLifecycleStates[f]=static_cast<FactionRuntimeLifecycleObservedState>(life[f]);g_GameFactionRuntimeImage.records[f].packedRelationStates=rows[f];}
  std::array<LevelScheduledCondition,64> conditions{};std::array<LevelEndConditionTrigger,16> triggers{};
  for(auto& c:conditions) {
   c.status_and_kind=uint8_t(2*(rng()%14)+(rng()&1));c.set_operand(0,rng()%8);c.set_operand(1,rng()%3);c.set_operand(2,rng());
   if(c.kind()==LevelScheduledConditionKind::BooleanPostfixExpression){c.payload_bytes.fill(0xFC);for(size_t i=0;i<10;++i)c.payload_bytes[i]=(rng()%3)?uint8_t(rng()%64):uint8_t(0xFD+rng()%3);}
  }
  for(auto& t:triggers){t.state_flags=rng()%4;t.faction_runtime_index=rng()%8;t.condition_index=rng()%64;t.movie_variant_selector=rng()%4;}
  std::memcpy(storage.schedule.triggers,triggers.data(),sizeof(storage.schedule.triggers));
  const uint32_t flags=rng()%8,other=1+rng()%7,faction=1+rng()%7;
  g_GameFactionRuntimeImage.tail.relationUiFlags=static_cast<GameRelationUiFlags>(flags);
  copy_conditions(conditions);const bool native=GameFactionRelations_TestPairTransitionAllowed(other,faction);
  const auto actual=retail_relation_pair_blocked(other,faction,flags,life,rows,conditions,triggers);
  check(native==actual,"native relation pair guard differs");
  check(std::memcmp(storage.schedule.conditions,conditions.data(),sizeof(storage.schedule.conditions))==0,"native prediction schedule side effects differ");
  ++predictions;blocked+=native;
 }
 std::cout<<"V654_NATIVE_RELATION_GUARD PASS cases="<<predictions<<" blocked="<<blocked<<"; original guard/bloc/prediction and packed-state reader; no drift/merge execution\n";
 const auto field=thandor::FieldGrid::create(8,8,0,0);native_field.gridWidth=native_field.gridHeight=8;root.worldRuntime.fieldGrid=&native_field;
 uint32_t evaluated=0;
 for(uint32_t n=0;n<600;++n) {
  SimulationLabConfig config;config.run_count=1;config.duration_ticks=40;config.parameter_index=100;config.runtime_callback_step_ticks=1+rng()%5;
  std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};std::vector<SimulationLabSeedEntity> seeds;
  const uint32_t count=1+rng()%6;
  for(uint32_t i=0;i<count;++i) {
   SimulationLabSeedEntity s;s.army_asset_id=300+i%2;s.faction_index=1+rng()%3;s.runtime_model_class_id=rng()%24;
   s.actor.position={0,0,0};s.root_model_maximum_integrity=1000;
   SimulationLabModelComponentSeed c;c.model_definition_id=i+1;c.runtime_class_id=s.runtime_model_class_id;c.maximum_integrity=1000;s.model_components.push_back(c);
   c.model_definition_id=100+i;c.runtime_class_id=11;c.parent_component_index=0;s.model_components.push_back(c);seeds.push_back(s);
  }
  SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=3;scenario.ai_planning_enabled=false;
  for(uint32_t i=0;i<64;++i) {
   auto& c=scenario.scheduled_conditions[i];c.status_and_kind=uint8_t(2*(rng()%14));c.set_operand(0,1+rng()%3);c.set_operand(1,rng());c.set_operand(2,300+rng()%3);
   if(c.kind()==LevelScheduledConditionKind::NoActiveEntityWithClassIdOutsideClassCommandGroupA)c.set_operand(0,rng()%24);
   if(c.kind()==LevelScheduledConditionKind::FactionInactiveOrRelationAtLeast8)c.set_operand(1,rng()%8);
   if(c.kind()==LevelScheduledConditionKind::MatchingDefinitionAndRuntimeIdActiveEntityCountAtLeast)c.set_operand(1,rng()%8);
   if(c.kind()==LevelScheduledConditionKind::CountdownElapsed && n%2)c.set_operand(1,rng()%9);
   if(c.kind()==LevelScheduledConditionKind::FactionTerrainOccupancyPercentAtLeast && n%2)c.set_operand(1,rng()%101);
   if(c.kind()==LevelScheduledConditionKind::BooleanPostfixExpression){c.payload_bytes.fill(0xFC);for(size_t j=0;j<10;++j)c.payload_bytes[j]=(rng()%3)?uint8_t(rng()%64):uint8_t(0xFD+rng()%3);}
  }
  SimulationLabExperiment lab;lab.reset(config,ki,seeds,{},{},{},{},{},scenario);auto& run=lab.runs()[0];run.tick=19;
  auto scratch=std::make_shared<SimulationLabRetailScratchImage>();scratch->width=scratch->height=32;scratch->state.resize(1024);scratch->field_occupancy_masks.resize(64);
  for(size_t i=0;i<64;++i){scratch->field_occupancy_masks[i]=(uint64_t(rng())<<32)|rng();cells[i].occupancyMask=scratch->field_occupancy_masks[i];}run.retail_scratch=scratch;
  g_GameFactionRuntimeImage={};
  for(uint32_t f=0;f<8;++f) {
   g_GameFactionRuntimeImage.tail.factionLifecycleStates[f]=static_cast<FactionRuntimeLifecycleObservedState>(run.faction_lifecycle_states[f]);
   auto& e=run.faction_economy[f];auto& native=g_GameFactionRuntimeImage.records[f];
   e.xenite_current_q4=native.xeniteCurrentQ4=rng();e.tritium_current_q4=native.tritiumCurrentQ4=rng();
   e.xenite_storage_limit_q4=native.xeniteStorageLimitQ4=rng();run.faction_tritium_extraction_rate_q4_per_tick[f]=native.tritiumExtractionRateQ4PerTick=rng();
   native.packedRelationStates=run.faction_packed_relation_states[f];
  }
  for(uint32_t i=0;i<count;++i) {
   models[i]={};nodes[i]={};armies[i]={};definitions[i]={};
   definitions[i].runtimeClassId=static_cast<ModelRuntimeClassId>(seeds[i].runtime_model_class_id);
   models[i].definitionOrSavedId.runtimeDefinition=&definitions[i];models[i].ownerArmyRuntimeOrSavedOffset.armyRuntime=&armies[i];
   armies[i].factionIndex=seeds[i].faction_index;armies[i].armyAssetId=seeds[i].army_asset_id;
   auto* node=reinterpret_cast<WorldOwnerListNode*>(&nodes[i]);node->ownerClassId=WORLD_OWNER_RUNTIME_MODEL;node->runtimePayload=&models[i];node->nextNode=i?reinterpret_cast<WorldOwnerListNode*>(&nodes[i-1]):nullptr;
  }
  root.worldRuntime.ownerListHead=reinterpret_cast<WorldOwnerListNode*>(&nodes[count-1]);
  copy_conditions(run.scheduled_conditions);g_InGameSimulationStepTicks=config.runtime_callback_step_ticks;
  for(auto& c:storage.schedule.conditions) {
   c.statusAndKind.raw&=~1U;
   if(InGameScheduledCondition_Holds(&storage,&c,static_cast<InGameScheduledConditionKind>(c.statusAndKind.raw&0xFEU)))c.statusAndKind.raw|=1U;
  }
  lab.step(field,1);
  for(size_t i=0;i<64;++i) {
   if(std::memcmp(&storage.schedule.conditions[i],&run.scheduled_conditions[i],16)!=0) {
    std::cerr<<"case="<<n<<" slot="<<i<<" kind="<<uint32_t(run.scheduled_conditions[i].status_and_kind&0xFE)<<" native_status="<<storage.schedule.conditions[i].statusAndKind.raw<<" lab_status="<<uint32_t(run.scheduled_conditions[i].status_and_kind)<<'\n';
    throw std::runtime_error("native scheduled condition/status/countdown differs");
   }
   ++evaluated;
  }
 }
 std::cout<<"V654_NATIVE_LEVEL_CONDITIONS PASS passes=600 records="<<evaluated<<"; original per-condition function and serialized order; controlled root owner list/class dispatch, real Lab tick20 and supplied persistent occupancy\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
