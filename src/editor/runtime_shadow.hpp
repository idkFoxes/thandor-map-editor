#pragma once

#include "model_transform.hpp"
#include "thandor/model.hpp"
#include "thandor/terrain_shadow.hpp"

#include <stdexcept>

namespace editor {

enum class RuntimeShadowSource : uint8_t { LevelPlacement, Produced, LinkedChild, LabEntity };
struct RuntimeShadowOwner {
    RuntimeShadowSource source{RuntimeShadowSource::LevelPlacement};
    uint64_t id{};
    uint32_t run{};
    bool operator==(const RuntimeShadowOwner&) const = default;
};

// Consume only the dedicated SPR shadow table, with the same posed node transform
// used by the visible model. Never substitute the visible LOD mesh.
inline void append_runtime_shadow_triangles(const thandor::SpriteMesh& mesh,
        const ModelHierarchyTransform& transform,
        std::vector<thandor::GeneratedShadowTriangle>& triangles) {
    if (mesh.indices.size() % 3U != 0U)
        throw std::runtime_error("runtime SPR shadow mesh is not triangle aligned");
    for (size_t i = 0; i < mesh.indices.size(); i += 3U) {
        const auto& a = mesh.vertices.at(mesh.indices[i]);
        const auto& b = mesh.vertices.at(mesh.indices[i+1U]);
        const auto& c = mesh.vertices.at(mesh.indices[i+2U]);
        if (a.mesh_record_flags != b.mesh_record_flags || a.mesh_record_flags != c.mesh_record_flags)
            throw std::runtime_error("runtime SPR shadow triangle mixes record flags");
        triangles.push_back({{transform_point(transform,a.position),
                              transform_point(transform,b.position),
                              transform_point(transform,c.position)},
                             (a.mesh_record_flags & 1U) != 0U});
    }
}

} // namespace editor
