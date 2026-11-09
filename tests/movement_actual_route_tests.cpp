#include "thandor/simulation_lab_geometry.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main()try{
 auto field=FieldGrid::create(40,40,0,0);const Vec3f start{q12_to_float(51430),q12_to_float(-30086),0},goal{q12_to_float(63718),q12_to_float(-30086),0};
 const V528PathRuntimeBlocker blocker{51680,-30336,180};
 SimulationLabSeedEntity s;s.army_asset_id=1;s.faction_index=1;s.runtime_model_class_id=1;s.actor.position=start;
 s.root_model_maximum_integrity=1000;s.has_traversal_profile=true;s.traversal_low_band_index=0;s.traversal_high_band_index=4;
 s.motion.speed_q12_per_tick=64;s.motion.collision_radius_q12=120;s.motion.yaw_max_velocity_turn16=0x4000;s.motion.yaw_acceleration_turn16=0x4000;
 SimulationLabConfig c;c.run_count=1;c.duration_ticks=512;c.sweep_mode=SimulationLabSweepMode::Fixed;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=scenario.assignable_faction_count=1;scenario.static_navigation_blockers={blocker};
 SimulationLabExperiment lab;lab.reset(c,ki,std::span(&s,1),{},{},{},{},{},scenario);auto& r=lab.runs()[0];auto& e=r.entities[0];e.military_ai_source_dispatch=true;e.actor.move_target=goal;
 for(uint32_t i=0;i<512;++i){const auto before=e.actor.position;lab.step(field,1);
  check(!simulation_lab_step_hits_blocker(before,e.actor.position,120,blocker),"route correction crossed a physical blocker");}
 std::cout<<"route end="<<e.actor.position.x<<","<<e.actor.position.y<<" blocks="<<r.metrics.navigation_actual_step_blocks<<" solves="<<r.metrics.navigation_routed_solves<<" target="<<bool(e.actor.move_target)<<'\n';
 check(!e.actor.move_target,"snapped direct route repeatedly stalled against actual swept blocker");
 check(e.actor.position.x>start.x+1,"unit failed to leave blocked initial approach");
 std::cout<<"V620_ACTUAL_ROUTE PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
