#include "shadow_receiver_vertex_compaction.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
struct Point { float x,y,z; };
struct Vertex {
    Point position{};
    Point normal{0.0F,0.0F,1.0F};
    float u{},v{};
    std::uint32_t texture_layer{},packed_color_argb{},direct_tint_bypass{};
    float animation_v_amplitude{},animation_cycle_ticks{};
};
void check(bool pass,const char* reason) {if(!pass)throw std::runtime_error(reason);}
bool equal(const Vertex& a,const Vertex& b) {
    return a.position.x==b.position.x&&a.position.y==b.position.y&&a.position.z==b.position.z&&
           a.normal.x==b.normal.x&&a.normal.y==b.normal.y&&a.normal.z==b.normal.z&&
           a.u==b.u&&a.v==b.v&&a.texture_layer==b.texture_layer&&
           a.packed_color_argb==b.packed_color_argb&&a.direct_tint_bypass==b.direct_tint_bypass&&
           a.animation_v_amplitude==b.animation_v_amplitude&&a.animation_cycle_ticks==b.animation_cycle_ticks;
}
}

int main() try {
    // V698 independently generated two adjacent right triangles, duplicating
    // the A and C vertices even though texture/alpha/world-space data match.
    std::vector<Vertex> vertices{
        {{0,0,0}, {0,0,1},0,0,1,0x50000000U,1},
        {{1,0,0}, {0,0,1},1,0,1,0x50000000U,1},
        {{0,1,0}, {0,0,1},0,1,1,0x50000000U,1},
        {{0,1,0}, {0,0,1},0,1,1,0x50000000U,1},
        {{1,0,0}, {0,0,1},1,0,1,0x50000000U,1},
        {{1,1,0}, {0,0,1},1,1,1,0x50000000U,1},
        // Same terrain point but DIFFERENT attenuation and UV: must remain distinct.
        {{1,1,0}, {0,0,1},.99F,1,1,0x60000000U,1}
    };
    std::vector<std::uint32_t> indices{0,1,2,3,4,5,0,2,6};
    std::vector<Vertex> expected;
    for(const auto index:indices)expected.push_back(vertices[index]);
    editor::compact_generated_shadow_receiver_vertices(vertices,indices);
    check(vertices.size()==5,"shared projected receiver coordinates were not deduplicated");
    check(indices.size()==expected.size(),"shadow triangle count was changed");
    for(std::size_t i=0;i<indices.size();++i)
        check(equal(vertices.at(indices.at(i)),expected.at(i)),"shadow image/geometry payload changed");
    const auto compacted_vertices=vertices.size();
    const auto compacted_indices=indices;
    editor::compact_generated_shadow_receiver_vertices(vertices,indices);
    check(compacted_vertices==vertices.size() && compacted_indices==indices,
          "receiver compaction is not idempotent");
    auto broken=indices;
    broken.push_back(static_cast<std::uint32_t>(vertices.size()+1U));
    bool failed=false;
    try{editor::compact_generated_shadow_receiver_vertices(vertices,broken);}catch(const std::runtime_error&){failed=true;}
    check(failed,"invalid renderer index silently accepted");
    std::cout<<"RENDERING_SHADOW_RECEIVER_COMPACTION_PASS\n";
} catch(const std::exception& e) {
    std::cerr<<"RENDERING_SHADOW_RECEIVER_COMPACTION_FAIL "<<e.what()<<'\n';return 1;
}
