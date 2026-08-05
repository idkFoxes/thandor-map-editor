#include "thandor/catalog.hpp"
#include "thandor/map_document.hpp"
#include "thandor/pck.hpp"
#include "thandor/scenario_catalog.hpp"

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

void copy_fixture(const fs::path& source, const fs::path& target, const char* leaf) {
    const auto input = source / leaf;
    check(fs::is_regular_file(input), "missing stock game fixture");
    fs::copy_file(input, target / leaf, fs::copy_options::overwrite_existing);
}

void add_faction_start(MapDocument& document, uint32_t faction, int32_t coordinate_q12) {
    LevelPlacement placement{};
    placement.army_asset_id = 50U;
    placement.faction_index = faction;
    placement.set_render_position_q12(coordinate_q12, coordinate_q12);
    document.level().add_placement(placement);
    document.placement_tags().push_back({document.level().placements().size() - 1U,
                                         PlacementRole::PlayerStart,
                                         "Faction " + std::to_string(faction) + " start"});
}
}

int main(int argc, char** argv) try {
    auto record = ScenarioCatalogRecord::minimal("g330", 0x5AU);
    record.set_player_count_range(2U, 2U);
    check(record.minimum_player_count() == 2U && record.maximum_player_count() == 2U,
          "scenario player-count fields did not round-trip");
    record.set_frontend_catalog_indices(6U, 2U, 4U);
    check(record.planet_theme_index() == 6U && record.difficulty_index() == 2U &&
          record.map_size_category_index() == 4U,
          "scenario frontend catalog selectors did not round-trip");

    if (argc < 2) {
        std::cout << "G330_PLAYER_FACTION_BEGIN_CONTRACT PASS metadata-api=yes stock-runtime=deferred\n";
        return 0;
    }

    const fs::path stock = argv[1];
    const fs::path root = fs::temp_directory_path() / "thandor_g330_player_faction_begin";
    fs::remove_all(root);
    fs::create_directories(root);
    for (const auto* leaf : {"game.exe", "LEVEL.PCK", "Level00.pck", "PATCH00.PCK"}) {
        copy_fixture(stock, root, leaf);
    }

    auto catalog = AssetCatalog::scan_game_root(stock);
    const auto shell = std::find_if(catalog.map_groups().begin(), catalog.map_groups().end(),
        [](const MapAssetGroup& group) { return group.level_path == "level/asgard.lev"; });
    check(shell != catalog.map_groups().end(), "asgard compatibility shell unavailable");

    auto map = MapDocument::create_empty(catalog, *shell, "g330_begin", "G330 Begin Map",
                                         21, 23, 0, 0);
    add_faction_start(map, 1U, 0x1000);
    add_faction_start(map, 2U, 0x3000);
    map.set_player_ai_configuration({2U, 2U});
    map.set_scenario_frontend_metadata({3U, 1U, 2U});
    map.set_scenario_detail_string(0U, "Two-player setup");
    map.set_scenario_detail_string(8U, "Faction one objective");
    const auto summary = map.playable_faction_summary();
    check(summary.exportable && summary.player_count == 2U && summary.faction_mask == 0x6U,
          "derived playable-faction summary changed");

    const auto installed = map.export_game_level_package(root, 1U, false);
    const auto package = PckArchive::open(installed);
    const auto emitted_record = ScenarioCatalogRecord::parse(package.extract("level/level01.dat"));
    const auto emitted_level = LevelAsset::parse(package.extract("level/g330_begin.lev"));
    const auto emitted_text = TextResourceAsset::parse(package.extract("level/g330_begin.str"));
    check(emitted_record.minimum_player_count() == 2U &&
          emitted_record.maximum_player_count() == 2U,
          "scenario +0x40/+0x48 were not synchronized to two players");
    check(emitted_record.planet_theme_index() == 3U && emitted_record.difficulty_index() == 1U &&
          emitted_record.map_size_category_index() == 2U,
          "scenario +0x50/+0x60/+0x80 selectors were not preserved");
    check(emitted_text.string(1U) == "Two-player setup" &&
          emitted_text.string(9U) == "Faction one objective",
          "scenario detail STR edits were not preserved");
    check(emitted_level.frontend_assignable_faction_count() == 2U &&
          emitted_level.frontend_active_faction_count() == 2U,
          "LEV +0x310/+0x314 were not synchronized to two factions");
    check(emitted_level.embedded_minimum_player_count() == 2U &&
          emitted_level.embedded_maximum_player_count() == 2U,
          "embedded LEV +0x140/+0x148 player-count digits were not synchronized");

    auto sparse = MapDocument::create_empty(catalog, *shell, "g330_sparse", "G330 Sparse",
                                            17, 17, 0, 0);
    add_faction_start(sparse, 1U, 0x1000);
    add_faction_start(sparse, 3U, 0x3000);
    check(!sparse.playable_faction_summary().exportable,
          "sparse faction IDs were accepted by the frontend count contract");
    bool sparse_rejected = false;
    try {
        (void)sparse.export_game_level_package(root, 2U, false);
    } catch (const FormatError&) {
        sparse_rejected = true;
    }
    check(sparse_rejected, "sparse faction IDs were not rejected during export");

    if (argc >= 3) {
        const auto submitted = PckArchive::open(argv[2]);
        const auto submitted_record = ScenarioCatalogRecord::parse(
            submitted.extract("level/level01.dat"));
        const auto submitted_level = LevelAsset::parse(submitted.extract("level/testj.lev"));
        const auto submitted_summary = [&]() {
            uint32_t highest = 0U;
            uint32_t mask = 0U;
            for (const auto& placement : submitted_level.placements()) {
                if (placement.faction_index == 0U || placement.faction_index > 7U) continue;
                highest = std::max(highest, placement.faction_index);
                mask |= 1U << placement.faction_index;
            }
            return std::pair{highest, mask};
        }();
        check(submitted_summary.first == 2U && submitted_summary.second == 0x6U,
              "submitted testj faction census changed");
        check(submitted_level.frontend_assignable_faction_count() == 4U &&
              submitted_level.frontend_active_faction_count() == 4U &&
              submitted_record.minimum_player_count() == 4U &&
              submitted_record.maximum_player_count() == 4U,
              "submitted G329 package no longer reproduces the four-player metadata mismatch");
    }

    if (argc >= 4) {
        const auto repaired = PckArchive::open(argv[3]);
        const auto repaired_record = ScenarioCatalogRecord::parse(
            repaired.extract("level/level01.dat"));
        const auto repaired_level = LevelAsset::parse(repaired.extract("level/testj.lev"));
        check(repaired_record.minimum_player_count() == 2U &&
              repaired_record.maximum_player_count() == 2U &&
              repaired_level.frontend_assignable_faction_count() == 2U &&
              repaired_level.frontend_active_faction_count() == 2U &&
              repaired_level.embedded_minimum_player_count() == 2U &&
              repaired_level.embedded_maximum_player_count() == 2U,
              "repaired G330 package does not synchronize all six player-count fields");
        const auto repaired_text = TextResourceAsset::parse(repaired.extract("level/testj.str"));
        check(repaired_text.string(1U).find("2 Spieler") != std::string::npos,
              "repaired G330 package still advertises four players in its summary text");
    }

    fs::remove_all(root);
    std::cout << "G330_PLAYER_FACTION_BEGIN_CONTRACT PASS placements=2 metadata=2 "
                 "scenario-digits=2 sparse-factions=rejected submitted-mismatch=4-vs-2\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G330_PLAYER_FACTION_BEGIN_CONTRACT FAIL: " << error.what() << '\n';
    return 1;
}
