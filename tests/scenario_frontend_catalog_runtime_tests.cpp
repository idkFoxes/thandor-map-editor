#include "thandor/catalog.hpp"
#include "thandor/map_document.hpp"
#include "thandor/pck.hpp"
#include "thandor/scenario_catalog.hpp"
#include "thandor/text_resource.hpp"

#include <algorithm>
#include <cstring>
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
    // The ordinary CTest path remains self-contained. The release validator invokes the same
    // executable with a stock game root to exercise startup mount precedence and repair.
    auto minimal = TextResourceAsset::minimal_level_compatibility("Synthetic G328");
    check(minimal.has_level_list_mutation_prefix(),
          "synthetic compatibility page lacks the mutable leading word");
    check(minimal.compatibility_display_name() == "Synthetic G328",
          "synthetic compatibility display name changed");
    const auto minimal_bytes = minimal.serialize();
    const std::vector<PckWriteEntry> synthetic_entries{{
        "level/synthetic.str", minimal_bytes, 0U,
    }};
    const auto synthetic_pck = PckArchive::parse(
        "synthetic.pck", build_stored_pck(synthetic_entries));
    const auto* synthetic_entry = synthetic_pck.find_exact("level\\synthetic.str");
    check(synthetic_entry != nullptr, "synthetic exact-path entry missing");
    check(synthetic_entry->type_tag == read_u32(minimal_bytes, 0U),
          "synthetic entry type tag is not payload dword zero");
    if (argc < 2) {
        std::cout << "G328_FRONTEND_CATALOG_RUNTIME PASS fixture=synthetic "
                     "mutable-prefix=yes payload-type-tag=yes stock-runtime=deferred\n";
        return 0;
    }
    const fs::path stock = argv[1];
    const fs::path root = fs::temp_directory_path() / "thandor_g328_frontend_catalog_runtime";
    fs::remove_all(root);
    fs::create_directories(root);
    for (const auto* leaf : {"game.exe", "LEVEL.PCK", "Level00.pck", "PATCH00.PCK", "FILME.PCK"}) {
        copy_fixture(stock, root, leaf);
    }

    auto catalog = AssetCatalog::scan_game_root(stock);
    const auto shell = std::find_if(catalog.map_groups().begin(), catalog.map_groups().end(),
        [](const MapAssetGroup& group) { return group.level_path == "level/asgard.lev"; });
    check(shell != catalog.map_groups().end(), "asgard compatibility shell unavailable");

    auto map = MapDocument::create_empty(catalog, *shell, "g328_live", "G328 Live Map", 59, 59, 0, 0);
    add_two_playable_factions(map);
    const auto installed = map.export_game_level_package(root, 7, false);
    auto level = PckArchive::open(installed);
    const auto* dat = level.find_exact("level\\level07.dat");
    const auto* text_entry = level.find_exact("level\\g328_live.str");
    check(dat != nullptr && text_entry != nullptr, "initial complete numbered package missing");
    const auto dat_payload = level.extract(*dat);
    check(dat->type_tag == read_u32(dat_payload, 0),
          "scenario fragment type tag is not payload dword zero");
    auto text = TextResourceAsset::parse(level.extract(*text_entry));
    check(text.has_level_list_mutation_prefix(),
          "level-list string lacks mutable leading word");
    check(text.compatibility_display_name() == "G328 Live Map",
          "compatibility display name changed");

    // Reproduce the first-mounted PATCH00 failure class directly. Current editor policy rejects
    // writing map chains into PATCH00, so the fixture injects old paths at the archive layer.
    auto patch = PckArchive::open(root / "PATCH00.PCK");
    std::vector<PckWriteEntry> stale_patch_entries;
    for (const auto& entry : patch.entries())
        stale_patch_entries.push_back({entry.name, patch.extract(entry), entry.type_tag});
    stale_patch_entries.push_back({"level/level07.dat", level.extract("level/level07.dat"), 0U});
    stale_patch_entries.push_back({"level/g328_live.lev", level.extract("level/g328_live.lev"), 0U});
    stale_patch_entries.push_back({"level/g328_live.fld", level.extract("level/g328_live.fld"), 0U});
    stale_patch_entries.push_back({"level/g328_live.str", level.extract("level/g328_live.str"), 0U});
    stale_patch_entries.push_back({"level/legacy_jmap.lev", level.extract("level/g328_live.lev"), 0U});
    stale_patch_entries.push_back({"level/legacy_jmap.fld", level.extract("level/g328_live.fld"), 0U});
    stale_patch_entries.push_back({"level/legacy_jmap.str", level.extract("level/g328_live.str"), 0U});
    write_stored_pck(root / "PATCH00.PCK", stale_patch_entries, patch.header_bytes());

    map.set_display_name("G328 Repaired Map");
    map.export_game_level_package(root, 7, false);
    patch = PckArchive::open(root / "PATCH00.PCK");
    check(patch.find_exact("level\\level07.dat") == nullptr &&
          patch.find_exact("level\\g328_live.lev") == nullptr &&
          patch.find_exact("level\\g328_live.fld") == nullptr &&
          patch.find_exact("level\\g328_live.str") == nullptr &&
          patch.find_exact("level\\legacy_jmap.lev") == nullptr &&
          patch.find_exact("level\\legacy_jmap.fld") == nullptr &&
          patch.find_exact("level\\legacy_jmap.str") == nullptr,
          "normal install left a first-mounted PATCH00 map shadow");
    level = PckArchive::open(root / "level07.pck");
    text = TextResourceAsset::parse(level.extract("level/g328_live.str"));
    check(text.compatibility_display_name() == "G328 Repaired Map",
          "repaired numbered package has stale display text");

    bool patch_map_chain_rejected = false;
    try {
        Patch00ExportOptions full;
        full.include_scenario_catalog = true;
        full.include_level = true;
        full.include_field = true;
        full.include_text = true;
        full.include_custom_assets = false;
        map.update_patch00_overrides(root, 7, full, false);
    } catch (const FormatError&) {
        patch_map_chain_rejected = true;
    }
    check(patch_map_chain_rejected, "PATCH00 map-chain policy did not reject a full override");

    // A higher-numbered package is mounted before level07.pck. Reusing the same path or alias
    // must fail and restore both the numbered package and PATCH00 exactly.
    fs::copy_file(root / "level07.pck", root / "level99.pck",
                  fs::copy_options::overwrite_existing);
    const auto level_before_conflict = read_file(root / "level07.pck");
    const auto patch_before_conflict = read_file(root / "PATCH00.PCK");
    map.set_display_name("G328 Must Roll Back");
    bool conflict_rejected = false;
    try {
        (void)map.export_game_level_package(root, 7, false);
    } catch (const FormatError&) {
        conflict_rejected = true;
    }
    check(conflict_rejected, "higher-numbered first-mounted shadow was not rejected");
    check(read_file(root / "level07.pck") == level_before_conflict,
          "failed install did not restore the numbered package");
    check(read_file(root / "PATCH00.PCK") == patch_before_conflict,
          "failed install did not restore PATCH00");
    fs::remove(root / "level99.pck");

    fs::remove_all(root);
    std::cout << "G328_FRONTEND_CATALOG_RUNTIME PASS patch-shadow-repair=yes "
                 "four-resolves=yes mutable-prefix=yes payload-type-tag=yes partial-patch=no "
                 "rollback=yes\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G328_FRONTEND_CATALOG_RUNTIME FAIL: " << error.what() << '\n';
    return 1;
}
