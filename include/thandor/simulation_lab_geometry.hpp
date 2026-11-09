#pragma once
#include "thandor/simulation_lab.hpp"
#include "thandor/retail_contact.hpp"
#include <algorithm>
#include <cmath>

namespace thandor {
// ModelNodeRuntime_TransformLocalPoint with the same root contact basis used by the editor.
inline Vec3f simulation_lab_factory_world_point(const FieldGrid& field,Vec3f position,
    uint32_t yaw_turn16,int32_t contact_kind,float sprite_height_offset,Vec3f local,
    int32_t definition_height_offset_q12=0,bool preserve_root_height=false) {
    const auto fixed=field.sample_retail_surface_heights_q12(float_to_q12(position.x),float_to_q12(position.y));
    const float offset=q12_to_float(definition_height_offset_q12);
    if(fixed.valid&&!preserve_root_height)switch(contact_kind) {
        case 1:position.z=q12_to_float(fixed.water_surface)+offset;break;
        case 4:position.z=q12_to_float(fixed.top)+offset;break;
        case 3:break; // mutable suspension owns height
        default:position.z=q12_to_float(fixed.terrain)+sprite_height_offset+offset;break;
    }
    uint32_t a0=0,a1=0x4000;
    if(contact_kind==2)if(const auto packed=retail_contact_normal_angles(field,float_to_q12(position.x),float_to_q12(position.y))) {
        a0=*packed&65535U;a1=*packed>>16;
    }
    const auto fixed_basis=retail_model_basis_q28(yaw_turn16,a1,a0);
    // FixedTransform_ApplyPoint sums each Q28 row before shifting, then adds
    // the Q12 translation. Float transforms followed by rounding differ by 1 Q12.
    const int32_t point[]={float_to_q12(local.x),float_to_q12(local.y),float_to_q12(local.z)};
    const int32_t origin[]={float_to_q12(position.x),float_to_q12(position.y),float_to_q12(position.z)};
    float result[3]{};
    for(size_t row=0;row<3;++row){
        int64_t sum=0;
        for(size_t column=0;column<3;++column)
            sum+=int64_t(fixed_basis[row*3+column])*point[column];
        result[row]=q12_to_float(static_cast<int32_t>((sum>>28)+origin[row]));
    }
    return {result[0],result[1],result[2]};
}

// Metadata-free API fixtures only specify circles. Native catalog owners carry
// a hierarchy and use the recovered class-specific influence callback.
inline uint32_t simulation_lab_route_blocker_class(const SimulationLabEntity& entity) noexcept {
    return entity.runtime_model_hierarchy && !entity.runtime_model_hierarchy->empty()
        ? entity.runtime_model_class_id : UINT32_MAX;
}

// Ground callbacks release the factory link outside the sum of MDL +0xDC radii.
inline bool simulation_lab_factory_footprint_cleared(const SimulationLabEntity& child,
                                                    const SimulationLabEntity& parent) noexcept {
    const auto cr=child.motion.collision_radius_q12,pr=parent.motion.collision_radius_q12;
    if(cr<=0||pr<=0) return true;
    const double dx=double(float_to_q12(child.actor.position.x))-float_to_q12(parent.actor.position.x);
    const double dy=double(float_to_q12(child.actor.position.y))-float_to_q12(parent.actor.position.y);
    const double radius=double(cr)+pr;
    return dx*dx+dy*dy>radius*radius;
}

// Test the actual swept step, not just its final point or the desired route direction.
inline bool simulation_lab_step_hits_blocker(Vec3f from,Vec3f to,int32_t moving_radius_q12,
                                             const V528PathRuntimeBlocker& blocker) noexcept {
    if(moving_radius_q12<=0||blocker.collision_radius_q12<=0) return false;
    const double x=double(float_to_q12(from.x))-blocker.world_x_q12;
    const double y=double(float_to_q12(from.y))-blocker.world_y_q12;
    const double dx=double(float_to_q12(to.x))-float_to_q12(from.x);
    const double dy=double(float_to_q12(to.y))-float_to_q12(from.y);
    const double radius=double(moving_radius_q12)+blocker.collision_radius_q12;
    const double start=x*x+y*y,step=dx*dx+dy*dy;
    // Source recovery may start overlapped. Permit an outward step, never a deeper crossing.
    if(start<=radius*radius&&x*dx+y*dy>=0.0) return false;
    const double t=step>0.0?std::clamp(-(x*dx+y*dy)/step,0.0,1.0):0.0;
    const double cx=x+t*dx,cy=y+t*dy;
    return cx*cx+cy*cy<=radius*radius;
}
}
