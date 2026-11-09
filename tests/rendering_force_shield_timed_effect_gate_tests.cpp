#include "thandor/simulation_render_policy.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message){
    if(!value)throw std::runtime_error(message);
}
}

int main() try {
    using thandor::timed_effect_preview_enabled;
    using thandor::timed_effect_requires_building_animation;

    // ARM 320 / MDL 311 / runtime class 4 is a one-node Special Research Lab whose
    // MDL +0x174 periodic EFF is independent of authored building-subnode animation.
    require(!timed_effect_requires_building_animation(true,false),
            "static building timed effect was incorrectly coupled to building animation");
    require(timed_effect_preview_enabled(true,false,true,false,false),
            "Special Research Lab timed effect is hidden when building animation is off");

    // ARM 300 Headquarters owns authored animated subnodes.  Preserve the G368 behavior:
    // with building animation disabled its owned EFF 131 exhaust is suppressed.
    require(timed_effect_requires_building_animation(true,true),
            "animated building timed effect lost its ownership coupling");
    require(!timed_effect_preview_enabled(true,true,true,false,false),
            "Headquarters effect escaped the building-animation gate");
    require(timed_effect_preview_enabled(true,true,true,true,false),
            "Headquarters effect did not resume with building animation");

    // Simulation/Lab runtime previews intentionally override the static preview gate.
    require(timed_effect_preview_enabled(true,true,true,false,true),
            "runtime preview failed to service an animated building effect");

    // The master timed-effect switch remains authoritative for both paths.
    require(!timed_effect_preview_enabled(true,false,false,true,true),
            "timed effect survived the master effect toggle");
    require(!timed_effect_preview_enabled(false,false,false,true,true),
            "non-building timed effect survived the master effect toggle");

    std::cout << "V567_FORCE_SHIELD_TIMED_EFFECT_GATE PASS\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr << "V567_FORCE_SHIELD_TIMED_EFFECT_GATE FAIL: " << error.what() << '\n';
    return 1;
}
