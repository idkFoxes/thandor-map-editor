#include "thandor/effect.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() try {
    using namespace thandor;

    // A pulse at cycle phase 5 creates a detached parent EffectRuntime. A linked shot emitted
    // 12 ticks later must resolve that exact absolute parent tick, including across cycle wrap.
    const auto first = linked_shot_parent_effect_spawn_tick(17U, 20U, 5U, 12U);
    require(first && *first == 5U, "same-cycle parent effect spawn was not recovered");

    const auto wrapped = linked_shot_parent_effect_spawn_tick(37U, 20U, 5U, 12U);
    require(wrapped && *wrapped == 25U, "wrapped parent effect spawn lost absolute cycle identity");

    require(!linked_shot_parent_effect_spawn_tick(36U, 20U, 5U, 12U),
            "wrong cycle phase fabricated a parent effect instance");
    require(!linked_shot_parent_effect_spawn_tick(7U, 20U, 5U, 12U),
            "pre-parent tick underflow fabricated a parent effect instance");
    require(!linked_shot_parent_effect_spawn_tick(17U, 0U, 0U, 12U),
            "zero schedule cycle fabricated a parent effect instance");

    std::cout << "V560_ENVIRONMENTAL_EFFECT_POSE_HISTORY absoluteParentTick=1 wrap=1 PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V560 environmental effect pose history test failed: " << error.what() << '\n';
    return 1;
}
