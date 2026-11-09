#include "research_fixture.hpp"
#include "../src/editor/simulation_lab_diagnostic_writer.hpp"
#include <fstream>
#include <iostream>
using namespace v638_fixture;
using Decision=SimulationLabRun::PurchaseDecision;
namespace {
void replay(SimulationLabRun& run,uint32_t budget){run.faction_economy[1].xenite_current_q4=budget;run.ai_candidate_cache_count[1]=3;run.ai_candidate_cache_reuse_state[1]=16;run.ai_primary_anchor_cooldown[1]=run.ai_anchor_cooldown0[1]=0;}
// Native local planning subtraction wraps before the affordability early return.
void row(const SimulationLabRun::PurchaseTrace& p,size_t rank,Decision d,uint32_t before,uint32_t after){check(p.tick==78&&p.cached&&p.recorded_count==3,"cached pass trace missing");check(p.candidates[rank].decision==d&&p.candidates[rank].budget_before_q4==before&&p.candidates[rank].budget_after_q4==after,"purchase reason/budget incorrect");}
}
int main()try{
 const auto field=FieldGrid::create(59,59,0,0);SimulationLabExperiment lab;
 {Data d;d.reset(lab);lab.step(field,74);auto& r=lab.runs()[0];replay(r,200);r.ai_candidate_cache[1][0]={351,16001,1};r.ai_candidate_cache[1][1]={99,14002,2};r.ai_candidate_cache[1][2]={352,12001,1};lab.step(field,4);
  const auto& p=r.ai_purchase_trace[1];row(p,0,Decision::Unaffordable,200,uint32_t(200U-40000U));row(p,1,Decision::NotReached,0,0);check(p.candidates[0].cost_q4==40000&&r.faction_economy[1].xenite_current_q4==200,"unaffordable purchase spent currency");}
 {Data d;d.products[0].army_runtime_flags14_raw=3;d.products[0].xenite_cost_q4=100;d.technologies[0].xenite_cost_q4=60;auto duplicate=d.products[0];duplicate.xenite_cost_q4=1;duplicate.army_runtime_flags14_raw=17;d.products.push_back(duplicate);auto t=d.technologies[0];t.xenite_cost_q4=1;d.technologies.push_back(t);d.reset(lab);lab.step(field,74);auto& r=lab.runs()[0];replay(r,150);r.ai_candidate_cache[1][0]={351,16001,1};r.ai_candidate_cache[1][1]={99,14002,2};r.ai_candidate_cache[1][2]={352,12001,1};lab.step(field,4);
  row(r.ai_purchase_trace[1],0,Decision::NoProducer,150,50);row(r.ai_purchase_trace[1],1,Decision::Unaffordable,50,uint32_t(50U-60U));check(r.faction_economy[1].xenite_current_q4==150,"planning reservation spent real currency");check(r.ai_purchase_trace[1].candidates[1].cost_q4==60,"duplicate technology lookup stopped using first record");}
 {Data d;d.products[0].xenite_cost_q4=100;d.products[1].xenite_cost_q4=150;d.technologies[0].xenite_cost_q4=60;d.reset(lab);lab.step(field,74);auto& r=lab.runs()[0];replay(r,300);r.ai_candidate_cache[1][0]={351,16001,1};r.ai_candidate_cache[1][1]={352,14001,1};r.ai_candidate_cache[1][2]={99,12002,2};lab.step(field,4);
  row(r.ai_purchase_trace[1],0,Decision::ArmyQueued,300,200);row(r.ai_purchase_trace[1],1,Decision::ProducerClassUsed,200,50);row(r.ai_purchase_trace[1],2,Decision::Unaffordable,50,uint32_t(50U-60U));}
 {Data d;d.technologies[0].xenite_cost_q4=60;d.reset(lab);lab.step(field,74);auto& r=lab.runs()[0];replay(r,150);r.ai_candidate_cache[1][0]={9999,16001,1};r.ai_candidate_cache[1][1]={99,14002,2};r.ai_candidate_cache[1][2]={352,12001,1};lab.step(field,4);row(r.ai_purchase_trace[1],0,Decision::MissingAsset,150,150);row(r.ai_purchase_trace[1],1,Decision::ResearchQueued,150,90);}
 // Send an actual decision trace through the asynchronous immutable formatter.
 const auto path=std::filesystem::temp_directory_path()/("v638-purchase-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".log");
 {editor::SimulationLabDiagnosticWriter writer(path,"V638\n",field,{});editor::SimulationLabDiagnosticSample s;s.snapshot=std::make_shared<const SimulationLabExperiment>(lab.snapshot_copy());s.active_factions=1;s.duration_ticks=256;writer.append(s,true);writer.close("complete");}
 std::ifstream in(path);std::string text((std::istreambuf_iterator<char>(in)),{});check(text.find("PURCHASE_PASS")!=std::string::npos&&text.find("decision=missing_asset")!=std::string::npos&&text.find("decision=research_queued")!=std::string::npos&&text.find("research_score_cache_hits=")!=std::string::npos&&text.find("runtime_callback_batch=1")!=std::string::npos,"asynchronous decision diagnostics missing");std::filesystem::remove(path);
 std::cout<<"V638_PURCHASE_TRACE PASS affordability, stop order, local reservation, producer mask, duplicate IDs, research dispatch, missing asset, immutable async log\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
