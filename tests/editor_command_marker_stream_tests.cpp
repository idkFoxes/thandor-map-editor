// Include the public header first: it owns its direct algorithm/array dependencies.
#include "thandor/simulation_command_markers.hpp"
#include "core_command_marker_reference.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Left,class Right> void equal(const Left& a,const Right& b){
 check(a.size()==b.size(),"marker output size changed");
 for(size_t i=0;i<a.size();++i)check(a[i].effect_id==b[i].effect_id&&a[i].faction_index==b[i].faction_index&&
  a[i].owner_entity_index==b[i].owner_entity_index&&a[i].point.x==b[i].point.x&&
  a[i].point.y==b[i].point.y&&a[i].point.z==b[i].point.z,"marker order/position/owner/tint changed");
}
SimulationLabRun fixture(size_t count,uint32_t seed){
 SimulationLabRun run;run.entities.resize(count+1);
 auto next=[&]{seed=seed*1664525U+1013904223U;return seed;};
 for(size_t i=0;i<count;++i){auto& e=run.entities[i];const auto bits=next();
  e.faction_index=1+bits%7;e.runtime_model_class_id=(bits&1)?13:1;
  e.actor.position={float(int(next()%30)-15),float(int(next()%30)-15),0};
  e.construction_complete=bits&2;e.root_model_terminal=(bits&1024)!=0;e.runtime_removed=(bits&2048)!=0;
  const Vec3f target{float(next()%30+20),float(next()%30+20),float(next()%3)};
  if(bits&4)e.actor.move_target=target;
  if(bits&8)e.actor.move_waypoint=(bits&16)?target:e.actor.position;
  if(bits&32)e.target_entity_index=count; // always-live target for predecessor equivalence
  if(bits&64)e.production_drive_target_world=target;
  if(bits&128)e.production_exit_world=Vec3f{target.x+1,target.y+1,0};
  if(bits&256){e.pioneer_movement_initialized=true;e.pioneer_movement.flags=3;e.pioneer_movement.stored.x=20*4096;e.pioneer_movement.stored.y=21*4096;}
 }
 run.entities.back().actor.position={32,40,0};run.entities.back().construction_complete=true;
 return run;
}
void equivalence(){
 for(unsigned seed=1;seed<=32;++seed){const auto run=fixture(256,seed);
  const auto old=marker_reference_v632::simulation_lab_command_markers(run);
  const auto actual=simulation_lab_command_markers(run);equal(actual,old);
  std::vector<SimulationCommandMarker> filtered;
  SimulationCommandMarkerDiagnostics stats;
  simulation_lab_visit_command_markers(run,[](size_t i,const auto&){return i%3!=0;},
   [&](const auto& marker){filtered.push_back(marker);return filtered.size()<17;},&stats);
  std::vector<marker_reference_v632::SimulationCommandMarker> expected;
  for(const auto& m:old)if(m.owner_entity_index%3!=0){expected.push_back(m);if(expected.size()==17)break;}
  equal(filtered,expected);check(stats.markers_visited==filtered.size(),"visited count mismatch");
 }
 std::cout<<"SEMANTICS predecessor_equivalent_fixtures=32 filtered_prefix_limit=17 PASS\n";
}
void lifecycle(){
 SimulationLabRun run;run.entities.resize(2);auto& unit=run.entities[0];auto& target=run.entities[1];
 unit.target_entity_index=1;target.actor.position={20,30,0};
 check(simulation_lab_command_markers(run).size()==1,"stationary attack marker missing");
 target.runtime_removed=true;check(simulation_lab_command_markers(run).empty(),"released target marker survived");
 target.runtime_removed=false;target.root_model_terminal=true;
 check(simulation_lab_command_markers(run).empty(),"terminal target marker survived");
 unit.target_entity_index.reset();unit.actor.move_target=Vec3f{10,10,0};unit.actor.move_waypoint=unit.actor.move_target;
 auto markers=simulation_lab_command_markers(run);check(markers.size()==1,"same-owner final/waypoint duplicate");
 unit.actor.position={9,9,0};unit.actor.move_waypoint.reset();
 check(simulation_lab_command_markers(run).empty(),"inclusive arrival marker retained");
 unit.runtime_model_class_id=13;unit.production_exit_world=unit.actor.position;
 check(simulation_lab_command_markers(run).size()==1,"standing factory rally marker cleared at arrival");
 unit.root_model_terminal=true;check(simulation_lab_command_markers(run).empty(),"terminal owner marker survived");
 unit.root_model_terminal=false;unit.runtime_removed=true;
 check(simulation_lab_command_markers(run).empty(),"released owner marker survived");
 // A callback may exhaust its budget on the first of three intents for an owner.
 unit.runtime_removed=false;unit.runtime_model_class_id=1;unit.construction_complete=false;
 unit.actor.position={0,0,0};unit.actor.move_waypoint=Vec3f{5,5,0};
 SimulationCommandMarkerDiagnostics stats;size_t calls=0;
 simulation_lab_visit_command_markers(run,[](size_t,const auto&){return true;},
  [&](const auto&){++calls;return false;},&stats);
 check(calls==1&&stats.candidates==1&&stats.owners_examined==1&&stats.stopped_early,
  "visitor continued after mid-owner stop");
 std::cout<<"LIFECYCLE target/owner release, arrival, factory rally and mid-owner stop PASS\n";
}
void scale(size_t count){
 SimulationLabRun run;run.entities.resize(count);
 for(size_t i=0;i<count;++i){auto& e=run.entities[i];e.faction_index=1+i%7;
  e.actor.move_target=Vec3f{float(10+i),20,0};e.actor.move_waypoint=e.actor.move_target;}
 marker_reference_v632::duplicate_comparisons=0;const auto before=std::chrono::steady_clock::now();
 const auto old=marker_reference_v632::simulation_lab_command_markers(run);
 const auto old_us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-before).count();
 const auto start=std::chrono::steady_clock::now();std::vector<SimulationCommandMarker> actual;
 SimulationCommandMarkerDiagnostics stats;
 simulation_lab_visit_command_markers(run,[](size_t,const auto&){return true;},
  [&](const auto& m){actual.push_back(m);return true;},&stats);
 const auto new_us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
 equal(actual,old);check(stats.duplicate_comparisons==count&&marker_reference_v632::duplicate_comparisons==count*count,
  "marker duplicate work is no longer bounded linearly");
 check(stats.duplicates==count,"duplicate counter mismatch");
 // Same first512 visible candidates as build-all/filter/render, without scanning/allocating the rest.
 stats={};actual.clear();
 simulation_lab_visit_command_markers(run,[](size_t i,const auto&){return i%2!=0;},
  [&](const auto& m){actual.push_back(m);return actual.size()<512;},&stats);
 check(actual.size()==512&&stats.owners_examined==1024&&stats.markers_visited==512&&stats.stopped_early,
  "visible marker cap still visits trailing owners");
 for(size_t i=0;i<actual.size();++i)check(actual[i].owner_entity_index==2*i+1,"fog filter changed capped marker order");
 // Missing/offscreen effects do not consume the render budget. Reject all and keep scanning.
 stats={};simulation_lab_visit_command_markers(run,[](size_t,const auto&){return true;},
  [](const auto&){return true;},&stats);
 check(stats.owners_examined==count&&!stats.stopped_early,"unrendered effects incorrectly consume budget");
 std::cout<<"MARKER_PROBE owners="<<count<<" old_duplicate_checks="<<marker_reference_v632::duplicate_comparisons
  <<" new_duplicate_checks="<<count<<" old_us="<<old_us<<" new_us="<<new_us
  <<" capped_visible_markers=512 capped_owners_examined=1024\n";
}
int main()try{equivalence();lifecycle();scale(4096);scale(8192);
 std::cout<<"V633_COMMAND_MARKER_STREAM PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
