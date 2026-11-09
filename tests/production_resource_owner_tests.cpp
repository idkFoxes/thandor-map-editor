#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/retail_power_candidate.hpp"
#include "thandor/simulation_lab_ai_workspaces.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
SimulationLabExperiment fixture(uint32_t id,uint32_t roots,const FieldGrid& field) {
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=100;config.parameter_index=100;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};ki[0]=256;ki[19]=256;
 ki[3]=100;ki[4]=100;ki[20]=ki[21]=2000;ki[22]=ki[23]=1;
 std::vector<SimulationLabSeedEntity> seeds(roots);
 for(auto& s:seeds){s.army_asset_id=390;s.faction_index=1;s.runtime_model_class_id=4;s.root_model_maximum_integrity=1000;s.actor.position={10,10,0};}
 seeds.back().army_asset_id=id==333?332:330;
 SimulationLabProductionAsset product;product.army_asset_id=id;product.army_runtime_flags14_raw=1;product.xenite_cost_q4=100;
 std::array<SimulationLabFactionEconomySeed,8> economy{};
 economy[1].derive_energy_allocation_from_components=true;
 economy[1].baseline_energy_generation_capacity_q4=160;
 economy[1].xenite_current_q4=economy[1].tritium_current_q4=1000;
 economy[1].baseline_xenite_storage_limit_q4=economy[1].baseline_tritium_storage_limit_q4=1000;
 std::array<FactionTechnologyMask256,8> masks{};set_faction_technology_unlocked(masks[1],0,true);set_faction_technology_unlocked(masks[1],1,true);
 SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=1;scenario.simulate_all_active_factions_as_ai=true;scenario.runtime_catalog=std::make_shared<SimulationLabRuntimeCatalog>();
 SimulationLabExperiment lab;lab.reset(config,ki,seeds,economy,std::span(&product,1),{},masks,{},scenario);
 lab.step(field,1); // Finish level field/energy initialization before injecting the planning snapshot.
 auto& run=lab.runs()[0];run.tick=73;run.faction_energy_generation_capacity_q4[1]=160;
 run.faction_economy[1].baseline_energy_supply_q4=320;run.faction_supplied_energy_demand_q4[1]=160;
 run.faction_economy[1].xenite_storage_limit_q4=run.faction_economy[1].tritium_storage_limit_q4=1000;
 run.secondary_purchase_requests.push_back({id,1,0,0});return lab;
}
int main()try {
 const auto field=FieldGrid::create(32,32,0,0);uint32_t cases=0;
 for(uint32_t id:{310U,331U,333U})for(uint32_t roots:{1U,128U}) {
  auto lab=fixture(id,roots,field);lab.step(field,1);const auto& trace=lab.runs()[0].ai_purchase_trace[1];
  bool found=false;for(uint32_t i=0;i<trace.recorded_count;++i)found|=trace.candidates[i].id==id;
  check(found==(roots==128),"global pending request overrode retained unassigned workspace");++cases;
 }
 check(retail_power_candidate_weight(160,320,0,16,0,256,256)==1,"ordinary power weight changed");++cases;
 check(!retail_power_candidate_weight(0,320,0,16,0,256,256),"zero capacity divisor accepted");++cases;
 check(!retail_power_candidate_weight(160,320,0,16,0,256,0),"zero coefficient divisor accepted");++cases;
 check(retail_power_candidate_weight(160,320,0,16,0,0,256)==0,"zero numerator removed native insertion/draw");++cases;
 std::cout<<"V653_RESOURCE_OWNER PASS cases="<<cases<<"; retained unassigned capacity and power divisor/draw guards\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
