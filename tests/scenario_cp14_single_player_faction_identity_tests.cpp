#include "thandor/catalog.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    constexpr std::array<const char*, 9> names{{
        "Neutral", "Ares", "Thoraner", "Devoken", "Ilurer", "Albor",
        "Urdoner", "Alderaden", "Golrathen"}};
    constexpr std::array<uint32_t, 9> text_rgb{{
        0xB0B8C0U, 0x0000DDU, 0x00DD00U, 0xDD0000U, 0x00CCCCU,
        0xCC6600U, 0x552200U, 0x990099U, 0x333333U}};

    for (uint32_t family = 0; family < names.size(); ++family) {
        require(std::string(thandor::graphics_family_single_player_faction_name(family)) == names[family],
                "single-player faction name drifted");
        require(thandor::graphics_family_single_player_text_rgb24(family) == text_rgb[family],
                "single-player faction text color drifted");
    }

    // The Single Player label color is a frontend rich-text property, not a sampled
    // world texture color. Keep the domains separate so another army2/army5 UI alias
    // cannot silently return through the texture representative table.
    require(thandor::graphics_family_single_player_text_rgb24(2U) !=
                thandor::graphics_family_representative_rgb24(2U),
            "army2 frontend text color collapsed into texture representative color");
    require(thandor::graphics_family_single_player_text_rgb24(5U) !=
                thandor::graphics_family_representative_rgb24(5U),
            "army5 frontend text color collapsed into texture representative color");
    require(std::string(thandor::graphics_family_single_player_faction_name(99U)) ==
                "Custom/invalid faction",
            "invalid family faction label fallback drifted");

    std::cout << "V574_CP14_SINGLE_PLAYER_FACTION_IDENTITY PASS\n";
    return 0;
}
