#include "thandor/field.hpp"
#include "thandor/terrain_shadow.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<thandor::GeneratedShadowTriangle> rectangle(float x, float y) {
    using thandor::GeneratedShadowTriangle;
    using thandor::Vec3f;
    const Vec3f a{x - 1.3F, y, 0.15F};
    const Vec3f b{x + 1.3F, y, 0.15F};
    const Vec3f c{x + 1.3F, y, 3.2F};
    const Vec3f d{x - 1.3F, y, 3.2F};
    return {{GeneratedShadowTriangle{{a, b, c}, true},
             GeneratedShadowTriangle{{a, c, d}, false}}};
}

bool near(float a, float b, float epsilon = 1.0e-5F) {
    return std::abs(a - b) <= epsilon;
}
} // namespace

int main() {
    try {
        using namespace thandor;
        FieldGrid field = FieldGrid::create(24U, 24U, 0, 0U);
        const auto& center_cell = field.at(12U, 12U);
        const float x = q12_to_float(center_cell.world_x_q12);
        const float y = q12_to_float(center_cell.world_y_q12);
        const Vec3f light{0.32F, 0.24F, -0.916F};

        const auto shadow = build_game_generated_object_shadow(field, rectangle(x, y), light);
        check(shadow.has_value(), "generated shadow was not built");
        check(shadow->receiver_vertices.size() == 12U,
              "retail receiver must expose exactly twelve terrain-contact vertices");
        check(shadow->receiver_indices.size() == 42U,
              "retail receiver must expose exactly fourteen triangle packets");

        static constexpr std::array<std::array<float, 2>, 12> expected_uv{{
            {{0.25F, 0.25F}}, {{0.75F, 0.25F}}, {{0.25F, 0.75F}}, {{0.75F, 0.75F}},
            {{0.50F, 0.00F}}, {{1.00F, 0.50F}}, {{0.50F, 1.00F}}, {{0.00F, 0.50F}},
            {{0.00F, 0.00F}}, {{1.00F, 0.00F}}, {{1.00F, 1.00F}}, {{0.00F, 1.00F}},
        }};
        for (std::size_t index = 0; index < expected_uv.size(); ++index) {
            const auto& vertex = shadow->receiver_vertices[index];
            check(near(vertex.u, expected_uv[index][0]) && near(vertex.v, expected_uv[index][1]),
                  "receiver UV seed does not match the recovered quarter/half lattice");
            const auto terrain = field.sample_terrain(vertex.position.x, vertex.position.y);
            check(terrain.inside, "receiver vertex escaped the field");
            check(std::abs(vertex.position.z - (terrain.height + GeneratedShadowSettings{}.surface_bias)) < 0.003F,
                  "receiver contact does not lie on terrain plus Vulkan depth bias");
        }

        static constexpr std::array<uint32_t, 42> expected_indices{{
            0U, 1U, 2U,   3U, 1U, 2U,   0U, 1U, 4U,   3U, 1U, 5U,
            3U, 6U, 2U,   0U, 7U, 2U,   0U, 8U, 4U,   9U, 1U, 4U,
            9U, 1U, 5U,   3U, 10U, 5U,  3U, 6U, 10U,  11U, 6U, 2U,
            11U, 7U, 2U,  0U, 7U, 8U,
        }};
        check(std::equal(shadow->receiver_indices.begin(), shadow->receiver_indices.end(),
                         expected_indices.begin(), expected_indices.end()),
              "receiver triangle sequence does not match the fourteen retail packet copies");

        // The recovered packet set must tile the whole normalized receiver square exactly:
        // fourteen triangles with total normalized area 1 and no degenerate packet.
        float total_area = 0.0F;
        for (std::size_t triangle = 0; triangle < expected_indices.size(); triangle += 3U) {
            const auto& a = shadow->receiver_vertices[expected_indices[triangle + 0U]];
            const auto& b = shadow->receiver_vertices[expected_indices[triangle + 1U]];
            const auto& c = shadow->receiver_vertices[expected_indices[triangle + 2U]];
            const float twice_area = std::abs((b.u - a.u) * (c.v - a.v) -
                                              (b.v - a.v) * (c.u - a.u));
            check(twice_area > 0.0F, "receiver packet is degenerate");
            total_area += 0.5F * twice_area;
        }
        check(near(total_area, 1.0F),
              "fourteen receiver packets do not tile the normalized receiver square");

        std::cout << "V535 generated-shadow receiver packet topology tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V535 receiver topology test failure: " << error.what() << '\n';
        return 1;
    }
}
