#include "simulation_native_field_ray.hpp"
#include "thandor/simulation_lab_shot_rays.hpp"
#include "thandor/field_raycast.hpp"
#include "thandor/retail_contact.hpp"
#include "thandor/simulation_lab_targeting.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
namespace thandor {
namespace {
float length(Vec3f p){return std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z);}
float maximum_ray(const ShotDefinitionRecord& shot){
    const auto product=static_cast<uint32_t>(shot.launch_speed_q12)*shot.projectile_lifetime_ticks;
    return std::max(0.F,q12_to_float(std::bit_cast<int32_t>(product)));
}
uint32_t material(const FieldGrid& field,const FieldRaycastHit& hit){
    return hit.quad_x<field.width()&&hit.quad_y<field.height()
        ?static_cast<uint32_t>(field.at(hit.quad_x,hit.quad_y).material()):0U;
}
SimulationAimAngles native_direct_los_angles(Vec3f origin,Vec3f aim){
    const int32_t dx=float_to_q12(aim.x)-float_to_q12(origin.x);
    const int32_t dy=float_to_q12(aim.y)-float_to_q12(origin.y);
    const int32_t dz=float_to_q12(aim.z)-float_to_q12(origin.z);
    const auto horizontal=retail_uint64_sqrt(uint64_t(int64_t(dx)*dx)+uint64_t(int64_t(dy)*dy));
    SimulationAimAngles result;
    result.yaw_turn16=fixed_atan2_angle16_compat(dy,dx);
    result.pitch_turn16=fixed_atan2_angle16_compat(dz,horizontal);
    return result;
}

SimulationAimAngles native_ballistic_los_angles(Vec3f origin,Vec3f target,
                                                 const ShotDefinitionRecord& shot){
    const int32_t x=float_to_q12(target.x)-float_to_q12(origin.x);
    const int32_t y=float_to_q12(target.y)-float_to_q12(origin.y);
    const int32_t z=float_to_q12(target.z)-float_to_q12(origin.z);
    const auto horizontal=retail_uint64_sqrt(uint64_t(int64_t(x)*x)+uint64_t(int64_t(y)*y));
    const int32_t speed2=std::bit_cast<int32_t>(uint32_t(shot.launch_speed_q12)*uint32_t(shot.launch_speed_q12));
    const int32_t range=std::bit_cast<int32_t>(uint32_t(horizontal)*uint32_t(shot.ballistic_divisor_q12));
    const int32_t height=std::bit_cast<int32_t>(uint32_t(speed2)-uint32_t(shot.ballistic_divisor_q12)*uint32_t(z)*2U);
    const int64_t discriminant=int64_t(height)*speed2-int64_t(range)*range;
    SimulationAimAngles result;
    if(discriminant<0){result.valid=false;return result;}
    uint32_t root=retail_uint64_sqrt(uint64_t(discriminant));
    if(-4096<z&&z<4096)root=0U-root;
    result.yaw_turn16=fixed_atan2_angle16_compat(y,x)&65535U;
    result.pitch_turn16=fixed_atan2_angle16_compat(std::bit_cast<int32_t>(uint32_t(speed2)+root),range);
    return result;
}

bool blocked_by_owner(const SimulationLabRun& run,size_t source,std::optional<size_t> target,
                      std::optional<size_t> hit){
    if(target&&hit==target)return false;
    // common.commandState aliases the army's signed class0 damage counter (+0x100).
    const auto state=simulation_lab_combat_metrics(run.entities[source]).class_damage_q12[0];
    const auto owner=run.entities[source].faction_index;
    const auto hit_owner=hit&&*hit<run.entities.size()?run.entities[*hit].faction_index:0U;
    return state<1?owner!=hit_owner:owner==hit_owner;
}
}
bool simulation_lab_refresh_beam_contact(SimulationProjectileState& projectile,
    const ShotDefinitionRecord& shot,const FieldGrid& field,const SimulationLabRuntimeCatalog& catalog,
    std::span<const SimulationLabLiveShotHull> live_hulls,
    std::span<const SimulationLabStaticShotHull> static_hulls){
    if(shot.trajectory_mode!=0)return false;
    const float maximum=maximum_ray(shot);
    const uint32_t native_pitch=projectile.launch_pitch_turn16-uint32_t(shot.elevation_offset_angle16);
    const auto direction_q28=retail_direction_scaled(native_pitch,projectile.launch_yaw_turn16,1<<28);
    const Vec3f direction{float(direction_q28[0])/268435456.F,float(direction_q28[1])/268435456.F,
                          float(direction_q28[2])/268435456.F};
    const SimulationLabFixedRay fixed_ray{direction_q28,float_to_q12(maximum)};
    float nearest=std::numeric_limits<float>::infinity();
    projectile.impact_kind=SimulationImpactKind::None;projectile.impact_material_index=0;
    projectile.impact_effect_id=0;projectile.impact_target_placement_index.reset();
    projectile.impact_target_model_component_index.reset();
    const auto excluded=projectile.owner_kind==SimulationProjectileOwnerKind::Environmental
        ?std::optional<size_t>{}:std::optional<size_t>{projectile.owner_placement_index};
    if(const auto model=simulation_lab_raycast_models(projectile.launch_position,direction,maximum,excluded,live_hulls,static_hulls,&fixed_ray);
       model&&model->impact_class<8U&&shot.target_impact_effect_id(model->impact_class)!=0U){
        nearest=model->distance;projectile.impact_kind=SimulationImpactKind::TargetModelImpactClass;
        projectile.impact_effect_id=shot.target_impact_effect_id(model->impact_class);
        projectile.impact_material_index=model->impact_class;projectile.impact_target_placement_index=model->entity_index;
        projectile.impact_target_model_component_index=model->component_index;
    }
    if(const auto terrain=native_field_ray::raycast(field,projectile.launch_position,native_pitch,projectile.launch_yaw_turn16,float_to_q12(maximum),false);
       terrain&&terrain->distance<nearest){
        const auto m=material(field,*terrain),effect=shot.terrain_impact_effect_id(m);
        if(effect){nearest=terrain->distance;projectile.impact_kind=SimulationImpactKind::TerrainSurface;
            projectile.impact_material_index=m;projectile.impact_effect_id=effect;
            projectile.impact_target_placement_index.reset();projectile.impact_target_model_component_index.reset();}
    }
    if(shot.primary_effect_id){
        if(const auto secondary=native_field_ray::raycast(field,projectile.launch_position,native_pitch,projectile.launch_yaw_turn16,float_to_q12(maximum),true);
           secondary&&secondary->distance<nearest){
            nearest=secondary->distance;projectile.impact_kind=SimulationImpactKind::SecondarySurface;
            projectile.impact_effect_id=shot.primary_effect_id;projectile.impact_material_index=0;
            projectile.impact_target_placement_index.reset();projectile.impact_target_model_component_index.reset();}
    }
    // The native terrain triangle may return a hit beyond the requested ray.
    // Beams apply the same explicit range admission as moving projectiles.
    if(std::isfinite(nearest)&&float_to_q12(nearest)>float_to_q12(maximum)){
        nearest=std::numeric_limits<float>::infinity();
        projectile.impact_kind=SimulationImpactKind::None;projectile.impact_effect_id=0;
        projectile.impact_material_index=0;projectile.impact_target_placement_index.reset();
        projectile.impact_target_model_component_index.reset();
    }
    // Effect emission uses the stored heading, without the collision offset.
    const auto step=retail_direction_scaled(projectile.launch_pitch_turn16,projectile.launch_yaw_turn16,
        float_to_q12(std::isfinite(nearest)?nearest:maximum));
    projectile.target_position={q12_to_float(float_to_q12(projectile.launch_position.x)+step[0]),
        q12_to_float(float_to_q12(projectile.launch_position.y)+step[1]),
        q12_to_float(float_to_q12(projectile.launch_position.z)+step[2])};
    projectile.impact_position=projectile.target_position;
    const auto lifetime=catalog.effect_lifetimes.find(projectile.impact_effect_id);
    projectile.impact_visual_lifetime_ticks=lifetime==catalog.effect_lifetimes.end()?1U:std::max(1U,lifetime->second);
    return projectile.impact_kind!=SimulationImpactKind::None;
}
bool simulation_lab_weapon_line_of_fire_clear(const SimulationLabRun& run,size_t source_index,
    std::optional<size_t> target_index,Vec3f origin,Vec3f aim,const SimulationWeaponProfile& weapon,
    const ShotDefinitionRecord& shot,const FieldGrid& field,
    std::span<const SimulationLabLiveShotHull> live_hulls,
    std::span<const SimulationLabStaticShotHull> static_hulls){
    if(source_index>=run.entities.size())return false;
    if(shot.trajectory_mode==2)return true;
    const auto angles=shot.trajectory_mode==1
        ?(run.level_runtime_enabled?native_ballistic_los_angles(origin,aim,shot):simulation_shot_line_of_fire_angles(origin,aim,shot))
        :(run.level_runtime_enabled?native_direct_los_angles(origin,aim):simulation_target_angles(origin,aim,0));
    if(!angles.valid)return false;
    const auto pitch=std::bit_cast<int16_t>(static_cast<uint16_t>(angles.pitch_turn16));
    if((shot.trajectory_mode==1||shot.mode3_lead_disabled==0U)&&
       (pitch<weapon.pitch_min_turn16||pitch>weapon.pitch_max_turn16))return false;
    Vec3f delta{aim.x-origin.x,aim.y-origin.y,aim.z-origin.z};const float distance=length(delta);
    float maximum=shot.trajectory_mode==1?std::hypot(delta.x,delta.y):maximum_ray(shot);
    const float yaw=turn16_to_radians(angles.yaw_turn16),elevation=turn16_to_radians(angles.pitch_turn16);
    Vec3f direction{std::cos(yaw)*std::cos(elevation),std::sin(yaw)*std::cos(elevation),std::sin(elevation)};
    std::optional<SimulationLabFixedRay> native_ray;
    if(run.level_runtime_enabled&&shot.trajectory_mode==1U){
        const int64_t dx=int64_t(float_to_q12(aim.x))-float_to_q12(origin.x);
        const int64_t dy=int64_t(float_to_q12(aim.y))-float_to_q12(origin.y);
        const auto horizontal=static_cast<int32_t>(retail_uint64_sqrt(uint64_t(dx*dx)+uint64_t(dy*dy)));
        maximum=q12_to_float(horizontal);
        native_ray=SimulationLabFixedRay{retail_direction_scaled(
            angles.pitch_turn16,angles.yaw_turn16,1<<28),horizontal};
        direction={float(native_ray->direction_q28[0])/268435456.F,
                   float(native_ray->direction_q28[1])/268435456.F,
                   float(native_ray->direction_q28[2])/268435456.F};
    }
    if(run.level_runtime_enabled&&shot.trajectory_mode!=1U){
        native_ray=SimulationLabFixedRay{retail_direction_scaled(
            angles.pitch_turn16,angles.yaw_turn16,1<<28),
            float_to_q12(maximum)};
        for(size_t axis=0;axis<3;++axis){
            const auto value=float(native_ray->direction_q28[axis])/268435456.F;
            if(axis==0)direction.x=value;else if(axis==1)direction.y=value;else direction.z=value;
        }
    }
    const auto model=simulation_lab_raycast_models(origin,direction,maximum,source_index,live_hulls,static_hulls,
        native_ray?&*native_ray:nullptr);
    if(shot.trajectory_mode==1){
        // Verified original quirk: after a ballistic ray hit it owner-tests originNode,
        // not hitModelNode. Preserve it rather than substituting the apparent intent.
        return !model||!blocked_by_owner(run,source_index,target_index,source_index);
    }
    std::optional<float> terrain_distance;
    if(run.level_runtime_enabled){
        if(const auto terrain=native_field_ray::raycast(field,origin,angles.pitch_turn16,
                angles.yaw_turn16,float_to_q12(maximum),false))
            terrain_distance=terrain->distance;
    }else if(const auto terrain=raycast_field_surface_compatibility(
                field,origin,direction,maximum,FieldRaycastSurface::Terrain))
        terrain_distance=terrain->distance;
    if(terrain_distance&&(!model||*terrain_distance<model->distance)){
        return !target_index&&std::abs(float_to_q12(*terrain_distance)-float_to_q12(distance))<=0x400;
    }
    // Static MODEL owners have faction zero. They participate in the same
    // signed command-state ownership test as represented army models.
    if(model&&blocked_by_owner(run,source_index,target_index,model->entity_index))return false;
    int64_t adjusted=float_to_q12(distance);
    if(run.level_runtime_enabled){
        const int64_t dx=int64_t(float_to_q12(aim.x))-float_to_q12(origin.x);
        const int64_t dy=int64_t(float_to_q12(aim.y))-float_to_q12(origin.y);
        const int64_t dz=int64_t(float_to_q12(aim.z))-float_to_q12(origin.z);
        adjusted=retail_uint64_sqrt(uint64_t(dx*dx)+uint64_t(dy*dy)+uint64_t(dz*dz));
    }
    // Native floors the vector length, then shifts the combined expression.
    // An odd footprint therefore removes the rounded-up half radius.
    if(target_index&&*target_index<run.entities.size())
        adjusted=(2*adjusted-run.entities[*target_index].motion.collision_radius_q12)>>1;
    const auto range=shot_line_of_fire_range_q12(shot);
    return range&&adjusted<=*range;
}
}
