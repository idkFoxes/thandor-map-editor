#include "thandor/catalog.hpp"
#include "thandor/map_document.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int main(int argc, char** argv) try {
    using namespace thandor;
    auto flat = FieldGrid::create(13, 17, float_to_q12(3.25F), 9);
    check(flat.width() == 13 && flat.height() == 17, "empty FLD dimensions changed");
    check(flat.at(0, 0).material() == 9 && flat.at(12, 16).material() == 9,
          "empty FLD initial material changed");
    flat.paint_material(6.0F, 8.0F, Brush{2.0F, 1.0F}, 23);
    check((flat.material_mask() & (1U << 23U)) != 0U, "texture paint did not update FLD mask");

    if (argc < 2) {
        std::cout << "G327_MAP_WORKFLOW PASS core-only; stock project roundtrip skipped\n";
        return 0;
    }

    const fs::path game_root = argv[1];
    auto catalog = AssetCatalog::scan_game_root(game_root);
    const auto group = std::find_if(catalog.map_groups().begin(), catalog.map_groups().end(),
        [](const MapAssetGroup& item) { return item.level_path == "level/asgard.lev"; });
    check(group != catalog.map_groups().end(), "asgard compatibility shell missing");

    auto document = MapDocument::create_empty(catalog, *group, "g325_test", "G325 Empty Test",
                                               67, 59, float_to_q12(2.0F), 4);
    check(document.display_name() == "G325 Empty Test", "display name changed");
    check(document.level().placements().empty(), "empty map copied stock placements");
    check(document.field().width() == 67 && document.field().height() == 59,
          "empty map dimensions changed");

    const auto source_level = LevelAsset::parse(catalog.load(group->level_path));
    std::string base = normalize_asset_path(source_level.paths().ground_textures);
    if (const auto dot = base.find_last_of('.'); dot != std::string::npos) base.resize(dot);
    document.import_terrain_material(catalog, 25, base + "a.gfx");
    check(document.custom_asset_count() == 27, "custom terrain family was not made complete");
    document.field().paint_material(10.0F, 10.0F, Brush{1.5F, 1.0F}, 25);

    LevelPlacement placement{};
    placement.army_asset_id = 50;
    placement.faction_index = 1;
    placement.set_render_position_q12(document.field().at(8, 8).world_x_q12,
                                      document.field().at(8, 8).world_y_q12);
    document.level().add_placement(placement);
    document.placement_tags().push_back({0, PlacementRole::PlayerStart, "North start"});
    placement.faction_index = 2;
    placement.set_render_position_q12(document.field().at(52, 48).world_x_q12,
                                      document.field().at(52, 48).world_y_q12);
    document.level().add_placement(placement);
    document.placement_tags().push_back({1, PlacementRole::PlayerStart, "South start"});

    const auto temp = fs::temp_directory_path() / "thandor_g325_map_workflow";
    fs::remove_all(temp);
    fs::create_directories(temp);
    for (const auto* leaf : {"game.exe", "LEVEL.PCK", "FILME.PCK", "PATCH00.PCK"}) {
        const auto input = game_root / leaf;
        check(fs::is_regular_file(input), "stock game workflow fixture missing");
        fs::copy_file(input, temp / leaf, fs::copy_options::overwrite_existing);
    }
    const auto project_path = temp / "g325_test.thmap";
    document.save_project(project_path);
    auto reopened = MapDocument::open_project(catalog, project_path);
    check(reopened.display_name() == "G325 Empty Test", "project lost display name");
    check(reopened.custom_asset_count() == 27, "project lost imported terrain assets");
    check(reopened.level().placements().size() == 2, "project lost placements");
    check(reopened.placement_tags().front().display_name == "North start",
          "project lost selected-object display name");
    check((reopened.field().material_mask() & (1U << 25U)) != 0U,
          "project lost painted second-biome layer");

    const auto patch_path = reopened.export_game_level_package(temp, 7, false);
    const auto patch = PckArchive::open(patch_path);
    check(patch_path.filename() == "level07.pck", "game package filename is not levelNN.pck");
    check(patch.find("level/g325_test.lev") != nullptr && patch.find("level/g325_test.fld") != nullptr,
          "game package lost LEV/FLD");
    check(patch.find("level/g325_test.str") != nullptr && patch.find("level/level07.dat") != nullptr,
          "game package lacks STR or scenario-catalog fragment");
    check(patch.find_exact("level\\g325_test.lev") != nullptr &&
          patch.find_exact("level\\g325_test.fld") != nullptr &&
          patch.find_exact("level\\g325_test.str") != nullptr &&
          patch.find_exact("level\\level07.dat") != nullptr,
          "game package persisted non-game archive path separators");
    check(patch.find_exact("level/level07.dat") == nullptr,
          "exact game lookup accepted a forward-slash catalog path");
    check(patch.entries().size() == 31, "game package did not include discovery files and all custom terrain files");
    const auto packaged_record = ScenarioCatalogRecord::parse(patch.extract("level/level07.dat"));
    const auto packaged_level = LevelAsset::parse(patch.extract("level/g325_test.lev"));
    const auto packaged_text = TextResourceAsset::parse(patch.extract("level/g325_test.str"));
    check(packaged_record.map_name() == "g325_test", "catalog identifier changed");
    check(packaged_record.alias_base() == packaged_level.compatibility_alias_base(),
          "catalog/LEV alias base mismatch");
    check(packaged_text.compatibility_display_name() == "G325 Empty Test", "STR display name changed");

    const auto loose = temp / "loose";
    reopened.export_loose(loose);
    auto loose_reopened = MapDocument::open_loose(catalog, loose / "g325_test.lev");
    check(loose_reopened.field().width() == 67 && loose_reopened.level().placements().size() == 2,
          "loose map loader changed gameplay data");
    check(loose_reopened.custom_asset_count() == 27,
          "loose map loader did not recover exported custom terrain assets");
    fs::remove_all(temp);

    std::cout << "G327_MAP_WORKFLOW PASS empty-map=yes display-name=yes load-save=yes custom-terrain=27 level-package-entries=31 exact-pck-paths=yes\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G327_MAP_WORKFLOW FAIL: " << error.what() << '\n';
    return 1;
}
