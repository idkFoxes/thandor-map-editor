#pragma once

#include "thandor/field.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace editor {
using thandor::Vec3f;

struct Vec4f { float x{}, y{}, z{}, w{}; };
struct Mat4 {
    std::array<float, 16> m{};
    float& operator()(int row, int col) { return m[static_cast<std::size_t>(col) * 4 + row]; }
    float operator()(int row, int col) const { return m[static_cast<std::size_t>(col) * 4 + row]; }
};

inline Vec3f add(Vec3f a, Vec3f b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3f sub(Vec3f a, Vec3f b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3f mul(Vec3f a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3f a, Vec3f b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
// Keep the 3D magnitude helper domain-specific. asset_preview.hpp also defines a 2D
// length(PreviewVec2) helper in namespace editor, so a generic unqualified length() call can
// bind to the wrong overload when the full GUI translation unit includes both headers.
inline float vec3_length(Vec3f value) noexcept { return std::sqrt(dot(value, value)); }
inline Vec3f cross(Vec3f a, Vec3f b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}
inline Vec3f normalize(Vec3f v) {
    const float magnitude = vec3_length(v);
    return magnitude > 1.0e-8F ? mul(v, 1.0F / magnitude) : Vec3f{0.0F, 0.0F, 1.0F};
}

inline Mat4 identity() {
    Mat4 result{};
    for (int index = 0; index < 4; ++index) result(index, index) = 1.0F;
    return result;
}
inline Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            for (int inner = 0; inner < 4; ++inner) {
                result(row, column) += a(row, inner) * b(inner, column);
            }
        }
    }
    return result;
}
inline Vec4f multiply(const Mat4& a, Vec4f v) {
    return {
        a(0, 0) * v.x + a(0, 1) * v.y + a(0, 2) * v.z + a(0, 3) * v.w,
        a(1, 0) * v.x + a(1, 1) * v.y + a(1, 2) * v.z + a(1, 3) * v.w,
        a(2, 0) * v.x + a(2, 1) * v.y + a(2, 2) * v.z + a(2, 3) * v.w,
        a(3, 0) * v.x + a(3, 1) * v.y + a(3, 2) * v.z + a(3, 3) * v.w,
    };
}
inline Mat4 perspective(float fov_y, float aspect, float near_z, float far_z) {
    Mat4 result{};
    const float focal = 1.0F / std::tan(fov_y * 0.5F);
    result(0, 0) = focal / aspect;
    result(1, 1) = -focal; // Vulkan framebuffer Y points down.
    result(2, 2) = far_z / (near_z - far_z);
    result(2, 3) = (far_z * near_z) / (near_z - far_z);
    result(3, 2) = -1.0F;
    return result;
}
inline Mat4 look_at(Vec3f eye, Vec3f center, Vec3f up) {
    const Vec3f forward = normalize(sub(center, eye));
    const Vec3f right = normalize(cross(forward, up));
    const Vec3f camera_up = cross(right, forward);
    Mat4 result = identity();

    // Mat4 is column-major in memory but indexed as (row, column). For column-vector
    // multiplication the camera basis belongs in rows. Putting it in columns transposes the
    // rotation and mirrors/skews the editor view whenever the camera is not axis-aligned.
    result(0, 0) = right.x;
    result(0, 1) = right.y;
    result(0, 2) = right.z;
    result(1, 0) = camera_up.x;
    result(1, 1) = camera_up.y;
    result(1, 2) = camera_up.z;
    result(2, 0) = -forward.x;
    result(2, 1) = -forward.y;
    result(2, 2) = -forward.z;
    result(0, 3) = -dot(right, eye);
    result(1, 3) = -dot(camera_up, eye);
    result(2, 3) = dot(forward, eye);
    return result;
}

struct Camera {
    static constexpr float kMinimumPitch = 0.12F;
    static constexpr float kMaximumPitch = 1.45F;

    Vec3f target{};
    float yaw{-0.8F};
    float pitch{0.75F};
    float distance{45.0F};
    float fov_y{0.75F};

    float clamped_pitch() const noexcept {
        return std::clamp(pitch, kMinimumPitch, kMaximumPitch);
    }
    Vec3f planar_forward() const noexcept {
        return {-std::cos(yaw), -std::sin(yaw), 0.0F};
    }
    Vec3f planar_right() const noexcept {
        return {-std::sin(yaw), std::cos(yaw), 0.0F};
    }
    void orbit(float yaw_delta, float pitch_delta) noexcept {
        yaw = std::remainder(yaw + yaw_delta, 2.0F * std::numbers::pi_v<float>);
        pitch = std::clamp(pitch + pitch_delta, kMinimumPitch, kMaximumPitch);
    }
    void move_planar(float right_distance, float forward_distance) noexcept {
        target = add(target, add(mul(planar_right(), right_distance),
                                 mul(planar_forward(), forward_distance)));
    }
    Vec3f eye() const {
        const float safe_pitch = clamped_pitch();
        const float cosine_pitch = std::cos(safe_pitch);
        return add(target, {distance * cosine_pitch * std::cos(yaw),
                            distance * cosine_pitch * std::sin(yaw),
                            distance * std::sin(safe_pitch)});
    }
    Mat4 view() const { return look_at(eye(), target, {0.0F, 0.0F, 1.0F}); }
    Mat4 projection(float aspect) const {
        return perspective(fov_y, std::max(aspect, 0.01F), 0.05F, 2000.0F);
    }
    Mat4 view_projection(float aspect) const { return multiply(projection(aspect), view()); }
};

inline bool project_to_screen(Vec3f point, const Mat4& view_projection, float width, float height,
                              float& screen_x, float& screen_y, float& depth) {
    const auto clip = multiply(view_projection, Vec4f{point.x, point.y, point.z, 1.0F});
    if (clip.w <= 0.0001F) return false;

    const float normalized_x = clip.x / clip.w;
    const float normalized_y = clip.y / clip.w;
    const float normalized_z = clip.z / clip.w;
    if (normalized_z < 0.0F || normalized_z > 1.0F) return false;

    screen_x = (normalized_x * 0.5F + 0.5F) * width;
    screen_y = (normalized_y * 0.5F + 0.5F) * height;
    depth = normalized_z;
    return true;
}
} // namespace editor
