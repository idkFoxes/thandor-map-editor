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
    // Retail generated-texture subresource dimension. Persistent settings default to 64.
    uint32_t texture_dimension{64U};
    // Retail per-caster tile side. The generated-texture cursor advances by this amount on both
    // axes; the default 32 therefore yields four caster tiles per 64x64 subresource.
    uint32_t grid_half_size{32U};
    // Number of generated-texture subresources. Default 16 => 64 caster tiles per retail pass.
    uint32_t subresource_count{16U};
    // Small Vulkan depth bias applied after the machine-backed receiver contact solve.
    float surface_bias{0.035F};
};

bool retail_generated_shadow_settings_valid(GeneratedShadowSettings settings) noexcept;
uint32_t retail_generated_shadow_tiles_per_subresource(GeneratedShadowSettings settings) noexcept;
uint32_t retail_generated_shadow_tile_capacity(GeneratedShadowSettings settings) noexcept;

// Exact alpha lane of the legacy generated-shadow packet tint path. Retail first halves the
// model-node alpha byte, expands it to the Q12-ish MMX alpha lane, then PMULHW-multiplies it by the
// high 16-bit alpha lane of the 0..255 distance-attenuation lookup table. The Vulkan shadow shader consumes only this alpha.
uint8_t retail_generated_shadow_packet_alpha(uint8_t model_tint_alpha,
                                             uint8_t distance_attenuation) noexcept;

struct GeneratedShadowTriangle {
    std::array<Vec3f, 3> world{};
    // game.exe rasterizes the dedicated SPR +0xEC shadow records whose +0x10 bit 0 is
    // set, blurs that first pass, then writes clear-bit records as a hard silhouette.
    bool blurred_group{};
};

enum class GeneratedShadowReceiverContactPath : uint8_t {
    DirectTerrainRay,
    FallbackSideRay,
    TopSurfaceLift,
    TopSurfaceRecast,
};

struct GeneratedShadowReceiverVertex {
    Vec3f position{};
    float u{};
    float v{};
    // Retail converts the projection-ray distance through
    // clamp((0x5000 - distanceQ12) >> 6, 0, 255) before modulating the
    // projected packet tint. Keeping the byte explicit prevents the Vulkan
    // path from folding distance fade into authored/model tint semantics.
    uint8_t distance_attenuation{255U};
    GeneratedShadowReceiverContactPath contact_path{
        GeneratedShadowReceiverContactPath::DirectTerrainRay};
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

// Reconstructs the machine-proven caster side of the generated-model-shadow path: dedicated
// SPR +0xEC record geometry, one grid_half_size-square caster tile inside a
// texture_dimension-square retail generated subresource, quantized soft-record blur, and
// hard-record overwrite. The receiver follows the retail twelve-contact / fourteen-triangle
// packet topology recovered from GraphicsShadingGeneratedTexture_ProcessRenderableHierarchy and
// ReserveFourteenProjectedPointBlocks. V537: LEV +0x2E0 supplies low-word azimuth and high-word elevation; +0x2F0 is field-region width/height.
// V536 additionally reproduces the shared twelve-point terrain-contact state machine: direct
// terrain ray, side-fallback ray with mutable V-coordinate correction, top-surface lift/recast,
// and the 0x5000 distance attenuation byte. Vulkan depth bias is applied only after that contact
// solve; legacy software projected-point packing remains a backend-specific representation.
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
