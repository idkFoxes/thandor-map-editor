#pragma once
#include "thandor/simulation_lab.hpp"
#include <stdexcept>
namespace v638_fixture {
using namespace thandor;
inline void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
struct Data {
 std::vector<SimulationLabSeedEntity> seeds;
 std::vector<SimulationLabProductionAsset> products;
 std::vector<SimulationLabTechnologyAsset> technologies;
 std::array<FactionTechnologyMask256,8> masks{};
 std::array<SimulationLabFactionEconomySeed,8> economy{};
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
 SimulationLabConfig config;
 explicit Data(uint32_t id_offset=1000,uint32_t runs=1){
  SimulationLabSeedEntity s;s.army_asset_id=330;s.faction_index=1;s.building=true;s.runtime_model_class_id=11;s.root_model_maximum_integrity=1000;s.actor.position={10,10,0};seeds.push_back(s);
  for(uint32_t lab=0;lab<2;++lab){auto source=s;source.army_asset_id=390+lab;source.runtime_model_class_id=4;source.actor.position.x+=4+4*float(lab);for(uint32_t i=0;i<16;++i)source.research_technology_ids.push_back(99+lab*16+i);seeds.push_back(source);}
  for(uint32_t id=99;id<=130;++id){SimulationLabTechnologyAsset t;t.technology_id=id;t.dependency_technology_id=id;t.ai_base_candidate_score=240+int32_t(id-99);t.category=3;t.research_duration_ticks=1000;t.xenite_cost_q4=400;t.compatible_model_resolved=true;t.compatible_model_lookup_complete=true;t.compatible_model_definition_id=id_offset+id;t.compatible_model_runtime_class_id=7;technologies.push_back(t);}
  SimulationLabTechnologyAsset category;category.technology_id=208;category.category=3;technologies.push_back(category);
  for(uint32_t index=0;index<130;++index){SimulationLabProductionAsset a;a.army_asset_id=351+index;a.army_runtime_flags14_raw=0x11;a.xenite_cost_q4=index?10:40000;a.build_duration_ticks=1000;a.ai_baseline_root_definition_value60=1000;a.ai_baseline_root_definition_value18=1;a.ai_baseline_root_category_maximum_index5c=1;a.ai_faction_score_seeds_prepared=index<2;
   for(auto& score:a.ai_faction_score_seeds){score.resolved=true;score.root_definition_value60=1000;score.root_definition_value18=1;score.root_category_maximum_index5c=1;}
   a.entity_seed.army_asset_id=a.army_asset_id;a.entity_seed.runtime_model_class_id=4;
   if(index==0)for(uint32_t i=0;i<24;++i)a.ai_linked_model_definition_ids[i/8].push_back(id_offset+99+i);
   if(index==1)for(uint32_t i=0;i<8;++i)a.ai_linked_model_definition_ids[0].push_back(id_offset+123+i);
   products.push_back(a);
  }
  for(auto id:{0U,208U})set_faction_technology_unlocked(masks[1],id,true);
  economy[1].xenite_current_q4=100000;economy[1].baseline_xenite_storage_limit_q4=100000;economy[1].baseline_energy_supply_q4=100000;
  ki[0x0D]=4096;config.run_count=runs;config.duration_ticks=256;config.parameter_index=100;config.sweep_mode=SimulationLabSweepMode::Fixed;
 }
 void reset(SimulationLabExperiment& lab)const{lab.reset(config,ki,seeds,economy,products,technologies,masks,{},{});}
};
}
