#include "thandor/effect.hpp"
#include "thandor/field.hpp"
#include "thandor/model.hpp"
#include "../src/editor/primitive_render.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    using editor::PrimitiveBlendPreset;
    using editor::ResourcePrimitiveRole;

    // Stock resource flags 0x201A/0x221A/0x221B/0x2020/0x2221 become selector 18 after
    // GraphicsPrimitiveQueue_SetMaterial adds the texture bit.  The DirectDraw/Direct3D table at
    // 0x00577EB8 routes selector 18 to TexturedPreset4: ONE/ONE, Z writes disabled.
    for (const std::uint32_t flags : {0x201AU, 0x221AU, 0x221BU, 0x2020U, 0x2221U}) {
        const auto decision = editor::classify_resource_preview_primitive(
            flags, true, ResourcePrimitiveRole::DenseBody);
        require(decision.supported, "resource selector is unsupported");
        require(decision.selector == 18U, "resource selector is not 18");
        require(decision.blend == PrimitiveBlendPreset::AdditiveNoDepthWrite,
                "resource triangles are not DirectDraw preset 4");
    }

    const std::vector<thandor::ResourceEmitterTiming> timings{
        {80U, 10U}, {80U, 10U}, {80U, 10U},
    };
    const auto schedule = thandor::build_resource_emitter_schedule(
        timings, thandor::FieldGrid::kDefaultSoilRandomSeed, 512U, 256U);
    require(schedule.cycle_ticks == 512U, "unexpected schedule cycle");
    require(schedule.pulses.size() > timings.size(), "schedule has no repeated pulses");
    for (std::uint32_t index = 0; index < timings.size(); ++index) {
        require(schedule.pulses[index].emitter_index == index,
                "initial resource pulse order changed");
        require(schedule.pulses[index].spawn_tick == 0U,
                "map resources must emit immediately at tick zero");
    }
    for (std::size_t i = timings.size(); i < schedule.pulses.size(); ++i) {
        require(schedule.pulses[i].spawn_tick >= 80U,
                "later resource pulse occurred before the minimum interval");
        require(schedule.pulses[i].spawn_tick < schedule.cycle_ticks,
                "resource pulse exceeds cycle");
    }

    thandor::ResourceShadingGlowProfile gold{};
    gold.packed_color_rgb = 0x009B5A00U;
    gold.target_radius_world = 2.0F;
    gold.grow_transition_ticks = 35.0F;
    gold.shading_stop_tick = 79.0F;
    gold.release_transition_ticks = 35.0F;
    const auto result = thandor::graphics_shading_directdraw_vertex_light(
        {0.25F, 0.25F, 0.25F}, gold, 35.0F, 0.0F);
    require(result[0] > result[1] && result[1] > result[2],
            "DirectDraw vertex lighting did not preserve the packed gold contribution");

    std::cout << "G340 DirectDraw resource animation checks passed\n";
    return 0;
}
