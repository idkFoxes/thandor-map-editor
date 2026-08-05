#include "thandor/scenario_catalog.hpp"
#include "thandor/text_resource.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

using namespace thandor;

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() try {
    auto base = ScenarioCatalogRecord::minimal("first", 0x5AU);
    auto replacement = ScenarioCatalogRecord::minimal("first", 0x5BU);
    auto appended = ScenarioCatalogRecord::minimal("second", 0x5CU);
    std::vector<ScenarioCatalogRecord> records{base};
    const std::vector<ScenarioCatalogRecord> incoming{replacement, appended};
    merge_scenario_catalog_records(records, incoming);
    check(records.size() == 2U, "scenario merge did not replace then append");
    check(records.front().identifier() == "first.", "scenario identifier lacks game-required trailing dot");
    check(records.front().alias_base() == 0x5BU, "scenario replacement did not preserve the incoming record");
    check(choose_unused_scenario_alias_base(records) == 0x5AU,
          "unused scenario alias selection changed");

    auto text = TextResourceAsset::minimal_level_compatibility("Initial title");
    auto bytes = text.serialize();
    auto parsed = TextResourceAsset::parse(bytes);
    check(parsed.locale_count() == 1U, "generated STR locale count changed");
    check(parsed.compatibility_display_name() == "Initial title", "generated STR title changed");
    for (uint32_t index = 0; index < 16U; ++index) check(!parsed.string(index).empty(), "generated STR lacks a compatibility string");
    parsed.replace_string_zero_all_locales("Second title");
    auto reparsed = TextResourceAsset::parse(parsed.serialize());
    check(reparsed.compatibility_display_name() == "Second title", "STR title replacement did not round-trip");

    check(game_custom_alias_for_slot(0U) == 0x5AU && game_custom_alias_for_slot(99U) == 0xBDU,
          "slot-scoped custom alias domain changed");
    std::cout << "G329_GAME_LEVEL_PACKAGE_FORMATS PASS records=2 locales=1 strings=16 aliases=0x5A..0xBD\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G326_GAME_LEVEL_PACKAGE_FORMATS FAIL: " << error.what() << '\n';
    return 1;
}
