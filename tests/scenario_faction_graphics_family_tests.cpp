#include "thandor/catalog.hpp"
#include "thandor/level.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() try {
    using namespace thandor;
    require(resolve_faction_graphics_family(0U, 0xFFFFFFFFU) == 0U,
            "neutral faction must remain army0");
    require(resolve_faction_graphics_family(1U, 0U) == 1U,
            "faction1 zero-offset selector drifted");
    require(resolve_faction_graphics_family(2U, 1U) == 3U,
            "faction2 +1 must resolve to army3/red");
    require(resolve_faction_graphics_family(3U, 1U) == 4U,
            "faction3 +1 must resolve to army4/teal green");
    require(resolve_faction_graphics_family(4U, static_cast<uint32_t>(-2)) == 2U,
            "faction4 -2 wrap must resolve to army2/lime-olive");
    require(std::string(graphics_family_color_name(1U)) == "Blue", "army1 label drifted");
    require(std::string(graphics_family_color_name(2U)) == "Lime / olive", "army2 label drifted");
    require(std::string(graphics_family_color_name(3U)) == "Red", "army3 label drifted");
    require(std::string(graphics_family_color_name(4U)) == "Teal green", "army4 label drifted");
    require(std::string(graphics_family_color_name(5U)) == "Yellow / gold", "army5 label drifted");
    require(std::string(graphics_family_color_name(8U)) == "Gray (special)", "army8 label drifted");
    std::cout << "V545_FACTION_GRAPHICS_FAMILY PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V545_FACTION_GRAPHICS_FAMILY FAIL: " << error.what() << "\n";
    return 1;
}
