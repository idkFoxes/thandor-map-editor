#include "model_vertex_lighting.hpp"

#include <algorithm>
#include <bit>
#include <iostream>
#include <stdexcept>

using namespace editor;
using namespace thandor;

namespace {
void require(bool condition, const char* why) {
    if (!condition) throw std::runtime_error(why);
}
bool same(float a, float b) {
    return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b);
}
uint32_t scalar_color(Vec3f normal, PreparedDirectionalLight light,
                      const std::array<uint32_t, 256>& ramp) {
    const float magnitude = std::sqrt(normal.x*normal.x + normal.y*normal.y + normal.z*normal.z);
    if (magnitude < 1.e-8F) normal = {0, 0, 1};
    else normal = {normal.x/magnitude, normal.y/magnitude, normal.z/magnitude};
    const float dot = std::clamp(normal.x*light.ray_direction.x + normal.y*light.ray_direction.y +
                                 normal.z*light.ray_direction.z, -1.F, 1.F);
    const int index = std::clamp(int(std::floor(dot*256.F)), -256, 256);
    return index >= 0 ? light.base_color : ramp[256+index];
}
void check(ModelVertexLighting actual, Vec3f source, const ModelHierarchyTransform& transform,
           PreparedDirectionalLight light, const std::array<uint32_t, 256>& ramp) {
    const Vec3f expected = normalize(transform_vector(transform, source));
    require(same(actual.normal.x, expected.x) && same(actual.normal.y, expected.y) &&
            same(actual.normal.z, expected.z), "normal reuse changed emitted float bits");
    require(actual.directional_color == scalar_color(expected, light, ramp),
            "normal reuse changed a signed light-ramp boundary");
}
}

int main() try {
    uint32_t random = 66103U;
    const auto component = [&] {
        random = random*1664525U + 1013904223U;
        return (float(int32_t(random >> 8U))-8388608.F)/131072.F;
    };
    size_t cases = 0;
    for (uint32_t pose = 0; pose < 1024U; ++pose) {
        auto transform = fixed_model_rotation_basis(pose*993U, pose*617U, pose*331U);
        if ((pose & 15U) == 0U) transform.basis.fill(0.F);
        if ((pose & 15U) == 1U) for (auto& value : transform.basis) value *= -0.5F;
        transform.translation = {component(), component(), component()};
        TerrainLighting lighting;
        lighting.azimuth = uint16_t(pose*787U); lighting.elevation = uint16_t(pose*229U);
        lighting.ramp_124 = 0xFF912C57U; lighting.ramp_12c = 0xFF17B957U;
        const auto ramp = lighting.build_game_ramp();
        const auto light = lighting.prepare_directional_light();
        ModelVertexLightingCache cache(transform, light, ramp);
        for (unsigned normal = 0; normal < 64U; ++normal) {
            Vec3f source{component(), component(), component()};
            if (normal == 0) source = {0, 0, 0};
            if (normal == 1) source = {0, 0, 1};
            if (normal == 2) source = {-0.F, 0.F, -0.F};
            // Equal corners hit; a changed normal misses; returning to it must be recomputed.
            for (unsigned repeat = 0; repeat < 3; ++repeat) {
                check(cache.sample(source), source, transform, light, ramp); ++cases;
            }
            const Vec3f alternate{source.z, -source.x, source.y};
            check(cache.sample(alternate), alternate, transform, light, ramp); ++cases;
            check(cache.sample(source), source, transform, light, ramp); ++cases;
        }
        // A new frame/node must use its new pose, direction and ramp, even for the same normal.
        auto changed = fixed_model_rotation_basis(pose*1171U, pose*233U, pose*73U);
        lighting.azimuth += 0x4000U; lighting.ramp_124 ^= 0x00FFFFFFU;
        const auto next_ramp = lighting.build_game_ramp();
        const auto next_light = lighting.prepare_directional_light();
        ModelVertexLightingCache next(changed, next_light, next_ramp);
        const Vec3f source{0, 0, 1};
        check(next.sample(source), source, changed, next_light, next_ramp); ++cases;
        // Pose/light inputs are copied into a node-local value, rather than held as stale aliases.
        transform.basis.fill(3.F); lighting.azimuth = 0;
        check(next.sample(source), source, changed, next_light, next_ramp); ++cases;
    }
    std::cout << "MODEL_VERTEX_LIGHTING cases=" << cases << " exact_normal_bits=1 exact_colors=1\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
}
