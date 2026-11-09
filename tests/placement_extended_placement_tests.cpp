#include "thandor/retail_extended_placement.hpp"
#include "thandor/simulation_lab_site_placement.hpp"
#include "thandor/simulation_lab_ai_owner_list_v718.hpp"
#include "thandor/simulation_lab_targeting.hpp"
#include "core_retail_extended_reference.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
uint32_t next(uint32_t& s){s=s*1664525U+1013904223U;return s;}
void differential(){
 uint32_t rng=628;
 for(size_t trial=0;trial<4000;++trial){
  const auto point=[&](){return MovementPoint{int32_t(next(rng)%300001)-150000,int32_t(next(rng)%300001)-150000};};
  const auto anchor=point();const uint32_t cooldown=trial%7==0?0:150;
  std::vector<MovementPoint> candidates,visible,unseen;std::vector<RetailExtendedPrimary> primary;
  for(size_t i=0,n=next(rng)%33;i<n;++i)candidates.push_back(point());
  for(size_t i=0,n=next(rng)%12;i<n;++i)primary.push_back({point(),next(rng)%3==0?0:1});
  for(size_t i=0,n=next(rng)%5;i<n;++i)visible.push_back(point());
  for(size_t i=0,n=next(rng)%9;i<n;++i)unseen.push_back(point());
  // Dense boundary cases, so the comparison exercises admission and improvement calls too.
  if(trial%3==0){candidates={{0,0},{4096,0},{0,4096},{8192,0}};visible.clear();unseen.clear();primary.clear();
   const std::array<int32_t,6> d{0,8191,8192,20480,20481,32768};
   if(trial&1)visible.push_back({d[trial%6],0});else unseen.push_back({32768+int32_t(trial%3)-1,0});
   if(trial%5==0)primary.push_back({{8191+int32_t(trial%3),0},1});}
  std::vector<native_v628::FieldGridCell> cells;std::vector<native_v628::FieldGridCell*> pointers;
  for(size_t i=0;i<candidates.size();++i)cells.push_back({candidates[i].x,candidates[i].y,uint32_t(i)});
  for(auto& c:cells)pointers.push_back(&c);
  std::vector<native_v628::ModelRuntimeNode> nodes(primary.size()+visible.size()+unseen.size());
  std::vector<native_v628::GameEntityRuntime> armies(nodes.size());std::vector<native_v628::ModelRuntimeSlot> slots(nodes.size());
  std::vector<native_v628::AiStructureWorkspaceEntry> p;std::vector<native_v628::AiRuntimeWorkspaceEntry> v,u;
  size_t n=0;const auto add=[&](MovementPoint pt,int32_t state){nodes[n].worldTransform.translation={pt.x,pt.y,0};armies[n].common.commandState=state;slots[n]={{&nodes[n]},{&armies[n]}};return &slots[n++];};
  for(auto a:primary)p.push_back({add(a.point,a.command_state),300});
  for(auto a:visible)v.push_back({add(a,0),1});for(auto a:unseen)u.push_back({add(a,0),1});
  native_v628::g_AiWorkspace00Structures=p.data();native_v628::g_AiWorkspace00Count=uint32_t(p.size());
  native_v628::g_AiWorkspace02VisibleHostiles=v.data();native_v628::g_AiWorkspace02Count=uint32_t(v.size());
  native_v628::g_AiWorkspace03UnseenHostiles=u.data();native_v628::g_AiWorkspace03Count=uint32_t(u.size());
  native_v628::g_AiWorkspace09Cells=pointers.data();native_v628::g_AiWorkspace09Count=uint32_t(pointers.size());
  native_v628::g_GameFactionRuntimeImage.records[1]={cooldown,anchor.y,anchor.x};native_v628::chosen=nullptr;native_v628::calls.clear();
  const auto allowed=[&](size_t i){return (i+trial)%4!=0;};native_v628::placement=allowed;
  native_v628::AiConstructionPlanner_PlaceExtendedAssetNearFactionAnchor(340,1,nullptr);
  std::vector<size_t> actual_calls;
  const auto actual=retail_extended_placement_select(cooldown,anchor,candidates,primary,visible,unseen,[&](size_t i){actual_calls.push_back(i);return allowed(i);});
  const auto expected=native_v628::chosen?std::optional<size_t>{size_t(native_v628::chosen-cells.data())}:std::nullopt;
  check(actual==expected&&actual_calls==native_v628::calls,"native extended placement decision/callback order mismatch");
 }
 std::cout<<"4000 native extended-placement decision/callback-order comparisons PASS\n";
}
void integration(){
 auto field=FieldGrid::create(80,80,0,0);const auto& c=field.at(40,40);const MovementPoint point{c.world_x_q12,c.world_y_q12};
 SimulationLabRun run;run.entities.resize(2);run.entities[0].army_asset_id=300;run.entities[0].faction_index=1;
 run.entities[1].army_asset_id=1;run.entities[1].faction_index=2;run.entities[1].runtime_model_class_id=2;run.entities[1].actor.position={q12_to_float(point.x+8192),q12_to_float(point.y),0};
 run.current_visibility_occupancy.assign(field.cells().size(),uint64_t{0x80}<<8);
 auto scratch=std::make_shared<SimulationLabRetailScratchImage>();scratch->field_occupancy_masks.assign(field.cells().size(),uint64_t{1}<<8);run.retail_scratch=scratch;
 run.ai_primary_anchor_cooldown[1]=150;run.ai_primary_impact_anchor[1].model_x_q12=point.x;run.ai_primary_impact_anchor[1].model_y_q12=point.y;
 SimulationLabSiteModel turret{340,4,1000,0,0,0,0,{}};
 std::vector<SimulationLabSiteOwner> owners{{{{point.x-20000,point.y},1,11,0,100000,true},1000,{},0,0,0},{{{point.x+8192,point.y},2,2,0,0,true},1000,{},0,0,1}};
 const std::array sites{Vec3f{q12_to_float(point.x),q12_to_float(point.y),0}};
 LabAiOwnerWorkspaceV718 planning_owners;
 planning_owners.structures00={0};
 planning_owners.unseen03={1};
 run.entities[0].actor.position={q12_to_float(point.x-20000),q12_to_float(point.y),0};
 const auto choose=[&](){return simulation_lab_extended_placement(run,field,owners,planning_owners,turret,sites,1);};
 const auto rng=run.rng_state;const auto before=scratch->state;
 check(bool(choose()),"extended planner failed real mode1 placement with unseen hostile");
 check(run.rng_state==rng&&run.retail_scratch->state==before&&run.metrics.placement_connectivity_tests==0,"extended search consumed RNG or rebuilt connectivity");
 run.ai_primary_anchor_cooldown[1]=0;check(!choose(),"extended planner ignored zero cooldown");run.ai_primary_anchor_cooldown[1]=150;
 // A nearby unarmed own structure is excluded by native +0x100, not by being a building.
 owners[0].support.position={point.x-6000,point.y};
 run.entities[0].actor.position.x=q12_to_float(point.x-6000);
 check(bool(choose()),"unarmed structure incorrectly counted as active");
 SimulationLabWeaponSeed gun;gun.profile.reload_ticks=1;gun.profile.yaw_model_node_index=0;gun.shot.target_impact_damage_q12[0]=1;
 SimulationLabModelComponentState component;component.source_model_node_index=0;component.integrity=100;
 run.entities[0].model_components={component};run.entities[0].root_model_component_index=0;run.entities[0].combat_weapons={gun};
 check(!choose(),"armed primary structure within2units failed native active-distance gate");
 run.entities[0].combat_weapons.clear();owners.push_back({{{point.x,point.y},2,11,0,0,true},1000,{}});
 check(!choose(),"extended planner bypassed real placement collision");
 std::cout<<"integrated cooldown/real placement/class0 active gate/no RNG/no flood controls PASS\n";
}
int main(){try{differential();integration();std::cout<<"V628_EXTENDED_PLACEMENT PASS\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
