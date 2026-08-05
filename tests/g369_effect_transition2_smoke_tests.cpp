#include "thandor/effect.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    thandor::EffectDefinitionRecord smoke{};
    smoke.transition_kind = 2U;
    smoke.runtime_value_0c = 8U;
    smoke.frame_advance_threshold_q4 = 160U;
    smoke.movement_speed_q12 = 0x30;
    smoke.alpha_fade_in_ticks = 8U;
    smoke.alpha_fade_out_ticks = 80U;

    assert(thandor::effect_lifetime_ticks(smoke) == 80U);
    assert(thandor::effect_alpha_at_age(smoke, 0U) == 0U);
    assert(thandor::effect_alpha_at_age(smoke, 4U) == 127U);
    assert(thandor::effect_alpha_at_age(smoke, 7U) == 223U);
    assert(thandor::effect_alpha_at_age(smoke, 8U) == 229U);
    assert(thandor::effect_alpha_at_age(smoke, 40U) == 127U);
    assert(thandor::effect_alpha_at_age(smoke, 79U) == 3U);
    assert(thandor::effect_alpha_at_age(smoke, 80U) == 0U);

    // Zero random produces a vertical transition-2 direction. The stock Q12 speed is 48/4096.
    const auto vertical = thandor::effect_transition2_displacement_at_age(smoke, 10U, 0U);
    assert(std::abs(vertical[0]) < 1.0e-5F);
    assert(std::abs(vertical[1]) < 1.0e-5F);
    assert(std::abs(vertical[2] - 10.0F * 48.0F / 4096.0F) < 1.0e-4F);

    // A nonzero high random field lowers elevation; transition smoothing bends it upward over time.
    const uint32_t randomized = (0x800U << 20U) | 0x2000U;
    const auto early = thandor::effect_transition2_displacement_at_age(smoke, 1U, randomized);
    const auto later = thandor::effect_transition2_displacement_at_age(smoke, 40U, randomized);
    assert(std::hypot(early[0], early[1]) > 0.0F);
    assert(early[2] > 0.0F);
    assert(later[2] > early[2] * 20.0F);

    std::cout << "G369 transition-2 smoke motion and runtime alpha PASS\n";
    return 0;
}
