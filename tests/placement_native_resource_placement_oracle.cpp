#include THANDOR_NATIVE_PURCHASING
#include THANDOR_NATIVE_WORKSPACES
#include THANDOR_NATIVE_RANDOM
#include THANDOR_NATIVE_ARMY_PLACEMENT
#include THANDOR_NATIVE_VECTOR
#include "thandor/retail_power_candidate.hpp"
#include "thandor/retail_random.hpp"
#include "thandor/simulation_lab_site_placement.hpp"
#include <iostream>
#include <random>
GameFactionRuntimeImage g_GameFactionRuntimeImage{};
AiKnowledgeDataImage* g_AiKnowledgeData{};
UiCommandRuntimeFlagMask g_UiCommandRuntimeFlags{};
void Thandor_Ptr32Overflow(uintptr_t){std::abort();}
void Thandor_Log(const char*,...){}
static bool clear_terrain(FieldGridRadiusUnits,Q12,Q12,Q12,FieldGridAsset*){return false;}
const TerrainClassPlacementAndOverlayCallbackTable10 g_TerrainClassPlacementAndOverlayCallbacks10={
 {THANDOR_SLOT(clear_terrain),THANDOR_SLOT(clear_terrain),THANDOR_SLOT(clear_terrain),THANDOR_SLOT(clear_terrain),THANDOR_SLOT(clear_terrain)}, {}};
bool ArmyPlacementCollision_TestPointAgainstRuntimeList(ArmyPlacementCollisionFilterFlags,Q12,Q12,Q12,WorldRuntimeContext*){return false;}
bool FieldGrid_TestWorldPointBlocked(FactionRuntimeIndex,Q12,Q12,FieldGridAsset*){return false;}
static AiKnowledgeDataImage knowledge{};
static AiStructureWorkspaceEntry structures[128]{};
static AiCandidateWorkspaceEntry candidates[128]{};
static ModelRuntimeSlot models[5]{};
static ModelRuntimeNode nodes[5]{};
static ModelDefinition definitions[5]{};
static ArmyRuntimeSlot armies[5]{};
void check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
int main()try {
 using namespace thandor;std::mt19937 random(0x653);
 g_AiKnowledgeData=&knowledge;g_AiWorkspace00Structures=structures;g_AiWorkspace13Candidates=candidates;
 uint32_t selected=0,power_cases=0;
 for(uint32_t n=0;n<10000;++n) {
  const bool has330=(random()%5)!=0,unassigned=(random()%5)==0;
  g_AiWorkspace00Count=0;
  if(has330)structures[g_AiWorkspace00Count++]={nullptr,330};
  if(unassigned)structures[g_AiWorkspace00Count++]={nullptr,310};
  auto& f=g_GameFactionRuntimeImage.records[1];f={};
  f.energyGenerationCapacityQ4=uint32_t(random())|16U;
  f.baselineEnergySupplyQ4=random();f.tritiumExtractionRateQ4PerTick=random();
  f.suppliedEnergyDemandQ4=random();f.unpoweredEnergyDemandQ4=random();
  knowledge.parameters.resource136DeficitScoreNumerator=n%11?uint32_t(random()):0;
  knowledge.parameters.resource136DeficitScoreDenominator=uint32_t(random())|1U;
  const uint32_t seed=random();Random_SetBothSeeds(seed);Random_SelectPrimaryStream();g_AiCandidateWorkspaceEntryCount=0;
  AiResourceCandidate_AddPowerPlant(1);
  std::optional<uint32_t> weight;
  if(has330&&!unassigned)weight=retail_power_candidate_weight(f.energyGenerationCapacityQ4,f.baselineEnergySupplyQ4,
   f.tritiumExtractionRateQ4PerTick,f.suppliedEnergyDemandQ4,f.unpoweredEnergyDemandQ4,
   knowledge.parameters.resource136DeficitScoreNumerator,knowledge.parameters.resource136DeficitScoreDenominator);
  uint32_t state=seed,score=0;bool admitted=false;
  if(weight){const auto value=retail_random_next(state);if(*weight>1){score=((*weight*5U+value%*weight)<<4U)|1U;admitted=true;}}
  if(g_AiCandidateWorkspaceEntryCount!=uint32_t(admitted)||g_RandomGeneratorState.primarySeed!=state ||
     (admitted&&(candidates[0].weightedScoreAndKind!=score||(candidates[0].entityIdAndMultiplicity&65535)!=310))) {
   std::cerr<<"power case="<<n<<" native_entries="<<g_AiCandidateWorkspaceEntryCount<<" helper_weight="<<weight.value_or(0)<<'\n';
   throw std::runtime_error("original power candidate/RNG mismatch");
  }
  selected+=admitted;++power_cases;
 }
 auto field=thandor::FieldGrid::create(40,40,0,0);const auto& cell=field.at(20,20);
 const MovementPoint point{cell.world_x_q12,cell.world_y_q12};
 SimulationLabRun run;run.level_runtime_enabled=true;
 auto scratch=std::make_shared<SimulationLabRetailScratchImage>();scratch->field_occupancy_masks.assign(field.cells().size(),uint64_t{1}<<8);run.retail_scratch=scratch;
 WorldRuntimeContext world{};ModelDefinition candidate{};candidate.runtimeClassId=static_cast<ModelRuntimeClassId>(11);
 uint32_t support_cases=0,accepted=0;
 for(uint32_t n=0;n<10000;++n) {
  run.entities.clear();SimulationLabRuntimeCatalog catalog;
  const uint32_t count=1+random()%5;
  for(uint32_t i=0;i<count;++i) {
   models[i]={};nodes[i]={};definitions[i]={};armies[i]={};
   definitions[i].runtimeClassId=static_cast<ModelRuntimeClassId>((random()&1)?11:18);
   definitions[i].supportRadius=random()%50000;definitions[i].definitionId=i+1;
   models[i].definitionOrSavedId.runtimeDefinition=&definitions[i];models[i].rootModelNodeOrSavedOffset.modelNode=&nodes[i];
   models[i].ownerArmyRuntimeOrSavedOffset.armyRuntime=&armies[i];armies[i].factionIndex=(random()%4)?1:2;
   models[i].classState.stateFlags=static_cast<ArmyRuntimeFlags>((random()%4)?0:0x18);
   const int32_t x=point.x+int32_t(random()%100001)-50000,y=point.y+int32_t(random()%100001)-50000;
   nodes[i].worldTransform.translation={x,y,0};
   auto* owner=reinterpret_cast<WorldOwnerListNode*>(&nodes[i]);owner->ownerClassId=WORLD_OWNER_RUNTIME_MODEL;
   owner->runtimePayload=&models[i];owner->nextNode=i?reinterpret_cast<WorldOwnerListNode*>(&nodes[i-1]):nullptr;
   SimulationLabEntity entity;entity.army_asset_id=300+i;entity.faction_index=armies[i].factionIndex;
   entity.actor.position={q12_to_float(x),q12_to_float(y),0};entity.runtime_flags=uint32_t(models[i].classState.stateFlags);
   entity.root_model_component_index=0;SimulationLabModelComponentState component;component.model_definition_id=i+1;
   entity.model_components.push_back(component);
   // A linked child with an enormous footprint/support radius is not a world owner.
   component.model_definition_id=100;component.parent_component_index=0;
   component.extraction_model_world_offset_at_seed=Vec3f{};entity.model_components.push_back(component);run.entities.push_back(entity);
   auto& record=catalog.definitions[i+1];record.runtime_class_id=uint32_t(definitions[i].runtimeClassId);
   record.simulation.class_specific_19c_raw=definitions[i].supportRadius;
  }
  auto& child=catalog.definitions[100];child.runtime_class_id=11;child.simulation.movement_collision_radius_q12=1000000;child.simulation.class_specific_19c_raw=1000000;
  world.ownerListHead=reinterpret_cast<WorldOwnerListNode*>(&nodes[count-1]);
  const uint32_t mode=random()%8;const int32_t padding=int32_t(random()%80001)-40000,margin=int32_t(random()%8001)-4000;
  candidate.placementFlags=margin;uint32_t native_value=0;
  const bool native=ArmyPlacement_CanPlaceBuilding(mode,padding,0,0,point.y,point.x,&candidate,1,&world,&native_value);
  const auto owners=simulation_lab_site_owners(run,field,catalog);check(owners.size()==count,"attached child is still a world owner");
  const auto actual=simulation_lab_query_site_placement(run,field,owners,{0,11,0,margin,0,0,0},point,1,mode,padding);
  if(native!=actual.accepted || (native && native_value!=actual.placement_value)) {
   std::cerr<<"support case="<<n<<" native="<<native<<" lab="<<actual.accepted<<" native_value="<<native_value<<" lab_value="<<actual.placement_value<<'\n';
   throw std::runtime_error("original support owner/clearance mismatch");
  }
  ++support_cases;accepted+=native;
 }
 std::cout<<"V653_NATIVE_RESOURCE_PLACEMENT PASS power_cases="<<power_cases<<" power_selected="<<selected<<" support_cases="<<support_cases<<" support_accepted="<<accepted<<"; original power/workspace/random and common support bodies; terrain/collision/field blockers controlled clear, no full game loop\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
