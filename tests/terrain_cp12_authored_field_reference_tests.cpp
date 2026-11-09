#include "thandor/binary.hpp"
#include "thandor/catalog.hpp"
#include "thandor/field.hpp"
#include "thandor/map_document.hpp"
#include "thandor/pck.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static void write_utf16le_ascii(std::vector<std::byte>& bytes, std::size_t offset,
                                std::size_t capacity, std::string_view text) {
    require((text.size() + 1U) * 2U <= capacity, "synthetic LEV path exceeds fixture slot");
    std::fill(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
              bytes.begin() + static_cast<std::ptrdiff_t>(offset + capacity), std::byte{0});
    for (std::size_t index = 0; index < text.size(); ++index) {
        bytes[offset + index * 2U] = std::byte{static_cast<unsigned char>(text[index])};
        bytes[offset + index * 2U + 1U] = std::byte{0};
    }
}

static std::vector<std::byte> make_level(std::string_view authored_level_base) {
    constexpr std::size_t kFixedSize = 0x700U;
    constexpr std::size_t kPathBase = 0x380U;
    constexpr std::size_t kPathStride = 0x40U;
    std::vector<std::byte> bytes(kFixedSize);
    thandor::write_u32(bytes, 0x00U, thandor::fourcc("lev"));
    thandor::write_u32(bytes, 0x04U, static_cast<uint32_t>(bytes.size()));
    thandor::write_u32(bytes, 0xD8U, 0U);
    thandor::write_u32(bytes, 0xDCU, static_cast<uint32_t>(bytes.size()));
    for (std::size_t index = 0; index < 10U; ++index) {
        thandor::write_u32(bytes, 0xB0U + index * 4U,
                           static_cast<uint32_t>(kPathBase + index * kPathStride));
    }
    write_utf16le_ascii(bytes, kPathBase, kPathStride, authored_level_base);
    return bytes;
}

int main() try {
    const auto root = fs::temp_directory_path() / "thandor_cp12_authored_field_reference";
    fs::remove_all(root);
    fs::create_directories(root);
    std::ofstream(root / "game.exe", std::ios::binary).put('\0');

    const auto level = make_level("level/tutorial/t01_tut");
    auto shared_field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    auto decoy_field = thandor::FieldGrid::create(17U, 17U, 0, 0U);
    const auto decoy_bytes = decoy_field.serialize();
    std::vector<thandor::PckWriteEntry> entries{
        {"level\\tutorial\\t02_tut.lev", level, thandor::fourcc("lev")},
        {"level\\tutorial\\t01_tut.fld", shared_field.serialize(), thandor::fourcc("fld")},
        {"level\\tutorial\\t02_tut.fld", decoy_bytes, thandor::fourcc("fld")},
    };
    thandor::write_stored_pck(root / "LEVEL.PCK", entries);

    auto catalog = thandor::AssetCatalog::scan_game_root(root);
    require(catalog.map_groups().size() == 1U, "synthetic tutorial map was not discovered");
    const auto& group = catalog.map_groups().front();
    require(group.level_path == "level/tutorial/t02_tut.lev", "LEV identity changed");
    require(group.field_path == "level/tutorial/t01_tut.fld",
            "catalog ignored the LEV-authored shared FLD reference");

    auto document = thandor::MapDocument::open(catalog, group);
    require(document.field().width() == 59U && document.field().height() == 59U,
            "MapDocument opened the filename-derived decoy FLD instead of the authored FLD");
    document.field().paint_material(12.0F, 12.0F, thandor::Brush{1.0F, 1.0F}, 3U);
    document.mark_dirty();
    const auto expected_shared = document.field().serialize();
    const auto result = document.save_stock_map_in_place(catalog, group, false);
    require(result.replaced_entries == 2U, "stock save did not replace LEV plus authored shared FLD");

    const auto saved = thandor::PckArchive::open(root / "LEVEL.PCK");
    require(saved.extract("level/tutorial/t01_tut.fld") == expected_shared,
            "stock save did not update the LEV-authored shared FLD");
    require(saved.extract("level/tutorial/t02_tut.fld") == decoy_bytes,
            "stock save incorrectly overwrote the filename-derived decoy FLD");

    fs::remove_all(root);
    std::cout << "CP12_AUTHORED_FIELD_REFERENCE PASS shared-field-open=yes shared-field-save=yes\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "CP12_AUTHORED_FIELD_REFERENCE FAIL: " << error.what() << '\n';
    return 1;
}
