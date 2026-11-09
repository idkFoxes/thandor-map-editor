#pragma once
#include "math.hpp"

namespace editor {
// A world-space light basis: object yaw and camera orbit cannot rotate sunlight.
// Rows map directly to Vulkan's [-1,1] XY and [0,1] depth conventions.
inline Mat4 directional_shadow_matrix(Vec3f ray, Vec3f focus, float radius,
                                      float depth_radius, uint32_t resolution) {
    ray=normalize(ray);
    const Vec3f up=std::abs(ray.z)>0.98F?Vec3f{0,1,0}:Vec3f{0,0,1};
    const Vec3f right=normalize(cross(ray,up));
    const Vec3f vertical=cross(right,ray);
    radius=std::max(radius,1.0F);
    depth_radius=std::max(depth_radius,radius);
    const float texel=2.0F*radius/static_cast<float>(std::max(1U,resolution));
    const float cx=std::floor(dot(right,focus)/texel+0.5F)*texel;
    const float cy=std::floor(dot(vertical,focus)/texel+0.5F)*texel;
    Mat4 result=identity();
    const std::array<Vec3f,3> axes{mul(right,1.0F/radius),mul(vertical,-1.0F/radius),mul(ray,0.5F/depth_radius)};
    for(int row=0;row<3;++row){
        result(row,0)=axes[row].x;result(row,1)=axes[row].y;result(row,2)=axes[row].z;
    }
    result(0,3)=-cx/radius;result(1,3)=cy/radius;
    result(2,3)=0.5F-dot(ray,focus)*0.5F/depth_radius;
    return result;
}
} // namespace editor
