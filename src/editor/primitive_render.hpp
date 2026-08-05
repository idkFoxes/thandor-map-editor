#pragma once

#include <cstdint>

namespace editor {

// Thandor's DirectDraw display backend queues hardware triangles through Direct3D 2.  The
// six-bit primitive selector is dispatched through the table that begins at game.exe 0x00577EB8;
// 0x00577EA0 contains six non-primitive callbacks and must never be treated as selector zero.
enum class LegacyGraphicsBackend : std::uint8_t {
    DirectDrawDirect3D,
    Direct3D = DirectDrawDirect3D, // compatibility alias
    Glide3,
};

enum class ResourcePrimitiveRole : std::uint8_t {
    None,
    DenseBody,
    LuminousParticle,
};

enum class PrimitiveBlendPreset : std::uint8_t {
    OpaqueDepthWrite,
    AlphaDepthWrite,
    AlphaNoDepthWrite,
    AdditiveNoDepthWrite,
    // Editor-only late compositing pass for simulated fog of war. It is intentionally absent
    // from the retail primitive selector table and must be queued after terrain, lava/water,
    // opaque models, and transparent effects.
    FogOverlayNoDepthTest,
};

struct PrimitiveRenderDecision {
    PrimitiveBlendPreset blend{PrimitiveBlendPreset::OpaqueDepthWrite};
    bool two_sided{};
    bool supported{};
    std::uint8_t selector{};
    LegacyGraphicsBackend backend{LegacyGraphicsBackend::DirectDrawDirect3D};
};

constexpr PrimitiveRenderDecision classify_directdraw_direct3d_sprite_primitive(
    std::uint32_t render_flags, bool textured) noexcept {
    // GraphicsPrimitiveQueue_SetMaterial ORs 0x10000 when a texture is present.  The hardware
    // DirectDraw backend then indexes the 64 primitive handlers at 0x00577EB8.
    std::uint8_t selector = static_cast<std::uint8_t>((render_flags >> 12U) & 0x3FU);
    if (textured) selector = static_cast<std::uint8_t>(selector | 0x10U);

    PrimitiveRenderDecision result{};
    result.backend = LegacyGraphicsBackend::DirectDrawDirect3D;
    result.two_sided = (render_flags & 0x400U) != 0U;
    result.selector = selector;

    // Exact pointer-table families recovered from immutable game.exe.  Preset 0 is ONE/ZERO with
    // Z writes, presets 1/3 are SRCALPHA/INVSRCALPHA with Z writes, preset 2 is the same blend
    // without Z writes, and preset 4 is ONE/ONE without Z writes.
    switch (selector) {
    case 0U: case 4U: case 8U: case 12U: case 16U: case 24U:
        result.blend = PrimitiveBlendPreset::OpaqueDepthWrite;
        result.supported = true;
        break;
    case 6U: case 14U: case 20U: case 22U: case 28U: case 30U:
        result.blend = PrimitiveBlendPreset::AlphaDepthWrite;
        result.supported = true;
        break;
    case 1U: case 9U: case 17U: case 25U:
    case 32U: case 33U: case 36U: case 38U: case 40U: case 41U: case 44U: case 46U:
    case 48U: case 49U: case 52U: case 54U: case 56U: case 57U: case 60U: case 62U:
        result.blend = PrimitiveBlendPreset::AlphaNoDepthWrite;
        result.supported = true;
        break;
    case 2U: case 10U: case 18U: case 26U: case 34U: case 42U: case 50U: case 58U:
        result.blend = PrimitiveBlendPreset::AdditiveNoDepthWrite;
        result.supported = true;
        break;
    default:
        result.blend = PrimitiveBlendPreset::OpaqueDepthWrite;
        result.supported = false;
        break;
    }
    return result;
}

// Compatibility spelling retained for source users of G339 and earlier.
constexpr PrimitiveRenderDecision classify_direct3d_sprite_primitive(
    std::uint32_t render_flags, bool textured) noexcept {
    return classify_directdraw_direct3d_sprite_primitive(render_flags, textured);
}

constexpr PrimitiveRenderDecision classify_glide3_sprite_primitive(
    std::uint32_t render_flags) noexcept {
    PrimitiveRenderDecision result{};
    result.backend = LegacyGraphicsBackend::Glide3;
    result.two_sided = (render_flags & 0x400U) != 0U;
    result.supported = true;
    const std::uint32_t blend_field = (render_flags & 0x20000U) != 0U
        ? 0x1000U : (render_flags & 0x7000U);
    result.selector = static_cast<std::uint8_t>(blend_field >> 12U);
    if (blend_field == 0x2000U) result.blend = PrimitiveBlendPreset::AdditiveNoDepthWrite;
    else if (blend_field == 0U) result.blend = PrimitiveBlendPreset::OpaqueDepthWrite;
    else if (blend_field == 0x1000U) result.blend = PrimitiveBlendPreset::AlphaNoDepthWrite;
    else result.blend = PrimitiveBlendPreset::AlphaDepthWrite;
    return result;
}

constexpr PrimitiveRenderDecision classify_sprite_primitive(
    std::uint32_t render_flags, bool textured, LegacyGraphicsBackend backend) noexcept {
    return backend == LegacyGraphicsBackend::Glide3
        ? classify_glide3_sprite_primitive(render_flags)
        : classify_directdraw_direct3d_sprite_primitive(render_flags, textured);
}

constexpr PrimitiveRenderDecision classify_resource_preview_primitive(
    std::uint32_t render_flags, bool textured, ResourcePrimitiveRole role) noexcept {
    (void)role;
    // Both stock resource body and luminous triangles use serialized selector 18 after the texture
    // bit is applied.  DirectDraw/Direct3D routes selector 18 to TexturedPreset4 (ONE/ONE,
    // ALPHABLENDENABLE, ZWRITEENABLE=0).  Do not invent a depth-writing dense-body override.
    return classify_directdraw_direct3d_sprite_primitive(render_flags, textured);
}

// ModelRender_PrepareProjectedVertex at game.exe 0x004BD4B0 masks triangle flags with
// 0x8E00 and, when bit 0x0200 is present, copies the model tint directly to the prepared
// vertex at 0x004BD5A2-0x004BD610.  That branch deliberately bypasses directional and
// GraphicsShadingRuntime lighting.
constexpr bool directdraw_model_vertex_uses_direct_tint(std::uint32_t render_flags) noexcept {
    return (render_flags & 0x0200U) != 0U;
}

// Selects the packed model-vertex colour before the Vulkan shader applies nearby resource
// shading. The DirectDraw path copies modelNode+0x58 unchanged for 0x0200 triangles. Every
// non-bypass triangle must retain the ordinary directional/model-lighting result; replacing that
// result with the direct tint makes the resource crystal facets uniformly white.
constexpr std::uint32_t directdraw_select_model_vertex_packed_color(
    std::uint32_t render_flags, std::uint32_t direct_model_tint,
    std::uint32_t directionally_lit_model_tint) noexcept {
    return directdraw_model_vertex_uses_direct_tint(render_flags)
        ? direct_model_tint : directionally_lit_model_tint;
}


// DirectDraw/Direct3D 2 has no sRGB render-target state. Selector-18 ONE/ONE blending adds packed
// channel values and saturates in the framebuffer's encoded domain. This helper is used by the
// G343 regression to distinguish the game result from modern linear-light blending.
constexpr std::uint8_t directdraw_add_encoded_byte(std::uint8_t destination,
                                                    std::uint8_t source) noexcept {
    const std::uint16_t sum = static_cast<std::uint16_t>(destination) + source;
    return static_cast<std::uint8_t>(sum > 255U ? 255U : sum);
}

constexpr std::uint32_t sprite_palette_index(std::uint32_t render_flags) noexcept {
    return render_flags & 0x1FFU;
}

} // namespace editor
