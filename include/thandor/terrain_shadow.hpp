#pragma once

#include "thandor/field.hpp"
#include "thandor/gfx.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace thandor {

// PersistentSettings_Load supplies these exact defaults to
// GraphicsShadingRuntime_InitializeGeneratedTextureCf in game.exe.
struct GeneratedShadowSettings {
    uint32_t texture_dimension{64U};
    uint32_t grid_half_size{32U};
    uint32_t subresource_count{16U};
    uint32_t receiver_grid_dimension{5U};
    float surface_bias{0.035F};
};

struct GeneratedShadowTriangle {
    std::array<Vec3f, 3> world{};
    // game.exe rasterizes the dedicated SPR +0xEC shadow records whose +0x10 bit 0 is
    // set, blurs that first pass, then writes clear-bit records as a hard silhouette.
    bool blurred_group{};
};

struct GeneratedShadowReceiverVertex {
    Vec3f position{};
    float u{};
    float v{};
};

struct GeneratedObjectShadow {
    RgbaImage mask;
    std::vector<GeneratedShadowReceiverVertex> receiver_vertices;
    std::vector<uint32_t> receiver_indices;
    Vec3f projection_direction{};
    float bounding_radius{};
    std::size_t blurred_triangle_count{};
    std::size_t hard_triangle_count{};
};

// Reconstructs the generated-model-shadow path used by game.exe: dedicated SPR +0xEC
// record geometry, a 64x64 plane normal to the LEV light direction, quantized soft-record
// blur, hard-record overwrite, and a light-direction raycast receiver conforming to the
// edited FLD surface. LEV +0x2F0 supplies low-word azimuth and high-word elevation.
std::optional<GeneratedObjectShadow> build_game_generated_object_shadow(
    const FieldGrid& field, std::span<const GeneratedShadowTriangle> triangles,
    Vec3f shadow_projection_direction, GeneratedShadowSettings settings = {});

// Legacy point-projection diagnostic retained for compatibility tests. The active generated
// shadow path uses the same LEV directional-light angles, but rasterizes the dedicated SPR
// shadow table into a receiver texture instead of projecting each visible-mesh vertex.
std::optional<Vec3f> project_directional_shadow_to_field(
    const FieldGrid& field, Vec3f world_point, Vec3f direction_to_light,
    float surface_bias = 0.035F, unsigned refinement_iterations = 3U,
    float maximum_projection_distance = 96.0F);

// Returns true when all three source vertices already lie on the edited FLD receiver surface.
// Such triangles are ground decals/footprints, not elevated shadow casters.
bool triangle_lies_on_field_receiver(
    const FieldGrid& field, const std::array<Vec3f, 3>& world_triangle,
    float maximum_height_delta = 0.08F) noexcept;

} // namespace thandor
