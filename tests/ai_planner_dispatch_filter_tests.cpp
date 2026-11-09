#include "thandor/simulation_lab.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
static void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static SimulationLabExperiment make(std::optional<uint32_t> mask){
 SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=3;
 scenario.human_faction_mask=2U;scenario.ai_planner_dispatch_mask=mask;scenario.game_speed_q8=307;
 SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=120;cfg.sweep_mode=SimulationLabSweepMode::Fixed;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};SimulationLabExperiment lab;
 lab.reset(cfg,ki,{}, {}, {}, {}, {}, {},scenario);return lab;
}
int main()try {
 const auto field=FieldGrid::create(16,16,0,0);
 auto normal=make(std::nullopt),forensic=make(4U),zero=make(0U);
 normal.step(field,120);forensic.step(field,120);zero.step(field,120);
 const auto& a=normal.runs()[0];const auto& b=forensic.runs()[0];const auto& c=zero.runs()[0];
 check(a.ai_controlled_faction_mask==12&&b.ai_controlled_faction_mask==12,"forensic input changed AI ownership");
 check(a.ai_workspace_diagnostics[2].last_planning_tick==86&&a.ai_workspace_diagnostics[3].last_planning_tick==94,"ordinary AI dispatch was changed");
 check(b.ai_workspace_diagnostics[2].last_planning_tick==86&&b.ai_workspace_diagnostics[3].last_planning_tick==0,"forensic planner filter did not isolate dispatch");
 check(c.metrics.planning_cycles==0,"explicit zero mask was treated as absent");
 check(b.faction_economy[3].terrain_contribution_scale_q8==307&&a.faction_economy[3].terrain_contribution_scale_q8==307,"planner filtering changed native income scaling");
 std::cout<<"V657_DISPATCH_FILTER PASS ordinary, explicit forensic and zero masks; AI ownership/income retained\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
