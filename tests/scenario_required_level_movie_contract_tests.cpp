#include "thandor/catalog.hpp"
#include "thandor/field.hpp"
#include "thandor/level.hpp"
#include "thandor/pck.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

using namespace thandor;

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void write_utf16_fixed(std::span<std::byte> bytes, std::size_t offset,
                       std::size_t capacity, std::string_view value) {
    if (value.size() * 2U + 2U > capacity) throw std::runtime_error("fixture path overflow");
    std::fill(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
              bytes.begin() + static_cast<std::ptrdiff_t>(offset + capacity), std::byte{});
    for (std::size_t index = 0; index < value.size(); ++index) {
        write_u16(bytes, offset + index * 2U,
                  static_cast<uint16_t>(static_cast<unsigned char>(value[index])));
    }
}

std::vector<std::byte> make_level_fixture(std::string_view movie_base) {
    constexpr std::size_t fixed_size = 0x1540U;
    constexpr std::array<std::size_t, 10> fields{
        0xB0U, 0xB4U, 0xB8U, 0xBCU, 0xC0U, 0xC4U, 0xC8U, 0xCCU, 0xD0U, 0xD4U};
    constexpr std::array<uint32_t, 10> offsets{
        0x12C0U, 0x1300U, 0x1340U, 0x1380U, 0x13C0U,
        0x1400U, 0x1440U, 0x1500U, 0x1480U, 0x14C0U};
    constexpr std::array<std::string_view, 10> paths{
        "level/source", "gfx/boden/soil", "gfx/texturen/water", "gfx/texturen/sky",
        "gfx/mdl/army", "gfx/texturen/shot", "gfx/texturen/effect", "",
        "sound/sound??", "engine/tech"};

    std::vector<std::byte> bytes(fixed_size);
    write_u32(bytes, 0x00U, 0x0076656CU);
    write_u32(bytes, 0x04U, static_cast<uint32_t>(fixed_size));
    write_u32(bytes, 0x0CU, 0x00070001U);
    write_u32(bytes, 0xD8U, 0U);
    write_u32(bytes, 0xDCU, static_cast<uint32_t>(fixed_size));
    for (std::size_t index = 0; index < fields.size(); ++index) {
        write_u32(bytes, fields[index], offsets[index]);
        const auto next = [&] {
            uint32_t result = static_cast<uint32_t>(fixed_size);
            for (const auto candidate : offsets) {
                if (candidate > offsets[index]) result = std::min(result, candidate);
            }
            return result;
        }();
        const auto value = index == 7U ? movie_base : paths[index];
        write_utf16_fixed(bytes, offsets[index], next - offsets[index], value);
    }
    return bytes;
}
} // namespace

int main(int argc, char** argv) try {
    const auto source = LevelAsset::parse(make_level_fixture("flm/erde"));
    check(source.paths().ending_movie == "flm/erde", "fixture movie base did not parse");
    const auto created = LevelAsset::from_template(source, "level/new_map", false);
    check(created.paths().ending_movie == "flm/erde",
          "new-map creation cleared the mandatory environment movie base");
    check(created.paths().level == "level/new_map", "new-map level path did not change");

    check(FieldGrid::is_game_compatible_shape(131U, 131U), "stock-proven 131x131 rejected");
    check(FieldGrid::is_game_compatible_shape(139U, 123U), "bounded 139x123 shape rejected");
    check(!FieldGrid::is_game_compatible_shape(139U, 139U),
          "unshipped 139x139 area was accepted as game-compatible");
    check(!FieldGrid::is_game_compatible_shape(200U, 200U), "oversized editor field accepted");

    std::size_t stock_maps = 0U;
    std::size_t stock_movies = 0U;
    uint64_t maximum_stock_area = 0U;
    if (argc >= 2) {
        const auto root = std::filesystem::path(argv[1]);
        const auto catalog = AssetCatalog::scan_game_root(root);
        std::filesystem::path movie_archive_path;
        for (const auto& item : std::filesystem::directory_iterator(root)) {
            if (item.is_regular_file() && lower_ascii(item.path().filename().string()) == "filme.pck") {
                movie_archive_path = item.path();
                break;
            }
        }
        check(!movie_archive_path.empty(), "stock FILME.PCK is missing");
        const auto movie_archive = PckArchive::open(movie_archive_path);
        for (const auto& group : catalog.map_groups()) {
            const auto level = LevelAsset::parse(catalog.load(group.level_path));
            ++stock_maps;
            check(!level.paths().ending_movie.empty(), "stock LEV lacks environment movie base");
            check(movie_archive.find(level.paths().ending_movie + ".flm") != nullptr,
                  "stock LEV environment movie does not resolve in FILME.PCK");
            ++stock_movies;
            if (const auto* field_record = catalog.find(group.field_path); field_record != nullptr) {
                const auto field = FieldGrid::parse(catalog.load(*field_record));
                const auto area = static_cast<uint64_t>(field.width()) * field.height();
                maximum_stock_area = std::max(maximum_stock_area, area);
                check(FieldGrid::is_game_compatible_dimension(field.width()) &&
                      FieldGrid::is_game_compatible_dimension(field.height()),
                      "stock FLD dimension leaves the 8*n+3 sequence");
            }
        }
        check(stock_maps != 0U, "stock map census found no maps");
        check(maximum_stock_area <= FieldGrid::kGameMaximumProvenArea,
              "stock map area exceeds the recovered compatibility envelope");
    }

    std::cout << "G332_REQUIRED_LEVEL_MOVIE_CONTRACT PASS movie=flm/erde "
                 "shape131=yes shape139x139=no stock_maps=" << stock_maps
              << " stock_movies=" << stock_movies
              << " max_stock_area=" << maximum_stock_area << "\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G332_REQUIRED_LEVEL_MOVIE_CONTRACT FAIL: " << error.what() << '\n';
    return 1;
}
