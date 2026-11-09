#include "thandor/catalog.hpp"
#include "thandor/map_document.hpp"
#include "thandor/pck.hpp"

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

std::vector<std::byte> aligned_bytes(std::initializer_list<unsigned char> values) {
    std::vector<std::byte> data;
    for (const auto value : values) data.push_back(static_cast<std::byte>(value));
    while ((data.size() & 3U) != 0U) data.push_back(std::byte{0});
    return data;
}
}

int main(int argc, char** argv) try {
    const std::vector<PckWriteEntry> synthetic{
        {"level/level07.dat", aligned_bytes({1,2,3,4}), fourcc("dat")},
        {"level\\exact.lev", aligned_bytes({5,6,7,8}), fourcc("lev")},
        {"gfx/boden/edit_test_a.gfx", aligned_bytes({9,10,11,12}), fourcc("gfx")},
    };
    const auto bytes = build_stored_pck(synthetic);
    const auto archive = PckArchive::parse("memory.pck", bytes);
    check(read_u32(bytes, 8) == 1U && read_u32(bytes, 0x0C) == 0x00010000U,
          "new PCK header lacks stock version/format fields");
    check(archive.find_exact("level\\level07.dat") != nullptr,
          "writer did not persist the exact backslash scenario path");
    check(archive.find_exact("level/level07.dat") == nullptr,
          "exact game lookup incorrectly accepted a forward slash");
    check(archive.find_exact("gfx\\boden\\edit_test_a.gfx") != nullptr,
          "writer did not persist an exact backslash custom-asset path");
    for (const auto& entry : archive.entries()) {
        check(entry.name.find('/') == std::string::npos,
              "persisted PCK directory contains a forward slash");
        check(entry.name == canonical_pck_entry_path(entry.name),
              "persisted PCK directory path is not canonical");
    }
    bool duplicate_rejected = false;
    try {
        const std::vector<PckWriteEntry> duplicate{
            {"level/a.lev", aligned_bytes({1}), fourcc("lev")},
            {"level\\a.lev", aligned_bytes({2}), fourcc("lev")},
        };
        (void)build_stored_pck(duplicate);
    } catch (const FormatError&) {
        duplicate_rejected = true;
    }
    check(duplicate_rejected, "slash-normalized duplicate PCK path was not rejected");

    if (argc >= 2) {
        const fs::path source_root = argv[1];
        auto catalog = AssetCatalog::scan_game_root(source_root);
        const auto group = std::find_if(catalog.map_groups().begin(), catalog.map_groups().end(),
            [](const MapAssetGroup& item) { return item.level_path == "level/asgard.lev"; });
        check(group != catalog.map_groups().end(), "stock compatibility shell is unavailable");

        auto document = MapDocument::create_empty(catalog, *group, "g327_exact", "G327 Exact Paths",
                                                   67, 67, 0, 0);
        add_two_playable_factions(document);
        const fs::path temp = fs::temp_directory_path() / "thandor_g327_game_package_paths";
        fs::remove_all(temp);
        fs::create_directories(temp);
        const auto level_template = source_root / "LEVEL.PCK";
        const auto patch_template = source_root / "PATCH00.PCK";
        const auto game_template = source_root / "game.exe";
        const auto movie_template = source_root / "FILME.PCK";
        check(fs::exists(level_template) && fs::exists(patch_template) &&
              fs::exists(game_template) && fs::exists(movie_template),
              "stock game.exe/LEVEL.PCK/PATCH00.PCK/FILME.PCK fixtures are missing");
        fs::copy_file(game_template, temp / "game.exe");
        fs::copy_file(level_template, temp / "LEVEL.PCK");
        fs::copy_file(patch_template, temp / "PATCH00.PCK");
        fs::copy_file(movie_template, temp / "FILME.PCK");

        const auto installed = document.export_game_level_package(temp, 7, false);
        const auto level_archive = PckArchive::open(installed);
        check(installed.filename() == "level07.pck", "numbered package filename changed");
        check(level_archive.find_exact("level\\level07.dat") != nullptr,
              "numbered package lacks exact scenario fragment path");
        check(level_archive.find_exact("level\\g327_exact.lev") != nullptr &&
              level_archive.find_exact("level\\g327_exact.fld") != nullptr &&
              level_archive.find_exact("level\\g327_exact.str") != nullptr,
              "numbered package lacks exact LEV/FLD/STR paths");
        check(level_archive.find_exact("level/level07.dat") == nullptr,
              "numbered package exact lookup accepted the old broken slash path");
        check(read_u32(level_archive.header_bytes(), 8) == 1U &&
              read_u32(level_archive.header_bytes(), 0x0C) == 0x00010000U,
              "numbered package did not preserve stock header structure");

        const auto patch_before_bytes = read_file(temp / "PATCH00.PCK");
        bool map_chain_rejected = false;
        try {
            Patch00ExportOptions unsafe_options;
            unsafe_options.include_scenario_catalog = true;
            unsafe_options.include_level = true;
            unsafe_options.include_field = true;
            unsafe_options.include_text = true;
            unsafe_options.include_custom_assets = false;
            (void)document.update_patch00_overrides(temp, 7, unsafe_options, false);
        } catch (const FormatError& error) {
            map_chain_rejected = std::string(error.what()).find("map-chain overrides are disabled") !=
                                 std::string::npos;
        }
        check(map_chain_rejected,
              "PATCH00 accepted a map DAT/LEV/FLD/STR shadow chain");
        check(read_file(temp / "PATCH00.PCK") == patch_before_bytes,
              "rejected PATCH00 map-chain request changed the archive");
        check(!fs::exists(temp / "patch01.pck") && !fs::exists(temp / "PATCH01.PCK"),
              "PATCH00 rejection incorrectly created patch01.pck");

        auto second = MapDocument::create_empty(catalog, *group, "g327_second", "G327 Second",
                                                67, 67, 0, 0);
        add_two_playable_factions(second);
        second.export_game_level_package(temp, 7, false);
        const auto replaced_level = PckArchive::open(installed);
        check(replaced_level.find_exact("level\\g327_second.lev") != nullptr,
              "one-map numbered-package replacement did not install the new map");
        check(replaced_level.find("level/g327_exact.lev") == nullptr,
              "numbered package retained a second independent map");
        fs::remove_all(temp);
    }

    std::cout << "G327_GAME_PACKAGE_PATHS PASS exact-backslashes=yes patch00-map-shadow-rejected=yes "
                 "patch01-loaded=no one-map-per-numbered-package=yes\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G327_GAME_PACKAGE_PATHS FAIL: " << error.what() << '\n';
    return 1;
}
