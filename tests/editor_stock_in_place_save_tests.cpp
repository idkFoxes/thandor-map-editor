#include "thandor/binary.hpp"
#include "thandor/catalog.hpp"
#include "thandor/field.hpp"
#include "thandor/map_document.hpp"
#include "thandor/pck.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static std::vector<std::byte> make_minimal_level() {
    std::vector<std::byte> bytes(0x340U);
    thandor::write_u32(bytes, 0x00U, thandor::fourcc("lev"));
    thandor::write_u32(bytes, 0x04U, static_cast<uint32_t>(bytes.size()));
    thandor::write_u32(bytes, 0xD8U, 0U);
    thandor::write_u32(bytes, 0xDCU, static_cast<uint32_t>(bytes.size()));
    for (std::size_t index = 0U; index < 10U; ++index) {
        thandor::write_u32(bytes, 0xB0U + index * 4U,
                           static_cast<uint32_t>(0x318U + index * 4U));
    }
    return bytes;
}

int main() try {
    const auto root = fs::temp_directory_path() / "thandor_v557_stock_save";
    fs::remove_all(root);
    fs::create_directories(root);
    std::ofstream(root / "game.exe", std::ios::binary).put('\0');

    auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    const auto level_bytes = make_minimal_level();
    const auto field_bytes = field.serialize();
    const std::vector<std::byte> untouched{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    std::vector<thandor::PckWriteEntry> entries{
        {"level\\stock.lev", level_bytes, thandor::fourcc("lev")},
        {"level\\stock.fld", field_bytes, thandor::fourcc("fld")},
        {"misc\\keep.dat", untouched, thandor::fourcc("dat")},
    };
    const auto pck_path = root / "LEVEL.PCK";
    thandor::write_stored_pck(pck_path, entries);
    const auto original_archive = thandor::read_file(pck_path);

    auto catalog = thandor::AssetCatalog::scan_game_root(root);
    require(catalog.map_groups().size() == 1U, "synthetic stock map was not discovered");
    const auto& group = catalog.map_groups().front();
    require(group.level_path == "level/stock.lev", "synthetic stock LEV path changed");

    auto document = thandor::MapDocument::open(catalog, group);
    document.field().paint_material(10.0F, 10.0F, thandor::Brush{1.0F, 1.0F}, 3U);
    document.mark_dirty();

    // Editor-only preview metadata deliberately differs from a fresh map. Stock in-place save must
    // still emit exactly the authored LEV/FLD bytes and never project this runtime/preview state into
    // the map file.
    document.faction_technology_mask(1U)[0] = 0xA5A5A5A5U;
    if (document.placement_tags().empty()) {
        document.placement_tags().push_back({0U, thandor::PlacementRole::Generic,
                                             "runtime-only sentinel", 0U});
    }
    const auto expected_level = document.level().serialize();
    const auto expected_field = document.field().serialize();

    const auto result = document.save_stock_map_in_place(catalog, group, true);
    require(result.updated_archives.size() == 1U, "stock save did not update exactly one backing PCK");
    require(result.replaced_entries == 2U, "stock save did not replace the LEV and FLD entries");
    require(result.inserted_entries == 0U, "stock save unexpectedly inserted an entry");
    require(result.backup_archives.size() == 1U && fs::is_regular_file(result.backup_archives.front()),
            "stock save did not create its safety backup");
    require(thandor::read_file(result.backup_archives.front()) == original_archive,
            "stock save backup is not the pre-save PCK");

    const auto saved = thandor::PckArchive::open(pck_path);
    require(saved.extract("level/stock.lev") == expected_level,
            "stock save LEV differs from authored document bytes");
    require(saved.extract("level/stock.fld") == expected_field,
            "stock save FLD differs from authored document bytes");
    require(saved.extract("misc/keep.dat") == untouched,
            "stock save changed an unrelated PCK entry");
    require(saved.find("editor/project.txt") == nullptr,
            "stock save leaked project/simulation metadata into the stock PCK");

    fs::remove_all(root);
    std::cout << "V557_STOCK_IN_PLACE_SAVE PASS backup=yes unrelated-preserved=yes simulation-metadata=excluded\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V557_STOCK_IN_PLACE_SAVE FAIL: " << error.what() << '\n';
    return 1;
}
