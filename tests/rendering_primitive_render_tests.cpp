#include "primitive_render.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() try {
    using editor::LegacyGraphicsBackend;
    using editor::PrimitiveBlendPreset;
    using editor::ResourcePrimitiveRole;
    using editor::classify_resource_preview_primitive;
    using editor::classify_sprite_primitive;

    // The table begins at 0x00577EB8 after six non-primitive callbacks.  These selectors seal one
    // representative of each exact DirectDraw/Direct3D render-state family.
    const auto p0 = classify_sprite_primitive(0U, true, LegacyGraphicsBackend::DirectDrawDirect3D);
    check(p0.supported && p0.selector == 16U && p0.blend == PrimitiveBlendPreset::OpaqueDepthWrite,
          "DirectDraw textured preset 0 mismatch");
    const auto p2 = classify_sprite_primitive(1U << 12U, true, LegacyGraphicsBackend::DirectDrawDirect3D);
    check(p2.supported && p2.selector == 17U && p2.blend == PrimitiveBlendPreset::AlphaNoDepthWrite,
          "DirectDraw textured preset 2 mismatch");
    const auto p4 = classify_sprite_primitive(2U << 12U, true, LegacyGraphicsBackend::DirectDrawDirect3D);
    check(p4.supported && p4.selector == 18U && p4.blend == PrimitiveBlendPreset::AdditiveNoDepthWrite,
          "DirectDraw textured preset 4 mismatch");
    const auto p1 = classify_sprite_primitive(4U << 12U, true, LegacyGraphicsBackend::DirectDrawDirect3D);
    check(p1.supported && p1.selector == 20U && p1.blend == PrimitiveBlendPreset::AlphaDepthWrite,
          "DirectDraw textured preset 1 mismatch");
    const auto p3 = classify_sprite_primitive(6U << 12U, true, LegacyGraphicsBackend::DirectDrawDirect3D);
    check(p3.supported && p3.selector == 22U && p3.blend == PrimitiveBlendPreset::AlphaDepthWrite,
          "DirectDraw textured preset 3 mismatch");

    const auto unsupported = classify_sprite_primitive(
        3U << 12U, true, LegacyGraphicsBackend::DirectDrawDirect3D);
    check(!unsupported.supported && unsupported.selector == 19U,
          "DirectDraw null dispatch selector was guessed instead of reported");

    constexpr std::array<std::uint32_t, 5> resource_flags{
        0x221AU, 0x201AU, 0x221BU, 0x2020U, 0x2221U};
    for (const auto flags : resource_flags) {
        for (const auto role : {ResourcePrimitiveRole::DenseBody,
                                ResourcePrimitiveRole::LuminousParticle}) {
            const auto decision = classify_resource_preview_primitive(flags, true, role);
            check(decision.supported && decision.selector == 18U &&
                  decision.backend == LegacyGraphicsBackend::DirectDrawDirect3D &&
                  decision.blend == PrimitiveBlendPreset::AdditiveNoDepthWrite,
                  "resource triangle did not use DirectDraw selector 18 preset 4");
        }
    }

    const auto ordinary = classify_resource_preview_primitive(
        0x8007U, true, ResourcePrimitiveRole::None);
    check(ordinary.selector == 24U && ordinary.blend == PrimitiveBlendPreset::OpaqueDepthWrite,
          "ordinary 0x8007 object did not use exact DirectDraw selector 24 preset 0");

    const auto two_sided = classify_resource_preview_primitive(
        0x221AU | 0x400U, true, ResourcePrimitiveRole::LuminousParticle);
    check(two_sided.two_sided, "render flag 0x400 did not bypass culling");
    check(editor::sprite_palette_index(0xFFFFFFFFU) == 0x1FFU,
          "low-nine-bit palette extraction changed");

    std::cout << "G340_DIRECTDRAW_RESOURCE_PRIMITIVE_ROUTING PASS"
              << " resource-selector=18"
              << " resource-preset=one-one-additive-no-zwrite"
              << " ordinary-0x8007-selector=24-opaque-zwrite"
              << " render-flag-0x400=two-sided\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G340_DIRECTDRAW_RESOURCE_PRIMITIVE_ROUTING FAIL: " << error.what() << '\n';
    return 1;
}
