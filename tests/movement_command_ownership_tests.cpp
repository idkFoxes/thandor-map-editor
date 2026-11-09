#include "thandor/simulation_lab.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main()try{
 auto field=FieldGrid::create(40,40,0,0);const auto& cell=field.at(20,20);
 const Vec3f start{q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
 SimulationLabSeedEntity unit;unit.army_asset_id=1;unit.faction_index=1;unit.runtime_model_class_id=1;
 unit.actor.position=start;unit.motion.speed_q12_per_tick=0x200;unit.motion.yaw_max_velocity_turn16=0x1000;unit.motion.yaw_acceleration_turn16=0x1000;
 auto enemy=unit;enemy.army_asset_id=330;enemy.faction_index=2;enemy.runtime_model_class_id=14;enemy.building=true;enemy.motion={};enemy.actor.position.x+=12;
 std::array seeds{unit,enemy};std::array<uint32_t,AiKnowledgeData::kParameterCount>ki{};
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=128;
 for(bool human:{false,true}){
  SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=scenario.assignable_faction_count=2;
  scenario.human_faction_mask=human?2U:0U;scenario.ai_planning_enabled=false;scenario.ai_planner_dispatch_mask=0;
  SimulationLabExperiment lab;lab.reset(config,ki,seeds,{},{},{},{},{},scenario);
  lab.step(field,32);auto& e=lab.runs()[0].entities[0];
  check(e.actor.position.x==start.x&&e.actor.position.y==start.y,"idle native unit invented a chase before an AI dispatch");
  check(!e.actor.move_target&&!e.target_entity_index&&!e.military_ai_source_dispatch,"idle native unit acquired a synthetic command");
  // An explicit route is consumed even without an AI dispatch, for humans and undispatched AI.
  e.actor.move_target=Vec3f{start.x+6,start.y,0};lab.step(field,16);
  check(e.actor.position.x>start.x,"command-only movement ignored an explicit route");
 }
 // Keep the documented nearest-enemy behavior of the compact non-level API fixtures.
 SimulationLabExperiment compact;compact.reset(config,ki,seeds);compact.step(field,16);
 check(compact.runs()[0].entities[0].actor.position.x>start.x,"non-level compact movement compatibility changed");
 std::cout<<"COMMAND_OWNERSHIP PASS human_idle=1 undispatched_idle=1 explicit_routes=2 compact_fallback=1\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
