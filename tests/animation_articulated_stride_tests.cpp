#include "thandor/simulation_lab_follow.hpp"
#include "thandor/simulation_lab_targeting.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/army_target_follow.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace thandor;
void check(bool x,const char* why){if(!x)throw std::runtime_error(why);}
struct Fixture {
 FieldGrid field=FieldGrid::create(100,60,0,0);SimulationLabExperiment lab;Vec3f origin{};
 Fixture(bool planner=false){
  const auto& c=field.at(25,25);origin={q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),0};
  SimulationLabSeedEntity a;a.army_asset_id=60;a.faction_index=1;a.runtime_model_class_id=2;a.actor.position=origin;a.root_model_maximum_integrity=10000;a.shot_impact_class_index=1;a.motion.speed_q12_per_tick=512;a.motion.yaw_max_velocity_turn16=a.motion.yaw_acceleration_turn16=0x7fff;a.fog_uncover_radius_q12=30*4096;a.fog_reference_height_offset_q12=4*4096;
  SimulationWeaponProfile w;w.shot_definition_id=88;w.reload_ticks=2;w.maximum_range_q12=10*4096;w.launch_attachment_count=1;w.yaw_max_velocity_turn16=w.yaw_acceleration_turn16=w.pitch_max_velocity_turn16=w.pitch_acceleration_turn16=0x7fff;w.pitch_min_turn16=-0x4000;w.pitch_max_turn16=0x4000;ShotDefinitionRecord shot;shot.definition_id=88;shot.trajectory_mode=3;shot.launch_speed_q12=4096;shot.projectile_lifetime_ticks=11;shot.target_impact_damage_q12[2]=4096;a.combat_weapons={{w,shot}};
  auto b=a;b.army_asset_id=331;b.runtime_model_class_id=15;b.faction_index=2;b.building=true;b.motion={};b.shot_impact_class_index=2;b.actor.position.x+=11;b.combat_weapons.clear();const std::array seeds{a,b};
  SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=512;cfg.sweep_mode=SimulationLabSweepMode::Fixed;std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};std::vector<SimulationLabProductionAsset> products;
  if(planner){SimulationLabProductionAsset product;product.army_asset_id=a.army_asset_id;product.entity_seed=a;product.ai_definition_class_value70=1;products.push_back(product);ki[0x23]=1000000;ki[0x27]=4096;}
  SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=scenario.assignable_faction_count=2;scenario.simulate_all_active_factions_as_ai=true;scenario.runtime_catalog=std::make_shared<SimulationLabRuntimeCatalog>();lab.reset(cfg,ki,seeds,{},products,{},{},{},scenario);
  origin=e().actor.position;e().military_ai_source_dispatch=true;
 }
 SimulationLabRun& run(){return lab.runs()[0];}SimulationLabEntity& e(){return run().entities[0];}
};
#include <cmath>
int main()try{
 const auto near=[](float a,float b){return std::abs(a-b)<0.0001F;};
 auto p=simulation_articulated_step_target({10,10,1},{10,11,0},{0,-1,0},0,4096,{20,10,0});
 check(near(p.x,11)&&near(p.y,9),"stride target followed per-frame root instead of MDL stride");
 p=simulation_articulated_step_target({10,10,1},{10,11,0},{0,-1,0},0,4096,{10.25F,10,0});
 check(near(p.x,10.25F)&&near(p.y,9),"foot step overshot the route point");
 p=simulation_articulated_step_target({10,10,1},{9,10,0},{1,0,0},0x4000,4096,{10,20,0});
 check(near(p.x,11)&&near(p.y,11),"quarter-turn stride lost side offset");
 p=simulation_articulated_step_target({10,10,1},{10.5F,11,0},{0,-1,0},0,4096,{20,10,0});
 check(near(p.x,11.5F),"opposite planted endpoint was omitted from reach");
 check(simulation_articulated_contact_step_q12(1400,250,4096)==2495,
       "native distance+2*lift denominator changed");
 {
  Fixture f;auto& e=f.e();
  check(!e.runtime_model_hierarchy,"metadata-only walker fixture unexpectedly has an authored hierarchy");
  e.runtime_model_class_id=3;e.articulated_contact_geometry_enabled=true;
  e.articulated_contact_geometry_prepared=true;e.articulated_contact_local_offsets={Vec3f{0,-.3F,0},Vec3f{0,.3F,0}};
  e.articulated_contact_interpolation_rate_q12=1400;e.articulated_contact_lift_q12=250;e.articulated_root_height_q12=4096;
  e.motion.speed_q12_per_tick=132;
  const auto held=f.lab.snapshot_copy();f.lab.step(f.field,160);
  check(e.combat_follow_starts&&e.combat_follow_stops&&f.run().metrics.weapon_shots_fired,
        "walker did not approach, stop following and fire in actual loop");
  check(f.run().metrics.articulated_contact_completions>0,"full-stride approach emitted no contact completions");
  check(e.actor.position.x>f.origin.x+.8F&&e.actor.position.x<f.origin.x+2.1F,"walker root escaped bounded contact-owned approach");
  check(!held.runs()[0].entities[0].actor.articulated_contacts_initialized,"held snapshot contact state changed");
  std::cout<<"WALKER_LIVE starts="<<e.combat_follow_starts<<" stops="<<e.combat_follow_stops<<" shots="<<f.run().metrics.weapon_shots_fired<<" movement="<<e.actor.position.x-f.origin.x<<" contacts="<<f.run().metrics.articulated_contact_completions<<'\n';
 }
 {
  // An empty authored hierarchy also has no native walker topology or weapon
  // attachments. It must remain safe without inventing a native contact record.
  Fixture f;auto& e=f.e();e.runtime_model_class_id=3;
  e.articulated_contact_geometry_enabled=e.articulated_contact_geometry_prepared=true;
  e.runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>();
  f.lab.step(f.field,160);
  check(!f.e().actor.articulated_native.initialized&&!f.e().runtime_removed,
        "empty walker topology entered the native hierarchy callback");
 }
 {
  Fixture f;auto& e=f.e();e.runtime_model_class_id=3;e.articulated_contact_geometry_enabled=true;
  e.articulated_contact_geometry_prepared=true;e.articulated_contact_local_offsets={Vec3f{0,-.3F,0},Vec3f{0,.3F,0}};
  e.articulated_contact_interpolation_rate_q12=1400;e.articulated_contact_lift_q12=250;e.articulated_root_height_q12=4096;
  e.motion.collision_radius_q12=512;
  auto blocker=f.run().entities[1];blocker.faction_index=1;blocker.actor.position=f.origin;
  blocker.actor.position.x+=.9F;blocker.motion.collision_radius_q12=1024;
  f.run().entities.push_back(blocker);
  for(int tick=0;tick<128;++tick){f.lab.step(f.field,1);
   check(f.e().actor.position.x<f.origin.x+.53F,"contact root crossed a live building blocker");
  }
  check(f.run().metrics.navigation_actual_step_blocks>0,"obstructed stride did not use the physical admission gate");
  std::cout<<"WALKER_BLOCKER blocks="<<f.run().metrics.navigation_actual_step_blocks<<" movement="<<f.e().actor.position.x-f.origin.x<<'\n';
 }
 std::cout<<"V642_ARTICULATED_STRIDE PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
