#include "thandor/catalog.hpp"
#include "thandor/map_document.hpp"
#include "thandor/pck.hpp"
#include "thandor/scenario_catalog.hpp"
#include "thandor/text_resource.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;
using namespace thandor;

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void add_two_playable_factions(MapDocument& document) {
    for (uint32_t faction = 1U; faction <= 2U; ++faction) {
        LevelPlacement placement{};
        placement.army_asset_id = 50U;
        placement.faction_index = faction;
        placement.set_render_position_q12(static_cast<int32_t>(faction) * 0x1000,
                                          static_cast<int32_t>(faction) * 0x1000);
        document.level().add_placement(placement);
        document.placement_tags().push_back({document.level().placements().size() - 1U,
                                             PlacementRole::PlayerStart,
                                             "Faction " + std::to_string(faction) + " start"});
    }
}

void copy_fixture(const fs::path& source, const fs::path& target, const char* leaf) {
    const auto input = source / leaf;
    check(fs::is_regular_file(input), "missing stock game fixture");
    fs::copy_file(input, target / leaf, fs::copy_options::overwrite_existing);
}
}

int main(int argc, char** argv) try {
    check(game_custom_alias_for_slot(0) == 0x5AU, "slot 00 alias changed");
    check(game_custom_alias_for_slot(1) == 0x5BU, "slot 01 alias changed");
    check(game_custom_alias_for_slot(99) == 0xBDU, "slot 99 alias changed");

    if (argc < 2) {
        std::cout << "G329_CUSTOM_ALIAS_FALLBACK PASS aliases=0x5A..0xBD stock-runtime=deferred\n";
        return 0;
    }

    const fs::path stock = argv[1];
    const fs::path root = fs::temp_directory_path() / "thandor_g329_custom_alias_fallback";
    fs::remove_all(root);
    fs::create_directories(root);
    for (const auto* leaf : {"game.exe", "LEVEL.PCK", "Level00.pck", "PATCH00.PCK", "FILME.PCK"}) {
        copy_fixture(stock, root, leaf);
    }

    const auto executable = read_file(root / "game.exe");
    check(executable.size() > 0x1CD10U, "game.exe fixture is too small");
    check(std::to_integer<unsigned>(executable[0x1B0EE]) == 0x83U &&
          std::to_integer<unsigned>(executable[0x1B0EF]) == 0xC8U &&
          std::to_integer<unsigned>(executable[0x1B0F0]) == 0xFFU,
          "override-table -1 initializer bytes changed");
    check(std::to_integer<unsigned>(executable[0x1CD04]) == 0x33U &&
          std::to_integer<unsigned>(executable[0x1CD05]) == 0xC0U &&
          std::to_integer<unsigned>(executable[0x1CD0B]) == 0xF2U &&
          std::to_integer<unsigned>(executable[0x1CD0C]) == 0xAFU,
          "override-register zero scan bytes changed");

    auto catalog = AssetCatalog::scan_game_root(stock);
    const auto shell = std::find_if(catalog.map_groups().begin(), catalog.map_groups().end(),
        [](const MapAssetGroup& group) { return group.level_path == "level/asgard.lev"; });
    check(shell != catalog.map_groups().end(), "asgard compatibility shell unavailable");
    auto map = MapDocument::create_empty(catalog, *shell, "g329_alias", "G329 Alias Map", 59, 59, 0, 0);
    add_two_playable_factions(map);
    const auto installed = map.export_game_level_package(root, 1, false);

    const auto level_package = PckArchive::open(installed);
    const auto record = ScenarioCatalogRecord::parse(level_package.extract("level/level01.dat"));
    const auto level = LevelAsset::parse(level_package.extract("level/g329_alias.lev"));
    const auto package_text = TextResourceAsset::parse(level_package.extract("level/g329_alias.str"));
    check(record.alias_base() == 0x5BU, "scenario +0x70 did not use slot-01 alias");
    check(level.compatibility_alias_base() == 0x5BU, "LEV +0x170 did not use slot-01 alias");

    const auto patch = PckArchive::open(root / "PATCH00.PCK");
    const auto page22 = TextResourceAsset::parse(patch.extract("texte/level.str"));
    const auto page23 = TextResourceAsset::parse(patch.extract("texte/inhalt.str"));
    const uint32_t title_index = 0x5BU + 0x30U;
    const uint32_t content_base = 0x5BU * 0x10U + 0x10U;
    check(page22.string(title_index) == package_text.string(0),
          "global compact title alias was not synchronized");
    for (uint32_t index = 1; index < 16; ++index) {
        check(page23.string(content_base + index - 1U) == package_text.string(index),
              "global extended content alias was not synchronized");
    }

    // The G326-G328 value 0x101 crosses the compact ID into page 0x23 and produces extended
    // page-0x23 index 0x1020, beyond the stock 0x510-string page. This is the submitted crash class.
    const uint32_t legacy_alias = 0x101U;
    const uint32_t legacy_compact = legacy_alias + 0x2230U;
    const uint32_t legacy_extended_index = legacy_alias * 0x10U + 0x10U;
    check((legacy_compact >> 8U) == 0x23U, "legacy alias no longer crosses compact page");
    const auto stock_patch = PckArchive::open(stock / "PATCH00.PCK");
    const auto stock_page23 = TextResourceAsset::parse(stock_patch.extract("texte/inhalt.str"));
    check(stock_page23.string_count() == 0x510U, "stock page-0x23 count changed");
    check(legacy_extended_index >= stock_page23.string_count(),
          "legacy extended alias unexpectedly resolves inside stock page 0x23");

    if (argc >= 4) {
        const auto submitted_level = PckArchive::open(argv[2]);
        const auto submitted_patch = PckArchive::open(argv[3]);
        const auto submitted_record = ScenarioCatalogRecord::parse(
            submitted_level.extract("level/level01.dat"));
        const auto submitted_lev = LevelAsset::parse(submitted_level.extract("level/testj.lev"));
        check(submitted_record.alias_base() == 0x101U &&
              submitted_lev.compatibility_alias_base() == 0x101U,
              "submitted package no longer reproduces the 0x101 alias failure");
        check(submitted_patch.find_exact("texte\\level.str") != nullptr &&
              submitted_patch.find_exact("texte\\inhalt.str") != nullptr,
              "submitted PATCH00 lacks the stock frontend text pages");
    }

    fs::remove_all(root);
    std::cout << "G329_CUSTOM_ALIAS_FALLBACK PASS alias=0x5B compact-page=0x22 "
                 "extended-page=0x23 submitted-crash=0x101\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G329_CUSTOM_ALIAS_FALLBACK FAIL: " << error.what() << '\n';
    return 1;
}
