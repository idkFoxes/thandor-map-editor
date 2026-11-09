#pragma once
// Cache already terrain-projected, immutable shadow-receiver vertices for a single
// authored/runtime shadow template. Recreating the template clears the cache.
// Only the world pose (position + yaw) affects the projection; camera sorting,
// fog visibility, and triangle index offsets remain caller responsibilities.
#include "math.hpp"
#include <cstdint>
#include <utility>
#include <vector>

namespace editor {
template<class Vertex>
class ShadowReceiverPoseCache {
public:
    const std::vector<Vertex>* find(Vec3f position, uint32_t yaw) const noexcept {
        if (!valid_ || yaw_ != yaw ||
            position_.x != position.x || position_.y != position.y || position_.z != position.z)
            return nullptr;
        return &vertices_;
    }

    void store(Vec3f position, uint32_t yaw, std::vector<Vertex> vertices) {
        position_ = position;
        yaw_ = yaw;
        vertices_ = std::move(vertices);
        valid_ = true;
    }

    void clear() noexcept { valid_ = false; vertices_.clear(); }
private:
    bool valid_{};
    Vec3f position_{};
    uint32_t yaw_{};
    std::vector<Vertex> vertices_;
};
} // namespace editor
