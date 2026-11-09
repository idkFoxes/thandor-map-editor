#include "thandor/simulation_lab.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool b,const char* why){if(!b)throw std::runtime_error(why);}
int main()try{
 const auto field=FieldGrid::create(60,60,0,0);const auto& c=field.at(25,25);
 SimulationLabSeedEntity unit;unit.army_asset_id=100;unit.faction_index=1;unit.runtime_model_class_id=2;
 unit.actor.position={q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),0};unit.root_model_maximum_integrity=1000;
 unit.fog_uncover_radius_q12=20*4096;unit.fog_reference_height_offset_q12=4*4096;
 auto other=unit;other.source_placement_index=1;other.actor.position.x+=1;
 auto old=unit;old.source_placement_index=2;old.faction_index=2;old.army_asset_id=331;old.runtime_model_class_id=11;old.building=true;old.actor.position.x+=3;
 auto next=old;next.source_placement_index=3;next.army_asset_id=332;next.actor.position.x+=2;
 auto point=unit;point.source_placement_index=4;point.runtime_model_class_id=11;point.building=true;
 const std::array seeds{unit,other,old,next,point};
 SimulationLabProductionAsset product;product.army_asset_id=100;product.entity_seed=unit;product.ai_definition_class_value78=1;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};ki[0x43]=1000000;ki[0x47]=4096;
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=64;config.sweep_mode=SimulationLabSweepMode::Fixed;
 SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=scenario.assignable_faction_count=2;scenario.simulate_all_active_factions_as_ai=true;
 SimulationLabExperiment lab;lab.reset(config,ki,seeds,{},std::span(&product,1),{},{},{},scenario);auto& run=lab.runs()[0];
 for(size_t i=0;i<2;++i){auto& e=run.entities[i];e.target_entity_index=2;e.military_ai_group_assigned=true;e.military_ai_command_kind=3;e.military_ai_busy_cooldown=8;e.actor.move_target=old.actor.position;e.actor.move_waypoint=old.actor.position;e.navigation_target_cell_valid=e.navigation_direct_route_valid=true;}
 const Vec3f destination{point.actor.position.x-3,point.actor.position.y,0};run.entities[4].actor.move_target=destination;
 auto& dead=run.entities[2];dead.model_components[dead.root_model_component_index].destruction_complete_tick=1;
 lab.step(field,1);check(dead.runtime_removed,"fixture did not release old target");
 for(size_t i=0;i<2;++i){const auto& e=run.entities[i];check(!e.target_entity_index&&e.military_ai_group_assigned,"released target failed to preserve native group assignment");check(e.actor.move_target&&e.actor.move_waypoint&&e.navigation_target_cell_valid&&e.navigation_direct_route_valid,"reference release erased the native retained movement segment");}
 check(run.entities[4].actor.move_target&&run.entities[4].actor.move_target->x==destination.x,"target release erased unrelated position command");
 // Later callbacks may choose another target, but must never resurrect the released owner.
 lab.step(field,13);
 for(size_t i=0;i<2;++i)check(run.entities[i].target_entity_index!=2,
     "released target was reacquired by a later callback");
 std::cout<<"V618_TARGET_RELEASE PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
