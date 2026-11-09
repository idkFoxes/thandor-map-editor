#include THANDOR_NATIVE_WALKER
#include THANDOR_NATIVE_HIERARCHY
#include THANDOR_NATIVE_GEOMETRY
#include "articulated_walker_pose.hpp"
#include "thandor/catalog.hpp"
#include "thandor/simulation_lab_environment.hpp"
#include <iostream>
static ModelRuntimeNode nodes[64]{};static ModelResource resources[64]{};static ModelDefinition definition{};static ArmyArticulatedRuntimeSlotView runtime{};
void Thandor_Ptr32Overflow(uintptr_t){std::abort();}
int main(int argc,char**argv){FixedMath_BuildSinCosTables();auto c=thandor::AssetCatalog::scan_archive_root(argv[1]);size_t cases=0;float maxposition=0,maxbasis=0;int32_t max_contact_q12=0,max_core_q12=0;int64_t max_core_basis=0;
for(const auto&p:c.army_presets())if(p.runtime_model_class_id==3&&p.browser_kind==thandor::PlacementBrowserKind::Unit){auto h=p.resolved_model_hierarchy;std::vector<thandor::SpriteAsset> ss;for(auto&n:h)ss.push_back(thandor::SpriteAsset::parse(c.load(n.sprite_path)));
for(uint32_t side:{0U,1U})for(int32_t steering:{-0x2000,0x2000})for(uint32_t heading:{0U,0x2000U,0x4000U,0xE000U})for(uint32_t progress:{0U,1024U,2048U,3072U})for(int32_t terrain_step:{-400,0,400}){
 std::fill(std::begin(nodes),std::end(nodes),ModelRuntimeNode{});runtime={};definition.placementHeightOffsetQ12=p.articulated_root_height_q12;definition.classParameterC4=p.articulated_contact_lift_q12;
 for(size_t i=0;i<h.size();++i){auto&n=nodes[i];auto& m=n.modelPayload;m.modelResource=&resources[i];resources[i].placementHeightOffsetQ12=thandor::float_to_q12(ss[i].placement_height_offset());m.localRotationAngle0=h[i].local_rotation_angle0;m.localRotationAngle1=h[i].local_rotation_angle1;m.localRotationAngle2=h[i].local_rotation_angle2;if(i){auto pi=h[i].parent_index;auto v=ss[pi].child_attachment(h[i].child_slot)->local_translation;m.localTranslationXQ12=thandor::float_to_q12(v.x);m.localTranslationYQ12=thandor::float_to_q12(v.y);m.localTranslationZQ12=thandor::float_to_q12(v.z);n.parentNode=&nodes[pi];nodes[pi].childNodes[h[i].child_slot]=&n;nodes[pi].childCount=std::max(nodes[pi].childCount,h[i].child_slot+1);}}
 nodes[0].modelPayload.worldRotationAngle1=0x4000;nodes[0].modelPayload.worldRotationAngle2=heading;nodes[0].runtimePayload.armyRuntime=reinterpret_cast<ArmyRuntimeSlot*>(&runtime);runtime.modelNodeRuntime=&nodes[0];runtime.definitionOrAsset=&definition;runtime.articulatedContact.lateralOffsetQ12=nodes[1].modelPayload.localTranslationYQ12;runtime.articulatedContact.contactRadiusOrLinkedSlotMask.contactRadiusQ12=nodes[2].modelPayload.localTranslationYQ12;
 WorldRuntimeContext world{};ArmyArticulatedRuntime_InitializeTerrainContactGeometry(nodes,&world);
 // Plan a clamped turn on either contact and reproduce its half-turn body interpolation.
 runtime.runtimeStateA8=side?0:progress;runtime.articulatedContact.terrainContactMode=static_cast<ArmyTerrainContactDispatchMode>(side?progress:0);runtime.classState60=heading;runtime.ownerValue64=(heading+(steering>>1))&65535;
 auto&v=runtime.linkedChildOverloadedState;if(side)v.rightHeadingCommandOrSpawnValue.headingOrTurnValue=(heading+steering)&65535;else v.leftHeadingCommandOrSpawnValue.headingOrTurnValue=(heading+steering)&65535;

 thandor::SimulationActorState actor;actor.body_yaw_turn16=heading;actor.articulated_contacts_initialized=true;actor.articulated_contact_transition_active=true;actor.articulated_was_moving=true;actor.articulated_contact_progress_q12=progress;actor.articulated_swing_side=side;
 actor.articulated_foot_anchor_world={thandor::Vec3f{thandor::q12_to_float(runtime.movementTarget0Q12),thandor::q12_to_float(runtime.definitionClassValue80),0},thandor::Vec3f{thandor::q12_to_float(runtime.movementTarget1Q12),thandor::q12_to_float(runtime.definitionClassValue84),0}};
 actor.articulated_foot_target_world=actor.articulated_foot_anchor_world;actor.articulated_contact_heading_turn16={heading,heading};actor.articulated_contact_target_heading_turn16={heading,heading};actor.articulated_contact_target_heading_turn16[side]=(heading+steering)&65535;
 // Frozen horizontal step plus turn is enough to expose wrong bend planes.
 if(side){runtime.runtimeState94+=400;runtime.runtimeStateA4+=terrain_step;}
 else{runtime.runtimeState90+=400;runtime.articulatedHeightOrStateA0+=terrain_step;}
 actor.articulated_foot_target_world[side].x+=400.F/4096;
 actor.articulated_foot_target_world[side].z+=thandor::q12_to_float(terrain_step);
 std::vector<editor::ModelHierarchyTransform>ts;for(size_t i=0;i<h.size();++i){auto t=editor::fixed_model_rotation_basis(h[i].local_rotation_angle2,h[i].local_rotation_angle1,h[i].local_rotation_angle0);if(i){t.translation=ss[h[i].parent_index].child_attachment(h[i].child_slot)->local_translation;t=editor::compose_model_transform(ts[h[i].parent_index],t);}else t=editor::placement_root_transform(0,0,0,thandor::turn16_to_radians(heading));ts.push_back(t);}
 ArmyArticulatedRuntime_UpdateSuspensionHierarchy(nodes,&world);ModelNodeRuntime_RebuildTransformsFromRoot(nodes);
 thandor::SimulationLabRuntimeCatalog contact_catalog;
 for(size_t i=0;i<h.size();++i){auto& sprite=contact_catalog.sprites[h[i].sprite_path];
  sprite.height_offset=ss[i].placement_height_offset();
  for(uint32_t slot=0;slot<6;++slot)if(auto attachment=ss[i].child_attachment(slot)){
   auto q=attachment->local_translation;sprite.child_translations_q12[slot]=std::array<int32_t,3>{thandor::float_to_q12(q.x),thandor::float_to_q12(q.y),thandor::float_to_q12(q.z)};}}
 thandor::SimulationLabEntity contact_entity;contact_entity.runtime_model_class_id=3;
 contact_entity.runtime_model_hierarchy=std::make_shared<const std::vector<thandor::ModelVisualNode>>(h);
 contact_entity.articulated_contact_lift_q12=p.articulated_contact_lift_q12;
 contact_entity.actor=actor;contact_entity.actor.articulated_native.initialized=true;
 contact_entity.actor.articulated_native.progress[side]=progress;
 contact_entity.actor.body_yaw_turn16=nodes[0].modelPayload.worldRotationAngle2;
 const auto rootp=nodes[0].worldTransform.translation;
 contact_entity.actor.position={thandor::q12_to_float(rootp.x),thandor::q12_to_float(rootp.y),thandor::q12_to_float(rootp.z)};
 for(size_t foot_side=0;foot_side<2;++foot_side){auto* foot=nodes[0].childNodes[foot_side].get();for(unsigned depth=0;depth<3;++depth)foot=foot->childNodes[0];
  if(foot_side==1)contact_entity.articulated_shared_foot_placement_offset=thandor::q12_to_float(((ModelResource*)foot->modelPayload.modelResource.get())->placementHeightOffsetQ12);}
 const auto contacts=thandor::simulation_lab_articulated_contact_positions(contact_entity,contact_catalog);
 auto pose_field=thandor::FieldGrid::create(20,20,0,0);
 const auto core_poses=thandor::simulation_lab_current_model_poses(contact_entity,pose_field,contact_catalog);
 for(size_t i=0;i<h.size();++i){const auto& pose=core_poses[i];const auto& m=nodes[i].worldTransform;
  const int32_t delta=std::max({std::abs(thandor::float_to_q12(pose.position.x)-m.translation.x),std::abs(thandor::float_to_q12(pose.position.y)-m.translation.y),std::abs(thandor::float_to_q12(pose.position.z)-m.translation.z)});
  if(delta>max_core_q12){max_core_q12=delta;if(delta>0)std::cout<<"CORE_POSITION arm="<<p.registry_id<<" node="<<i<<" q12="<<delta<<'\n';}
  if(pose.native_basis){const int32_t b[]={m.basisRow0[0],m.basisRow0[1],m.basisRow0[2],m.basisRow1[0],m.basisRow1[1],m.basisRow1[2],m.basisRow2[0],m.basisRow2[1],m.basisRow2[2]};for(size_t k=0;k<9;++k)max_core_basis=std::max<int64_t>(max_core_basis,std::llabs(int64_t((*pose.native_basis)[k])-b[k]));}}

 for(size_t foot_side=0;foot_side<2;++foot_side){auto* foot=nodes[0].childNodes[foot_side].get();for(unsigned depth=0;depth<3;++depth)foot=foot->childNodes[0];
  const auto& actual=foot->worldTransform.translation;const auto predicted=contacts[foot_side];
  const auto difference=std::max({std::abs(thandor::float_to_q12(predicted.x)-actual.x),std::abs(thandor::float_to_q12(predicted.y)-actual.y),std::abs(thandor::float_to_q12(predicted.z)-actual.z)});
  if(difference>max_contact_q12){max_contact_q12=difference;std::cout<<"CONTACT_DIFF arm="<<p.registry_id<<" side="<<foot_side<<" heading="<<heading<<" progress="<<progress<<" q12="<<difference<<'\n';}}
 auto result=editor::apply_articulated_walker_support_pose(h,ts,p.articulated_root_height_q12,p.articulated_contact_interpolation_rate_q12,p.articulated_contact_lift_q12,p.articulated_contact_steering_limit_turn16,&actor,false,[](float,float){return 0.F;},[&](uint32_t i){return ss[i].placement_height_offset();});
 for(size_t i=0;i<h.size();++i){auto&m=nodes[i].worldTransform;float pe=std::max({std::abs(ts[i].translation.x-thandor::q12_to_float(m.translation.x)),std::abs(ts[i].translation.y-thandor::q12_to_float(m.translation.y)),std::abs(ts[i].translation.z-thandor::q12_to_float(m.translation.z))});maxposition=std::max(maxposition,pe);{int32_t b[]={m.basisRow0[0],m.basisRow0[1],m.basisRow0[2],m.basisRow1[0],m.basisRow1[1],m.basisRow1[2],m.basisRow2[0],m.basisRow2[1],m.basisRow2[2]};for(int j=0;j<9;++j)maxbasis=std::max(maxbasis,std::abs(ts[i].basis[j]-b[j]/268435456.F));}
 if(pe>.02)std::cout<<"DIFF arm="<<p.registry_id<<" yaw="<<heading<<" p="<<progress<<" node="<<i<<" error="<<pe<<'\n';}++cases;
}}
std::cout<<"NATIVE_WALKER cases="<<cases<<" position="<<maxposition<<" contact_q12="<<max_contact_q12<<" core_q12="<<max_core_q12<<" core_basis="<<max_core_basis<<" basis="<<maxbasis<<'\n';return cases==5376&&maxposition<0.003F&&maxbasis<0.008F&&max_contact_q12==0&&max_core_q12==0&&max_core_basis<64?0:1;}
