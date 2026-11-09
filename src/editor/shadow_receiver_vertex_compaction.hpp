#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <utility>

namespace editor {

// The V698 adaptive receiver builder emits independently indexed triangles.
// Deduplicate matching render vertices *after* subdivision, preserving the original
// triangles, mask UVs, alpha and depth. The caller runs this once when building
// templates; the dynamic scene then transforms each unique vertex only once/tick.
// Hash the exact floating point bits, rather than quantizing geometry or UVs.
// This is a lossless optimization, not a visual approximation.
template<class Vertex>
void compact_generated_shadow_receiver_vertices(std::vector<Vertex>& vertices,
                                                std::vector<std::uint32_t>& indices) {
    if (vertices.empty() || indices.empty()) return;
    struct Key {
        std::array<std::uint32_t, 13> bits{};
        bool operator==(const Key&) const = default;
    };
    struct Hash {
        std::size_t operator()(const Key& key) const noexcept {
            std::uint64_t hash = 1469598103934665603ULL;
            for (const auto value : key.bits) {
                hash ^= static_cast<std::uint64_t>(value);
                hash *= 1099511628211ULL;
            }
            return static_cast<std::size_t>(hash);
        }
    };
    auto key_for=[](const Vertex& v) -> Key {
        return Key{{
            std::bit_cast<std::uint32_t>(v.position.x),
            std::bit_cast<std::uint32_t>(v.position.y),
            std::bit_cast<std::uint32_t>(v.position.z),
            std::bit_cast<std::uint32_t>(v.u),
            std::bit_cast<std::uint32_t>(v.v),
            std::bit_cast<std::uint32_t>(v.normal.x),
            std::bit_cast<std::uint32_t>(v.normal.y),
            std::bit_cast<std::uint32_t>(v.normal.z),
            v.texture_layer,v.packed_color_argb,v.direct_tint_bypass,
            std::bit_cast<std::uint32_t>(v.animation_v_amplitude),
            std::bit_cast<std::uint32_t>(v.animation_cycle_ticks)
        }};
    };
    std::vector<Vertex> unique;
    unique.reserve(vertices.size());
    std::vector<std::uint32_t> remapped;
    remapped.reserve(indices.size());
    std::unordered_map<Key,std::uint32_t,Hash> seen;
    seen.reserve(vertices.size());
    for (const auto original_index : indices) {
        if (original_index >= vertices.size())
            throw std::runtime_error("generated shadow receiver has an invalid index");
        const auto& vertex = vertices[original_index];
        const auto [at, inserted] = seen.try_emplace(key_for(vertex),
            static_cast<std::uint32_t>(unique.size()));
        if (inserted) unique.push_back(vertex);
        remapped.push_back(at->second);
    }
    vertices = std::move(unique);
    indices = std::move(remapped);
}

} // namespace editor
