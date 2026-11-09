#include "thandor/simulation_lab_army_pool.hpp"
#include "thandor/simulation_lab_environment.hpp"
#include "thandor/map_document.hpp"
#include "thandor/projectile_runtime.hpp"
#include "thandor/projectile_contacts.hpp"
#include "thandor/retail_contact.hpp"
#include "thandor/retail_random.hpp"
#include "thandor/simulation_lab_shot_rays.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void terrain_contact_releases_effect_slot(){
 auto catalog=std::make_shared<SimulationLabRuntimeCatalog>();
 EffectDefinitionRecord definition;
 definition.definition_id=1;definition.transition_kind=4;
 definition.runtime_value_0c=60;definition.frame_advance_threshold_q4=16;
 definition.terrain_pitch_drop_per_age_squared=0x1000;
 definition.terrain_contact_alpha_fade_step=255;
 catalog->effects[1]={definition,{}};
 SimulationLabScenarioRuntimeSeed scene;
 scene.enabled=true;scene.active_faction_count=1;
 scene.ai_planning_enabled=false;scene.runtime_catalog=catalog;
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=3;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
 SimulationLabExperiment lab;
 lab.reset(config,parameters,{},{},{},{},{},{},scene);
 auto& run=lab.runs()[0];
 const auto field=FieldGrid::create(30,30,0,0);
 const auto& cell=field.at(15,15);
 SimulationLabEnvironmentalEffect effect;
 effect.effect_id=1;effect.world_pool_only=true;
 effect.pose.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),1};
 effect.pose.native_angles=std::array<uint32_t,3>{0x2000,0x4000,0};
 using Kind=SimulationLabWorldObjectPool::Kind;
 effect.native_world_slot=run.native_world_objects.claim(Kind::Effect,1);
 const auto slot=run.native_world_objects.slot(effect.native_world_slot);
 run.environmental_effects.push_back(effect);
 lab.step(field,2);
 check(run.environmental_effects.size()==1&&run.environmental_effects.front().terrain_contact_alpha==0,
       "ground contact must fade the effect before releasing its slot");
 lab.step(field);
 check(run.environmental_effects.empty(),"transparent ground contact did not end the effect");
 const auto model=run.native_world_objects.claim(Kind::Model,2);
 check(run.native_world_objects.slot(model)==slot&&
       run.native_world_objects.retained_pose(model).yaw==0x2000,
       "the released world slot did not retain the effect heading");
}
SimulationLabStaticShotHull cube(Vec3f p,float r,uint32_t cls=0){SimulationLabStaticShotHull h;h.impact_class=cls;
 for(uint32_t i=0;i<8;++i)h.corners[i]={p.x+((i&1)?r:-r),p.y+((i&2)?r:-r),p.z+((i&4)?r:-r)};return h;}
void linked_effect_owner_order(){
 auto catalog=std::make_shared<SimulationLabRuntimeCatalog>();
 EffectDefinitionRecord parent;parent.definition_id=1;parent.transition_kind=0;
 parent.runtime_value_0c=3;parent.frame_advance_threshold_q4=16;
 parent.linked_effect_present=1;parent.linked_effect_id=2;parent.sprite_path="effect_parent";
 EffectDefinitionRecord child=parent;child.definition_id=2;child.transition_kind=1;
 child.linked_effect_present=0;child.linked_effect_id=0;child.creation_flags=1;
 catalog->effects[1]={parent,{}};catalog->effects[2]={child,{{100U,0U}}};
 catalog->sprites["effect_parent"].damage_effect_points.push_back({0,{0,0,0}});
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=1;config.base_seed=12345;
 SimulationLabScenarioRuntimeSeed scene;scene.enabled=true;scene.active_faction_count=1;
 scene.ai_planning_enabled=false;scene.runtime_catalog=catalog;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
 SimulationLabExperiment lab;lab.reset(config,parameters,{},{},{},{},{},{},scene);
 auto& run=lab.runs()[0];SimulationLabEffectPose older,newer;
 older.position={10,10,1};newer.position={20,10,1};
 older.native_angles=newer.native_angles=std::array<uint32_t,3>{0,0x4000,0};
 run.environmental_effects.push_back({1,0,older,0});run.environmental_effects.push_back({1,0,newer,0});
 auto expected_seed=run.rng_state;const auto first=retail_random_next(expected_seed)&65535U;
 const auto second=retail_random_next(expected_seed)&65535U;
 lab.step(FieldGrid::create(20,20,0,0));
 size_t found=0;for(const auto& e:run.environmental_effects)if(e.effect_id==2){
  ++found;check(e.pose.native_angles.has_value(),"linked EFF lost its fixed angles");
  check((*e.pose.native_angles)[0]==(e.pose.position.x==20?first:second),
        "linked EFF RNG was assigned in oldest-first owner order");
 }
 check(found==2&&run.rng_state==expected_seed,"linked EFF owner-order fixture did not run both callbacks");
}
void terminal_effect_creation_order(){
 auto catalog=std::make_shared<SimulationLabRuntimeCatalog>();
 EffectDefinitionRecord parent;parent.definition_id=1;parent.transition_kind=0;
 parent.runtime_value_0c=10;parent.frame_advance_threshold_q4=16;
 parent.linked_effect_present=1;parent.linked_effect_id=2;parent.sprite_path="terminal_parent";
 auto child=parent;child.definition_id=2;child.transition_kind=1;
 child.linked_effect_present=0;child.linked_effect_id=0;child.creation_flags=1;
 catalog->effects[1]={parent,{}};catalog->effects[2]={child,{{100U,0U}}};
 catalog->sprites["terminal_parent"].damage_effect_points={{0,{0,0,0}}};
 catalog->sprites["dying_model"].damage_effect_points={{0,{0,0,0}}};
 ModelVisualNode node;node.model_definition_id=1;node.runtime_class_id=10;
 node.sprite_path="dying_model";node.parent_index=UINT32_MAX;
 node.simulation.placement_contact_kind=3;
 auto nodes=std::make_shared<const std::vector<ModelVisualNode>>(std::vector{node});
 SimulationLabSeedEntity older;older.army_asset_id=1;older.faction_index=1;
 older.source_placement_index=0;older.runtime_model_class_id=10;
 older.actor.position={10,-10,1};older.root_model_maximum_integrity=100;
 SimulationLabModelComponentSeed component;component.source_model_node_index=0;
 component.model_definition_id=1;component.runtime_class_id=10;component.maximum_integrity=100;
 component.terminal_attachment_effect_ids[0]=1;older.model_components={component};
 auto newer=older;newer.source_placement_index=1;newer.actor.position.x=20;
 const std::array seeds{older,newer};
 SimulationLabScenarioRuntimeSeed scene;scene.enabled=true;scene.active_faction_count=1;
 scene.ai_planning_enabled=false;scene.runtime_catalog=catalog;
 SimulationLabConfig config;config.run_count=1;config.duration_ticks=3;config.base_seed=12345;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
 SimulationLabExperiment lab;lab.reset(config,parameters,seeds,{},{},{},{},{},scene);
 auto& run=lab.runs()[0];
 for(auto& e:run.entities){e.runtime_model_hierarchy=nodes;e.root_model_integrity=0;e.root_model_terminal=true;
  auto& c=e.model_components[0];c.integrity=0;c.terminal_runtime_flag8=true;c.terminal_self_reference=true;}
 const auto field=FieldGrid::create(30,30,0,0);lab.step(field,1);
 check(run.environmental_effects.size()==2,"terminal fixture did not create both parent effects");
 check(run.environmental_effects[0].pose.position.x==20&&run.environmental_effects[1].pose.position.x==10,
       "terminal model callbacks created effects in oldest-first order");
 auto expected=run.rng_state;const auto first=retail_random_next(expected)&65535U;
 const auto second=retail_random_next(expected)&65535U;lab.step(field,1);
 size_t found=0;for(const auto& e:run.environmental_effects)if(e.effect_id==2){++found;
  check(e.pose.native_angles&&(*e.pose.native_angles)[0]==(e.pose.position.x==10?first:second),
        "terminal callback creation order assigned linked RNG to the wrong parent");}
 check(found==2&&run.rng_state==expected,"terminal linked-effect fixture changed draw count");
}
void model_callback_before_older_projectile(){
 // A newly created structure gets its complete model/weapon/AI callback
 // before shots already in flight. It may acquire a still-living target
 // which an older projectile kills later in this same owner traversal.
 for(const auto cls:{2U,4U})for(const bool newer:{false,true}){
  auto field=FieldGrid::create(60,60,0,0);const auto& cell=field.at(25,25);
  const Vec3f origin{q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),3};
  auto catalog=std::make_shared<SimulationLabRuntimeCatalog>();
  auto& sprite=catalog->sprites["callback_target"];sprite.has_bounds=true;
  sprite.bounds_min={-.2F,-.2F,-.2F};sprite.bounds_max={.2F,.2F,.2F};
  ShotDefinitionRecord incoming;incoming.definition_id=99;incoming.trajectory_mode=3;
  incoming.launch_speed_q12=4096;incoming.projectile_lifetime_ticks=20;
  incoming.target_impact_damage_q12[1]=200;incoming.target_impact_effect_ids[1]=44;
  catalog->shots[99]=incoming;catalog->effect_lifetimes[44]=5;
  SimulationLabSeedEntity source;source.army_asset_id=370;source.faction_index=1;
  source.runtime_model_class_id=cls;source.root_model_maximum_integrity=1000;
  source.shot_impact_class_index=1;source.actor.position=origin;source.building=cls==4U;
  auto target=source;target.army_asset_id=16;target.faction_index=2;
  target.runtime_model_class_id=10;target.root_model_maximum_integrity=100;
  target.actor.position.x+=2;target.building=false;
  const std::array seeds{source,target};SimulationLabScenarioRuntimeSeed scene;
  scene.enabled=true;scene.active_faction_count=2;scene.ai_planning_enabled=false;scene.runtime_catalog=catalog;
  SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=5;
  std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
  SimulationLabExperiment lab;lab.reset(cfg,parameters,seeds,{},{},{},{},{},scene);
  auto& run=lab.runs()[0];run.tick=2;
  ModelVisualNode root;root.model_definition_id=1;root.runtime_class_id=cls;
  root.parent_index=UINT32_MAX;root.simulation.placement_contact_kind=3;
  auto target_node=root;target_node.model_definition_id=2;target_node.runtime_class_id=10;
  target_node.sprite_path="callback_target";target_node.shot_impact_class_index=1;
  run.entities[0].runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(std::vector{root});
  run.entities[1].runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(std::vector{target_node});
  for(size_t i=0;i<2;++i){auto& e=run.entities[i];auto& c=e.model_components[0];
   c.source_model_node_index=0;c.model_definition_id=uint32_t(i+1);
   c.runtime_class_id=i==0?cls:10U;e.model_terrain_state_initialized=true;e.model_primary_occupancy_mask=0xAAAAU;}
  auto& shooter=run.entities[0];shooter.synthetic=newer;shooter.construction_start_tick=newer?2U:0U;
  ShotDefinitionRecord aim;aim.definition_id=100;aim.trajectory_mode=2;
  aim.mode2_selection_range_q12=10*4096;aim.projectile_lifetime_ticks=20;
  aim.target_impact_damage_q12[1]=10;
  SimulationWeaponProfile weapon;weapon.runtime_class_id=7;weapon.shot_definition_id=100;
  weapon.reload_ticks=1;weapon.launch_attachment_count=1;weapon.maximum_range_q12=10*4096;
  weapon.pitch_min_turn16=-0x4000;weapon.pitch_max_turn16=0x4000;
  shooter.combat_weapons={{weapon,aim}};
  SimulationProjectileState projectile;projectile.serial=1;projectile.spawn_tick=1;
  projectile.owner_kind=SimulationProjectileOwnerKind::Environmental;projectile.owner_placement_index=SIZE_MAX;
  projectile.shot_definition_id=99;projectile.launch_position=target.actor.position;projectile.launch_position.x-=1;
  projectile.target_position=target.actor.position;projectile.lifetime_ticks=20;projectile.travel_ticks=20;
  run.projectiles={projectile};run.next_projectile_serial=2;
  lab.step(field,1);
  check(run.entities[1].root_model_integrity==0,"owner-order fixture did not kill the target");
  check((run.entities[0].target_entity_index==std::optional<size_t>{1})==newer,
        "AI assignment did not occupy the model's position among projectile owners");
  check(run.entities[0].combat_command_acquisitions==(newer?1U:0U),"model callback acquired twice or admitted an already-dead candidate");
 }
}
int main(int argc,char** argv)try{
 {
  auto field=FieldGrid::create(30,30,4096,0);
  field.capture_runtime_normals();
  const auto cell=field.at(12,12);
  const int32_t x=cell.world_x_q12+500,y=cell.world_y_q12-500;
  SimulationLabEntity scenery;
  scenery.actor.position={q12_to_float(x),q12_to_float(y),0};
  ModelVisualNode root;
  root.parent_index=UINT32_MAX;
  root.simulation.placement_contact_kind=2;
  scenery.runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(std::vector{root});
  const SimulationLabRuntimeCatalog catalog;
  check(simulation_lab_place_root_on_surface(scenery,field,catalog),"scenery placement fixture missed the terrain");
  const auto initial=simulation_lab_current_model_poses(scenery,field,catalog).front().native_angles;
  field.apply_runtime_terrain_impact(x+1500,y,3000,2000,0);
  check(simulation_lab_current_model_poses(scenery,field,catalog).front().native_angles==initial,
        "terrain impact rotated an already placed scenery model");
  check(simulation_lab_place_root_on_surface(scenery,field,catalog),"scenery replacement fixture missed the terrain");
  check(simulation_lab_current_model_poses(scenery,field,catalog).front().native_angles!=initial,
        "scenery placement did not refresh its terrain normal");
 }
 terrain_contact_releases_effect_slot();
 linked_effect_owner_order();terminal_effect_creation_order();model_callback_before_older_projectile();
 auto catalog=std::make_shared<SimulationLabRuntimeCatalog>();auto field=FieldGrid::create(80,80,0,0);
 const auto& cell=field.at(40,40);const Vec3f origin{q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),4};
 // Native beams select the traversed cell's material, and emit along the
 // stored heading even when the collision ray has an elevation offset.
 {
  auto materials=field;for(auto& c:materials.cells())c.set_material(7);materials.at(0,0).set_material(3);
  ShotDefinitionRecord beam;beam.trajectory_mode=0;beam.launch_speed_q12=8192;beam.projectile_lifetime_ticks=1;
  beam.terrain_impact_effect_ids[7]=70;beam.terrain_impact_effect_ids[3]=30;
  SimulationProjectileState ray;ray.launch_position={origin.x,origin.y,1};
  ray.launch_pitch_turn16=0;ray.launch_yaw_turn16=0;beam.elevation_offset_angle16=0x4000;
  check(simulation_lab_refresh_beam_contact(ray,beam,materials,*catalog,{},{}),"native offset beam missed terrain");
  check(ray.impact_material_index==7&&ray.impact_effect_id==70,"beam used cell-zero material instead of hit cell");
  check(float_to_q12(ray.impact_position.x)==float_to_q12(origin.x)+4096&&
        float_to_q12(ray.impact_position.y)==float_to_q12(origin.y)&&float_to_q12(ray.impact_position.z)==4096,
        "beam EFF inherited the collision ray's elevation offset");
 }
 ShotDefinitionRecord shot;shot.definition_id=28;shot.trajectory_mode=1;shot.launch_speed_q12=450;shot.ballistic_divisor_q12=4;
 shot.projectile_lifetime_ticks=100;shot.target_impact_effect_ids[0]=2;shot.target_impact_damage_q12[0]=1000;shot.terrain_impact_effect_ids[0]=3;
 catalog->shots[28]=shot;catalog->effect_lifetimes[2]=12;catalog->effect_lifetimes[3]=12;
 // Native projectile rays cross geometric terrain on flagged edge cells even
 // though ordinary water/height point sampling rejects those same cells.
 {
  auto edge_field=field;
  for(uint32_t y=39;y<=41;++y)for(uint32_t x=39;x<=41;++x)
   edge_field.at(x,y).flags_and_material|=0x88006000U;
  SimulationProjectileState falling;
  falling.owner_kind=SimulationProjectileOwnerKind::Environmental;
  falling.runtime_flight_previous={origin.x,origin.y,1.F};
  falling.runtime_flight_ray_end={origin.x,origin.y,-1.F};
  falling.runtime_flight_speed_q12=8192;falling.runtime_flight_pitch=-16384;
  check(!edge_field.sample_retail_surface_heights_q12(float_to_q12(origin.x),float_to_q12(origin.y)).valid,
        "edge fixture unexpectedly allowed ordinary point sampling");
  check(simulation_lab_refresh_projectile_interval_contact(falling,shot,1,edge_field,*catalog,{},{}),
        "flagged terrain edge let a falling projectile pass through");
  check(falling.impact_kind==SimulationImpactKind::TerrainSurface&&falling.impact_effect_id==3&&
        std::abs(float_to_q12(falling.impact_position.z))<=1,
        "edge ray did not retain its terrain contact and material effect");
 }

 EffectDefinitionRecord eff;eff.definition_id=25;eff.transition_kind=1;eff.runtime_value_0c=5;eff.frame_advance_threshold_q4=16;
 eff.linked_shot_present=1;eff.linked_shot_id=28;eff.model_scale_x_q12=eff.model_scale_y_q12=4096;eff.sprite_path="controller";
 catalog->effects[25]={eff,build_linked_shot_emissions(eff)};
 catalog->sprites["controller"].shot_points={{0,{1,0,0}},{1,{0,1,0}},{2,{-1,0,0}},{3,{0,-1,0}}};
 ModelVisualRecord model;model.definition_id=647;model.runtime_class_id=0;model.simulation.timed_effect_id=25;
 model.simulation.timed_effect_interval_ticks=20;catalog->definitions[647]=model;
 SimulationLabScenarioRuntimeSeed scenario;scenario.runtime_catalog=catalog;
 SimulationLabEnvironmentalEmitter emitter;emitter.model_definition_id=647;emitter.pose.position=origin;scenario.environmental_emitters={emitter};
 std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};SimulationLabConfig config;config.run_count=1;config.duration_ticks=80;
 // Native timed caller chooses transition-2 launch angles before pool creation;
 // pool randomization then replaces only worldRotationAngle2. Preserve integer
 // angles rather than re-extracting Euler angles from an approximate basis.
 for(uint32_t transition:{1U,2U})for(uint32_t flags:{0U,1U}){
  auto oriented=std::make_shared<SimulationLabRuntimeCatalog>();
  auto definition=eff;definition.transition_kind=transition;definition.creation_flags=flags;
  definition.runtime_value_0c=20;definition.linked_shot_present=0;definition.linked_shot_id=0;
  oriented->effects[25]={definition,{}};oriented->definitions[647]=model;
  auto source=emitter;source.pose.native_angles=std::array<uint32_t,3>{0x1234,0x3000,0x3456};
  source.pose.native_basis=retail_model_basis_q28(0x1234,0x3000,0x3456);
  for(size_t j=0;j<9;++j)source.pose.basis[j]=float((*source.pose.native_basis)[j])/268435456.F;
  auto setup=scenario;setup.runtime_catalog=oriented;setup.environmental_emitters={source};
  auto cfg=config;cfg.base_seed=1538888426U;
  SimulationLabExperiment test;test.reset(cfg,parameters,{}, {},{}, {},{}, {},setup);test.step(field,1);
  const auto& run=test.runs()[0];check(run.environmental_effects.size()==1,"timed orientation fixture missing EFF owner");
  const auto expected=transition==2U?std::array<uint32_t,3>{flags?50511U:16565U,12541U,16565U}:
      std::array<uint32_t,3>{flags?16565U:0x1234U,0x3000U,0x3456U};
  check(run.environmental_effects[0].pose.native_angles==expected,"timed EFF lost native orientation or transition-2 draw");
  const auto expected_rng=transition==2U?(flags?4076213566U:812253908U):(flags?812253908U:1538888426U);
  check(run.rng_state==expected_rng,"timed EFF orientation consumed the wrong RNG draws");
 }
 SimulationLabExperiment ambient;ambient.reset(config,parameters,{}, {},{}, {},{}, {},scenario);ambient.step(field,6);
 const auto& a=ambient.runs()[0];check(a.metrics.environmental_effects_spawned==1&&a.metrics.environmental_shots_spawned==4,"wrong frame-based emission cadence");
 check(a.environmental_effects.empty()&&a.projectiles.size()==4,"parent release removed children or leaked state");
 for(const auto& p:a.projectiles)check(p.owner_kind==SimulationProjectileOwnerKind::Environmental&&p.owner_placement_index==SIZE_MAX,"fabricated army owner");
 // A linked EFF remains a renderable world owner after its final SHT launch.
 // Emission exhaustion and the EFF's authored visual lifetime are distinct.
 {
  auto lasting_catalog=std::make_shared<SimulationLabRuntimeCatalog>(*catalog);
  auto lasting_effect=eff;
  lasting_effect.definition_id=35;
  lasting_effect.runtime_value_0c=20;
  lasting_effect.frame_advance_threshold_q4=16;
  lasting_catalog->effects[35]={lasting_effect,{{1U,0U}}};
  lasting_catalog->definitions[647].simulation.timed_effect_id=35;
  lasting_catalog->definitions[647].simulation.timed_effect_interval_ticks=100;
  auto lasting_scenario=scenario;
  lasting_scenario.runtime_catalog=lasting_catalog;
  const auto authored_lifetime=effect_lifetime_ticks(lasting_effect);
  check(authored_lifetime>3,"lasting EFF fixture has no visual tail");
  SimulationLabExperiment lasting;
  lasting.reset(config,parameters,{}, {},{}, {},{}, {},lasting_scenario);
  lasting.step(field,3);
  const auto& retained=lasting.runs()[0];
  check(retained.metrics.environmental_shots_spawned==1,"single linked shot fixture emitted incorrectly");
  check(retained.environmental_effects.size()==1 &&
        retained.environmental_effects[0].next_emission==1,
        "linked EFF disappeared after its final SHT emission");
  lasting.step(field,authored_lifetime+1U);
  check(lasting.runs()[0].environmental_effects.empty(),
        "expired EFF retained after authored lifetime");
 }
 // A visual-only stock-style EFF has a world model even with no SHT children.
 {
  auto visual_catalog=std::make_shared<SimulationLabRuntimeCatalog>(*catalog);
  auto visual_effect=eff;
  visual_effect.definition_id=36;
  visual_effect.linked_shot_present=0;
  visual_effect.linked_shot_id=0;
  visual_effect.runtime_value_0c=20;
  visual_catalog->effects[36]={visual_effect,{}};
  visual_catalog->definitions[647].simulation.timed_effect_id=36;
  visual_catalog->definitions[647].simulation.timed_effect_interval_ticks=100;
  auto visual_scenario=scenario;
  visual_scenario.runtime_catalog=visual_catalog;
  SimulationLabExperiment visual;
  visual.reset(config,parameters,{}, {},{}, {},{}, {},visual_scenario);
  visual.step(field,2);
  check(visual.runs()[0].environmental_effects.size()==1 &&
        visual.runs()[0].metrics.environmental_shots_spawned==0,
        "visual-only EFF lost its linked world owner");
 }
 check(a.projectiles[0].launch_position.x>origin.x+.99F&&a.projectiles[1].launch_position.y>origin.y+.99F,"lost class2 attachment identity");
 auto wet=field;for(auto& c:wet.cells())c.water_delta_q12=64;
 SimulationLabExperiment water;water.reset(config,parameters,{}, {},{}, {},{}, {},scenario);water.step(wet,6);
 check(water.runs()[0].metrics.environmental_shots_spawned==0&&water.runs()[0].environmental_emitter_states[0].point_index==1,"null water alternate changed timer/index");
 SimulationWeaponProfile weapon;weapon.shot_definition_id=28;weapon.reload_ticks=1;weapon.launch_attachment_count=1;
 uint64_t serial=1;const std::array launch{origin};
 auto volley=simulation_create_volley(SIZE_MAX,0,weapon,launch,{origin.x+3,origin.y,origin.z},shot,0,serial,0,std::nullopt,SimulationProjectileOwnerKind::Environmental,0);
 const auto p=volley.front();const auto hit=simulation_projectile_position(p,shot,1);
 SimulationLabSeedEntity target;target.army_asset_id=1;target.faction_index=1;target.actor.position=hit;target.runtime_model_class_id=10;target.root_model_maximum_integrity=10000;
 SimulationLabModelComponentSeed component;component.source_model_node_index=0;component.model_definition_id=1;component.runtime_class_id=10;component.maximum_integrity=10000;target.model_components={component};
 ModelVisualNode node;node.model_definition_id=1;node.runtime_class_id=10;node.sprite_path="target";node.simulation.placement_contact_kind=3;
 auto hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(std::vector{node});auto& sprite=catalog->sprites["target"];
 sprite.has_bounds=true;sprite.bounds_min={-.01F,-.01F,-.01F};sprite.bounds_max={.01F,.01F,.01F};
 const Vec3f middle{(origin.x+hit.x)*.5F,(origin.y+hit.y)*.5F,(origin.z+hit.z)*.5F};
 // A completing EFF runs before shots that already existed when it was
 // created, and after shots created later. Rebuild collision hulls on release.
 for(bool completion_newer:{false,true}){
  auto setup=scenario;setup.environmental_emitters.clear();
  SimulationLabExperiment ordered;ordered.reset(config,parameters,std::span(&target,1),{},{},{},{},{},setup);
  auto& r=ordered.runs()[0];r.level_runtime_enabled=true;
  auto& e=r.entities[0];e.runtime_model_hierarchy=hierarchy;e.root_model_integrity=0;e.root_model_terminal=true;
  auto& c=e.model_components[0];c.integrity=0;c.terminal_runtime_flag8=true;c.terminal_self_reference=true;
  c.destruction_complete_tick=1;c.destruction_effect_after_projectile_serial=completion_newer?p.serial:p.serial-1;
  r.next_projectile_serial=p.serial+1;r.projectiles={p};ordered.step(field,1);
  check(e.runtime_removed,"completion did not release its root model");
  check(r.metrics.projectile_impacts==(completion_newer?0U:1U),
        "completion EFF was not interleaved with projectiles by creation order");
 }
 // A stock-style impact EFF can launch a linked EFF/SHT chain. Keeping only
 // the impact journal suppressed the child callback (stock EFF53 -> EFF23).
 {
  auto chained=std::make_shared<SimulationLabRuntimeCatalog>(*catalog);
  auto parent=eff;parent.definition_id=2;parent.runtime_value_0c=10;
  parent.linked_shot_present=0;parent.linked_shot_id=0;parent.linked_effect_present=2;
  parent.linked_effect_id=91;parent.sprite_path="impact-controller";
  chained->sprites[parent.sprite_path].damage_effect_points={{0,{0,0,0}}};
  chained->effects[2]={parent,{}};
  auto child=eff;child.definition_id=91;child.runtime_value_0c=5;child.creation_flags=1;
  chained->effects[91]={child,build_linked_shot_emissions(child)};
  auto setup=scenario;setup.runtime_catalog=chained;setup.environmental_emitters.clear();
  SimulationLabExperiment chain;chain.reset(config,parameters,std::span(&target,1),{},{},{},{},{},setup);
  auto& run=chain.runs()[0];run.level_runtime_enabled=true;run.entities[0].runtime_model_hierarchy=hierarchy;
  run.projectiles={p};const auto initial_rng=run.rng_state;
  chain.step(field,1);
  check(run.environmental_effects.size()==1&&run.environmental_effects[0].effect_id==2,
        "projectile impact did not retain its linked-effect owner");
  check(run.environmental_effects[0].pose.native_angles==std::optional{std::array<uint32_t,3>{0U,
        (0U-run.projectiles[0].launch_pitch_turn16),
        (run.projectiles[0].launch_yaw_turn16+0x8000U)&0xFFFFU}},"army impact lost native inherited angles");
  check(run.rng_state==initial_rng,"unrandomized impact consumed a draw");
  chain.step(field,2);
  check(std::any_of(run.environmental_effects.begin(),run.environmental_effects.end(),[](auto& e){return e.effect_id==91;}),
        "impact's linked child callback was skipped");
  auto expected_rng=initial_rng;retail_random_next(expected_rng);
  check(run.rng_state==expected_rng,"impact child consumed the wrong pool draws");
  chain.step(field,1);
  check(run.metrics.environmental_shots_spawned==1,"impact child did not launch its own projectile");

  // An impact and a linked EFF consume the same two draws in either order,
  // but the draw must be assigned to the correct owner, even on a birth-tick tie.
  chained->effects[2].definition.creation_flags=1;
  auto queued=parent;queued.definition_id=3;queued.linked_effect_present=1;
  chained->effects[3]={queued,{}};
  for(bool effect_is_newer:{false,true}){
   SimulationLabExperiment mixed;mixed.reset(config,parameters,std::span(&target,1),{},{},{},{},{},setup);
   auto& r=mixed.runs()[0];r.level_runtime_enabled=true;r.entities[0].runtime_model_hierarchy=hierarchy;
   r.projectiles={p};r.next_projectile_serial=p.serial+1;
   SimulationLabEnvironmentalEffect effect{3,p.spawn_tick,{},0};
   effect.next_projectile_serial_at_creation=p.serial+unsigned(effect_is_newer);
   r.environmental_effects.push_back(effect);
   auto seed=r.rng_state;const auto first=retail_random_next(seed)&65535U;
   const auto second=retail_random_next(seed)&65535U;
   mixed.step(field,1);
   size_t checked=0;
   for(const auto& active:r.environmental_effects)if(active.effect_id==2||active.effect_id==91){
    const bool should_get_first=(active.effect_id==91)==effect_is_newer;
    check(active.pose.native_angles&&(*active.pose.native_angles)[0]==(should_get_first?first:second),
          "shot impact and linked EFF were serviced in the wrong owner order");
    ++checked;
   }
   check(checked==2&&r.rng_state==seed,"mixed owner traversal skipped or repeated a callback");
  }
 }
 // Captured native sequence: a faction2 projectile survives its army;
 // faction1 ARM333 probes reuse/release that slot before impact. The impact
 // reads faction1 and clears its pending relation with faction3.
 {
  auto victim=target;victim.faction_index=3;
  auto attacker=target;attacker.faction_index=2;attacker.actor.position.x+=10;
  attacker.combat_weapons={{weapon,shot}};
  const std::array seeds{victim,attacker};auto setup=scenario;setup.environmental_emitters.clear();
  auto cfg=config;cfg.duration_ticks=1000;
  SimulationLabExperiment alias;alias.reset(cfg,parameters,seeds,{},{},{},{},{},setup);
  auto& run=alias.runs()[0];run.level_runtime_enabled=true;run.army_pool.enabled=true;
  run.faction_lifecycle_states[2]=run.faction_lifecycle_states[3]=1;
  run.entities[0].runtime_model_hierarchy=hierarchy;
  simulation_lab_claim_army_slot(run,run.entities[0],0);
  simulation_lab_claim_army_slot(run,run.entities[1],1);
  run.entities[1].combat_command_mode_flags=9;run.entities[1].target_entity_index=0;
  auto pending=p;pending.owner_kind=SimulationProjectileOwnerKind::Placement;
  pending.owner_placement_index=1;pending.spawn_tick=100;run.projectiles={pending};
  simulation_lab_release_army_slot(run,run.entities[1]);run.entities[1].runtime_removed=true;
  const auto probe=run.army_pool.create(333,1);check(probe==1,"native first-free army slot differs");
  simulation_lab_release_army_slot(run,run.entities[1]);
  check(run.army_pool.slots[*probe].occupied,"repeated entity release freed a reused army slot");
  run.army_pool.release(*probe);
  simulation_lab_apply_relation_transition(run,3,1,10,9);run.tick=100;alias.step(field,1);
  check(run.faction_pair_pressure[3][1]==256U&&run.faction_pair_pressure[3][2]==0U,
      "projectile attributed to its stable old entity instead of reused army slot");
  check(((run.faction_packed_relation_states[1]>>12U)&15U)==0U&&
        ((run.faction_packed_relation_states[3]>>4U)&15U)==0U,
        "reused projectile owner failed native diplomacy reset");
 }
 // A ray-transparent MDL excludes both its own SPR and attached child SPRs.
 auto transparent_nodes=std::make_shared<std::vector<ModelVisualNode>>(*hierarchy);
 transparent_nodes->front().simulation.runtime_render_flags|=0x100U;
 auto child_node=node;child_node.parent_index=0U;transparent_nodes->push_back(child_node);
 for(uint32_t mode=0;mode<4;++mode){auto setup=scenario;setup.environmental_emitters.clear();if(mode==1)setup.static_shot_hulls={cube(middle,.005F)};
  SimulationLabExperiment lab;lab.reset(config,parameters,std::span(&target,1),{}, {},{}, {},{},setup);auto& r=lab.runs()[0];auto& e=r.entities[0];
  e.runtime_model_hierarchy=mode==3?transparent_nodes:hierarchy;e.runtime_removed=mode==2;r.projectiles={p};const auto before=e.root_model_integrity;lab.step(field,1);
  check(e.root_model_integrity==before-(mode==0?1000:0),"wrong ownerless damage/interceptor/removal behavior");
  check(r.metrics.impact_anchor_updates==0,"ownerless hit invented hostile faction");
  if(mode<2){check(r.metrics.projectile_impacts==1,"no impact");lab.step(field,3);check(r.metrics.projectile_impacts==1,"repeated impact");}
  if(mode==3)check(r.metrics.projectile_impacts==0,"ray-transparent hierarchy intercepted debris");
  if(mode==0)check(r.projectiles[0].impact_visual_lifetime_ticks==12,"lost impact visual lifetime");
 }
 // Marking an army for dismantling must not make its still-linked model invulnerable.
 {
  auto setup=scenario;setup.environmental_emitters.clear();
  SimulationLabExperiment lab;lab.reset(config,parameters,std::span(&target,1),{},{},{},{},{},setup);
  auto& r=lab.runs()[0];auto& e=r.entities[0];e.runtime_model_hierarchy=hierarchy;
  e.root_model_terminal=true;e.runtime_flags|=0x418U;
  r.projectiles={p};const auto before=e.root_model_integrity;lab.step(field,1);
  check(e.root_model_integrity==before-1000,"dismantling model became invulnerable before its release");
 }
 // A guided projectile also retains that live model reference through flag 8.
 {
  auto guided_catalog=std::make_shared<SimulationLabRuntimeCatalog>(*catalog);
  auto guided=shot;guided.definition_id=901;guided.trajectory_mode=3;guided.mode3_lead_disabled=123;
  guided.launch_speed_q12=512;guided.projectile_lifetime_ticks=100;
  guided_catalog->shots[901]=guided;
  auto setup=scenario;setup.environmental_emitters.clear();setup.runtime_catalog=guided_catalog;
  SimulationLabExperiment lab;lab.reset(config,parameters,std::span(&target,1),{},{},{},{},{},setup);
  auto& r=lab.runs()[0];auto& e=r.entities[0];e.runtime_model_hierarchy=hierarchy;
  e.root_model_terminal=true;e.runtime_flags|=8U;
  SimulationProjectileState missile;missile.owner_kind=SimulationProjectileOwnerKind::Environmental;
  missile.shot_definition_id=901;missile.live_target_placement_index=0;missile.lifetime_ticks=100;
  missile.launch_position={e.actor.position.x-10.F,e.actor.position.y-10.F,e.actor.position.z+5.F};
  r.projectiles={missile};lab.step(field,1);
  check(r.projectiles[0].runtime_flight_yaw==123,"guided shot dropped a dismantling target before model release");
 }
 // ShotModel_CastHitRays invalidates the nearest model when its SHT EFF slot is null.
 auto effectless=shot;effectless.definition_id=29;effectless.target_impact_effect_ids.fill(0);
 catalog->shots[29]=effectless;
 auto effectless_projectile=p;effectless_projectile.shot_definition_id=29;
 {
  auto setup=scenario;setup.environmental_emitters.clear();
  SimulationLabExperiment lab;lab.reset(config,parameters,std::span(&target,1),{},{},{},{},{},setup);
  auto& r=lab.runs()[0];auto& e=r.entities[0];e.runtime_model_hierarchy=hierarchy;
  r.projectiles={effectless_projectile};const auto before=e.root_model_integrity;
  lab.step(field,1);
  check(e.root_model_integrity==before&&r.metrics.projectile_impacts==0,
        "null target-effect slot was accepted as a native collision");
  check(r.projectiles[0].impact_kind!=SimulationImpactKind::TargetModelImpactClass&&
        r.metrics.weapon_impact_effect_events==0,"ineligible model fabricated an impact");
 }
 const std::array live{SimulationLabLiveShotHull{cube(hit,.01F),0,0}};auto near=cube(middle,.005F,1);auto contact=p;
 simulation_lab_refresh_environmental_projectile_contact(contact,shot,1,*catalog,live,std::span(&near,1));
 check(contact.impact_kind==p.impact_kind&&contact.impact_target_placement_index==p.impact_target_placement_index,
       "null-effect nearest model stopped the projectile or searched behind it");
 contact=p;contact.impact_kind=SimulationImpactKind::TerrainSurface;contact.travel_ticks=1;contact.impact_position=origin;
 simulation_lab_refresh_environmental_projectile_contact(contact,shot,1,*catalog,live,{});check(contact.impact_kind==SimulationImpactKind::TerrainSurface,"replaced earlier surface hit");
 SimulationLabSeedEntity shooter;shooter.army_asset_id=1;shooter.faction_index=1;shooter.actor.position=origin;shooter.runtime_model_class_id=10;shooter.root_model_maximum_integrity=10000;
 auto gun=weapon;gun.maximum_range_q12=20*4096;gun.pitch_max_turn16=0x8000;gun.yaw_max_velocity_turn16=gun.pitch_max_velocity_turn16=0x4000;
 gun.yaw_acceleration_turn16=gun.pitch_acceleration_turn16=0x4000;auto damaging=shot;damaging.target_impact_damage_q12.fill(0);damaging.target_impact_damage_q12[1]=100;shooter.combat_weapons={{gun,damaging}};
 auto immune=shooter;immune.army_asset_id=2;immune.faction_index=2;immune.combat_weapons.clear();immune.actor.position.x+=1;immune.shot_impact_class_index=2;
 auto valid=immune;valid.army_asset_id=3;valid.actor.position.x+=1;valid.shot_impact_class_index=1;
 const std::array battle_seeds{shooter,immune,valid};SimulationLabExperiment battle;battle.reset(config,parameters,battle_seeds);battle.step(field,3);
 check(battle.runs()[0].entities[0].target_entity_index==2&&battle.runs()[0].metrics.weapon_shots_fired>0,"immune nearest model blocked valid attack");
 SimulationLabExperiment idle;idle.reset(config,parameters,std::span(&shooter,1));idle.runs()[0].entities[0].actor.weapons.resize(1);idle.runs()[0].entities[0].actor.weapons[0].cooldown_ticks=5;
 idle.step(field,5);check(idle.runs()[0].entities[0].actor.weapons[0].cooldown_ticks==0,"idle reload froze");
 if(argc>1){const auto assets=AssetCatalog::scan_archive_root(argv[1]);const auto runtime=simulation_lab_prepare_runtime_catalog(assets);
  const auto group=std::find_if(assets.map_groups().begin(),assets.map_groups().end(),[](const auto& g){return g.level_path=="level/lavam.lev";});check(group!=assets.map_groups().end(),"missing LAVAM");
  const auto doc=MapDocument::open(assets,*group);const auto env=simulation_lab_prepare_environment(assets,doc.level(),doc.field(),*runtime);
  check(!env.emitters.empty()&&runtime->effects.at(25).emissions.size()==8,"stock controller metadata missing");
  for(const auto& emitter:env.emitters){
   check(emitter.pose.native_basis.has_value(),"static emitter lost its native model basis");
   for(const auto& point:emitter.world_effect_point_records){const auto p=point.second;
    check(p.x==q12_to_float(float_to_q12(p.x))&&p.y==q12_to_float(float_to_q12(p.y))&&
          p.z==q12_to_float(float_to_q12(p.z)),"prepared environmental EFF point lost native Q12 precision");}}
  // Independent native EFF23 -> SHT8 frame at tick 12887, after the
  // linked child's orientation was randomized by its pool creation.
  SimulationLabEffectPose controller;controller.position={q12_to_float(215301),q12_to_float(-179268),q12_to_float(8931)};
  controller.native_angles=std::array<uint32_t,3>{53247,16384,0};
  controller.native_basis=retail_model_basis_q28(53247,16384,0);
  const auto& points=runtime->sprites.at(runtime->effects.at(23).definition.sprite_path).shot_points;
  const auto attachment=std::find_if(points.begin(),points.end(),[](const auto& point){return point.first==5;});
  check(attachment!=points.end(),"native controller fixture missing SPR shot point 5");
  const auto geometry=simulation_linked_effect_shot_geometry(controller.position,
      simulation_lab_model_world_point(controller,attachment->second));
  check(float_to_q12(geometry.launch_position.x)==215491&&float_to_q12(geometry.launch_position.y)==-178815&&
        float_to_q12(geometry.launch_position.z)==9198&&float_to_q12(geometry.aim_position.x)==215681&&
        float_to_q12(geometry.aim_position.y)==-178362&&float_to_q12(geometry.aim_position.z)==9465,
        "linked environmental shot geometry differs from captured native frame");
  // Independent native SHT8 flight: age 36 starts at (239320,-92641,15769)
  // with speed 324 and angles (63185,-6732), then hits the secondary surface.
  // Float offset multiplication rounded the effect's Y one Q12 too high.
  {
   const auto middle_group=std::find_if(assets.map_groups().begin(),assets.map_groups().end(),
       [](const auto& g){return g.level_path=="level/mittelpunkt.lev";});
   check(middle_group!=assets.map_groups().end(),"missing captured secondary-surface map");
   const auto middle=MapDocument::open(assets,*middle_group);
   // Another native SHT8 frame returns an intersection beyond speed 304.
   // Native keeps flying; accepting every non-null triangle result ended it
   // one step early and consumed a randomized terrain-impact draw.
   SimulationProjectileState beyond;beyond.owner_kind=SimulationProjectileOwnerKind::Environmental;
   beyond.runtime_flight_previous={q12_to_float(223776),q12_to_float(-97652),q12_to_float(16553)};
   beyond.runtime_flight_yaw=41662;beyond.runtime_flight_pitch=-6856;beyond.runtime_flight_speed_q12=304;
   const auto beyond_offset=retail_direction_scaled(uint32_t(-6856),41662,304);
   beyond.runtime_flight_ray_end={q12_to_float(223776+beyond_offset[0]),q12_to_float(-97652+beyond_offset[1]),q12_to_float(16553+beyond_offset[2])};
   check(!simulation_lab_refresh_projectile_interval_contact(beyond,runtime->shots.at(8),38,middle.field(),*runtime,{},{}),
       "out-of-step native terrain distance ended the projectile early");
   check(beyond.impact_kind==SimulationImpactKind::None&&!beyond.impact_effect_id,
       "rejected out-of-step hit retained an impact effect");
   auto bounded_beam=runtime->shots.at(8);bounded_beam.trajectory_mode=0;
   bounded_beam.launch_speed_q12=304;bounded_beam.projectile_lifetime_ticks=1;
   SimulationProjectileState beam;beam.launch_position=beyond.runtime_flight_previous;
   beam.launch_pitch_turn16=uint32_t(-6856);beam.launch_yaw_turn16=41662;
   check(!simulation_lab_refresh_beam_contact(beam,bounded_beam,middle.field(),*runtime,{},{}),
       "beam accepted a native terrain intersection beyond its length");

   SimulationProjectileState flight;flight.owner_kind=SimulationProjectileOwnerKind::Environmental;
   flight.runtime_flight_previous={q12_to_float(239320),q12_to_float(-92641),q12_to_float(15769)};
   flight.runtime_flight_yaw=63185;flight.runtime_flight_pitch=-6732;flight.runtime_flight_speed_q12=324;
   const auto offset=retail_direction_scaled(uint32_t(-6732),63185,324);
   flight.runtime_flight_ray_end={q12_to_float(239320+offset[0]),q12_to_float(-92641+offset[1]),q12_to_float(15769+offset[2])};
   check(simulation_lab_refresh_projectile_interval_contact(flight,runtime->shots.at(8),36,middle.field(),*runtime,{},{}),
       "captured native secondary surface was missed");
   check(flight.impact_kind==SimulationImpactKind::SecondarySurface&&
         float_to_q12(flight.impact_position.x)==239420&&float_to_q12(flight.impact_position.y)==-92665&&
         float_to_q12(flight.impact_position.z)==15691,"impact EFF point lost native fixed-distance projection");
  }
  // That shot hits an ARM303 / MDL316 model surface on its first native
  // callback, at distance 75. It must not escape and later create a terrain EFF.
  SimulationLabRun collision_run;SimulationLabEntity factory;
  factory.runtime_model_class_id=13;factory.construction_complete=true;factory.production_child3_removed=true;
  factory.actor.body_yaw_turn16=54604;
  factory.actor.position={q12_to_float(214320),q12_to_float(-179910),q12_to_float(7491)};
  const auto& hierarchy316=runtime->definitions.at(316).hierarchy_nodes;
  factory.runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(hierarchy316);
  factory.runtime_model_node_height_offsets_q12.resize(hierarchy316.size());
  // Native MDL316's channel is [-100,100] Q12 with step 16. Phase
  // 19 supplies the captured panel translation 92, independently of map time.
  const auto& factory_profile=hierarchy316.front().simulation;
  check(factory_profile.pitch_min_turn16==-100&&factory_profile.pitch_max_turn16==100&&
        factory_profile.pitch_max_velocity_turn16==16,"native factory fixture channel metadata changed");
  factory.production_animated_subnode_ticks=19;
  collision_run.entities.push_back(factory);
  const auto hulls=simulation_lab_current_shot_hulls(collision_run,doc.field(),*runtime);
  const SimulationLabFixedRay ray{retail_direction_scaled(5485,12238,1<<28),300};
  const Vec3f direction{float(ray.direction_q28[0])/268435456.F,float(ray.direction_q28[1])/268435456.F,float(ray.direction_q28[2])/268435456.F};
  const auto hit=simulation_lab_raycast_models(geometry.launch_position,direction,q12_to_float(300),{},hulls,{},&ray);
  if(!hit)std::cerr<<"NATIVE_FACTORY_RAY miss hulls="<<hulls.size()<<" nodes="<<hierarchy316.size()<<'\n';
  else std::cerr<<"NATIVE_FACTORY_RAY distance="<<float_to_q12(hit->distance)<<" class="<<hit->impact_class<<'\n';
  check(hit&&hit->impact_class==7&&float_to_q12(hit->distance)==75,"captured native factory surface contact was missed");
  // L01/54321: a first SHT11 destroys ARM586 at15956; its terrain-
  // contact terminal effect completes before the older second missile15957.
  {
   const auto level=LevelAsset::parse(assets.load("level/luke/l01_dejavu.lev"));
   auto lc=AssetCatalog::scan_archive_root(argv[1],level.model_resource_paths_in_load_order(),level.army_resource_paths_in_load_order());
   const auto group=std::find_if(lc.map_groups().begin(),lc.map_groups().end(),[](const auto& g){return g.level_path=="level/luke/l01_dejavu.lev";});
   const auto map=MapDocument::open(lc,*group);auto lr=simulation_lab_prepare_runtime_catalog(lc);
   const auto surroundings=simulation_lab_prepare_environment(lc,level,map.field(),*lr);
   const auto blockers=simulation_lab_prepare_static_navigation_blockers(lc,level);
   size_t sinking=0;
   for(const auto& scenery:surroundings.crushable_scenery){
    if(lc.army_preset(scenery.army_asset_id)->runtime_model_class_id==12U)++sinking;
    check(std::none_of(blockers.begin(),blockers.end(),[&](const auto& b){return b.source_placement_index==scenery.placement_index;}),
        "mutable scenery left a duplicate permanent navigation collider");
   }
   check(sinking>0,"L01 ownership regression did not cover preplaced sinking ruins");
   SimulationLabScenarioRuntimeSeed scene;scene.enabled=true;scene.active_faction_count=1;scene.ai_planning_enabled=false;
   scene.runtime_catalog=lr;scene.crushable_scenery.push_back({178,586,0,{q12_to_float(195869),q12_to_float(-215711),0}});
   auto cfg=config;cfg.duration_ticks=15960;
   SimulationLabProductionAsset tree;tree.army_asset_id=586;tree.entity_seed.army_asset_id=586;
   tree.entity_seed.root_model_maximum_integrity=50;
   SimulationLabExperiment test;test.reset(cfg,parameters,{},{},std::span(&tree,1),{},{},{},scene);
   auto& r=test.runs()[0];check(r.entities.size()==1,"tree completion fixture missing ARM586");r.tick=15955;
   SimulationProjectileState lower;lower.serial=1;lower.shot_definition_id=11;lower.spawn_tick=15943;
   lower.owner_kind=SimulationProjectileOwnerKind::Environmental;lower.runtime_flight_initialized=true;
   lower.runtime_flight_age=12;lower.runtime_flight_tick=15955;lower.runtime_flight_speed_q12=750;
   lower.runtime_flight_yaw=54225;lower.runtime_flight_pitch=-1421;
   lower.runtime_flight_position={q12_to_float(195525),q12_to_float(-214892),q12_to_float(8211)};
   auto upper=lower;upper.serial=2;upper.runtime_flight_yaw=54451;upper.runtime_flight_pitch=-2343;
   upper.runtime_flight_position={q12_to_float(195330),q12_to_float(-214996),q12_to_float(9145)};
   r.projectiles={lower,upper};r.next_projectile_serial=3;test.step(map.field(),1);
   check(r.entities[0].root_model_integrity==0,"captured first missile did not destroy the tree");
   const auto& c=r.entities[0].model_components[0];
   check(c.destruction_complete_tick==15957&&c.destruction_effect_after_projectile_serial==2,
       "terrain-contact terminal completion lost its owner-list creation order");
   test.step(map.field(),1);
   check(r.entities[0].runtime_removed&&!r.projectiles[1].impact_damage_applied,
       "second missile hit the tree after its newer completion effect released it");
  }
  // L01 seed24680 tick12683: the extractor's preview-only child3 was
  // absent in native. Its large ground quad must not intercept this shot.
  {
   SimulationLabEntity extractor;extractor.runtime_model_class_id=14;extractor.construction_complete=true;
   extractor.actor.position={q12_to_float(180890),q12_to_float(-209895),q12_to_float(7128)};
   extractor.actor.body_yaw_turn16=41182;
   const auto& hierarchy=runtime->definitions.at(303).hierarchy_nodes;
   extractor.runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(hierarchy);
   extractor.runtime_model_node_height_offsets_q12.resize(hierarchy.size());
   SimulationLabRun r;r.entities={extractor};auto hulls=simulation_lab_current_shot_hulls(r,doc.field(),*runtime);
   check(!hulls.empty()&&hulls.front().hull.native_bounding_radius_q12==5842,
       "detached extractor preview changed native subtree radius");
   SimulationLabFixedRay ray{retail_direction_scaled(1590,10326,1<<28),500};
   Vec3f origin{q12_to_float(177490),q12_to_float(-213856),q12_to_float(7015)};
   Vec3f direction{float(ray.direction_q28[0])/268435456.F,float(ray.direction_q28[1])/268435456.F,float(ray.direction_q28[2])/268435456.F};
   check(!simulation_lab_raycast_models(origin,direction,q12_to_float(500),{},hulls,{},&ray),
       "extractor preview plane fabricated a native projectile hit");
  }
  SimulationLabScenarioRuntimeSeed stock;stock.runtime_catalog=runtime;stock.environmental_emitters=env.emitters;stock.static_shot_hulls=env.collision_hulls;
  SimulationLabExperiment lab;lab.reset(config,parameters,{}, {},{}, {},{}, {},stock);lab.step(doc.field(),80);const auto& state=lab.runs()[0];
  check(state.metrics.environmental_shots_spawned>0,"stock lava emitted no real shot");
  std::cout<<"LAVAM80 emitters="<<env.emitters.size()<<" hulls="<<env.collision_hulls.size()<<" effects="<<state.metrics.environmental_effects_spawned<<" shots="<<state.metrics.environmental_shots_spawned<<" impacts="<<state.metrics.projectile_impacts<<'\n';
  // Put a controlled, depth-immune MODEL into the next interval of an actual
  // stock lava shot. This isolates projectile damage from drowning and AI.
  const auto active=std::find_if(state.projectiles.begin(),state.projectiles.end(),[](const auto& p){return !p.impact_damage_applied;});
  check(active!=state.projectiles.end(),"stock lava had no active shot to test");
  auto incoming=*active;const auto& lava_shot=runtime->shots.at(incoming.shot_definition_id);
  auto next=incoming;check(simulation_begin_projectile_runtime_step(next,lava_shot,state.tick+1),"lava interval expired");
  uint32_t cls=0;while(cls<8&&(lava_shot.target_impact_damage_q12[cls]<=0||!lava_shot.target_impact_effect_id(cls)))++cls;
  check(cls<8,"stock lava SHT has no damaging eligible target class");
  const Vec3f contact{(next.runtime_flight_previous.x+next.runtime_flight_ray_end.x)*.5F,
      (next.runtime_flight_previous.y+next.runtime_flight_ray_end.y)*.5F,(next.runtime_flight_previous.z+next.runtime_flight_ray_end.z)*.5F};
  auto augmented=std::make_shared<SimulationLabRuntimeCatalog>(*runtime);augmented->sprites["target"]=sprite;
  auto victim=target;victim.army_asset_id=900000;victim.actor.position=contact;victim.shot_impact_class_index=cls;
  victim.root_model_maximum_integrity=1000000;victim.model_components[0].maximum_integrity=1000000;
  auto victim_nodes=std::make_shared<std::vector<ModelVisualNode>>(*hierarchy);
  (*victim_nodes)[0].model_definition_id=900000;(*victim_nodes)[0].shot_impact_class_index=cls;
  victim.model_components[0].model_definition_id=900000;
  SimulationLabScenarioRuntimeSeed isolated;isolated.runtime_catalog=augmented;isolated.ai_planning_enabled=false;
  SimulationLabExperiment intercepted;intercepted.reset(config,parameters,std::span(&victim,1),{},{},{},{},{},isolated);
  auto& hit_run=intercepted.runs()[0];hit_run.tick=state.tick;hit_run.entities[0].runtime_model_hierarchy=victim_nodes;hit_run.projectiles={incoming};
  intercepted.step(doc.field());
  std::cout<<"LAVAM_CONTACT class="<<cls<<" raw_damage="<<lava_shot.target_impact_damage_q12[cls]
      <<" health="<<hit_run.entities[0].root_model_integrity<<" contacts="<<hit_run.metrics.projectile_impacts<<'\n';
  check(hit_run.entities[0].root_model_integrity==1000000-lava_shot.target_impact_damage_q12[cls],"stock lava projectile failed to damage contacted MODEL");
  check(hit_run.metrics.impact_anchor_updates==0,"stock lava invented a hostile faction");
  std::cout<<"LAVAM_DAMAGE PASS shot="<<incoming.shot_definition_id<<" target_class="<<cls<<" lost="<<1000000-hit_run.entities[0].root_model_integrity<<" water_damage="<<hit_run.metrics.water_damage_integrity_lost<<'\n';
 }
 std::cout<<"V616_ENVIRONMENT_PROJECTILE PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
