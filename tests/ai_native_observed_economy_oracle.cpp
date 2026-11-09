// Reuse the existing native globals/shims. No random fixture cases run here.
#define main native_random_fixture_main_unused
#include "placement_native_resource_placement_oracle.cpp"
#undef main
#include THANDOR_NATIVE_PLANNING
#include <fstream>
#include <sstream>
#include <map>
#include <cstring>
static std::vector<std::string> split(const std::string& line){std::vector<std::string> parts;std::stringstream stream(line);std::string p;while(std::getline(stream,p,','))parts.push_back(p);return parts;}
int main(int argc,char**argv)try{
 if(argc!=4)throw std::runtime_error("Usage: oracle PCK_ROOT factions.csv comparison.csv");
 const auto catalog=thandor::AssetCatalog::scan_archive_root(argv[1]);const auto ki=thandor::AiKnowledgeData::parse(catalog.load("engine/ki.dat"));
 std::memcpy(&knowledge,ki.parameters().data(),sizeof(knowledge));g_AiKnowledgeData=&knowledge;
 g_AiWorkspace00Structures=structures;g_AiWorkspace13Candidates=candidates;
 // Arithmetic comparison only: force ARM330 presence and no queued ARM310. The
 // actual per-sample model/workspace admission gates are outside this oracle.
 g_AiWorkspace00Count=1;structures[0]={nullptr,330};
 std::ifstream input(argv[2]);std::ofstream output(argv[3]);std::string line;std::getline(input,line);auto names=split(line);std::map<std::string,size_t> columns;for(size_t i=0;i<names.size();++i)columns[names[i]]=i;
 output<<"tick,faction,generation_q4,baseline_q4,tritium_rate_q4,demand_q4,native_power_admitted,helper_power_admitted,power_weight,native_packed_score,capacity_extra4_exceeded\n";
 size_t samples=0,capacity_cases=0,power_admitted=0,zero_surplus=0,initial_zero_capacity=0;
 while(std::getline(input,line)){
  auto row=split(line);if(row.size()!=names.size())throw std::runtime_error("Incomplete audit row");
  auto value=[&](const char* name){return uint32_t(std::stoull(row.at(columns.at(name))));};
  const auto faction=value("faction");auto& f=g_GameFactionRuntimeImage.records[faction];f={};
  f.energyGenerationCapacityQ4=value("energy_capacity_q4");f.baselineEnergySupplyQ4=640;
  f.tritiumExtractionRateQ4PerTick=value("tritium_rate_q4");f.suppliedEnergyDemandQ4=value("energy_supplied_q4");f.unpoweredEnergyDemandQ4=value("energy_unpowered_q4");
  // At tick zero no class14 allocation has run and generation is zero. Do not
  // force a native divide-by-zero path that the real no-ARM330 workspace excludes.
  g_AiWorkspace00Count=f.energyGenerationCapacityQ4?1:0;initial_zero_capacity+=!g_AiWorkspace00Count;
  Random_SetBothSeeds(12345);Random_SelectPrimaryStream();g_AiCandidateWorkspaceEntryCount=0;AiResourceCandidate_AddPowerPlant(faction);
  const auto weight=thandor::retail_power_candidate_weight(f.energyGenerationCapacityQ4,f.baselineEnergySupplyQ4,f.tritiumExtractionRateQ4PerTick,f.suppliedEnergyDemandQ4,f.unpoweredEnergyDemandQ4,knowledge.parameters.resource136DeficitScoreNumerator,knowledge.parameters.resource136DeficitScoreDenominator);
  uint32_t state=12345,score=0;bool admitted=false;if(weight){const auto draw=thandor::retail_random_next(state);if(*weight>1){score=((*weight*5U+draw%*weight)<<4U)|1U;admitted=true;}}
  check(g_AiCandidateWorkspaceEntryCount==uint32_t(admitted)&&g_RandomGeneratorState.primarySeed==state,"Observed power/RNG differs");
  if(admitted)check(candidates[0].weightedScoreAndKind==score,"Observed packed power score differs");
  for(uint32_t additional:{4U,ki.parameters()[16],ki.parameters()[17],ki.parameters()[18]}){
   const bool helper=std::min((f.baselineEnergySupplyQ4>>4)+f.tritiumExtractionRateQ4PerTick,f.energyGenerationCapacityQ4>>4)<((f.suppliedEnergyDemandQ4+f.unpoweredEnergyDemandQ4)>>4)+additional;
   check(AiFactionRuntime_TestPlanningCapacityExceeded(additional,faction)==helper,"Observed capacity gate differs");++capacity_cases;
  }
  output<<value("tick")<<','<<faction<<','<<f.energyGenerationCapacityQ4<<','<<f.baselineEnergySupplyQ4<<','<<f.tritiumExtractionRateQ4PerTick<<','<<f.suppliedEnergyDemandQ4+f.unpoweredEnergyDemandQ4<<','<<g_AiCandidateWorkspaceEntryCount<<','<<admitted<<','<<weight.value_or(0)<<','<<score<<','<<AiFactionRuntime_TestPlanningCapacityExceeded(4,faction)<<'\n';
  ++samples;power_admitted+=admitted;zero_surplus+=((f.baselineEnergySupplyQ4>>4)+f.tritiumExtractionRateQ4PerTick)==(f.energyGenerationCapacityQ4>>4);
 }
 if(samples!=7204)throw std::runtime_error("Expected 1801 one-second samples for each of four factions");
 std::cout<<"NATIVE_OBSERVED_ECONOMY PASS samples="<<samples<<" capacity_gates="<<capacity_cases<<" power_admitted="<<power_admitted<<" initial_zero_capacity_gated="<<initial_zero_capacity<<" equal_supply_capacity="<<zero_surplus<<"; actual 30-minute Lab numeric inputs; workspace presence/queue gates forced, native game not executed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
