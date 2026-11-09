#include "thandor/frontend_player_texture.hpp"
#include "thandor/binary.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void put_u16(std::vector<std::byte>& bytes, std::size_t offset, uint16_t value) {
    bytes[offset + 0U] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

std::vector<std::byte> synthetic_pcx64() {
    std::vector<std::byte> bytes(128U, std::byte{0});
    bytes[0] = std::byte{0x0A};
    bytes[1] = std::byte{5};
    bytes[2] = std::byte{1};
    bytes[3] = std::byte{8};
    put_u16(bytes, 4U, 0U); put_u16(bytes, 6U, 0U);
    put_u16(bytes, 8U, 63U); put_u16(bytes, 10U, 63U);
    bytes[65] = std::byte{1};
    put_u16(bytes, 66U, 64U);

    // 4096 pixels, alternating palette indices 0,1,2. Encode runs where useful and literal bytes
    // otherwise; values >= 0xC0 are deliberately absent from this fixture.
    for (std::size_t y = 0; y < 64U; ++y) {
        for (std::size_t x = 0; x < 64U; ++x) {
            const uint8_t index = static_cast<uint8_t>((x + y) % 3U);
            bytes.push_back(static_cast<std::byte>(index));
        }
    }
    bytes.push_back(std::byte{0x0C});
    bytes.resize(bytes.size() + 256U * 3U, std::byte{0});
    const std::size_t palette = bytes.size() - 256U * 3U;
    // Entry 0: black -> retail alpha 0.
    bytes[palette + 3U] = std::byte{10};
    bytes[palette + 4U] = std::byte{20};
    bytes[palette + 5U] = std::byte{30};
    bytes[palette + 6U] = std::byte{255};
    bytes[palette + 7U] = std::byte{128};
    bytes[palette + 8U] = std::byte{1};
    return bytes;
}

} // namespace

int main() {
    try {
        std::vector<std::byte> settings(200U, std::byte{0});
        const std::u16string player = u"A.B*C?D/E\\F<G>H\"I:J|K";
        for (std::size_t i = 0; i < player.size() && i < 20U; ++i)
            put_u16(settings, 0x60U + i * 2U, static_cast<uint16_t>(player[i]));
        const auto loaded = thandor::persistent_settings_player_name(settings);
        require(loaded == player.substr(0U, 20U), "thandor.dat player name offset/width changed");
        require(thandor::retail_frontend_player_pcx_leaf(loaded) == u"ABCDEFGHIJ.pcx",
                "retail frontend PCX filename sanitization changed");

        const auto preview = thandor::decode_frontend_player_pcx64(synthetic_pcx64());
        require(preview.indexed_pixels[0] == 0U && preview.indexed_pixels[1] == 1U &&
                preview.indexed_pixels[2] == 2U, "PCX 64x64 indices decoded incorrectly");
        require(preview.palette_rgb[1][0] == 10U && preview.palette_rgb[1][1] == 20U &&
                preview.palette_rgb[1][2] == 30U, "PCX palette RGB triplet decoded incorrectly");
        const auto image = thandor::render_retail_frontend_player_texture(preview);
        require(image.width == 64U && image.height == 64U && image.pixels.size() == 16384U,
                "retail player texture dimensions changed");
        require(image.pixels[0] == 0U && image.pixels[1] == 0U && image.pixels[2] == 0U &&
                image.pixels[3] == 0U, "retail black player-palette entry must remain alpha zero");
        require(image.pixels[4] == 10U && image.pixels[5] == 20U && image.pixels[6] == 30U &&
                image.pixels[7] == 255U, "retail non-black player-palette entry must be opaque");
        require(image.pixels[8] == 255U && image.pixels[9] == 128U && image.pixels[10] == 1U &&
                image.pixels[11] == 255U, "retail player palette RGB order changed");

        std::cout << "v555 frontend player texture tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "v555 frontend player texture tests failed: " << error.what() << '\n';
        return 1;
    }
}
