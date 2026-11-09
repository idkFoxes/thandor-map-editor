#include "thandor/catalog.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
std::array<uint32_t,3> rgb(uint32_t packed) {
    return {(packed >> 16U) & 0xFFU, (packed >> 8U) & 0xFFU, packed & 0xFFU};
}
}

int main() try {
    using namespace thandor;
    const std::array<const char*,9> expected{{
        "Neutral", "Blue", "Lime / olive", "Red", "Teal green", "Yellow / gold",
        "Brown", "Purple", "Gray (special)"}};
    for (uint32_t family=0; family<expected.size(); ++family)
        require(std::string(graphics_family_color_name(family)) == expected[family],
                "graphics-family semantic label drifted");

    for (uint32_t family=1; family<=7; ++family)
        require(graphics_family_is_standard_player_color(family),
                "normal seven-entry retail player colour lost standard status");
    require(!graphics_family_is_standard_player_color(0U), "neutral became a player colour");
    require(!graphics_family_is_standard_player_color(8U), "special gray became a normal eighth player colour");

    const auto lime=rgb(graphics_family_representative_rgb24(2U));
    const auto teal=rgb(graphics_family_representative_rgb24(4U));
    const auto yellow=rgb(graphics_family_representative_rgb24(5U));
    const auto gray=rgb(graphics_family_representative_rgb24(8U));
    require(lime[1] > lime[0] && lime[0] > lime[2], "army2 is no longer lime/olive-biased");
    require(teal[1] > teal[0] && teal[1] > teal[2] && teal[0] < teal[1],
            "army4 is no longer teal/green-biased");
    require(yellow[0] > yellow[1] && yellow[1] > yellow[2],
            "army5 is no longer yellow/gold-biased");
    const uint32_t gray_min=std::min({gray[0],gray[1],gray[2]});
    const uint32_t gray_max=std::max({gray[0],gray[1],gray[2]});
    require(gray_max-gray_min <= 4U, "army8 representative is no longer neutral gray");

    // Preserve V545's machine-backed selector semantics. CP13 fixes family identity only.
    require(default_faction_graphics_family(2U)==2U, "zero-offset family2 fallback drifted");
    require(default_faction_graphics_family(5U)==5U, "zero-offset yellow family fallback drifted");

    std::cout << "V574_CP13_FACTION_FAMILY_COLOR PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V574_CP13_FACTION_FAMILY_COLOR FAIL: " << error.what() << '\n';
    return 1;
}
