#include "simulation_native_field_ray.hpp"
#include "thandor/projectile_contacts.hpp"
#include "thandor/retail_contact.hpp"
#include "thandor/field_raycast.hpp"
#include "thandor/simulation_lab_shot_rays.hpp"
#include <cmath>
#include <limits>
namespace thandor {
bool simulation_lab_refresh_projectile_interval_contact(SimulationProjectileState& p,
    const ShotDefinitionRecord& shot,uint32_t tick,const FieldGrid& field,
    const SimulationLabRuntimeCatalog& catalog,std::span<const SimulationLabLiveShotHull> live,
    std::span<const SimulationLabStaticShotHull> fixed){
 const Vec3f from=p.runtime_flight_previous,to=p.runtime_flight_ray_end;
 const Vec3f delta{to.x-from.x,to.y-from.y,to.z-from.z};
 float length=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
 p.impact_kind=SimulationImpactKind::None;p.impact_effect_id=0;p.impact_target_placement_index.reset();p.impact_target_model_component_index.reset();
 const bool native_ray=p.runtime_flight_speed_q12>0||
     (p.runtime_flight_initialized&&p.runtime_flight_speed_q12==0);
 if(!native_ray&&length<=1e-12F)return false;
 Vec3f direction{};
 float nearest=std::numeric_limits<float>::infinity();
 const auto excluded=p.owner_kind==SimulationProjectileOwnerKind::Environmental?std::optional<size_t>{}:std::optional<size_t>{p.owner_placement_index};
 const SimulationLabFixedRay fixed_ray{retail_direction_scaled(
     uint32_t(p.runtime_flight_pitch)-uint32_t(p.runtime_elevation_offset),
     uint32_t(p.runtime_flight_yaw),1<<28),p.runtime_flight_speed_q12};
 const float model_ray_length=native_ray
     ?q12_to_float(p.runtime_flight_speed_q12):length;
 if(native_ray){
  // The collision ray uses the stored angles and speed, not the length of
  // the rounded Q12 translation step (which can cross a surface too early).
  length=model_ray_length;
  direction={float(fixed_ray.direction_q28[0])/268435456.F,
             float(fixed_ray.direction_q28[1])/268435456.F,
             float(fixed_ray.direction_q28[2])/268435456.F};
 }else direction={delta.x/length,delta.y/length,delta.z/length};
 if(auto hit=simulation_lab_raycast_models(from,direction,model_ray_length,excluded,live,fixed,
                                          native_ray?&fixed_ray:nullptr);
    hit&&hit->impact_class<8&&shot.target_impact_effect_id(hit->impact_class)!=0U){
  nearest=hit->distance;p.impact_kind=SimulationImpactKind::TargetModelImpactClass;p.impact_effect_id=shot.target_impact_effect_id(hit->impact_class);
  p.impact_material_index=hit->impact_class;p.impact_target_placement_index=hit->entity_index;p.impact_target_model_component_index=hit->component_index;
 }
 auto terrain=native_ray?native_field_ray::raycast(field,from,
     p.runtime_flight_pitch-p.runtime_elevation_offset,p.runtime_flight_yaw,p.runtime_flight_speed_q12,false):
     raycast_field_surface_compatibility(field,from,direction,length,FieldRaycastSurface::Terrain);
 if(!terrain&&!native_ray){
  // Native projectile rays can hit the terrain in cells that the editor
  // picking ray rejects. Recover a crossing in the fixed-point FLD surface.
  const auto clearance=[&](float fraction){
   const Vec3f sample{from.x+delta.x*fraction,from.y+delta.y*fraction,from.z+delta.z*fraction};
   const int32_t x=float_to_q12(sample.x),y=float_to_q12(sample.y);
   const auto half=(int64_t(y)*-0x20c8cc)>>21;
   const auto column=((int64_t(x)*0x1c6e9c)>>20)-half,row=half*2;
   const auto cx=column>>12,cy=row>>12;
   if(cx<0||cy<0||cx+1>=field.width()||cy+1>=field.height())return INT32_MAX;
   // Native surface rays test geometric triangles even on cells whose edge
   // flags prohibit ordinary point sampling and editor picking.
   const int64_t fx=column&4095,fy=row&4095,diagonal=fx+fy-4096;
   const auto ux=static_cast<uint32_t>(cx),uy=static_cast<uint32_t>(cy);
   const int64_t h00=field.at(ux,uy).terrain_height_q12,h10=field.at(ux+1U,uy).terrain_height_q12;
   const int64_t h01=field.at(ux,uy+1U).terrain_height_q12,h11=field.at(ux+1U,uy+1U).terrain_height_q12;
   const auto height=(diagonal<0?h10*fx+h01*fy-h00*diagonal:
       h11*diagonal-h01*(fx-4096)-h10*(fy-4096))>>12;
   return float_to_q12(sample.z)-static_cast<int32_t>(height);
  };
  if(clearance(0.F)>0&&clearance(1.F)<0){
   float lo=0.F,hi=1.F;
   for(unsigned step=0;step<16U;++step){
    const float middle=(lo+hi)*0.5F;
    if(clearance(middle)>0)lo=middle;else hi=middle;
   }
   const Vec3f contact{from.x+delta.x*hi,from.y+delta.y*hi,from.z+delta.z*hi};
   const auto half_row=(int64_t(float_to_q12(contact.y))*-0x20c8cc)>>21;
   const auto row=(half_row*2)>>12;
   const auto column=(((int64_t(float_to_q12(contact.x))*0x1c6e9c)>>20)-half_row)>>12;
   if(row>=0&&column>=0&&row+1<field.height()&&column+1<field.width())
    terrain=FieldRaycastHit{uint32_t(column),uint32_t(row),0,0,contact,length*hi,false,1};
  }
 }
 if(terrain&&terrain->distance<nearest){
  const uint32_t material=uint32_t(field.at(terrain->quad_x,terrain->quad_y).material());
  if(auto effect=shot.terrain_impact_effect_id(material);effect){nearest=terrain->distance;p.impact_kind=SimulationImpactKind::TerrainSurface;
   p.impact_effect_id=effect;p.impact_material_index=material;p.impact_target_placement_index.reset();p.impact_target_model_component_index.reset();}
 }
 if(shot.primary_effect_id)if(auto hit=native_ray?
     native_field_ray::raycast(field,from,p.runtime_flight_pitch-p.runtime_elevation_offset,
        p.runtime_flight_yaw,p.runtime_flight_speed_q12,true):
     raycast_field_surface_compatibility(field,from,direction,length,FieldRaycastSurface::Secondary);hit&&hit->distance<nearest){
  nearest=hit->distance;p.impact_kind=SimulationImpactKind::SecondarySurface;p.impact_effect_id=shot.primary_effect_id;p.impact_material_index=0;
  p.impact_target_placement_index.reset();p.impact_target_model_component_index.reset();
 }
 if(!std::isfinite(nearest))return false;
 // The native triangle routine can return a distance just beyond this step.
 // ShotProjectile_ApplyNearestHit checks it against the stored launch speed;
 // a non-null terrain ray result alone does not end the projectile.
 if(native_ray&&float_to_q12(nearest)>p.runtime_flight_speed_q12){
  p.impact_kind=SimulationImpactKind::None;p.impact_effect_id=0;
  p.impact_target_placement_index.reset();p.impact_target_model_component_index.reset();
  return false;
 }
 p.travel_ticks=tick-p.spawn_tick;
 if(native_ray){
  // Native emits at an integer distance along the stored heading, without
  // the collision ray's elevation offset (ShotModel_EmitEffectAlongHeading).
  const auto offset=retail_direction_scaled(uint32_t(p.runtime_flight_pitch),
      uint32_t(p.runtime_flight_yaw),float_to_q12(nearest));
  p.impact_position={q12_to_float(float_to_q12(from.x)+offset[0]),
      q12_to_float(float_to_q12(from.y)+offset[1]),q12_to_float(float_to_q12(from.z)+offset[2])};
 }else p.impact_position={from.x+direction.x*nearest,from.y+direction.y*nearest,from.z+direction.z*nearest};
 p.runtime_flight_position=p.impact_position;
 const auto life=catalog.effect_lifetimes.find(p.impact_effect_id);p.impact_visual_lifetime_ticks=life==catalog.effect_lifetimes.end()?1:std::max(1U,life->second);
 return true;
}
}
