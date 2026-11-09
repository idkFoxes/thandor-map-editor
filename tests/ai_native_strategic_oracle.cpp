#include THANDOR_NATIVE_PLANNING
#include THANDOR_NATIVE_TECHNOLOGY
#include THANDOR_NATIVE_WORKSPACES
#include THANDOR_NATIVE_RANDOM
#include "thandor/catalog.hpp"
#include "thandor/ai_knowledge.hpp"
#include <iostream>
#include <random>
#include <cstring>
GameFactionRuntimeImage g_GameFactionRuntimeImage{};
AiKnowledgeDataImage* g_AiKnowledgeData{};
static AiKnowledgeDataImage knowledge{};
static AiStructureWorkspaceEntry structures[128]{};
static AiCandidateWorkspaceEntry entries[128]{};
void Thandor_Log(const char*,...){}
void Thandor_Ptr32Overflow(uintptr_t){std::abort();}
bool ArmyAssetRegistry_FindEnabledById(PckArmyAssetIdCatalog){return false;}
// The original terrain selector's grid is unused while TEC1 is locked.
decltype(g_GridScratchPrimary) g_GridScratchPrimary{};
decltype(g_GridScratchWidth) g_GridScratchWidth{};
decltype(g_GridScratchHeight) g_GridScratchHeight{};
int main(int argc,char**argv)try {
 g_AiKnowledgeData=&knowledge;g_AiWorkspace00Structures=structures;g_AiWorkspace13Candidates=entries;g_AiWorkspace00Count=1;structures[0].armyAssetId=330;
 auto& f=g_GameFactionRuntimeImage.records[2];f.technologyMasks256Bits[0]=1;f.baselineEnergySupplyQ4=f.energyGenerationCapacityQ4=640;f.suppliedEnergyDemandQ4=272;for(auto& p:f.aiPressureValues)p=2;
 if(argc>1){const auto catalog=thandor::AssetCatalog::scan_archive_root(argv[1]);const auto ki=thandor::AiKnowledgeData::parse(catalog.load("engine/ki.dat"));static_assert(sizeof(knowledge)==512);std::memcpy(&knowledge,ki.parameters().data(),512);}
 else {knowledge.parameters.strategicClass12dBaseWeight=57344;knowledge.parameters.strategicClass141To143BaseWeight=42960;knowledge.parameters.workspace12BestCandidateBaseWeight=40960;knowledge.parameters.strategic12dAnd141To143AdditionalPlanningCapacity=10;}
 std::mt19937 rng(0x656);WorldRuntimeContext world{};uint32_t checks=0;
 for(uint32_t i=0;i<10000;++i){g_AiCandidateWorkspaceEntryCount=0;g_RandomGeneratorState.primarySeed=rng();AiStrategicClass_AddArmsFactoriesStageBuilding(2,&world);AiStrategicClass_AddWeightedClassCandidate(2,&world);AiCandidateWorkspace_AddOrAccumulateWeightedEntry(201,knowledge.parameters.workspace12BestCandidateBaseWeight,2);AiCandidateWorkspace_SortDescending();if(g_AiCandidateWorkspaceEntryCount<2 || (entries[0].entityIdAndMultiplicity&AI_CANDIDATE_ID_MASK)!=301)throw std::runtime_error("record-input strategic rank differs from bound");++checks;}
 std::cout<<"V656_NATIVE_STRATEGIC_INPUT PASS seeds="<<checks<<" faction2_stock=18400 supply=640 capacity=640 demand=272 TEC1_locked=1 ARM330_presence=1; native_stage301_weight="<<knowledge.parameters.strategicClass12dBaseWeight<<" native_research_weight="<<knowledge.parameters.workspace12BestCandidateBaseWeight<<" additional_capacity="<<knowledge.parameters.strategic12dAnd141To143AdditionalPlanningCapacity<<"; native ARM301 ranks before TEC201 under these inputs\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
