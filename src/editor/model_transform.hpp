#pragma once

#include "thandor/field.hpp"
#include "thandor/semantic_types.hpp"

#include <array>
#include <cmath>
#include <numbers>

namespace editor {

using thandor::Vec3f;

struct ModelHierarchyTransform {
    std::array<float, 9> basis{1.0F, 0.0F, 0.0F,
                               0.0F, 1.0F, 0.0F,
                               0.0F, 0.0F, 1.0F};
    Vec3f translation{};
};

inline float model_turn32_to_radians(thandor::AngleTurn32 value) noexcept {
    return thandor::angle_turn32_to_radians(value);
}
inline float model_turn32_to_radians(uint32_t value) noexcept {
    return model_turn32_to_radians(thandor::runtime_angle_from_storage(value));
}

inline ModelHierarchyTransform fixed_model_rotation_basis(
    thandor::AngleTurn32 runtime_angle2, thandor::AngleTurn32 runtime_angle1,
    thandor::AngleTurn32 runtime_angle0) noexcept {
    // game.exe ModelNodeRuntime_ComposeChildTransformsRecursive passes
    // modelPayload.{rotationAngle2, rotationAngle1, rotationAngle0} to
    // FixedTransform_BuildRotationBasis's formal {angle0, angle1, angle2}.
    // The field-to-formal permutation is essential; treating the field suffixes as the formal
    // parameter names collapses cardinal child rotations and breaks placed model hierarchies.
    const float angle0 = model_turn32_to_radians(runtime_angle2);
    const float angle1 = model_turn32_to_radians(runtime_angle1);
    const float angle2 = model_turn32_to_radians(runtime_angle0);
    const float angle0_minus_2angle2 = angle0 - 2.0F * angle2;
    const float sin_angle1 = std::sin(angle1);
    const float d = (std::cos(angle0) - std::cos(angle0_minus_2angle2)) * 0.5F;
    const float c = (std::cos(angle0) + std::cos(angle0_minus_2angle2)) * 0.5F;
    const float s = (std::sin(angle0) + std::sin(angle0_minus_2angle2)) * 0.5F;
    const float t = (std::sin(angle0_minus_2angle2) - std::sin(angle0)) * 0.5F;
    ModelHierarchyTransform result;
    result.basis = {
        d + c * sin_angle1, t - s * sin_angle1, std::cos(angle1) * std::cos(angle2),
        s - t * sin_angle1, d * sin_angle1 + c, std::cos(angle1) * std::sin(angle2),
        -std::cos(angle1) * std::cos(angle0 - angle2),
        std::cos(angle1) * std::sin(angle0 - angle2), std::sin(angle1)};
    return result;
}


inline ModelHierarchyTransform fixed_model_rotation_basis(
    uint32_t runtime_angle2, uint32_t runtime_angle1, uint32_t runtime_angle0) noexcept {
    return fixed_model_rotation_basis(thandor::runtime_angle_from_storage(runtime_angle2),
        thandor::runtime_angle_from_storage(runtime_angle1),
        thandor::runtime_angle_from_storage(runtime_angle0));
}

inline ModelHierarchyTransform placement_root_transform(
    float x, float y, float z, float yaw) noexcept {
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    ModelHierarchyTransform result;
    result.basis = {c, -s, 0.0F,
                    s,  c, 0.0F,
                    0.0F, 0.0F, 1.0F};
    result.translation = {x, y, z};
    return result;
}

struct PlacementContactPose {
    float height{};
    Vec3f normal{0.0F, 0.0F, 1.0F};
    bool align_to_normal{};
    bool exact_callback{};
};

inline const char* placement_contact_kind_name(int32_t kind) noexcept {
    switch (kind) {
        case 0: return "terrain height + SPR placement offset";
        case 1: return "water surface";
        case 2: return "terrain height/normal + SPR placement offset";
        case 3: return "articulated suspension";
        case 4: return "top terrain/water surface";
        default: return "unknown contact callback";
    }
}

inline PlacementContactPose resolve_placement_contact_pose(
    int32_t kind, const thandor::TerrainSample& terrain, float water_surface_height,
    float top_surface_height, float sprite_placement_height_offset) noexcept {
    switch (kind) {
        case 0:
            return {terrain.height + sprite_placement_height_offset,
                    {0.0F, 0.0F, 1.0F}, false, true};
        case 1:
            return {water_surface_height, {0.0F, 0.0F, 1.0F}, false, true};
        case 2:
            return {terrain.height + sprite_placement_height_offset,
                    terrain.normal, true, true};
        case 3:
            // The paired-foot hierarchy solver owns the final class-3 root and terminal-contact
            // positions. This value is only the initial terrain anchor before that solver runs.
            return {terrain.height, {0.0F, 0.0F, 1.0F}, false, false};
        case 4:
            return {top_surface_height, {0.0F, 0.0F, 1.0F}, false, true};
        default:
            return {terrain.height + sprite_placement_height_offset,
                    {0.0F, 0.0F, 1.0F}, false, false};
    }
}

inline ModelHierarchyTransform terrain_contact_root_transform(
    Vec3f position, float yaw, Vec3f normal) noexcept {
    const float normal_length = std::sqrt(normal.x * normal.x + normal.y * normal.y +
                                          normal.z * normal.z);
    if (normal_length <= 1.0e-8F)
        return placement_root_transform(position.x, position.y, position.z, yaw);
    normal = {normal.x / normal_length, normal.y / normal_length, normal.z / normal_length};
    if (normal.z < 0.0F) normal = {-normal.x, -normal.y, -normal.z};
    const Vec3f desired_x{std::cos(yaw), std::sin(yaw), 0.0F};
    const float projection = desired_x.x * normal.x + desired_x.y * normal.y +
                             desired_x.z * normal.z;
    Vec3f axis_x{desired_x.x - normal.x * projection,
                 desired_x.y - normal.y * projection,
                 desired_x.z - normal.z * projection};
    float axis_x_length = std::sqrt(axis_x.x * axis_x.x + axis_x.y * axis_x.y +
                                    axis_x.z * axis_x.z);
    if (axis_x_length <= 1.0e-8F) {
        axis_x = {normal.z, 0.0F, -normal.x};
        axis_x_length = std::sqrt(axis_x.x * axis_x.x + axis_x.z * axis_x.z);
    }
    axis_x = {axis_x.x / axis_x_length, axis_x.y / axis_x_length,
              axis_x.z / axis_x_length};
    const Vec3f axis_y{normal.y * axis_x.z - normal.z * axis_x.y,
                       normal.z * axis_x.x - normal.x * axis_x.z,
                       normal.x * axis_x.y - normal.y * axis_x.x};
    ModelHierarchyTransform result;
    // Columns contain the world-space local X, local Y, and local Z axes.
    result.basis = {axis_x.x, axis_y.x, normal.x,
                    axis_x.y, axis_y.y, normal.y,
                    axis_x.z, axis_y.z, normal.z};
    result.translation = position;
    return result;
}

// Runtime class 17 composes a shallow movement-bank transform after terrain contact.
// classState60 uses 0x4000 as neutral and bottoms at 0x3800; ownerValue64 stores
// the world heading toward which the body leans. This matrix form preserves the
// recovered magnitude/heading state without flattening the root to yaw-only.
inline ModelHierarchyTransform class17_banked_root_transform(
    Vec3f position, uint32_t body_yaw_turn16, uint32_t bank_heading_turn16,
    uint32_t bank_angle_turn16, Vec3f contact_normal = {0.0F, 0.0F, 1.0F}) noexcept {
    ModelHierarchyTransform result = terrain_contact_root_transform(
        position, model_turn32_to_radians(body_yaw_turn16), contact_normal);
    const int32_t signed_bank = static_cast<int32_t>(0x4000U) -
        static_cast<int32_t>(bank_angle_turn16 & 0xFFFFU);
    if (signed_bank <= 0) return result;
    const float tilt = static_cast<float>(signed_bank) *
        (2.0F * std::numbers::pi_v<float> / 65536.0F);
    const float heading = model_turn32_to_radians(bank_heading_turn16);
    // Tilt toward the movement heading; the rotation axis is horizontal and
    // perpendicular to that heading. Rodrigues rotation is applied in world space.
    const Vec3f axis{-std::sin(heading), std::cos(heading), 0.0F};
    const float c=std::cos(tilt), si=std::sin(tilt);
    const auto rotate=[&](Vec3f v){
        const Vec3f cross{axis.y*v.z-axis.z*v.y,
                          axis.z*v.x-axis.x*v.z,
                          axis.x*v.y-axis.y*v.x};
        const float projection=axis.x*v.x+axis.y*v.y+axis.z*v.z;
        return Vec3f{v.x*c+cross.x*si+axis.x*projection*(1.0F-c),
                     v.y*c+cross.y*si+axis.y*projection*(1.0F-c),
                     v.z*c+cross.z*si+axis.z*projection*(1.0F-c)};
    };
    for(std::size_t column=0;column<3U;++column){
        const Vec3f source{result.basis[column],result.basis[3U+column],
                           result.basis[6U+column]};
        const Vec3f target=rotate(source);
        result.basis[column]=target.x;result.basis[3U+column]=target.y;
        result.basis[6U+column]=target.z;
    }
    return result;
}

// ModelRender_DrawMeshGroupsWithTemporaryTransform checks the selected SPR LOD flags before
// submitting triangles. Bit 0 applies the cylindrical/view-facing helper and bit 1 applies the
// full camera billboard helper. The latter runs second and therefore wins when both flags exist.
inline ModelHierarchyTransform view_facing_model_transform(
    Vec3f position, Vec3f camera_eye, float fallback_yaw = 0.0F) noexcept {
    Vec3f toward_camera{camera_eye.x - position.x, camera_eye.y - position.y, 0.0F};
    const float length_squared = toward_camera.x * toward_camera.x +
                                 toward_camera.y * toward_camera.y;
    float yaw = fallback_yaw;
    if (length_squared > 1.0e-10F) {
        // Local +Y faces the camera while local +Z remains the world vertical axis. This matches
        // the original helper's azimuth-only replacement of the model rotation basis.
        yaw = std::atan2(toward_camera.y, toward_camera.x) -
              std::numbers::pi_v<float> * 0.5F;
    }
    return placement_root_transform(position.x, position.y, position.z, yaw);
}

inline ModelHierarchyTransform billboard_model_transform(
    Vec3f position, Vec3f camera_eye, float fallback_yaw = 0.0F) noexcept {
    Vec3f normal{camera_eye.x - position.x,
                 camera_eye.y - position.y,
                 camera_eye.z - position.z};
    const float normal_length = std::sqrt(normal.x * normal.x + normal.y * normal.y +
                                          normal.z * normal.z);
    if (normal_length <= 1.0e-8F) {
        return placement_root_transform(position.x, position.y, position.z, fallback_yaw);
    }
    normal.x /= normal_length;
    normal.y /= normal_length;
    normal.z /= normal_length;

    Vec3f right{-normal.y, normal.x, 0.0F};
    float right_length = std::sqrt(right.x * right.x + right.y * right.y);
    if (right_length <= 1.0e-8F) {
        right = {std::cos(fallback_yaw), std::sin(fallback_yaw), 0.0F};
        right_length = 1.0F;
    }
    right.x /= right_length;
    right.y /= right_length;
    right.z = 0.0F;
    const Vec3f up{normal.y * right.z - normal.z * right.y,
                   normal.z * right.x - normal.x * right.z,
                   normal.x * right.y - normal.y * right.x};

    // transform_vector treats each triplet as one row. Columns therefore contain the desired
    // world-space local X/right, local Y/up and local Z/normal axes.
    ModelHierarchyTransform result;
    result.basis = {right.x, up.x, normal.x,
                    right.y, up.y, normal.y,
                    right.z, up.z, normal.z};
    result.translation = position;
    return result;
}

// ShotRuntimePool_CreateProjectileFromDefinitionCf stores the launch yaw in both
// modelPayload.worldRotationAngle0 and worldRotationAngle2, and stores the launch pitch in
// worldRotationAngle1. ShotModelRuntimeMaintenance adds spin only to angle2. The authored SHT
// forward axis is therefore local +Z; mapping local +X to flight inverts stock rockets and places
// kind-3 trail/launch emitters on the wrong end of the projectile.
inline ModelHierarchyTransform shot_model_transform(
    Vec3f position, uint32_t yaw_turn16, uint32_t pitch_turn16,
    uint32_t spin_turn16 = 0U) noexcept {
    ModelHierarchyTransform result = fixed_model_rotation_basis(
        static_cast<uint32_t>(yaw_turn16 + spin_turn16), pitch_turn16, yaw_turn16);
    result.translation = position;
    return result;
}

inline ModelHierarchyTransform direction_model_transform(
    Vec3f position, Vec3f direction, float fallback_yaw = 0.0F) noexcept {
    const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y +
                                   direction.z * direction.z);
    if (length <= 1.0e-8F) {
        return placement_root_transform(position.x, position.y, position.z, fallback_yaw);
    }
    const Vec3f forward{direction.x / length, direction.y / length, direction.z / length};
    Vec3f side{-forward.y, forward.x, 0.0F};
    float side_length = std::sqrt(side.x * side.x + side.y * side.y);
    if (side_length <= 1.0e-8F) {
        side = {std::cos(fallback_yaw + std::numbers::pi_v<float> * 0.5F),
                std::sin(fallback_yaw + std::numbers::pi_v<float> * 0.5F), 0.0F};
        side_length = 1.0F;
    }
    side.x /= side_length;
    side.y /= side_length;
    const Vec3f up{forward.y * side.z - forward.z * side.y,
                   forward.z * side.x - forward.x * side.z,
                   forward.x * side.y - forward.y * side.x};
    ModelHierarchyTransform result;
    // Columns are the world-space local +X (flight), +Y (side), and +Z (up) axes.
    result.basis = {forward.x, side.x, up.x,
                    forward.y, side.y, up.y,
                    forward.z, side.z, up.z};
    result.translation = position;
    return result;
}

inline ModelHierarchyTransform sprite_lod_facing_transform(
    Vec3f position, Vec3f camera_eye, float fallback_yaw, uint32_t lod_flags) noexcept {
    if ((lod_flags & 2U) != 0U) {
        return billboard_model_transform(position, camera_eye, fallback_yaw);
    }
    if ((lod_flags & 1U) != 0U) {
        return view_facing_model_transform(position, camera_eye, fallback_yaw);
    }
    return placement_root_transform(position.x, position.y, position.z, fallback_yaw);
}

// EffectRuntimePool_CreateInstanceFromDefinitionCf receives all three owner orientation angles.
// Billboard and cylindrical SPR flags still override that authored orientation, but fixed effect
// sprites must retain the complete owner basis instead of being flattened to world-up yaw only.
inline ModelHierarchyTransform sprite_lod_oriented_transform(
    Vec3f position, Vec3f camera_eye, const ModelHierarchyTransform& authored_orientation,
    float fallback_yaw, uint32_t lod_flags) noexcept {
    if ((lod_flags & 2U) != 0U) {
        return billboard_model_transform(position, camera_eye, fallback_yaw);
    }
    if ((lod_flags & 1U) != 0U) {
        return view_facing_model_transform(position, camera_eye, fallback_yaw);
    }
    ModelHierarchyTransform result = authored_orientation;
    result.translation = position;
    return result;
}

inline Vec3f transform_vector(const ModelHierarchyTransform& transform, Vec3f value) noexcept {
    return {transform.basis[0] * value.x + transform.basis[1] * value.y +
                transform.basis[2] * value.z,
            transform.basis[3] * value.x + transform.basis[4] * value.y +
                transform.basis[5] * value.z,
            transform.basis[6] * value.x + transform.basis[7] * value.y +
                transform.basis[8] * value.z};
}

inline Vec3f transform_point(const ModelHierarchyTransform& transform, Vec3f value) noexcept {
    const Vec3f rotated = transform_vector(transform, value);
    return {rotated.x + transform.translation.x,
            rotated.y + transform.translation.y,
            rotated.z + transform.translation.z};
}

inline ModelHierarchyTransform compose_model_transform(
    const ModelHierarchyTransform& parent, const ModelHierarchyTransform& local) noexcept {
    ModelHierarchyTransform result;
    for (std::size_t row = 0; row < 3U; ++row) {
        for (std::size_t column = 0; column < 3U; ++column) {
            result.basis[row * 3U + column] =
                parent.basis[row * 3U + 0U] * local.basis[0U * 3U + column] +
                parent.basis[row * 3U + 1U] * local.basis[1U * 3U + column] +
                parent.basis[row * 3U + 2U] * local.basis[2U * 3U + column];
        }
    }
    result.translation = transform_point(parent, local.translation);
    return result;
}

} // namespace editor
