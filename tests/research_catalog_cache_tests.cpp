#include "research_fixture.hpp"
#include <iostream>
using namespace v638_fixture;
int main(int argc,char** argv)try{
 const bool digest=argc>1&&std::string(argv[1])=="--digest-only";const auto field=FieldGrid::create(59,59,0,0);SimulationLabExperiment lab;
 for(uint32_t offset:{1000U,2000U}){
  Data data(offset,4);data.reset(lab);lab.step(field,74);
  for(const auto& run:lab.runs()){
   const auto& t=run.ai_research_trace[1];check(t.admitted_candidates==32&&t.best_technology_id==130,"dense research result changed");
   std::cout<<"DENSE offset="<<offset<<" run="<<run.run_index<<" best="<<t.best_technology_id<<" score="<<t.best_score<<" queued="<<run.metrics.research_jobs_queued<<" count="<<t.candidate_count;
   for(uint32_t i=0;i<t.candidate_count;++i)std::cout<<' '<<t.candidates[i].technology_id<<':'<<t.candidates[i].score<<':'<<t.candidates[i].compatible_arm_matches<<':'<<t.candidates[i].best_potential_arm_score;
   std::cout<<'\n';
   if(!digest){check(t.compatibility_index_entries_visited==32,"index rescanned irrelevant assets");check(t.potential_seed_resolutions==2&&t.potential_score_evaluations==2&&t.potential_score_cache_hits==30,"potential score cache missed shared upgrades");}
  }
  auto held=lab.snapshot_copy();const auto previous=held.runs()[0].ai_research_trace[1].best_score;lab.step(field,4);check(held.runs()[0].ai_research_trace[1].best_score==previous,"held snapshot changed");
  if(!digest)for(const auto& run:lab.runs()){const auto& t=run.ai_research_trace[1];check(t.last_rebuild_tick==78&&t.potential_score_evaluations==2,"score cache leaked across planning passes");}
 }
 // A definition in both root and child lists contributes once, even while the
 // child gate is locked; the native root comparison wins first.
 Data aliases;auto& a=aliases.products[0];a.ai_linked_model_definition_ids[1][0]=a.ai_linked_model_definition_ids[0][0];a.ai_root_first_variant_required_technology_bit=206;aliases.reset(lab);lab.step(field,74);
 const auto& t=lab.runs()[0].ai_research_trace[1];
 for(uint32_t i=0;i<t.candidate_count;++i)if(t.candidates[i].technology_id==99)check(t.candidates[i].compatible_arm_matches==1&&t.candidates[i].locked_root_child_matches==0,"root/child duplicate counted twice or blocked");
 // A new experiment copy owns independent immutable lookup indexes.
 auto copied=lab;copied.clear();aliases.reset(copied);copied.step(field,74);check(copied.runs()[0].ai_research_trace[1].best_score==t.best_score,"clear/reset kept stale indexes");
 if(!digest)std::cout<<"V638_RESEARCH_CACHE PASS dense_candidates=32 catalog_assets=130 indexed_visits=32 score_evaluations=2 score_cache_hits=30 runs=4 reset_catalogs=2\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
