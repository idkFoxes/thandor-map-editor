#include "thandor/catalog.hpp"
#include "thandor/map_document.hpp"
#include "thandor/pck.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using namespace thandor;

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
std::string read_text(const fs::path& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("missing source file: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
void copy_fixture(const fs::path& source, const fs::path& target, const char* leaf) {
    check(fs::is_regular_file(source / leaf), "missing game fixture");
    fs::copy_file(source / leaf, target / leaf, fs::copy_options::overwrite_existing);
}
void add_start(MapDocument& document, uint32_t faction, int32_t coordinate_q12) {
    LevelPlacement placement{};
    placement.army_asset_id = 50U;
    placement.faction_index = faction;
    placement.set_render_position_q12(coordinate_q12, coordinate_q12);
    document.level().add_placement(placement);
    document.placement_tags().push_back({document.level().placements().size() - 1U,
                                         PlacementRole::PlayerStart,
                                         "Faction start"});
}
}

int main(int argc, char** argv) try {
    TerrainLighting lighting{};
    lighting.ramp_124 = 0xFF201008U;
    lighting.base_color = 0x00102030U;
    const auto ramp = lighting.build_game_ramp();
    check(ramp.front() == 0xFF303038U, "G337 ramp operand order changed");
    check(ramp.back() == 0xFF201008U, "G337 ramp terminal base changed");

    auto field = FieldGrid::create(3U, 3U, 0, 0U);
    field.at(0U, 0U).set_material(2U);
    field.at(1U, 0U).set_material(3U);
    field.at(0U, 1U).set_material(4U);
    field.at(1U, 1U).set_material(5U);
    const auto direct = field.build_vertices(lighting, 4.0F,
        TerrainMaterialPreviewMode::DirectCellMaterial);
    check(direct.size() == 24U, "direct terrain vertex count changed");
    for (std::size_t triangle = 0; triangle < direct.size(); triangle += 3U) {
        check(direct[triangle].material == direct[triangle + 1U].material &&
              direct[triangle].material == direct[triangle + 2U].material,
              "direct preview no longer keeps one owning FLD material per triangle");
        for (std::size_t vertex = triangle; vertex < triangle + 3U; ++vertex) {
            check(direct[vertex].blend_weights[0] == 1.0F &&
                  direct[vertex].blend_weights[1] == 0.0F,
                  "direct preview unexpectedly blends neighboring materials");
        }
    }

#ifdef THANDOR_SOURCE_DIR
    const fs::path source = THANDOR_SOURCE_DIR;
    const auto editor = read_text(source / "src/editor/editor_app.cpp");
    const auto level_hpp = read_text(source / "include/thandor/level.hpp");
    const auto map_cpp = read_text(source / "src/core/map_document.cpp");
    const auto terrain_shader = read_text(source / "shaders/terrain.frag");
    const auto model_shader = read_text(source / "shaders/model.frag");
    check(editor.find("Players & AI") != std::string::npos &&
          editor.find("Total active factions") != std::string::npos &&
          editor.find("Human player slots") != std::string::npos &&
          editor.find("Starting resources by active faction") != std::string::npos,
          "Players & AI controls are not visible in the editor");
    check(level_hpp.find("set_frontend_faction_counts") != std::string::npos &&
          level_hpp.find("set_embedded_player_count_range") != std::string::npos &&
          map_cpp.find("scenario.set_player_count_range(human_count, active_count)") != std::string::npos,
          "human/active faction ranges are being collapsed again");
    check(terrain_shader.find("gameByteModulate") != std::string::npos &&
          terrain_shader.find("linearToSrgb") != std::string::npos &&
          model_shader.find("game_byte_modulate") != std::string::npos,
          "original encoded-byte texture/light modulation path is missing");
#endif

    if (argc >= 2) {
        const fs::path stock = argv[1];
        auto catalog = AssetCatalog::scan_game_root(stock);
        bool found_asymmetric = false;
        std::size_t stock_map_count = 0U;
        std::size_t asymmetric_map_count = 0U;
        std::size_t range_mirror_mismatches = 0U;
        for (const auto& group : catalog.map_groups()) {
            if (group.level_path.empty()) continue;
            const auto* record = catalog.find(group.level_path);
            check(record != nullptr, "stock LEV missing from catalog");
            const auto level = LevelAsset::parse(catalog.load(*record));
            const uint32_t human = level.frontend_assignable_faction_count();
            const uint32_t active = level.frontend_active_faction_count();
            check(human >= 1U && human <= active && active <= 7U,
                  "stock LEV has invalid human/active faction counts");
            ++stock_map_count;
            if (human < active) ++asymmetric_map_count;
            if (level.embedded_minimum_player_count() != human ||
                level.embedded_maximum_player_count() != active) {
                ++range_mirror_mismatches;
            }
            if (group.level_path == "level/hansolo/s04_konflikt.lev") {
                check(human == 1U && active == 3U &&
                      level.embedded_minimum_player_count() == 1U &&
                      level.embedded_maximum_player_count() == 3U,
                      "stock 1-human/3-active campaign contract changed");
                found_asymmetric = true;
            }
        }
        check(stock_map_count == 56U, "stock map census count changed");
        check(asymmetric_map_count == 19U, "stock asymmetric human/active map count changed");
        check(range_mirror_mismatches == 0U, "stock embedded range no longer mirrors human/active counts");
        check(found_asymmetric, "stock asymmetric human/computer map not found");

        const auto shell = std::find_if(catalog.map_groups().begin(), catalog.map_groups().end(),
            [](const MapAssetGroup& group) { return group.level_path == "level/asgard.lev"; });
        check(shell != catalog.map_groups().end(), "asgard shell unavailable");
        auto map = MapDocument::create_empty(catalog, *shell, "g337ai", "G337 AI", 59U, 59U);
        add_start(map, 1U, 0x1000);
        add_start(map, 2U, 0x4000);
        map.set_player_ai_configuration({1U, 2U});
        const auto config = map.player_ai_configuration();
        check(config.human_assignable_factions == 1U && config.active_factions == 2U,
              "player/AI configuration did not round-trip");

        const fs::path root = fs::temp_directory_path() / "thandor_g337_players_ai";
        fs::remove_all(root);
        fs::create_directories(root);
        for (const char* leaf : {"game.exe", "LEVEL.PCK", "Level00.pck", "PATCH00.PCK",
                                 "FILME.PCK", "DATEN.PCK", "ENGINE.PCK", "GRAPHIK.PCK",
                                 "MODELLE.PCK", "SOUND.PCK"})
            copy_fixture(stock, root, leaf);
        const auto package_path = map.export_game_level_package(root, 1U, false);
        const auto package = PckArchive::open(package_path);
        const auto emitted = LevelAsset::parse(package.extract("level/g337ai.lev"));
        const auto scenario = ScenarioCatalogRecord::parse(package.extract("level/level01.dat"));
        check(emitted.frontend_assignable_faction_count() == 1U &&
              emitted.frontend_active_faction_count() == 2U &&
              emitted.embedded_minimum_player_count() == 1U &&
              emitted.embedded_maximum_player_count() == 2U,
              "export collapsed human and active faction counts");
        check(scenario.minimum_player_count() == 1U && scenario.maximum_player_count() == 2U,
              "scenario range does not match human..active faction range");
        fs::remove_all(root);
        std::cout << "G337_STOCK_FACTION_CENSUS maps=" << stock_map_count
                  << " asymmetric=" << asymmetric_map_count
                  << " mirrorMismatches=" << range_mirror_mismatches << '\n';
    }

    std::cout << "G337_PLAYERS_AI_RENDERER_CORRESPONDENCE PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G337 players/AI/renderer tests: FAIL: " << error.what() << '\n';
    return 1;
}
