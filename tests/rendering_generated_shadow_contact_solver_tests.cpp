#include "thandor/field.hpp"
#include "thandor/terrain_shadow.hpp"

#include <algorithm>
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

bool near(float a, float b, float epsilon = 1.0e-5F) {
    return std::abs(a - b) <= epsilon;
}

std::vector<thandor::GeneratedShadowTriangle> rectangle(
    float x, float y, float z0 = 0.15F, float z1 = 3.2F) {
    using thandor::GeneratedShadowTriangle;
    using thandor::Vec3f;
    const Vec3f a{x - 1.3F, y, z0};
    const Vec3f b{x + 1.3F, y, z0};
    const Vec3f c{x + 1.3F, y, z1};
    const Vec3f d{x - 1.3F, y, z1};
    return {{GeneratedShadowTriangle{{a, b, c}, true},
             GeneratedShadowTriangle{{a, c, d}, false}}};
}

uint8_t attenuation_for_distance_q12(int32_t distance_q12) {
    int32_t value = 0x5000 - std::max(distance_q12, 0);
    if (value < 0) value = 0;
    value >>= 6;
    return static_cast<uint8_t>(std::min(value, 0xFF));
}
} // namespace

int main() {
    try {
        using namespace thandor;

        // 004CDD40: model-node alpha is halved, expanded to the same 12-bit-ish lane as
        // the 0x0041EE80 attenuation entry, then PMULHW keeps the high word.
        check(retail_generated_shadow_packet_alpha(0xFFU, 0xFFU) == 127U,
              "opaque/full-distance generated-shadow alpha must be 127");
        check(retail_generated_shadow_packet_alpha(0xFFU, 128U) == 63U,
              "half attenuation must produce the recovered PMULHW alpha");
        check(retail_generated_shadow_packet_alpha(0x80U, 0xFFU) == 64U,
              "model tint alpha must participate before the attenuation multiply");
        check(retail_generated_shadow_packet_alpha(0xFFU, 0U) == 0U,
              "zero distance attenuation must erase the packet alpha");

        FieldGrid flat = FieldGrid::create(24U, 24U, 0, 0U);
        const auto& center_cell = flat.at(12U, 12U);
        const float center_x = q12_to_float(center_cell.world_x_q12);
        const float center_y = q12_to_float(center_cell.world_y_q12);
        const Vec3f standard_projection{0.32F, 0.24F, -0.916F};

        // Baseline flat terrain forces the ordinary projection ray at all twelve contacts.
        // It also proves the newly exposed distance byte is not a decorative field: contacts
        // farther down the receiver carry progressively weaker retail attenuation.
        const auto direct = build_game_generated_object_shadow(
            flat, rectangle(center_x, center_y), standard_projection);
        check(direct.has_value(), "flat direct-contact receiver was not built");
        check(direct->receiver_vertices.size() == 12U,
              "direct-contact receiver must retain twelve contacts");
        for (const auto& vertex : direct->receiver_vertices) {
            check(vertex.contact_path == GeneratedShadowReceiverContactPath::DirectTerrainRay,
                  "flat terrain unexpectedly left the direct-ray contact path");
        }
        check(direct->receiver_vertices[4].distance_attenuation <
                  direct->receiver_vertices[6].distance_attenuation,
              "direct-ray distances are not reflected in the recovered attenuation byte");

        // Raise the complete terrain above part of the receiver plane. The recovered branch
        // subtracts a cosine-scaled amount from V. Underflow at the three zero-V records
        // triggers the retail recast path rather than emitting a partial receiver.
        FieldGrid raised = flat;
        for (auto& cell : raised.cells()) cell.terrain_height_q12 = float_to_q12(4.0F);
        const auto top_surface = build_game_generated_object_shadow(
            raised, rectangle(center_x, center_y), standard_projection);
        check(top_surface.has_value(), "raised top-surface receiver was not built");
        std::size_t lift_count = 0U;
        std::size_t recast_count = 0U;
        for (std::size_t index = 0; index < top_surface->receiver_vertices.size(); ++index) {
            const auto& vertex = top_surface->receiver_vertices[index];
            if (vertex.contact_path == GeneratedShadowReceiverContactPath::TopSurfaceLift)
                ++lift_count;
            if (vertex.contact_path == GeneratedShadowReceiverContactPath::TopSurfaceRecast)
                ++recast_count;
            check(vertex.distance_attenuation == 255U,
                  "top-surface branch must keep the retail zero-distance attenuation");
        }
        check(lift_count == 9U && recast_count == 3U,
              "raised fixture must force nine lifts and three V-underflow recasts");
        check(top_surface->receiver_vertices[4].contact_path ==
                  GeneratedShadowReceiverContactPath::TopSurfaceRecast &&
              top_surface->receiver_vertices[8].contact_path ==
                  GeneratedShadowReceiverContactPath::TopSurfaceRecast &&
              top_surface->receiver_vertices[9].contact_path ==
                  GeneratedShadowReceiverContactPath::TopSurfaceRecast,
              "zero-V machine records did not select the recovered recast branch");
        check(near(top_surface->receiver_vertices[4].v, 0.0F) &&
              near(top_surface->receiver_vertices[8].v, 0.0F) &&
              near(top_surface->receiver_vertices[9].v, 0.0F),
              "top-surface recast must clamp an underflowed retail V coordinate to zero");
        check(top_surface->receiver_vertices[0].v < 0.25F &&
              top_surface->receiver_vertices[2].v < 0.75F &&
              top_surface->receiver_vertices[6].v < 1.0F,
              "top-surface lift did not apply the recovered cosine-scaled V subtraction");

        // Hard fallback fixture. A shallow -5 degree projection misses the low half of this
        // two-level field on the first 2*radius ray. The perpendicular/reversed-angle ray then
        // reaches terrain for all records with remaining V span. The three max-V records lie on
        // the high half and hit directly, preventing the retail fail-fast exit.
        FieldGrid stepped = flat;
        for (auto& cell : stepped.cells()) {
            const float world_x = q12_to_float(cell.world_x_q12);
            cell.terrain_height_q12 = float_to_q12(world_x < center_x ? -12.0F : 0.0F);
        }
        constexpr float pi = 3.14159265358979323846F;
        const float elevation = -5.0F * pi / 180.0F;
        const float azimuth = 30.0F * pi / 180.0F;
        const Vec3f shallow_projection{
            std::cos(elevation) * std::cos(azimuth),
            std::cos(elevation) * std::sin(azimuth),
            std::sin(elevation),
        };
        const auto fallback = build_game_generated_object_shadow(
            stepped, rectangle(center_x, center_y), shallow_projection);
        check(fallback.has_value(), "side-fallback receiver was not built");
        std::size_t direct_count = 0U;
        std::size_t fallback_count = 0U;
        for (const auto& vertex : fallback->receiver_vertices) {
            if (vertex.contact_path == GeneratedShadowReceiverContactPath::DirectTerrainRay)
                ++direct_count;
            if (vertex.contact_path == GeneratedShadowReceiverContactPath::FallbackSideRay)
                ++fallback_count;
        }
        check(direct_count == 3U && fallback_count == 9U,
              "step fixture must force nine side fallbacks and three direct contacts");
        check(fallback->receiver_vertices[6].contact_path ==
                  GeneratedShadowReceiverContactPath::DirectTerrainRay &&
              fallback->receiver_vertices[10].contact_path ==
                  GeneratedShadowReceiverContactPath::DirectTerrainRay &&
              fallback->receiver_vertices[11].contact_path ==
                  GeneratedShadowReceiverContactPath::DirectTerrainRay,
              "max-V records must remain direct hits in the side-fallback fixture");

        const int32_t radius_q12 = std::max(1, float_to_q12(fallback->bounding_radius));
        const int32_t twice_radius_q12 = radius_q12 * 2;
        const uint8_t expected_fallback_attenuation =
            attenuation_for_distance_q12(twice_radius_q12);
        static constexpr std::array<float, 12> seed_v{{
            0.25F, 0.25F, 0.75F, 0.75F, 0.0F, 0.5F,
            1.0F, 0.5F, 0.0F, 0.0F, 1.0F, 1.0F,
        }};
        for (std::size_t index = 0; index < fallback->receiver_vertices.size(); ++index) {
            const auto& vertex = fallback->receiver_vertices[index];
            if (vertex.contact_path != GeneratedShadowReceiverContactPath::FallbackSideRay)
                continue;
            check(vertex.distance_attenuation == expected_fallback_attenuation,
                  "side fallback must retain 2*radius as the packet attenuation distance");
            check(vertex.v > seed_v[index],
                  "side fallback did not apply the recovered positive V travel correction");
        }

        // Retail exits before packet reservation when a contact with no remaining V span misses
        // its first ray. Move the same object over uniformly deep terrain: max-V records cannot
        // use the side fallback, so no partial 14-packet mesh may escape.
        FieldGrid too_deep = flat;
        for (auto& cell : too_deep.cells()) cell.terrain_height_q12 = float_to_q12(-12.0F);
        const auto rejected = build_game_generated_object_shadow(
            too_deep, rectangle(center_x, center_y), shallow_projection);
        check(!rejected.has_value(),
              "receiver contact failure must abort instead of emitting a partial packet set");

        std::cout << "V536 generated-shadow terrain-contact solver tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V536 generated-shadow contact test failure: " << error.what() << '\n';
        return 1;
    }
}
