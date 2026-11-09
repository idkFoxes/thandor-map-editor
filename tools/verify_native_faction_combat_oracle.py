#!/usr/bin/env python3
"""Compile native combat scoring and compare the editor's score/admission.
Usage: python3 tools/verify_native_faction_combat_oracle.py OPEN_THANDOR_ROOT BUILD_DIR
Adapters supply relationships, condition, node masks and line-of-fire results.
The scoring function and three-step integer square root are read unmodified
from the supplied checkout. This is not a full-game AI hash comparison.
"""
from pathlib import Path
import re,sys,subprocess
reference,build=[Path(v).resolve() for v in sys.argv[1:]]
root=Path(__file__).resolve().parents[1]
combat=(reference/'src/gameplay/ai/combat.cpp').read_text()
math=(reference/'src/core/math/fixed_vector.cpp').read_text()
def function(text,name):
 m=re.search(r'^(?:static )?(?:AiCandidateScore32|uint32_t) '+name+r'\b',text,re.M)
 if not m:raise ValueError(name)
 start=m.start();brace=text.index('{',m.end());depth=1;end=brace+1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]
consts=[]
for m in re.finditer(r'^static const (?:uint32_t|int32_t) g_AiCombatTarget\w+[^=]*=\s*(?:\{.*?\}|[^;]+);',combat,re.M|re.S):consts.append(m[0])
prefix=r'''
#include "thandor/simulation_lab_targeting.hpp"
#include "thandor/simulation_lab_visibility.hpp"
#include <random>
#include <iostream>
namespace native {
using AiCandidateScore32=int32_t;using AiSourceClassCount=int32_t;using DepthBinMask32=uint32_t;
using Q12=int32_t;using UInt64Half32=uint32_t;
constexpr int Q12_ONE=4096,MODEL_RUNTIME_CLASS_21_AIRCRAFT=21;
constexpr int ARMY_WEAPON_SLOT_PRIMARY=0,ARMY_WEAPON_SLOT_SECONDARY=1;
struct ModelDefinition{uint32_t runtimeClassId{},targetClassIndex{},accelerationPerTick{1};int32_t aimHeightOffsetQ12{};};
struct ModelRuntimeNode {struct {struct {int32_t x{},y{},z{};}translation;}worldTransform;
 uint32_t depthBinMaskFar{1},depthBinMaskNear{1};ModelRuntimeNode* childNodes[1]{};};
struct ModelRuntimeSlot {struct {ModelDefinition* runtimeDefinition{};}definitionOrSavedId;
 uint32_t attachmentCount{};struct Attachment {ModelRuntimeSlot* childModelRuntimeOrSavedOffset{};}attachments[2];};
struct ArmyRuntimeSlot {uint32_t factionIndex{};ModelRuntimeNode* modelNodeRuntime{};
 struct{ModelRuntimeSlot* modelRuntime{};}modelRuntimeOrSavedOffset;
 int32_t weaponRangeQ12{};int32_t targetClassShotDamage[8]{};int32_t condition{4096};};
using RuntimeModelFactionPrefix=ArmyRuntimeSlot;
struct WorldRuntimeContext{};struct Root{WorldRuntimeContext worldRuntime;};Root root;Root* g_InGameRuntimeRoot=&root;
uint32_t g_AiCombatTargetCurrentCommandGenerationRightShiftBits{};
bool blocked{};bool friendly{};
bool DepthBinMasks_Overlap(uint32_t a,uint32_t b,uint32_t c,uint32_t d){return (a&c)&&(b&d);}
bool GameFactionRuntime_TestCapabilityBitClear(uint32_t,uint32_t){return !friendly;}
Q12 ModelRuntime_QueryHierarchyConditionRatioQ12(ArmyRuntimeSlot* p){return p->condition;}
bool ArmyWeaponRuntime_TestTargetLineOfFire(Q12,Q12,Q12,WorldRuntimeContext*,ModelRuntimeSlot*){return blocked;}
'''
code=prefix+'\n'.join(consts)+'\n'+function(math,'FixedMath_UInt64Sqrt')+'\n'+function(combat,'AiCombatTarget_EvaluateCandidateScore')+'\n}\n'
main=r'''
using namespace thandor;
int main(){
 std::mt19937 random(0xA1642026);unsigned tested=0;
 const auto field=FieldGrid::create(59,59,0,0);const auto& cell=field.at(25,25);
 for(unsigned trial=0;trial<25000;++trial){
  SimulationLabCombatScoreInput in;
  in.source_range_q12=4096+random()%(20*4096);in.delta_x_q12=int32_t(random()%(30*4096))-15*4096;
  in.delta_y_q12=int32_t(random()%(30*4096))-15*4096;in.target_runtime_class=random()%24;
  in.source_counter_for_target=int32_t(random()%20001)-10000;
  in.target_counter_for_source=int32_t(random()%20001)-10000;
  if(trial%4==0)in.target_counter_for_source=0;
  if(!in.source_counter_for_target)in.source_counter_for_target=1;
  in.target_condition_q12=random()%6000;in.source_class_count=trial%2?4096:-4096;
  native::ModelDefinition sd,td;sd.targetClassIndex=0;td.targetClassIndex=1;td.runtimeClassId=in.target_runtime_class;
  native::ModelRuntimeSlot sm,tm;sm.definitionOrSavedId.runtimeDefinition=&sd;tm.definitionOrSavedId.runtimeDefinition=&td;
  native::ModelRuntimeNode sn,tn;tn.worldTransform.translation={-in.delta_x_q12,-in.delta_y_q12,0};
  native::ArmyRuntimeSlot source,target;source.factionIndex=1;target.factionIndex=in.source_class_count>0?2:1;
  source.modelNodeRuntime=&sn;target.modelNodeRuntime=&tn;source.modelRuntimeOrSavedOffset.modelRuntime=&sm;target.modelRuntimeOrSavedOffset.modelRuntime=&tm;
  source.weaponRangeQ12=in.source_range_q12;source.targetClassShotDamage[1]=in.source_counter_for_target;
  target.targetClassShotDamage[0]=in.target_counter_for_source;target.condition=in.target_condition_q12;
  native::friendly=in.source_class_count<0;native::blocked=false;
  const int32_t expected=native::AiCombatTarget_EvaluateCandidateScore(0,in.source_class_count,1,1,&target,&source);
  const auto score=simulation_lab_combat_score(in);
  if(expected!=score.value){std::cerr<<"numeric mismatch trial="<<trial<<" native="<<expected<<" editor="<<score.value<<'\n';return 1;}
  // Repeat admitted candidates with a supplied blocked line and both native
  // acceleration states through the real editor command selector.
  if(expected>0){
   for(bool accelerates:{false,true}){
    native::blocked=true;sd.accelerationPerTick=accelerates?1:0;
    sm.attachmentCount=1;sm.attachments[0].childModelRuntimeOrSavedOffset=&sm;
    if(td.runtimeClassId==21)tn.childNodes[0]=&tn;
    const auto adjusted=native::AiCombatTarget_EvaluateCandidateScore(0,in.source_class_count,1,1,&target,&source);
    SimulationLabRun run;run.entities.resize(2);
    auto& s=run.entities[0];auto& t=run.entities[1];s.faction_index=1;t.faction_index=target.factionIndex;
    s.shot_impact_class_index=0;t.shot_impact_class_index=1;t.runtime_model_class_id=in.target_runtime_class;
    s.actor.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
    t.actor.position={s.actor.position.x-q12_to_float(in.delta_x_q12),s.actor.position.y-q12_to_float(in.delta_y_q12),0};
    t.motion.collision_radius_q12=1000;s.motion.speed_q12_per_tick=64;s.motion.ground_acceleration_q12=accelerates?1:0;
    s.runtime_model_hierarchy=std::make_shared<std::vector<ModelVisualNode>>(1);
    t.model_terrain_state_initialized=true;t.model_primary_occupancy_mask=2U<<2;
    if(native::friendly)run.faction_capability_flags[1]|=1U<<1;
    std::vector<SimulationLabCombatMetrics> metrics(2);metrics[0].range_q12=in.source_range_q12;
    metrics[0].class_sum=in.source_class_count;metrics[0].class_damage_q12[1]=in.source_counter_for_target;
    metrics[1].condition_q12=in.target_condition_q12;metrics[1].class_damage_q12[0]=in.target_counter_for_source;
    const std::array<uint8_t,2> blocked{0,1};
    const auto chosen=simulation_lab_select_combat_target(run,field,0,metrics,true,blocked);
    if(chosen.entity_index.has_value()!=(adjusted>0)||(chosen.entity_index&&chosen.score.value!=adjusted)){
     std::cerr<<"admission mismatch trial="<<trial<<" acceleration="<<accelerates<<" native="<<adjusted<<" editor="<<chosen.score.value<<'\n';return 1;
    }++tested;
   }
  }++tested;
 }
 std::cout<<"NATIVE_FACTION_COMBAT_ORACLE PASS cases="<<tested<<" signed damage, repair, condition, radial score, 24 classes, blocked LOS, acceleration\n";
}
'''
p=build/'native_faction_combat_oracle.cpp';p.write_text(code+main);exe=build/'native_faction_combat_oracle'
subprocess.run(['g++','-std=c++20','-O2','-fwrapv','-I'+str(root/'include'),str(p),str(build/'libthandor_map_core.a'),'-pthread','-ldl','-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
