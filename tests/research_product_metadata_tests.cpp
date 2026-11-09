#include "thandor/simulation_lab_catalog.hpp"
#include "thandor/ai_unit_producibility_v601.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
ArmyAssetPreset preset(){
 ArmyAssetPreset p;p.registry_id=351;
 const auto group=[](uint32_t parent,uint32_t slot,uint32_t id,uint32_t tech){
  ArmyModelVariantGroup g;g.parent_group_index=parent;g.child_slot=slot;
  ModelTechnologyVariant v;v.model_definition_id=id;v.technology_unlock_id=tech;
  ModelVisualNode n;n.model_definition_id=id;n.runtime_class_id=parent==UINT32_MAX?4:7;
  n.shot_impact_class_index=1;n.simulation.maximum_integrity=1000;n.simulation.runtime_field_18_raw=1;
  v.model_hierarchy.push_back(n);g.variants.push_back(v);return g;
 };
 p.model_variant_groups={group(UINT32_MAX,0,322,206),group(0,1,220,0),group(0,0,225,99),group(0,2,999,0)};
 auto ignored=p.model_variant_groups[0].variants[0];ignored.variant_index=8;ignored.model_definition_id=998;p.model_variant_groups[0].variants.push_back(ignored);
 auto replacement=ModelFactionTechnologyReplacement{};replacement.model_definition_id=777;replacement.technology_unlock_id=0;
 p.model_variant_groups[2].variants[0].faction_technology_replacements.push_back(replacement);
 return p;
}
SimulationLabProductionAsset product(){
 SimulationLabProductionAsset a;a.army_asset_id=351;a.army_runtime_flags14_raw=0x11;
 a.entity_seed.army_asset_id=351;a.entity_seed.runtime_model_class_id=4;
 simulation_lab_prepare_ai_product(preset(),AssetCatalog{},a);return a;
}
SimulationLabExperiment fixture(SimulationLabProductionAsset a,bool root=true){
 SimulationLabSeedEntity hq;hq.army_asset_id=330;hq.faction_index=1;hq.building=true;hq.runtime_model_class_id=11;hq.root_model_maximum_integrity=1000;hq.actor.position={10,10,0};
 auto lab=hq;lab.army_asset_id=390;lab.runtime_model_class_id=4;lab.actor.position={14,10,0};lab.research_technology_ids={99};
 SimulationLabTechnologyAsset target;target.technology_id=99;target.dependency_technology_id=99;target.research_duration_ticks=2;target.ai_base_candidate_score=240;target.category=3;target.compatible_model_resolved=true;target.compatible_model_definition_id=225;target.compatible_model_runtime_class_id=7;
 SimulationLabTechnologyAsset category;category.technology_id=208;category.category=3;
 std::array<FactionTechnologyMask256,8> masks{};for(auto id:{0U,208U})set_faction_technology_unlocked(masks[1],id,true);if(root)set_faction_technology_unlocked(masks[1],206,true);
 std::array<SimulationLabFactionEconomySeed,8> economy{};economy[1].xenite_current_q4=60000;economy[1].baseline_xenite_storage_limit_q4=60000;economy[1].baseline_energy_supply_q4=60000;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};ki[0x0D]=4096;
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=128;config.parameter_index=100;config.sweep_mode=SimulationLabSweepMode::Fixed;
 const std::array seeds{hq,lab};const std::array techs{target,category};SimulationLabExperiment result;result.reset(config,ki,seeds,economy,std::span(&a,1),techs,masks,{},{});return result;
}
const SimulationLabRun::ResearchCandidateTrace& trace(const SimulationLabExperiment& lab){
 const auto& t=lab.runs()[0].ai_research_trace[1];for(uint32_t i=0;i<t.candidate_count;++i)if(t.candidates[i].technology_id==99)return t.candidates[i];throw std::runtime_error("candidate absent");
}
}
int main()try{
 auto p=product();check(p.ai_linked_model_definition_ids[0]==std::vector<uint32_t>{322},"raw root slots wrong");
 check(p.ai_linked_model_definition_ids[1]==std::vector<uint32_t>{225}&&p.ai_linked_model_definition_ids[2]==std::vector<uint32_t>{220},"raw child order or replacement wrong");
 simulation_lab_prepare_ai_product(preset(),AssetCatalog{},p);check(p.ai_linked_model_definition_ids[1].size()==1,"reprepare appended duplicates");
 const auto field=FieldGrid::create(59,59,0,0);auto lab=fixture(p);lab.step(field,74);
 const auto t=trace(lab);std::cout<<"SHARED_RESEARCH score="<<t.score<<" queued="<<lab.runs()[0].metrics.research_jobs_queued<<" matches="<<t.compatible_arm_matches<<'\n';
 check(t.score>0&&lab.runs()[0].metrics.research_jobs_queued==1,"shared builder produced no research job");
 check(t.compatible_arm_matches==1&&t.compatible_arm_positive==1&&t.best_potential_arm_id==351,"positive diagnostics missing");
 check(lab.runs()[0].ai_army_profile_trace[1].workspace11_count==0,"research bypassed purchase technology gate");
 check(!faction_technology_unlocked(lab.runs()[0].faction_technology_masks[1],99),"research completed before payment/duration");
 lab.step(field,5);check(faction_technology_unlocked(lab.runs()[0].faction_technology_masks[1],99),"research did not complete");
 check(simulation_lab_current_ai_score_seed(p,lab.runs()[0].faction_technology_masks[1],1).all_selected_model_technologies_unlocked,"purchase hierarchy still locked after completion");
 auto locked=fixture(p,false);locked.step(field,74);check(trace(locked).compatible_arm_matches==0&&trace(locked).locked_root_child_matches==1&&trace(locked).score==0,"locked root diagnostics wrong");
 auto missing=p;missing.ai_faction_score_seeds_prepared=false;auto absent=fixture(missing);absent.step(field,74);check(trace(absent).compatible_arm_matches==1&&trace(absent).compatible_arm_missing_metadata==1&&trace(absent).score==0,"metadata diagnostics wrong");
 auto nolink=p;nolink.ai_linked_model_definition_ids={};auto none=fixture(nolink);none.step(field,74);check(trace(none).compatible_arm_matches==0&&trace(none).score==0,"missing raw link bypassed");
 auto negative=p;for(auto& variant:negative.entity_seed.linked_model_technologies[0].variants)variant.score.root_definition_value18=0;auto neg=fixture(negative);neg.step(field,74);check(trace(neg).compatible_arm_matches==1&&trace(neg).compatible_arm_positive==0&&trace(neg).best_potential_arm_score<0,"negative potential collapsed into absent metadata");
 simulation_lab_prepare_ai_product(ArmyAssetPreset{},AssetCatalog{},p);check(!p.ai_faction_score_seeds_prepared&&p.ai_linked_model_definition_ids[0].empty()&&p.ai_baseline_root_definition_value60==0&&p.ai_root_first_variant_required_technology_bit==UINT32_MAX,"rootless product kept stale metadata");
 std::cout<<"V637_RESEARCH_METADATA PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
