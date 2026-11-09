#include "thandor/simulation_lab_environment.hpp"
#include "thandor/binary.hpp"
#include "thandor/simulation_lab_geometry.hpp"
#include "thandor/simulation_lab_shot_rays.hpp"
#include "thandor/retail_contact.hpp"
#include <iostream>
#include <cmath>
#include <limits>
#include <stdexcept>
using namespace thandor;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
SimulationLabStaticShotHull box(float x0,float x1){SimulationLabStaticShotHull h;h.impact_class=1;
 for(uint32_t i=0;i<8;++i)h.corners[i]={(i&1)?x1:x0,(i&2)?.2F:-.2F,(i&4)?2.F:0.F};return h;}
int main()try{
 {
  auto surface=std::make_shared<SimulationLabTriangleSurface>();
  SpriteRaycastTriangle triangle;triangle.vertices_q12={std::array<int32_t,3>{0,0,0},
      std::array<int32_t,3>{4096,0,0},std::array<int32_t,3>{0,4096,0}};
  triangle.normal_q12={0,0,4096};surface->native_triangles.push_back(triangle);
  SimulationLabStaticShotHull hull;hull.impact_class=7;hull.collision_surface=surface;
  hull.native_bounding_radius_q12=8192;hull.collision_pose.native_angles=std::array<uint32_t,3>{0,0x4000,0};
  hull.collision_pose.native_basis=retail_model_basis_q28(0,0x4000,0);
  // Visual bounds can exclude a quantized native face edge by one Q12 unit.
  for(uint32_t i=0;i<8;++i)hull.corners[i]={(i&1)?1.F:q12_to_float(1),(i&2)?1.F:0.F,0.F};
  const SimulationLabFixedRay ray{{0,0,-(1<<28)},8192};const std::array hulls{hull};
  const auto hit=simulation_lab_raycast_models({0,.5F,1},{0,0,-1},2,{}, {},hulls,&ray);
  check(hit&&hit->impact_class==7&&float_to_q12(hit->distance)==4096,
      "render AABB rejected a packed native triangle edge hit");
  // Native range admission compares signed high dwords before dividing.
  // A face can pass this coarse check while its final distance exceeds the
  // requested length; an extra exact cap incorrectly changes ballistic LOS.
  const SimulationLabFixedRay short_ray{{0,0,-(1<<28)},4202};
  const auto coarse_hit=simulation_lab_raycast_models({0,.5F,q12_to_float(4207)},
      {0,0,-1},q12_to_float(4202),{}, {},hulls,&short_ray);
  check(coarse_hit&&float_to_q12(coarse_hit->distance)==4207,
      "exact distance cap rejected a native quantized plane-range hit");
  const SimulationLabFixedRay stopped_ray{{0,0,-(1<<28)},0};
  const auto point_hit=simulation_lab_raycast_models({.25F,.25F,0.F},
      {0,0,-1},0.F,{}, {},hulls,&stopped_ray);
  check(point_hit&&point_hit->distance==0.F,
      "zero-speed native ray missed a face through its origin");
  auto empty=hull;empty.collision_surface.reset();const std::array empty_hulls{empty};
  check(!simulation_lab_raycast_models({.5F,.5F,1},{0,0,-1},2,{}, {},empty_hulls,&ray),
      "native bounds without mesh triangles became a solid box");

 }

 {
  // A packed SPR face with a repeated vertex uses the invalid normal sentinel.
  auto surface=std::make_shared<SimulationLabTriangleSurface>();
  constexpr auto sentinel=std::numeric_limits<int32_t>::min();
  const std::array<int32_t,3> repeated{339,-126,7};
  surface->native_triangles.push_back({{repeated,repeated,{339,134,7}},{sentinel,sentinel,sentinel}});
  SimulationLabStaticShotHull hull;
  hull.collision_surface=surface;
  hull.collision_pose.position={q12_to_float(118407),q12_to_float(-105211),q12_to_float(23825)};
  hull.collision_pose.native_angles=std::array<uint32_t,3>{21805U,0U-0x4000U,0U};
  hull.native_bounding_radius_q12=1876;
  const std::array hulls{hull};
  const Vec3f origin{q12_to_float(116003),q12_to_float(-108235),q12_to_float(23815)};
  const SimulationLabFixedRay ray{retail_direction_scaled(0U-533U,10800U,1<<28),37600};
  const Vec3f direction{float(ray.direction_q28[0])/(1<<28),float(ray.direction_q28[1])/(1<<28),
      float(ray.direction_q28[2])/(1<<28)};
  check(!simulation_lab_raycast_models(origin,direction,q12_to_float(ray.distance_q12),{}, {},hulls,&ray),
      "packed invalid normal created a hit on a degenerate face");
 }

 {
  // A fatal scenery hit stores angle0 but retains the identity render matrix.
  // The native inverse of that stored orientation moves this near-face origin
  // from (1028,7,-1350) to (1027,6,-1350), giving distance7 instead of6.
  const auto field=FieldGrid::create(59,59,0,0U);
  SimulationLabRuntimeCatalog catalog;
  ModelVisualNode node;node.parent_index=UINT32_MAX;node.model_definition_id=501;
  node.sprite_path="test/scenery";node.runtime_class_id=0;
  SimulationLabEntity entity;entity.runtime_model_class_id=0;
  entity.actor.position={q12_to_float(42625),q12_to_float(-48505),q12_to_float(25355)};
  entity.runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(std::vector{node});
  entity.runtime_model_node_height_offsets_q12={0};entity.model_components.resize(1);
  auto& component=entity.model_components[0];component.source_model_node_index=0;
  component.runtime_class_id=0;component.placement_contact_kind=0;
  component.terminal_self_reference=true;component.model_rotation_angle0=21041;
  const auto poses=simulation_lab_current_model_poses(entity,field,catalog);
  check(poses[0].native_basis==retail_model_basis_q28(0,0x4000,0),
        "stored impact angle rebuilt the native cached render matrix");
  check(poses[0].native_angles&&(*poses[0].native_angles)[2]==21041,
        "fatal scenery impact angle was lost from the collision pose");
  auto surface=std::make_shared<SimulationLabTriangleSurface>();
  SpriteRaycastTriangle impact_face;
  impact_face.vertices_q12={std::array<int32_t,3>{1215,13,2306},
      std::array<int32_t,3>{1215,13,-2326},std::array<int32_t,3>{-1210,13,2306}};
  impact_face.normal_q12={0,4096,0};surface->native_triangles.push_back(impact_face);
  SimulationLabStaticShotHull hull;hull.impact_class=7;hull.collision_surface=surface;
  hull.collision_pose=poses[0];hull.native_bounding_radius_q12=8192;
  const SimulationLabFixedRay ray{retail_direction_scaled(0U-392U,21367U,1<<28),1125};
  const Vec3f direction{float(ray.direction_q28[0])/(1<<28),float(ray.direction_q28[1])/(1<<28),float(ray.direction_q28[2])/(1<<28)};
  const std::array hulls{hull};
  const auto hit=simulation_lab_raycast_models({q12_to_float(43653),q12_to_float(-48498),q12_to_float(24005)},direction,q12_to_float(1125),{}, {},hulls,&ray);
  check(hit&&float_to_q12(hit->distance)==7,"raycast ignored stored scenery angles after a fatal impact");
 }

 // Decode aim height independently of the fog eye-height field and preserve its sign.
 std::vector<std::byte> bytes(0x480);write_u32(bytes,0,fourcc("mdl"));write_u32(bytes,4,bytes.size());write_u32(bytes,0xB0,1);
 write_u32(bytes,0x200,0x280);write_u32(bytes,0x208,1);write_u32(bytes,0x24C,11);write_u32(bytes,0x250,static_cast<uint32_t>(-4096));write_u32(bytes,0x270,8192);
 write_u32(bytes,0x264,0x300);write_u32(bytes,0x300,0x80);write_u32(bytes,0x30C,0x4000);
 const std::string path="spr/test.spr";for(size_t i=0;i<path.size();++i)write_u16(bytes,0x338+i*2,static_cast<uint16_t>(path[i]));
 const auto definitions=parse_model_visual_records(bytes,"test.mdl");
 check(definitions[0].simulation.aim_height_offset_q12==-4096&&definitions[0].simulation.fog_reference_height_offset_q12==8192,"MDL aim height lost its independent signed field");
 SimulationLabRuntimeCatalog catalog;catalog.effect_lifetimes[44]=8;
 ModelVisualNode root;root.parent_index=UINT32_MAX;root.model_definition_id=1;root.sprite_path="root";root.simulation=definitions[0].simulation;
 SimulationLabEntity entity;entity.runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(std::vector{root});
 auto field=FieldGrid::create(40,40,0,0);entity.actor.position={q12_to_float(field.at(15,15).world_x_q12),q12_to_float(field.at(15,15).world_y_q12),0};catalog.sprites["root"].height_offset=2;
 check(std::abs(simulation_lab_target_aim_point(entity,field,catalog).z-1.F)<1.e-5F,"target aim did not add MDL height to contacted root position");
 // A class-21 aim child may be destroyed independently of its root.  A cached
 // pose is not evidence that the detached child is still a valid aim target.
 ModelVisualNode aim_child;aim_child.parent_index=0;aim_child.child_slot=0;
 aim_child.model_definition_id=2;aim_child.sprite_path="child";
 auto hierarchy=std::vector<ModelVisualNode>{root,aim_child};
 SimulationLabEntity split_target;
 split_target.runtime_model_class_id=21;
 split_target.actor.position=entity.actor.position;
 split_target.runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(hierarchy);
 split_target.model_components.resize(2);
 split_target.model_components[0].source_model_node_index=0;
 split_target.model_components[1].source_model_node_index=1;
 split_target.model_components[1].parent_component_index=0;
 catalog.sprites["root"].child_translations_q12[0]=std::array<std::int32_t,3>{0,0,12288};
 catalog.sprites["root"].shot_points.emplace_back(0U,Vec3f{});
 const auto mounted_geometry=simulation_lab_target_aim_geometry(split_target,field,catalog);
 const auto mounted_aim=mounted_geometry.selected;
 check(std::abs(mounted_aim.z-4.F)<1.e-4F,"class-21 live child-zero aim was not used");
 check(std::abs(mounted_geometry.root_aim_z-1.F)<1.e-4F,
       "class-21 endpoint's root height incorrectly followed child-zero");
 SimulationWeaponProfile child_pivot;
 child_pivot.launch_model_node_index=0;child_pivot.launch_model_node_indices={0};
 child_pivot.aim_model_node_index=1;child_pivot.yaw_model_node_index=1;
 const SimulationWeaponState child_weapon{};
 const auto before_detach=simulation_lab_weapon_geometry(split_target,child_pivot,child_weapon,field,catalog);
 check(std::abs(before_detach.aim_origin.z-5.F)<1.e-4F,"live weapon pivot did not use the mounted child pose");
 split_target.model_components[1].runtime_removed=true;
 check(std::abs(simulation_lab_target_aim_point(split_target,field,catalog).z-1.F)<1.e-4F,
       "detached class-21 target retained a ghost aimpoint");
 const auto after_detach=simulation_lab_weapon_geometry(split_target,child_pivot,child_weapon,field,catalog);
 check(std::abs(after_detach.aim_origin.z-2.F)<1.e-4F,
       "detached weapon pivot overrode the surviving launch position");
 ShotDefinitionRecord shot;shot.trajectory_mode=1;shot.launch_speed_q12=4096;shot.ballistic_divisor_q12=1;shot.projectile_lifetime_ticks=20;shot.target_impact_effect_ids[1]=44;
 SimulationWeaponProfile weapon;weapon.shot_definition_id=1;weapon.reload_ticks=1;weapon.launch_attachment_count=1;uint64_t serial=1;
 const std::array launch{Vec3f{0,0,1}};
 auto make_projectile=[&](){auto volley=simulation_create_volley(0,0,weapon,launch,{8,0,1},shot,0,serial);
  check(!volley.empty(),"test ballistic launch could not be created");auto p=volley.front();
  p.launch_velocity={1,0,0};return p;};
 auto expiry=make_projectile();
 const auto before_expiry=simulation_projectile_position(expiry,shot,19);
 const auto at_expiry=simulation_projectile_position(expiry,shot,20);
 check(before_expiry.x==19.F&&at_expiry.x==20.F,"ballistic expiry snapped back to initial aim point");
 const std::array false_endpoint_hull{SimulationLabLiveShotHull{box(10.F,11.F),2,0}};
 check(!simulation_lab_refresh_environmental_projectile_contact(expiry,shot,20,catalog,false_endpoint_hull,{}),"ballistic expiry manufactured a return-leg model hit");
 auto projectile=make_projectile();
 const std::array hulls{SimulationLabLiveShotHull{box(-.1F,.1F),0,0},SimulationLabLiveShotHull{box(.4F,.6F),2,3},SimulationLabLiveShotHull{box(.8F,.9F),1,0}};
 check(simulation_lab_refresh_environmental_projectile_contact(projectile,shot,1,catalog,hulls,{}),"ballistic army shot missed live model contact");
 check(projectile.impact_target_placement_index==2&&projectile.impact_target_model_component_index==3,"nearest intercept or owner exclusion was wrong");
 check(projectile.impact_visual_lifetime_ticks==8&&projectile.travel_ticks==1,"intercept failed to shorten shot lifetime");
 auto terrain=make_projectile();terrain.travel_ticks=1;terrain.impact_kind=SimulationImpactKind::TerrainSurface;terrain.impact_position={.1F,0,1};
 check(!simulation_lab_refresh_environmental_projectile_contact(terrain,shot,1,catalog,hulls,{})&&terrain.impact_kind==SimulationImpactKind::TerrainSurface,"farther model replaced nearer terrain");
 auto stat=make_projectile();const std::array static_hulls{box(.2F,.3F)};
 check(simulation_lab_refresh_environmental_projectile_contact(stat,shot,1,catalog,hulls,static_hulls)&&!stat.impact_target_placement_index,"static model interception retained the intended enemy index");
 std::cout<<"V620_PROJECTILE_GEOMETRY PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
