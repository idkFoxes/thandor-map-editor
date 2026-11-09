#include "thandor/simulation_lab_environment.hpp"
#include "thandor/retail_presence.hpp"
#include "thandor/retail_contact.hpp"
#include "thandor/simulation_lab_follow.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main()try{
 for(auto cls:{14U,15U}){
  auto nodes=std::make_shared<std::vector<ModelVisualNode>>(4);
  (*nodes)[0].runtime_class_id=cls;(*nodes)[0].model_definition_id=303;
  (*nodes)[1]=(*nodes)[0];(*nodes)[1].parent_index=0;(*nodes)[1].child_slot=cls==14?3:1;
  (*nodes)[2]=(*nodes)[0];(*nodes)[2].parent_index=1;(*nodes)[2].child_slot=0;
  (*nodes)[3]=(*nodes)[0];(*nodes)[3].parent_index=0;(*nodes)[3].child_slot=0;
  SimulationLabEntity world;world.runtime_model_hierarchy=nodes;
  check(simulation_lab_model_node_present(world,0)&&simulation_lab_model_node_present(world,3)&&
        !simulation_lab_model_node_present(world,1)&&!simulation_lab_model_node_present(world,2),
        "extractor/storage preview-only subtree remained in world simulation");
 }

 // Native Asgard seed 98765, class-17 root at tick 15304. Banking is
 // applied in world space; reversing the product doubles its azimuth.
 check(retail_compose_model_angles({21313,16384,0},{0,14336,21314})==
       std::array<uint32_t,3>{21312,14335,21313},"banking composition order differs from native");

 auto field=FieldGrid::create(59,59,0,0);const auto& c=field.at(20,20);
 SimulationLabRuntimeCatalog catalog;
 auto nodes=std::make_shared<std::vector<ModelVisualNode>>(3);
 auto& root=(*nodes)[0];root.parent_index=UINT32_MAX;root.model_definition_id=1;root.runtime_class_id=11;
 root.sprite_path="root";root.simulation.placement_contact_kind=0;root.simulation.fog_uncover_radius_q12=20000;root.simulation.fog_reference_height_offset_q12=1500;
 auto& visual=(*nodes)[1];visual=root;visual.parent_index=0;visual.child_slot=0;visual.sprite_path="visual";visual.local_rotation_angle1=0x4000;
 auto& attached=(*nodes)[2];attached.parent_index=1;attached.child_slot=0;attached.model_definition_id=2;
 attached.local_rotation_angle1=0x4000;attached.runtime_class_id=14;attached.sprite_path="attached";attached.simulation.fog_reference_height_offset_q12=1500;
 catalog.sprites["root"].child_translations_q12[0]=std::array<int32_t,3>{0,0,2000};
 catalog.sprites["visual"].child_translations_q12[0]=std::array<int32_t,3>{0,0,2440};
 SimulationLabEntity entity;entity.runtime_model_hierarchy=nodes;entity.actor.position={q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),17};
 entity.model_components.resize(2);entity.model_components[0].source_model_node_index=0;
 entity.model_components[1].source_model_node_index=2;entity.model_components[1].runtime_class_id=14;
 check(simulation_lab_prepare_missing_model_geometry(entity,field,catalog),"missing creation geometry not prepared");
 check(entity.fog_reference_height_offset_q12==5940,"attached model height excluded from native eye height");
 check(float_to_q12(entity.actor.position.z)==0,"root was not seated on terrain at creation");
 check(entity.model_components[1].extraction_model_world_offset_at_seed.has_value(),"linked extractor lost its registration point");
 check(float_to_q12(entity.model_components[1].extraction_model_world_offset_at_seed->z)==4440,"extractor used army root instead of attached model position");
 const auto cached=entity.runtime_model_node_height_offsets_q12;entity.actor.position.z=99;
 check(!simulation_lab_prepare_missing_model_geometry(entity,field,catalog)&&entity.runtime_model_node_height_offsets_q12==cached,"moving owner resampled cached creation metrics");
 // Contact kinds use separate terrain/water/top rules and sprite offsets.
 auto one=std::make_shared<std::vector<ModelVisualNode>>(1,root);entity.runtime_model_hierarchy=one;
 for(auto& cell:field.cells()){cell.terrain_height_q12=8192;cell.water_delta_q12=4096;}
 catalog.sprites["root"].height_offset=q12_to_float(123);(*one)[0].simulation.placement_height_offset_q12=77;
 for(int kind:{0,1,2,4}){(*one)[0].simulation.placement_contact_kind=kind;
  check(simulation_lab_place_root_on_surface(entity,field,catalog),"valid surface rejected");
  check(float_to_q12(entity.actor.position.z)==((kind==0||kind==2)?8192+123+77:12288+77),"native contact height or sprite exception differs");}
 (*one)[0].simulation.placement_contact_kind=3;entity.actor.position.z=11;
 check(!simulation_lab_place_root_on_surface(entity,field,catalog)&&entity.actor.position.z==11,"suspension height overwritten by ground placement");
 for(auto& cell:field.cells()){cell.terrain_height_q12=0;cell.water_delta_q12=0;}
 // Removing a weapon must not change a class-4 building's fixed geometry.
 entity.runtime_model_class_id=4U;entity.actor.body_yaw_turn16=0x3456U;
 (*one)[0].simulation.placement_contact_kind=0;
 const auto unarmed_pose=simulation_lab_current_model_poses(entity,field,catalog).front();
 entity.combat_weapons.push_back({});
 const auto armed_pose=simulation_lab_current_model_poses(entity,field,catalog).front();
 check(unarmed_pose.native_basis&&unarmed_pose.native_basis==armed_pose.native_basis,
       "unarmed class-4 model lost native fixed-point geometry");
 const Vec3f marker{q12_to_float(537),q12_to_float(-135),q12_to_float(376)};
 entity.runtime_model_class_id=15U;
 const auto storage_pose=simulation_lab_current_model_poses(entity,field,catalog).front();
 check(storage_pose.native_basis==unarmed_pose.native_basis,
       "unarmed storage model lost native fixed-point geometry");
 entity.runtime_model_class_id=4U;
 const auto unarmed_point=simulation_lab_model_world_point(unarmed_pose,marker);
 const auto armed_point=simulation_lab_model_world_point(armed_pose,marker);
 check(float_to_q12(unarmed_point.x)==float_to_q12(armed_point.x)&&
       float_to_q12(unarmed_point.y)==float_to_q12(armed_point.y)&&
       float_to_q12(unarmed_point.z)==float_to_q12(armed_point.z),
       "weapon inventory changed a terminal effect's model point");
 // Failed native terrain interpolation retains the old planar position.
 {
  auto terrain=FieldGrid::create(59,59,0,0);
  auto runtime=std::make_shared<SimulationLabRuntimeCatalog>();
  ModelVisualNode mobile{};mobile.parent_index=UINT32_MAX;mobile.model_definition_id=200;
  mobile.runtime_class_id=1;mobile.sprite_path="mobile";mobile.simulation.maximum_integrity=100;
  mobile.simulation.placement_contact_kind=2;
  ArmyAssetPreset preset{};preset.registry_id=200;preset.runtime_model_class_id=1;preset.model_hierarchy={mobile};preset.resolved_model_hierarchy={mobile};
  ArmyModelVariantGroup group{};ModelTechnologyVariant variant{};
  variant.model_definition_id=200;variant.model_hierarchy={mobile};group.variants={variant};preset.model_variant_groups={group};
  runtime->armies.emplace(200,preset);runtime->sprites["mobile"]={};
  ModelVisualRecord definition{};definition.definition_id=200;definition.runtime_class_id=1;definition.simulation=mobile.simulation;definition.hierarchy_nodes={mobile};runtime->definitions.emplace(200,definition);
  const auto& start=terrain.at(20,20);
  SimulationLabSeedEntity walker{};walker.army_asset_id=200;walker.faction_index=1;walker.runtime_model_class_id=1;
  walker.model_components=simulation_lab_model_components_from_hierarchy(preset.model_hierarchy);
  walker.actor.position={q12_to_float(start.world_x_q12),q12_to_float(start.world_y_q12),0};
  walker.actor.move_target=Vec3f{walker.actor.position.x+4,walker.actor.position.y,0};
  walker.motion.ground_speed_control=true;walker.motion.placement_contact_kind=2;
  walker.motion.speed_q12_per_tick=512;walker.motion.ground_acceleration_q12=512;walker.motion.ground_slope_threshold_turn16=10500;walker.motion.ground_secondary_slope_threshold_turn16=14500;walker.motion.yaw_max_velocity_turn16=0x7fff;walker.motion.yaw_acceleration_turn16=0x7fff;
  terrain.at(23,20).flags_and_material|=0x80000000U;
  SimulationLabConfig settings{};settings.run_count=1;settings.duration_ticks=100;
  SimulationLabScenarioRuntimeSeed session{};session.enabled=true;session.active_faction_count=session.assignable_faction_count=1;session.runtime_catalog=runtime;
  std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
  SimulationLabExperiment simulation;simulation.reset(settings,parameters,std::span(&walker,1),{},{},{},{},{},session);
  simulation.runs()[0].ai_planning_enabled=false;
  simulation.runs()[0].entities[0].motion=walker.motion;
  V565PathTerrainCache routes;
  simulation_lab_set_site_route(simulation.runs()[0],terrain,0,
      {float_to_q12(walker.actor.position.x+4),float_to_q12(walker.actor.position.y)},{},routes);
  bool advanced=false;
  for(unsigned tick=0;tick<100;++tick){
   simulation.step(terrain,1);const auto& position=simulation.runs()[0].entities[0].actor.position;
   advanced|=position.x!=walker.actor.position.x;
   check(terrain.sample_retail_surface_heights_q12(float_to_q12(position.x),float_to_q12(position.y)).valid,
         "failed terrain contact retained provisional ground translation");
  }
  check(advanced,"terrain rejection fixture never attempted ground motion");
 }
 // The bottom presence fringe has +1, not -2, column displacement.
 SimulationLabRetailScratchImage presence;presence.width=field.width()*4;presence.height=field.height()*4;
 presence.state.assign(size_t(presence.width)*presence.height,0x02000000U);
 presence.field_presence_by_faction.assign(field.cells().size(),0);presence.field_presence_by_faction[20*field.width()+20]=2;
 retail_propagate_field_presence_into_scratch(presence,field);
 check((presence.state[size_t(85)*presence.width+81]&2U)!=0U,"native lower-right presence fringe omitted");
 check((presence.state[size_t(85)*presence.width+78]&2U)==0U,"non-native lower-left presence fringe admitted");
 check((presence.state[size_t(85)*presence.width+81]&0x02000000U)!=0U,"presence write destroyed terrain bands");
 // The world job issues an AI order after owner motion. First steering is next tick.
 SimulationLabSeedEntity unit;unit.army_asset_id=100;unit.faction_index=1;unit.runtime_model_class_id=2;
 unit.actor.position={q12_to_float(c.world_x_q12)+2,q12_to_float(c.world_y_q12),0};unit.root_model_maximum_integrity=1000;
 unit.motion.speed_q12_per_tick=4096;unit.motion.yaw_max_velocity_turn16=0x7fff;unit.motion.yaw_acceleration_turn16=0x7fff;
 unit.fog_uncover_radius_q12=8*4096;unit.fog_reference_height_offset_q12=4*4096;unit.occupancy_mark_radius_units=4096;
 unit.has_traversal_profile=true;unit.traversal_low_band_index=0;unit.traversal_high_band_index=4;
 SimulationLabProductionAsset product;product.army_asset_id=100;product.entity_seed=unit;product.ai_definition_class_value70=1;
 SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=32;
 SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=scenario.assignable_faction_count=2;scenario.simulate_all_active_factions_as_ai=true;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};ki[0x23]=250000;ki[0x27]=4096;
 SimulationLabExperiment lab;lab.reset(cfg,ki,std::span(&unit,1),{},std::span(&product,1),{},{},{},scenario);
 auto& actor=lab.runs()[0].entities[0].actor;lab.step(field,9);const auto before=actor.position;const auto yaw=actor.body_yaw_turn16;
 lab.step(field,1);check(lab.runs()[0].metrics.military_ai_site_orders!=0,"fixture produced no world-job order");
 check(actor.position.x==before.x&&actor.position.y==before.y&&actor.body_yaw_turn16==yaw&&actor.body_turn_velocity_turn16==0,"new AI order moved or turned an owner in the completed callback");
 lab.step(field,2);check(actor.body_turn_velocity_turn16!=0||actor.body_yaw_turn16!=yaw||actor.position.x!=before.x||actor.position.y!=before.y,"world-job order did not take effect on following callbacks");
 lab.runs()[0].ai_planning_enabled=false;
 lab.runs()[0].entities[0].motion.ground_speed_control=true;
 actor.move_target.reset();actor.move_waypoint.reset();actor.body_turn_velocity_turn16=500;
 lab.step(field,1);check(actor.body_turn_velocity_turn16==0,"idle ground callback retained steering momentum for its next order");
 std::cout<<"MODEL_GEOMETRY_WORLD_ORDER_V776 PASS eye height, extractor registration, contact classes, creation cache and next-tick AI orders\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
