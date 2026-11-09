#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace editor {
// Capacity fallbacks may deliberately alias one physical image. Select one
// deterministic semantic representative per image before constructing a permutation.
// All aliases are remapped using that same resulting physical-index permutation.
inline std::unordered_map<std::uint64_t,std::uint32_t> unique_object_texture_slot_keys(
    const std::unordered_map<std::uint64_t,std::uint32_t>& keys,std::size_t layer_count){
    std::vector<std::uint64_t> smallest(layer_count);
    std::vector<bool> used(layer_count,false);
    for(const auto& [key,index]:keys){
        if(index>=layer_count)throw std::invalid_argument("texture alias exceeds atlas");
        if(!used[index]||key<smallest[index])smallest[index]=key;
        used[index]=true;
    }
    std::unordered_map<std::uint64_t,std::uint32_t> result;
    for(std::size_t index=0;index<layer_count;++index)
        if(used[index])result.emplace(smallest[index],static_cast<std::uint32_t>(index));
    return result;
}

// Pure permutation of object texture layers. Retain old physical indices for
// resident semantic sprite keys when possible, WITHOUT relocating anonymous
// layers. Generated shadow-mask atlases are appended directly to texture_layers
// and intentionally have no entry in the semantic key map. Their texture-layer
// references can already exist in static receiver vertices and dynamic shadow
// templates. Moving those anonymous layers corrupts projected shadows.
//
// Only permute keyed sprite layers into slots not occupied by anonymous images.
// The returned mapping covers every image index, including fixed anonymous ones.
// Apply it to images AND static object-mesh texture references, but never to
// static shadow receiver vertices or existing dynamic shadow templates.
inline std::vector<std::uint32_t> stable_object_texture_slot_permutation(
    const std::unordered_map<std::uint64_t,std::uint32_t>& current,
    const std::unordered_map<std::uint64_t,std::uint32_t>& previous,
    std::size_t layer_count) {
    if (layer_count > std::numeric_limits<std::uint32_t>::max() ||
        current.size() > layer_count)
        throw std::invalid_argument("semantic atlas has more keys than images");
    constexpr auto invalid = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> result(layer_count,invalid);
    std::vector<bool> keyed_index(layer_count,false),new_index_taken(layer_count,false);
    for (const auto& [key,index] : current) {
        (void)key;
        if (index >= layer_count || keyed_index[index])
            throw std::invalid_argument("invalid or duplicate source texture-layer index");
        keyed_index[index]=true;
    }
    // Anonymous shadow atlas images cannot move: shadow receiver vertices are
    // not part of the semantic mesh remapping below. Reserve their exact slots
    // before trying to retain indices from the previous scene.
    for (std::size_t index=0;index<layer_count;++index) {
        if (keyed_index[index]) continue;
        result[index]=static_cast<std::uint32_t>(index);
        new_index_taken[index]=true;
    }
    for (const auto& [key,index] : current) {
        const auto found=previous.find(key);
        if (found != previous.end() && found->second < layer_count &&
            !new_index_taken[found->second]) {
            result[index]=found->second;
            new_index_taken[found->second]=true;
        }
    }
    std::uint32_t next_free=0;
    for (std::size_t old_index=0;old_index<layer_count;++old_index) {
        if (result[old_index] != invalid) continue;
        while (next_free<layer_count && new_index_taken[next_free]) ++next_free;
        if (next_free>=layer_count)
            throw std::logic_error("texture-layer stable permutation exhausted free slots");
        result[old_index]=next_free;
        new_index_taken[next_free]=true;
    }
    return result;
}
} // namespace editor
