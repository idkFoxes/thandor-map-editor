#include "thandor/simulation_lab_site_placement.hpp"
#include "thandor/retail_placement.hpp"
#include "thandor/simulation_placement.hpp"
#include "core_retail_inputs_reference.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
uint32_t next(uint32_t& s){s=s*1664525U+1013904223U;return s;}
void influence(){
 auto field=FieldGrid::create(32,32,0,0);uint32_t rng=625;
 for(int test=0;test<288;++test){
  SimulationLabRetailScratchImage scratch;scratch.width=128;scratch.height=128;scratch.state.resize(128*128);
  std::vector<native_v625::GridScratchCell> ref(scratch.state.size());
  for(size_t i=0;i<ref.size();++i){auto x=i%128,y=i/128;uint32_t v=next(rng)&0x00FFFFFE;
   if(x<4||y<4||x>123||y>123||(test%3==0&&x==72))v|=0x80000000;
   ref[i]={v,0x12345678};scratch.state[i]=v;
  }
  const auto& c=field.at(13+next(rng)%5,13+next(rng)%5);MovementPoint p{c.world_x_q12+int32_t(next(rng)%1024),c.world_y_q12-int32_t(next(rng)%1024)};
  const uint32_t cls=uint32_t(test)%24;const int32_t radius=int32_t(next(rng)%6000);
  native_v625::g_GridScratchWidth=128;native_v625::g_GridScratchHeight=128;native_v625::g_GridScratchPrimary=ref.data();
  const bool low=cls==4||(cls>=10&&cls<=16)||cls==20||cls==22;
  const bool high=cls<=3||(cls>=17&&cls<=19)||cls==23;
  if(radius!=0&&(low||high))native_v625::GridInfluence_WalkDistanceBandsAroundWorldPoint(radius,p.y,p.x,
    low?native_v625::GridInfluence_SetLowDistanceBandsDiagonalPositive:native_v625::GridInfluence_SetHighDistanceBandsDiagonalPositive,
    low?native_v625::GridInfluence_SetLowDistanceBandsDiagonalNegative:native_v625::GridInfluence_SetHighDistanceBandsDiagonalNegative);
  retail_placement_stamp_influence(scratch,p,radius,cls);
  for(size_t i=0;i<ref.size();++i){if(scratch.state[i]!=ref[i].stateMask){std::cerr<<"test="<<test<<" cls="<<cls<<" radius="<<radius<<" p="<<p.x<<","<<p.y<<" cell="<<i<<" actual="<<std::hex<<scratch.state[i]<<" expected="<<ref[i].stateMask<<std::dec<<"\n";throw std::runtime_error("influence differs from unchanged native oracle");}check(ref[i].pathCost==0x12345678,"oracle changed pathCost");}
 }
 std::cout<<"influence cases=288 all24 classes complete scratch masks identical to native source oracle\n";
}
void sites(){
 auto field=FieldGrid::create(24,24,0,0);SimulationLabRetailScratchImage scratch;scratch.width=96;scratch.height=96;
 scratch.state.resize(96*96);scratch.field_occupancy_masks.resize(field.cells().size());
 std::vector<native_v625::FieldGridCell> cells(field.cells().size()+2);
 const size_t padding=96*8;std::vector<native_v625::GridScratchCell> ref(scratch.state.size()+padding*2);
 uint32_t rng=625;size_t count=0;
 for(unsigned pass=0;pass<18;++pass){
  for(size_t i=0;i<field.cells().size();++i){uint32_t flags=pass%3?0:((next(rng)%20==0)?0x800U:0U);uint64_t occ=uint64_t((next(rng)%4)?0x81U:0x10U)<<8;
   field.cells()[i].flags_and_material=flags;scratch.field_occupancy_masks[i]=occ;cells[i]={flags,occ};}
  for(auto& c:ref)c={0x80000000U,0};
  for(size_t i=0;i<scratch.state.size();++i){uint32_t v=pass<2?0U:((next(rng)%13==0)?(1U<<(8+next(rng)%24)):0U);scratch.state[i]=v;ref[padding+i].stateMask=v;}
  std::array<uint32_t,4> active{0x90000100U,0x82000200U,~0U,~0U};
  std::copy(active.begin(),active.end(),native_v625::g_AiActiveGridMaskClasses);
  for(uint32_t y=1;y+1<24;++y)for(uint32_t x=0;x<24;++x){
   auto a=retail_ai_site_admission(field,scratch,1,x,y,active);
   uint32_t actual=uint32_t(a.general)|(uint32_t(a.flagged)<<1)|(uint32_t(a.resource)<<2)|(uint32_t(a.base)<<3)|(uint32_t(a.large)<<4);
   native_v625::admitted=0;native_v625::AiPlanningRebuild_ScanSiteCell(1,cells.data()+(y-1)*24+x,24,ref.data()+padding+(4*(y-1)+2)*96+4*x+2,96*3,96*24);
   check(actual==native_v625::admitted,"site admission differs from native ScanSiteCell");++count;
  }
 }
 // Regression: low band0 through5 reject base09; band6/7 reject only large10.
 for(auto& c:field.cells())c.flags_and_material=0;
 std::fill(scratch.field_occupancy_masks.begin(),scratch.field_occupancy_masks.end(),uint64_t(0x81)<<8);
 std::fill(scratch.state.begin(),scratch.state.end(),0U);std::array<uint32_t,4> active{~0U,~0U,~0U,~0U};
 auto center=size_t(4*(10-1)+2+3)*96+4*10+2;
 for(uint32_t bit=8;bit<16;++bit){scratch.state[center]=1U<<bit;auto a=retail_ai_site_admission(field,scratch,1,10,10,active);check(a.base==(bit>=14),"workspace09 low-band boundary wrong");check(!a.large,"workspace10 admitted low band");}
 std::cout<<"site predicate cases="<<count<<" all five family masks identical, all columns and bounded edges included\n";
}
void owners(){
 auto field=FieldGrid::create(32,32,0,0);SimulationLabRuntimeCatalog catalog;ModelVisualRecord root;root.runtime_class_id=11;root.simulation.movement_collision_radius_q12=1000;
 catalog.definitions.emplace(1,root);root.runtime_class_id=13;root.simulation.placement_contact_kind=1;catalog.definitions.emplace(2,root);
 SimulationLabRun run;SimulationLabEntity entity;entity.faction_index=1;entity.root_model_terminal=true;entity.actor.position={10,-10,0};entity.root_model_component_index=0;
 SimulationLabModelComponentState a;a.model_definition_id=1;a.source_model_node_index=0;entity.model_components.push_back(a);
 a.model_definition_id=2;a.source_model_node_index=1;a.extraction_model_world_offset_at_seed=Vec3f{4,0,0};entity.model_components.push_back(a);run.entities.push_back(entity);
 SimulationLabStaticPlacementOwner neutral;neutral.model_definition_id=1;neutral.pose.position={20,-20,0};
 auto list=simulation_lab_site_owners(run,field,catalog,std::span(&neutral,1));check(list.size()==2,"root/static owner list included an attached child");check(list[0].contact_kind==0,"root contact kind lost");
 run.entities[0].model_components[1].runtime_removed=true;list=simulation_lab_site_owners(run,field,catalog,std::span(&neutral,1));check(list.size()==2,"removing an unlinked child changed the owner list");
 run.entities[0].model_components[0].runtime_removed=true;list=simulation_lab_site_owners(run,field,catalog,std::span(&neutral,1));check(list.size()==1,"removed root still owner");
 std::cout<<"root-only terminal/static-neutral owner and contact-kind controls PASS\n";
}
void integrated_static_influence(){
 auto field=FieldGrid::create(32,32,0,0);auto catalog=std::make_shared<SimulationLabRuntimeCatalog>();
 ModelVisualRecord model;model.runtime_class_id=11;model.simulation.movement_collision_radius_q12=2000;catalog->definitions.emplace(1,model);
 const auto& cell=field.at(16,16);SimulationLabStaticPlacementOwner owner;owner.model_definition_id=1;
 owner.pose.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
 SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.runtime_catalog=catalog;scenario.static_placement_owners.push_back(owner);
 SimulationLabConfig config;config.run_count=1;std::array<uint32_t,AiKnowledgeData::kParameterCount> params{};SimulationLabExperiment experiment;
 experiment.reset(config,params,{},{},{},{},{},{},scenario);experiment.step(field,1);
 const auto& run=experiment.runs()[0];check(bool(run.retail_scratch),"native scratch not initialized");
 auto index=retail_placement_scratch_index(*run.retail_scratch,{cell.world_x_q12,cell.world_y_q12});
 check(index&&((run.retail_scratch->state[*index]&0xFF00U)!=0),"integrated scratch rebuild omitted static model influence");
 experiment.step(field,4);check((experiment.runs()[0].retail_scratch->state[*index]&0xFF00U)!=0,"case4 refresh erased static model influence");
 std::cout<<"integrated startup/case4 static-model influence PASS\n";
}
void live_exit_boundary(){
 auto field=FieldGrid::create(32,32,0,0);const auto& cell=field.at(16,16);
 SimulationLabEntity factory;factory.runtime_model_class_id=13;factory.production_exit_local=Vec3f{};
 factory.actor.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
 Vec3f query=factory.actor.position;query.x+=q12_to_float(3073);query.y+=q12_to_float(1);
 check(simulation_placement_factory_anchor_overlap(query,1,factory,field),"live factory exit boundary did not use floor(length)");
 query.x+=q12_to_float(1);check(!simulation_placement_factory_anchor_overlap(query,1,factory,field),"live exit boundary too large");
 std::cout<<"live factory exit integer-distance boundary PASS\n";
}
int main(){try{influence();sites();owners();integrated_static_influence();live_exit_boundary();std::cout<<"V625_PLACEMENT_INPUTS PASS\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
