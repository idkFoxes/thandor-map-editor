#!/usr/bin/env python3
"""Compare native research scoring functions with the public Lab planning loop.
Usage: python3 tools/verify_native_faction_research_oracle.py OPEN_THANDOR_ROOT BUILD_DIR
No native game executable is used. Unsupported pointer-valued kind-3 classes
are explicitly excluded from numeric equality; the editor rejects those scores.
"""
from pathlib import Path
import re,subprocess,sys
reference,build=[Path(v).resolve() for v in sys.argv[1:]]
root=Path(__file__).resolve().parents[1]
text=(reference/'src/gameplay/ai/technology.cpp').read_text()
def function(name):
 m=re.search(r'^static (?:AiTechnologyCandidateScoreKind|AiTechnologyCandidateScore|UQ8) '+name+r'\b',text,re.M)
 if not m: raise ValueError(name)
 start=m.start();brace=text.index('{',m.end());depth=1;end=brace+1
 # A forward declaration exists for the relation helper: start at its final definition.
 if ';' in text[m.end():brace]:
  matches=list(re.finditer(r'^static UQ8 '+name+r'\b',text,re.M));m=matches[-1];start=m.start();brace=text.index('{',m.end());end=brace+1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]
functions=[function(n) for n in ['AiTechnologyPlanning_SelectScoreKind','AiTechnologyCompatibility_ComputeAverageRuntimeRelationScaleQ8','AiTechnologyScore_ComputeRuntimeClassCompatibleCandidateValue','AiTechnologyScore_ComputeFactionScaledCandidateValue','AiTechnologyScore_ReturnBaseCandidateValue']]
constants={}
for token in re.findall(r'\b(?:TEC_\d+_[A-Z0-9_]+|ARM_\d+_[A-Z0-9_]+|MODEL_RUNTIME_CLASS_\d+(?:_[A-Z0-9_]+)?)\b','\n'.join(functions)):
 constants[token]=int(re.search(r'_(\d+)',token)[1])
prefix=r'''
#include "thandor/simulation_lab.hpp"
#include <set>
#include <iostream>
#include <stdexcept>
namespace native {
using UQ8=uint32_t;using FactionRuntimeIndex=uint32_t;using PckTechnologyIdCatalog=uint32_t;
using AiTechnologyCandidateScore=int;using AiTechnologyCandidateScoreKind=int;
using PckModelDefinitionIdCatalog=uint32_t;using EnergyDemandQ4=uint32_t;
constexpr int Q8_ONE=256,AI_TECHNOLOGY_RELATION_SCORE_FACTOR=40000;
constexpr int AI_TECHNOLOGY_SCORE_DEFAULT_ZERO=0,AI_TECHNOLOGY_SCORE_FACTION_SCALED=1,
 AI_TECHNOLOGY_SCORE_BASE_VALUE_KIND2=2,AI_TECHNOLOGY_SCORE_RUNTIME_CLASS_COMPATIBLE=3,
 AI_TECHNOLOGY_SCORE_BASE_VALUE_KIND4=4,AI_TECHNOLOGY_SCORE_CATEGORY_COMPATIBLE=5;
struct ModelDefinition {uint32_t definitionId{100},runtimeClassId{2};};
using ModelDefinitionRecordPrefix=ModelDefinition;using MdlDefinitionSemanticPrefix=ModelDefinition;
struct ModelRuntimeSlot {struct{ModelDefinition* definition{};}definitionOrSavedId;};
struct AiRuntimeWorkspaceEntry {ModelRuntimeSlot* modelRuntime{};};
AiRuntimeWorkspaceEntry g_AiWorkspace01Units[64]{};int g_AiWorkspace01Count{};
struct TechnologyAsset {struct Record{int baseCandidateScore{};}records[256];};
TechnologyAsset technology;TechnologyAsset* g_TechnologyAsset=&technology;
struct Knowledge{struct{uint32_t factionScaledTechnologyMinimumXeniteQ4{};}parameters;};
using AiKnowledgeDataImage=Knowledge;
Knowledge knowledge;Knowledge* g_AiKnowledgeData=&knowledge;
struct WorldRuntimeContext{};
struct Image {struct Faction{uint32_t xeniteCurrentQ4{},suppliedEnergyDemandQ4{},unpoweredEnergyDemandQ4{},tritiumExtractionRateQ4PerTick{},baselineEnergySupplyQ4{};}records[8];};
Image g_GameFactionRuntimeImage;
ModelDefinition selected;std::set<uint32_t> structures;
ModelDefinition* ModelDefinitionRegistry_FindByRuntimeClassId(uint32_t){return &selected;}
bool AiPrimaryWorkspace_HasEntryById(uint32_t id){return structures.count(id)!=0;}
'''
source=prefix+'\n'.join('constexpr uint32_t '+k+'='+str(v)+';' for k,v in constants.items())+'\n'+'\n'.join(functions)+'\n}\n'
main=r'''
using namespace thandor;
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main()try{
 const auto field=FieldGrid::create(35,35,0,0);
 const std::array<uint32_t,14> ids{1,99,183,184,185,189,190,191,210,211,213,214,216,217};
 unsigned cases=0,kind3=0;
 for(uint32_t source_class=0;source_class<24;++source_class)for(uint32_t id:ids){
  SimulationLabSeedEntity seed;seed.army_asset_id=330;seed.faction_index=1;seed.building=true;
  seed.runtime_model_class_id=source_class;seed.root_model_maximum_integrity=4096;seed.actor.position={10,-5,0};
  seed.research_technology_ids={id};
  SimulationLabTechnologyAsset tech;tech.technology_id=id;tech.dependency_technology_id=id;
  tech.ai_base_candidate_score=160;tech.research_duration_ticks=10000;
  // Kind 5 has an explicit zero-score carrier universe. Kind 3 uses a tracked definition.
  tech.compatible_model_resolved=true;tech.compatible_model_lookup_complete=true;
  tech.compatible_model_definition_id=100;tech.compatible_model_runtime_class_id=2;
  std::array<SimulationLabFactionEconomySeed,8> economy{};
  economy[1].xenite_current_q4=10000;economy[1].baseline_xenite_storage_limit_q4=10000;
  economy[1].baseline_energy_supply_q4=10000;
  std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};ki[0x0D]=4096;
  SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=74;cfg.sweep_mode=SimulationLabSweepMode::Fixed;cfg.parameter_index=100;
  SimulationLabExperiment lab;lab.reset(cfg,ki,std::span(&seed,1),economy,{},std::span(&tech,1),{},{});
  lab.step(field,74);
  const auto& trace=lab.runs()[0].ai_research_trace[1];
  check(trace.candidate_count==1,"research fixture did not reach workspace12");
  native::ModelDefinition def;def.runtimeClassId=source_class;
  const auto expected_kind=native::AiTechnologyPlanning_SelectScoreKind(id,&def);
  const auto& c=trace.candidates[0];
  if(c.score_kind!=uint32_t(expected_kind)){std::cerr<<"kind mismatch class="<<source_class<<" tech="<<id<<'\n';return 1;}
  native::technology.records[id].baseCandidateScore=160;
  native::g_GameFactionRuntimeImage.records[1]={10000,0,0,0,10000};native::knowledge.parameters.factionScaledTechnologyMinimumXeniteQ4=0;
  native::selected.runtimeClassId=2;native::g_AiWorkspace01Count=0;native::structures.clear();
  int expected=0;
  if(expected_kind==1)expected=native::AiTechnologyScore_ComputeFactionScaledCandidateValue(1,id,nullptr);
  if(expected_kind==2||expected_kind==4)expected=native::AiTechnologyScore_ReturnBaseCandidateValue(1,id,nullptr);
  if(expected_kind==3)expected=native::AiTechnologyScore_ComputeRuntimeClassCompatibleCandidateValue(1,id,nullptr);
  if(c.score!=uint32_t(expected)){std::cerr<<"score mismatch class="<<source_class<<" tech="<<id<<" native="<<expected<<" editor="<<c.score<<'\n';return 1;}
  ++cases;
 }
 // Relation scoring uses actual runtime MDL family counts, and the ground-family factory exception.
 for(uint32_t mdl_class:{1U,2U,3U,17U,19U,21U})for(uint32_t extra:{0U,302U,305U})for(uint32_t units:{0U,1U,4U}){
  native::structures.clear();if(extra)native::structures.insert(extra);
  std::vector<SimulationLabSeedEntity> seeds;
  SimulationLabSeedEntity source;source.army_asset_id=330;source.faction_index=1;source.building=true;source.runtime_model_class_id=13;
  source.root_model_maximum_integrity=4096;source.actor.position={10,-5,0};source.research_technology_ids={99};seeds.push_back(source);
  if(extra){auto e=source;e.army_asset_id=extra;e.runtime_model_class_id=13;e.research_technology_ids.clear();seeds.push_back(e);}
  std::array<native::ModelDefinition,4> defs;std::array<native::ModelRuntimeSlot,4> runtimes;
  for(uint32_t i=0;i<units;++i){
   const uint32_t mdl=i==3?101:100+1000*i;
   auto e=source;e.army_asset_id=10+i;e.runtime_model_class_id=2;e.building=false;e.research_technology_ids.clear();
   SimulationLabModelComponentSeed component;component.model_definition_id=mdl;component.runtime_class_id=2;component.maximum_integrity=4096;
   e.model_components={component};seeds.push_back(e);
   defs[i].definitionId=mdl;runtimes[i].definitionOrSavedId.definition=&defs[i];native::g_AiWorkspace01Units[i].modelRuntime=&runtimes[i];
  }
  native::g_AiWorkspace01Count=units;native::selected={100,mdl_class};native::technology.records[99].baseCandidateScore=160;
  const auto expected=native::AiTechnologyScore_ComputeRuntimeClassCompatibleCandidateValue(1,99,nullptr);
  SimulationLabTechnologyAsset tech;tech.technology_id=99;tech.dependency_technology_id=99;tech.ai_base_candidate_score=160;tech.research_duration_ticks=10000;
  tech.compatible_model_resolved=true;tech.compatible_model_lookup_complete=true;tech.compatible_model_definition_id=100;tech.compatible_model_runtime_class_id=mdl_class;
  std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};ki[0x0D]=4096;SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=74;cfg.sweep_mode=SimulationLabSweepMode::Fixed;cfg.parameter_index=100;
  SimulationLabExperiment lab;lab.reset(cfg,ki,seeds,{},{},std::span(&tech,1),{},{});lab.step(field,74);
  const auto& trace=lab.runs()[0].ai_research_trace[1];
  check(trace.candidate_count==1,"relation fixture was not admitted");
  if(trace.candidates[0].score!=uint32_t(expected)){std::cerr<<"relation mismatch class="<<mdl_class<<" factory="<<extra<<" units="<<units<<" native="<<expected<<" editor="<<trace.candidates[0].score<<'\n';return 1;}++kind3;
 }
 std::cout<<"NATIVE_FACTION_RESEARCH_ORACLE PASS score_kind_and_base_cases="<<cases<<" relation_cases="<<kind3<<"\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
'''
p=build/'native_faction_research_oracle.cpp';p.write_text(source+main)
exe=build/'native_faction_research_oracle'
subprocess.run(['g++','-std=c++20','-O2','-fwrapv','-I'+str(root/'include'),str(p),str(build/'libthandor_map_core.a'),'-pthread','-ldl','-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
