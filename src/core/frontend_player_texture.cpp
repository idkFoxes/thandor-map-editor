#include "thandor/frontend_player_texture.hpp"

#include "thandor/binary.hpp"

#include <algorithm>
#include <limits>

namespace thandor {
namespace {

constexpr std::size_t kPcxHeaderSize = 128U;
constexpr std::size_t kPaletteBytes = 256U * 3U;
constexpr std::size_t kPaletteTrailerSize = 1U + kPaletteBytes;

bool retail_removed_player_filename_character(char16_t value) noexcept {
    switch (value) {
        case u'*': case u'.': case u'?': case u'/': case u'\\':
        case u'<': case u'>': case u'"': case u':': case u'|':
            return true;
        default:
            return false;
    }
}

uint8_t byte_at(std::span<const std::byte> data, std::size_t offset) {
    if (offset >= data.size()) throw FormatError("PCX payload is truncated");
    return std::to_integer<uint8_t>(data[offset]);
}

} // namespace

std::u16string persistent_settings_player_name(std::span<const std::byte> settings_image) {
    constexpr std::size_t kPlayerNameOffset = 0x60U;
    constexpr std::size_t kPlayerNameCodeUnits = 20U;
    if (settings_image.size() < kPlayerNameOffset + kPlayerNameCodeUnits * 2U) return {};
    std::u16string result;
    result.reserve(kPlayerNameCodeUnits);
    for (std::size_t index = 0; index < kPlayerNameCodeUnits; ++index) {
        const char16_t value = static_cast<char16_t>(
            read_u16(settings_image, kPlayerNameOffset + index * 2U));
        if (value == u'\0') break;
        result.push_back(value);
    }
    return result;
}

std::u16string retail_frontend_player_pcx_leaf(std::u16string_view player_name) {
    std::u16string result;
    result.reserve(player_name.size() + 4U);
    for (const char16_t value : player_name) {
        if (value == u'\0') break;
        if (!retail_removed_player_filename_character(value)) result.push_back(value);
    }
    result.append(u".pcx");
    return result;
}

FrontendPlayerPcx64 decode_frontend_player_pcx64(std::span<const std::byte> data) {
    if (data.size() < kPcxHeaderSize + kPaletteTrailerSize)
        throw FormatError("frontend PCX is too small");
    if (byte_at(data, 0U) != 0x0AU || byte_at(data, 2U) != 1U || byte_at(data, 3U) != 8U)
        throw FormatError("frontend PCX must use 8-bit PCX RLE");
    const uint16_t xmin = read_u16(data, 4U);
    const uint16_t ymin = read_u16(data, 6U);
    const uint16_t xmax = read_u16(data, 8U);
    const uint16_t ymax = read_u16(data, 10U);
    if (xmax < xmin || ymax < ymin) throw FormatError("frontend PCX has invalid bounds");
    const uint32_t width = static_cast<uint32_t>(xmax - xmin) + 1U;
    const uint32_t height = static_cast<uint32_t>(ymax - ymin) + 1U;
    if (width != 64U || height != 64U)
        throw FormatError("frontend PCX must be exactly 64x64");
    const uint8_t planes = byte_at(data, 65U);
    const uint16_t bytes_per_line = read_u16(data, 66U);
    if (planes != 1U || bytes_per_line < width)
        throw FormatError("frontend PCX must be one 8-bit indexed plane");

    const std::size_t palette_marker_offset = data.size() - kPaletteTrailerSize;
    if (byte_at(data, palette_marker_offset) != 0x0CU)
        throw FormatError("frontend PCX is missing its 256-color palette marker");

    const std::size_t decoded_scanline_bytes = static_cast<std::size_t>(bytes_per_line) * height;
    std::vector<uint8_t> decoded;
    decoded.reserve(decoded_scanline_bytes);
    std::size_t source = kPcxHeaderSize;
    while (decoded.size() < decoded_scanline_bytes) {
        if (source >= palette_marker_offset)
            throw FormatError("frontend PCX RLE stream ends before the 64x64 image");
        const uint8_t token = byte_at(data, source++);
        std::size_t run = 1U;
        uint8_t value = token;
        if ((token & 0xC0U) == 0xC0U) {
            run = token & 0x3FU;
            if (run == 0U || source >= palette_marker_offset)
                throw FormatError("frontend PCX has an invalid RLE run");
            value = byte_at(data, source++);
        }
        // Native decodes one scanline at a time; RLE may include padding but never cross it.
        if (run > bytes_per_line - decoded.size() % bytes_per_line)
            throw FormatError("frontend PCX RLE run crosses a scanline");
        if (run > decoded_scanline_bytes - decoded.size())
            throw FormatError("frontend PCX RLE stream overruns the expected scanlines");
        decoded.insert(decoded.end(), run, value);
    }

    FrontendPlayerPcx64 result;
    for (uint32_t y = 0U; y < 64U; ++y) {
        const std::size_t source_row = static_cast<std::size_t>(y) * bytes_per_line;
        const std::size_t destination_row = static_cast<std::size_t>(y) * 64U;
        std::copy_n(decoded.begin() + static_cast<std::ptrdiff_t>(source_row), 64U,
                    result.indexed_pixels.begin() + static_cast<std::ptrdiff_t>(destination_row));
    }
    const std::size_t palette_offset = palette_marker_offset + 1U;
    for (std::size_t index = 0U; index < result.palette_rgb.size(); ++index) {
        result.palette_rgb[index][0] = byte_at(data, palette_offset + index * 3U + 0U);
        result.palette_rgb[index][1] = byte_at(data, palette_offset + index * 3U + 1U);
        result.palette_rgb[index][2] = byte_at(data, palette_offset + index * 3U + 2U);
    }
    return result;
}

RgbaImage render_retail_frontend_player_texture(const FrontendPlayerPcx64& preview) {
    RgbaImage image{64U, 64U, {}};
    image.pixels.resize(64U * 64U * 4U);
    for (std::size_t pixel = 0U; pixel < preview.indexed_pixels.size(); ++pixel) {
        const auto& rgb = preview.palette_rgb[preview.indexed_pixels[pixel]];
        const bool black = rgb[0] == 0U && rgb[1] == 0U && rgb[2] == 0U;
        const std::size_t destination = pixel * 4U;
        image.pixels[destination + 0U] = rgb[0];
        image.pixels[destination + 1U] = rgb[1];
        image.pixels[destination + 2U] = rgb[2];
        image.pixels[destination + 3U] = black ? 0U : 255U;
    }
    return image;
}

} // namespace thandor
