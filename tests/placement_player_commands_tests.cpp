#include "thandor/simulation_lab_site_placement.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Fixture {
 FieldGrid field=FieldGrid::create(48,48,0,0);
 SimulationLabExperiment lab;
 MovementPoint site;
 Fixture(){
  const auto& c=field.at(24,24);site={c.world_x_q12,c.world_y_q12};
  for(auto& cell:field.cells())cell.flags_and_material|=0x800U;
  auto catalog=std::make_shared<SimulationLabRuntimeCatalog>();
  auto& root=catalog->definitions[1];root.runtime_class_id=11;
  root.simulation.class_specific_19c_raw=100000;root.simulation.movement_collision_radius_q12=16;
  auto& mine=catalog->definitions[2];mine.runtime_class_id=14;
  mine.simulation.movement_collision_radius_q12=16;
  auto& group=catalog->armies[330].model_variant_groups.emplace_back();
  group.parent_group_index=UINT32_MAX;group.variants.resize(1);group.variants[0].model_definition_id=2;
  SimulationLabSeedEntity support;support.army_asset_id=300;support.faction_index=1;
  support.runtime_model_class_id=11;support.building=true;support.root_model_maximum_integrity=1000;
  support.runtime_class_specific_19c_raw=100000;
  support.actor.position={q12_to_float(site.x-20000),q12_to_float(site.y),0};
  SimulationLabModelComponentSeed component;component.model_definition_id=1;
  component.runtime_class_id=11;component.maximum_integrity=1000;support.model_components={component};
  SimulationLabProductionAsset product;product.army_asset_id=330;product.placement_effect_id=153;
  product.entity_seed.army_asset_id=330;product.entity_seed.runtime_model_class_id=14;
  product.entity_seed.building=true;product.entity_seed.root_model_maximum_integrity=15000;
  component.model_definition_id=2;component.runtime_class_id=14;component.maximum_integrity=15000;
  product.entity_seed.model_components={component};
  SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=2;
  scenario.assignable_faction_count=2;scenario.human_faction_mask=2;scenario.ai_planning_enabled=false;
  scenario.runtime_catalog=catalog;
  SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=256;
  std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
  std::array<SimulationLabFactionEconomySeed,8> economy{};
  economy[1].xenite_current_q4=12345;economy[1].baseline_xenite_storage_limit_q4=100000;
  lab.reset(cfg,ki,std::span(&support,1),economy,std::span(&product,1),{},{},{},scenario);
  lab.step(field);
  // Controlled visibility image isolates command admission from the independently
  // tested owner-influence writer; it is not used by normal runs or replay tooling.
  auto scratch=std::make_shared<SimulationLabRetailScratchImage>(*run().retail_scratch);
  scratch->field_occupancy_masks.assign(field.cells().size(),uint64_t{1}<<8U);run().retail_scratch=scratch;
 }
 SimulationLabRun& run(){return lab.runs()[0];}
 void input(SimulationLabPlayerCommandKind kind,uint32_t player=0,uint32_t asset=0,
            MovementPoint p={},uint32_t faction=1){
  SimulationLabPlayerCommand c;c.kind=kind;c.player_index=player;c.faction_index=faction;
  c.army_asset_id=asset;c.world_x_q12=p.x;c.world_y_q12=p.y;c.heading_turn16=0x2345;
  check(lab.queue_player_command(0,c),"valid command not queued");
 }
 void step(){lab.step(field);}
};
void stock(){
 Fixture f;auto& r=f.run();r.pending_asset_requests={{330,1,0,0},{330,2,0,0},{330,1,0,0}};
 f.input(SimulationLabPlayerCommandKind::TakeStock,0,330);f.step();
 check(r.pending_asset_requests.size()==2&&r.pending_asset_requests[0].faction_index==2,"take did not remove first matching own stock");
 check(r.player_pending_placements[0].army_asset_id==330&&r.player_command_results[0].accepted,"take failed to arm pending slot");
 f.input(SimulationLabPlayerCommandKind::TakeStock,1,330);f.step();
 check(r.player_pending_placements[1].army_asset_id==330&&r.player_pending_placements[0].army_asset_id==330,"players did not retain independent slots");
 f.input(SimulationLabPlayerCommandKind::TakeStock,2,330);f.step();
 check(!r.player_command_results[0].accepted&&!r.player_pending_placements[2].army_asset_id,"missing primary stock invented pending asset");
 f.input(SimulationLabPlayerCommandKind::ReturnStock);f.step();
 check(!r.player_pending_placements[0].army_asset_id&&r.pending_asset_requests.back().faction_index==1,"cancel did not append held asset to own primary list");
 check(r.player_pending_placements[1].army_asset_id==330,"cancel changed another player's slot");
 r.pending_asset_requests.assign(64,{330,1,0,0});
 f.input(SimulationLabPlayerCommandKind::ReturnStock,1);f.step();
 check(r.pending_asset_requests.size()==64&&!r.player_pending_placements[1].army_asset_id,"full primary list return did not clear pending or exceeded capacity");
}
void placement(){
 Fixture f;auto& r=f.run();r.pending_asset_requests={{330,1,0,0}};
 const auto budget=r.faction_economy[1].xenite_current_q4,seed=r.rng_state;
 f.input(SimulationLabPlayerCommandKind::TakeStock,0,330);
 f.input(SimulationLabPlayerCommandKind::PlaceStock,0,0,{INT32_MIN,INT32_MIN});f.step();
 check(r.player_command_results[0].accepted&&!r.player_command_results[1].accepted,"invalid placement accepted");
 check(r.player_pending_placements[0].army_asset_id==330&&r.entities.size()==1,"rejected placement lost pending stock or created a runtime");
 f.input(SimulationLabPlayerCommandKind::PlaceStock,0,0,f.site);f.step();
 check(r.player_command_results[0].accepted&&r.player_command_results[0].placed_point->x==f.site.x,"valid requested placement rejected or snapped");
 check(r.entities.size()==2&&!r.player_pending_placements[0].army_asset_id&&r.pending_asset_requests.empty(),"success did not consume only held stock");
 const auto& entity=r.entities.back();
 check(entity.army_asset_id==330&&entity.faction_index==1&&entity.actor.body_yaw_turn16==0x2345&&entity.root_model_integrity==15000,"created runtime has wrong asset, faction, heading or health");
 check(entity.construction_start_tick==r.tick&&r.faction_score[1].relation_counter_b==1,"placement did not commit on input tick or increment native relation counter");
 check(r.faction_economy[1].xenite_current_q4==budget&&r.rng_state==seed,"placement charged or consumed RNG");
 const auto events=simulation_lab_one_shot_events_after(r,0);
 check(std::any_of(events.begin(),events.end(),[](const auto& e){return e.kind==SimulationLabOneShotEventKind::ConstructionPlacementEffect&&e.effect_id==153;}),"placement effect missing");
 f.input(SimulationLabPlayerCommandKind::PlaceStock,0,0,f.site);f.step();
 check(!r.player_command_results[0].accepted&&r.entities.size()==2,"empty pending slot placed another runtime");
}
void snapped(){
 Fixture f;auto& r=f.run();
 const MovementPoint raw{(f.site.x&~0xFF)+90,(f.site.y&~0xFF)+90};
 auto blocker=r.entities[0];blocker.actor.position={q12_to_float(raw.x),q12_to_float(raw.y),0};
 blocker.faction_index=2;blocker.motion.collision_radius_q12=1;r.entities.push_back(blocker);
 r.pending_asset_requests={{330,1,0,0}};
 f.input(SimulationLabPlayerCommandKind::TakeStock,0,330);
 f.input(SimulationLabPlayerCommandKind::PlaceStock,0,0,raw);f.step();
 const auto& result=r.player_command_results.back();
 check(result.accepted&&result.placed_point->x==(raw.x&~0xFF)&&result.placed_point->y==(raw.y&~0xFF),"cursor did not try first snapped alternative after raw collision");
}
void pioneer(){
 auto field=FieldGrid::create(48,48,0,0);const auto& c=field.at(24,24);
 SimulationLabSeedEntity unit;unit.army_asset_id=50;unit.faction_index=1;unit.runtime_model_class_id=18;
 unit.actor.position={q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),0};
 unit.motion.speed_q12_per_tick=512;unit.motion.yaw_max_velocity_turn16=4096;unit.motion.yaw_acceleration_turn16=4096;
 SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=100;
 SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=1;scenario.human_faction_mask=2;scenario.ai_planning_enabled=false;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};SimulationLabExperiment lab;
 lab.reset(cfg,ki,std::span(&unit,1),{},{},{},{},{},scenario);lab.step(field,10);
 check(lab.runs()[0].entities[0].actor.position.x==unit.actor.position.x,"human Pioneer moved without command");
 SimulationLabPlayerCommand command;command.kind=SimulationLabPlayerCommandKind::MovePioneer;command.faction_index=1;
 command.entity_index=0;command.world_x_q12=c.world_x_q12+20000;command.world_y_q12=c.world_y_q12;
 check(lab.queue_player_command(0,command),"Pioneer move not queued");lab.step(field,10);
 check(lab.runs()[0].entities[0].actor.position.x>unit.actor.position.x,"explicit routed Pioneer input did not drive native movement state");
 command.faction_index=2;check(lab.queue_player_command(0,command),"structurally valid input not queued");lab.step(field);
 check(!lab.runs()[0].player_command_results[0].accepted,"wrong faction moved selected Pioneer");
 command.player_index=255;check(!lab.queue_player_command(0,command),"reserved player id accepted");
}
}
int main()try{stock();placement();snapped();pioneer();std::cout<<"PLAYER_COMMANDS PASS stock, per-player slots, cancel capacity, rejected/successful/snapped placement, effects, payment, routed Pioneer\n";}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
