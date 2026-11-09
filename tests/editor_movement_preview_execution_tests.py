#!/usr/bin/env python3
"""Execute the real preview movement methods with a small owner/catalog adapter.
Uses the built core, without Vulkan, GLFW or an editor window.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser=argparse.ArgumentParser()
parser.add_argument('--source',type=Path,required=True)
parser.add_argument('--build',type=Path,required=True)
args=parser.parse_args();source=args.source.resolve();build=args.build.resolve()
app=(source/'src/editor/editor_app.cpp').read_text()
methods=app[app.index('void EditorApp::prepare_simulation_navigation_terrain()'):app.index('std::optional<std::string> EditorApp::resolve_runtime_spatial_sound_path')]
methods=methods.replace('EditorApp::','Preview::')
prefix=r'''
#include "thandor/catalog.hpp"
#include "thandor/simulation_lab_geometry.hpp"
#include "thandor/retail_contact.hpp"
#include <map>
#include <memory>
#include <iostream>
#include <stdexcept>
using namespace thandor;
struct Placement {uint32_t army_asset_id{},faction_index{1};Vec3f position{};};
struct Level {std::vector<Placement> records;const auto& placements()const{return records;}};
struct Document {FieldGrid grid=FieldGrid::create(59,59,0,0);Level lev;
 const FieldGrid& field()const{return grid;}FieldGrid& field(){return grid;}
 const Level& level()const{return lev;}};
struct Catalog {std::map<uint32_t,ArmyAssetPreset> presets;
 const ArmyAssetPreset* army_preset(uint32_t id)const {auto it=presets.find(id);return it==presets.end()?nullptr:&it->second;}};
class Preview {public:
 struct SimulationProducedEntity {uint64_t serial{};uint32_t army_asset_id{},faction_index{1};size_t producer_placement_index{SIZE_MAX};SimulationActorState actor{};bool linked_to_producer{true};};
 std::unique_ptr<Document> document_=std::make_unique<Document>();Catalog catalog_;
 std::map<size_t,SimulationActorState> simulation_actor_states_;
 std::map<uint64_t,SimulationProducedEntity> simulation_produced_entities_;
 mutable V565PathTerrainCache simulation_navigation_terrain_cache_;uint64_t simulation_navigation_terrain_signature_{};
 Vec3f placement_command_target_point(size_t i)const{return document_->lev.records.at(i).position;}
 void prepare_simulation_navigation_terrain();
 std::vector<V528PathRuntimeBlocker> simulation_movement_blockers(size_t,std::optional<uint64_t>={})const;
 bool simulation_path_segment_clear(size_t,Vec3f,Vec3f,const ArmyAssetPreset&,std::optional<uint64_t>={})const;
 V528RouteResult simulation_resolve_v528_route(size_t,Vec3f,Vec3f,const ArmyAssetPreset&,std::optional<uint64_t>={})const;
 bool simulation_preview_motion(SimulationActorState&,const ArmyAssetPreset&,size_t,std::optional<uint64_t>={});
};
'''.replace('>={}', '> = {}')
main=r'''
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
ArmyAssetPreset mover(uint32_t cls=1){ArmyAssetPreset p;p.runtime_model_class_id=cls;
 p.has_traversal_profile=true;p.traversal_low_band_index=0;p.traversal_high_band_index=4;
 auto& m=p.simulation_motion_profile;m.speed_q12_per_tick=64;m.collision_radius_q12=1000;
 m.yaw_acceleration_turn16=m.yaw_max_velocity_turn16=0x4000;return p;}
int main()try{
 unsigned cases=0;
 // Both map placements and newly produced units must use terrain-normal speed controls.
 for(bool produced:{false,true})for(uint32_t heading:{0U,0x8000U}){
  Preview p;auto unit=mover();auto& motion=unit.simulation_motion_profile;
  motion.placement_contact_kind=2;motion.ground_speed_control=true;motion.speed_q12_per_tick=84;
  motion.ground_acceleration_q12=84;motion.ground_slope_threshold_turn16=10500;motion.ground_secondary_slope_threshold_turn16=14500;
  for(auto& c:p.document_->grid.cells())c.terrain_height_q12=c.world_x_q12/4;
  const auto& c=p.document_->grid.at(25,25);SimulationActorState actor;
  actor.position={q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),0};actor.body_yaw_turn16=heading;
  actor.move_target=Vec3f{actor.position.x+(heading? -5.F:5.F),actor.position.y,0};
  p.catalog_.presets[1]=unit;
  if(produced){Preview::SimulationProducedEntity e;e.serial=1;e.army_asset_id=1;e.actor=actor;p.simulation_produced_entities_[1]=e;}
  else p.document_->lev.records.push_back({1,1,actor.position});
  p.prepare_simulation_navigation_terrain();
  check(p.simulation_preview_motion(actor,unit,produced?SIZE_MAX:0,produced?std::optional<uint64_t>{1}:std::nullopt),"slope fixture did not move");
  check(actor.ground_advance_q12==(heading?84U:52U),"preview omitted the normal-dependent native slope speed");++cases;
 }
 // A produced unit stays linked to its own producer but must route around the next building.
 for(uint32_t cls:{1U,2U,3U,17U,18U,19U}){
  Preview p;const auto unit=mover(cls);p.catalog_.presets[1]=unit;
  auto building=mover(11);building.simulation_motion_profile.speed_q12_per_tick=0;
  building.simulation_motion_profile.collision_radius_q12=2000;p.catalog_.presets[300]=building;
  const auto& c=p.document_->grid.at(25,25);const Vec3f start{q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),0};
  p.document_->lev.records={{300,1,start},{300,1,{start.x+2.5F,start.y,0}}};
  Preview::SimulationProducedEntity entity;entity.serial=1;entity.army_asset_id=1;entity.producer_placement_index=0;
  entity.actor.position=start;entity.actor.move_target=Vec3f{start.x+6,start.y,0};p.simulation_produced_entities_[1]=entity;
  p.prepare_simulation_navigation_terrain();auto& actor=p.simulation_produced_entities_[1].actor;
  const V528PathRuntimeBlocker obstacle{float_to_q12(start.x+2.5F),float_to_q12(start.y),2000,11};
  for(int tick=0;tick<1000&&actor.move_target;++tick){auto before=actor.position;
   p.simulation_preview_motion(actor,unit,SIZE_MAX,1);
   check(!simulation_lab_step_hits_blocker(before,actor.position,1000,obstacle),"produced unit crossed a building");}
  check(!actor.move_target,"produced preview unit failed to navigate around a building");
  check(actor.position.x>start.x+4,"produced unit discarded its destination");++cases;
 }
 // Editing the same FLD object invalidates the movement lattice.
 Preview p;p.prepare_simulation_navigation_terrain();auto original=p.simulation_navigation_terrain_cache_.states;
 p.document_->grid.at(25,25).water_delta_q12=900;p.prepare_simulation_navigation_terrain();
 check(p.simulation_navigation_terrain_cache_.states!=original,"preview terrain edit retained stale path cells");
 std::cout<<"EDITOR_PREVIEW_MOVEMENT PASS cases="<<cases<<" slopes, produced owners, six ground classes, cache refresh\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
'''
with tempfile.TemporaryDirectory(prefix='thandor-preview-movement-') as tmp:
 cpp=Path(tmp)/'preview.cpp';exe=Path(tmp)/'preview';cpp.write_text(prefix+methods+main)
 subprocess.run(['g++','-std=c++20','-O2','-I'+str(source/'include'),str(cpp),str(build/'libthandor_map_core.a'),'-pthread','-ldl','-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
