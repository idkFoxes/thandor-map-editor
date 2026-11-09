#include "thandor/gfx.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>

namespace thandor {
namespace {
constexpr uint32_t kGfxMagic = 0x00786667U;
constexpr uint32_t kPalMagic = 0x006C6170U;
constexpr std::size_t kHeaderSize = 0x200;
constexpr std::size_t kSourceEntrySize = 0x20;

void write_u32_to(std::span<std::byte> bytes, std::size_t offset, uint32_t value) {
    if (offset > bytes.size() || sizeof(value) > bytes.size() - offset) {
        throw FormatError("write_u32 exceeds output buffer");
    }
    for (std::size_t index = 0; index < 4U; ++index) {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

void require_rgba_image(const RgbaImage& image, const char* label) {
    if (image.width == 0U || image.height == 0U || image.width > 16384U || image.height > 16384U) {
        throw FormatError(std::string(label) + " has invalid dimensions");
    }
    const uint64_t expected = static_cast<uint64_t>(image.width) * image.height * 4ULL;
    if (expected != image.pixels.size()) {
        throw FormatError(std::string(label) + " RGBA payload size does not match dimensions");
    }
}

void append_argb(std::vector<uint8_t>& out, uint32_t value, bool zero_alpha_is_opaque) {
    uint8_t alpha = static_cast<uint8_t>((value >> 24U) & 0xFFU);
    if (alpha == 0 && zero_alpha_is_opaque) alpha = 0xFF;
    out.push_back(static_cast<uint8_t>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<uint8_t>(value & 0xFFU));
    out.push_back(alpha);
}

} // namespace

RgbaImage load_tga_rgba(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw FormatError("could not open TGA texture: " + path.string());
    std::array<uint8_t, 18> header{};
    input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (!input) throw FormatError("TGA header is truncated");
    const uint8_t id_length = header[0];
    const uint8_t color_map_type = header[1];
    const uint8_t image_type = header[2];
    const uint16_t width = static_cast<uint16_t>(header[12] | (header[13] << 8U));
    const uint16_t height = static_cast<uint16_t>(header[14] | (header[15] << 8U));
    const uint8_t bits_per_pixel = header[16];
    const uint8_t descriptor = header[17];
    if (color_map_type != 0U || image_type != 2U || (bits_per_pixel != 24U && bits_per_pixel != 32U)) {
        throw FormatError("TGA import requires an uncompressed 24-bit or 32-bit true-colour image");
    }
    if (width == 0U || height == 0U) throw FormatError("TGA texture has zero dimensions");
    if ((descriptor & 0x10U) != 0U) throw FormatError("right-to-left TGA images are not supported");
    input.seekg(static_cast<std::streamoff>(id_length), std::ios::cur);
    if (!input) throw FormatError("TGA identification field is truncated");
    const std::size_t source_stride = bits_per_pixel / 8U;
    const std::size_t pixel_count = static_cast<std::size_t>(width) * height;
    std::vector<uint8_t> source(pixel_count * source_stride);
    input.read(reinterpret_cast<char*>(source.data()), static_cast<std::streamsize>(source.size()));
    if (!input) throw FormatError("TGA pixel payload is truncated");

    RgbaImage image{width, height, std::vector<uint8_t>(pixel_count * 4U)};
    const bool top_origin = (descriptor & 0x20U) != 0U;
    for (uint32_t y = 0; y < height; ++y) {
        const uint32_t source_y = top_origin ? y : static_cast<uint32_t>(height - 1U - y);
        for (uint32_t x = 0; x < width; ++x) {
            const std::size_t source_offset =
                (static_cast<std::size_t>(source_y) * width + x) * source_stride;
            const std::size_t destination_offset =
                (static_cast<std::size_t>(y) * width + x) * 4U;
            image.pixels[destination_offset + 0U] = source[source_offset + 2U];
            image.pixels[destination_offset + 1U] = source[source_offset + 1U];
            image.pixels[destination_offset + 2U] = source[source_offset + 0U];
            image.pixels[destination_offset + 3U] =
                bits_per_pixel == 32U ? source[source_offset + 3U] : 0xFFU;
        }
    }
    return image;
}

void save_tga_rgba(const std::filesystem::path& path, const RgbaImage& image) {
    require_rgba_image(image, "TGA export image");
    if (image.width > std::numeric_limits<uint16_t>::max() ||
        image.height > std::numeric_limits<uint16_t>::max()) {
        throw FormatError("TGA export dimensions exceed the format limit");
    }
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw FormatError("could not create TGA texture: " + path.string());
    std::array<uint8_t, 18> header{};
    header[2] = 2U;
    header[12] = static_cast<uint8_t>(image.width & 0xFFU);
    header[13] = static_cast<uint8_t>((image.width >> 8U) & 0xFFU);
    header[14] = static_cast<uint8_t>(image.height & 0xFFU);
    header[15] = static_cast<uint8_t>((image.height >> 8U) & 0xFFU);
    header[16] = 32U;
    header[17] = 0x28U; // top-left origin, eight alpha bits
    output.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    std::array<uint8_t, 4> bgra{};
    for (std::size_t offset = 0; offset < image.pixels.size(); offset += 4U) {
        bgra = {image.pixels[offset + 2U], image.pixels[offset + 1U],
                image.pixels[offset + 0U], image.pixels[offset + 3U]};
        output.write(reinterpret_cast<const char*>(bgra.data()), 4);
    }
    if (!output) throw FormatError("failed while writing TGA texture: " + path.string());
}

std::vector<std::byte> encode_single_rgba_gfx(const RgbaImage& image) {
    require_rgba_image(image, "GFX source image");
    const uint64_t pixel_bytes = static_cast<uint64_t>(image.width) * image.height * 4ULL;
    const uint64_t total_size64 = kHeaderSize + kSourceEntrySize + pixel_bytes;
    if (total_size64 > std::numeric_limits<uint32_t>::max()) {
        throw FormatError("encoded GFX exceeds the 32-bit allocation-size field");
    }
    const auto total_size = static_cast<std::size_t>(total_size64);
    std::vector<std::byte> bytes(total_size, std::byte{0});
    const uint32_t table_offset = static_cast<uint32_t>(kHeaderSize);
    const uint32_t data_offset = static_cast<uint32_t>(kHeaderSize + kSourceEntrySize);
    write_u32_to(bytes, 0x00U, kGfxMagic);
    write_u32_to(bytes, 0x04U, static_cast<uint32_t>(total_size));
    write_u32_to(bytes, 0x08U, 1U);
    write_u32_to(bytes, 0xB0U, 1U);
    write_u32_to(bytes, 0xB4U, 0U);
    write_u32_to(bytes, 0xB8U, table_offset);
    write_u32_to(bytes, table_offset + 0x00U, image.width);
    write_u32_to(bytes, table_offset + 0x04U, image.height);
    write_u32_to(bytes, table_offset + 0x08U, 0xFFFFFFFFU); // direct ARGB32 source
    write_u32_to(bytes, table_offset + 0x0CU, data_offset);
    write_u32_to(bytes, table_offset + 0x10U, 0U);
    write_u32_to(bytes, table_offset + 0x14U, 0U);
    write_u32_to(bytes, table_offset + 0x18U, image.width);
    write_u32_to(bytes, table_offset + 0x1CU, image.height);
    for (std::size_t pixel = 0; pixel < image.pixels.size() / 4U; ++pixel) {
        const std::size_t source = pixel * 4U;
        const uint32_t argb = static_cast<uint32_t>(image.pixels[source + 3U]) << 24U |
                              static_cast<uint32_t>(image.pixels[source + 0U]) << 16U |
                              static_cast<uint32_t>(image.pixels[source + 1U]) << 8U |
                              static_cast<uint32_t>(image.pixels[source + 2U]);
        write_u32_to(bytes, data_offset + pixel * 4U, argb);
    }
    return bytes;
}

GfxAsset GfxAsset::parse(std::span<const std::byte> data) {
    if (data.size() < kHeaderSize || read_u32(data, 0) != kGfxMagic) throw FormatError("invalid GFX magic");
    const auto allocation_size = read_u32(data, 4);
    if (allocation_size != 0 && allocation_size > data.size()) throw FormatError("GFX allocation size exceeds payload");
    const auto subresource_count = read_u32(data, 0xB0);
    const auto palette_bank_count = read_u32(data, 0xB4);
    const auto table_offset = read_u32(data, 0xB8);
    if (subresource_count == 0 || subresource_count > 4096) throw FormatError("invalid GFX subresource count");
    const uint64_t palette_entries = static_cast<uint64_t>(palette_bank_count) * 256ULL;
    const uint64_t palette_end = kHeaderSize + palette_entries * 8ULL;
    const uint64_t table_end = static_cast<uint64_t>(table_offset) + static_cast<uint64_t>(subresource_count) * kSourceEntrySize;
    if (palette_end > data.size() || table_offset < kHeaderSize || table_end > data.size()) throw FormatError("GFX table exceeds payload");
    GfxAsset result;
    result.bytes_.assign(data.begin(), data.end());
    result.palette_argb_.reserve(static_cast<std::size_t>(palette_entries));
    for (uint64_t i = 0; i < palette_entries; ++i) result.palette_argb_.push_back(read_u32(data, kHeaderSize + static_cast<std::size_t>(i) * 8));
    result.subresources_.reserve(subresource_count);
    for (uint32_t i = 0; i < subresource_count; ++i) {
        const auto offset = static_cast<std::size_t>(table_offset) + i * kSourceEntrySize;
        GfxSubresource source;
        source.logical_width = read_u32(data, offset + 0x00);
        source.logical_height = read_u32(data, offset + 0x04);
        source.palette_bank = PaletteBankIndex{read_i32(data, offset + 0x08)};
        source.data_offset = read_u32(data, offset + 0x0C);
        source.origin_x = read_i32(data, offset + 0x10);
        source.origin_y = read_i32(data, offset + 0x14);
        source.pixel_width = read_u32(data, offset + 0x18);
        source.pixel_height = read_u32(data, offset + 0x1C);
        if (source.pixel_width == 0 || source.pixel_height == 0 || source.pixel_width > 16384 || source.pixel_height > 16384) throw FormatError("invalid GFX dimensions");
        const uint64_t pixels = static_cast<uint64_t>(source.pixel_width) * source.pixel_height;
        const uint64_t bytes = source.palette_bank.present() ? pixels : pixels * 4ULL;
        if (source.data_offset > data.size() || bytes > data.size() - source.data_offset) throw FormatError("GFX pixel payload exceeds bounds");
        if (source.palette_bank.present() && static_cast<uint32_t>(source.palette_bank.value()) >= palette_bank_count) throw FormatError("GFX subresource uses missing palette bank");
        result.subresources_.push_back(source);
    }
    return result;
}

RgbaImage GfxAsset::decode(GraphicsSubresourceIndex subresource_index, bool zero_alpha_is_opaque) const {
    if (subresource_index.as_size() >= subresources_.size()) throw std::out_of_range("GFX subresource index");
    const auto& source = subresources_[subresource_index.as_size()];
    RgbaImage image{source.pixel_width, source.pixel_height, {}};
    const auto count = static_cast<std::size_t>(source.pixel_width) * source.pixel_height;
    image.pixels.reserve(count * 4);
    if (source.palette_bank.present()) {
        const auto palette_base = source.palette_bank.as_size() * 256;
        for (std::size_t i = 0; i < count; ++i) {
            const auto index = std::to_integer<uint8_t>(bytes_[source.data_offset + i]);
            append_argb(image.pixels, palette_argb_[palette_base + index], zero_alpha_is_opaque);
        }
    } else {
        for (std::size_t i = 0; i < count; ++i) append_argb(image.pixels, read_u32(bytes_, source.data_offset + i * 4), zero_alpha_is_opaque);
    }
    return image;
}

RgbaImage GfxAsset::decode_logical(GraphicsSubresourceIndex subresource_index, bool zero_alpha_is_opaque) const {
    if (subresource_index.as_size() >= subresources_.size()) throw std::out_of_range("GFX subresource index");
    const auto& source = subresources_[subresource_index.as_size()];
    if (source.logical_width == 0 || source.logical_height == 0 ||
        source.logical_width > 16384 || source.logical_height > 16384) {
        throw FormatError("invalid GFX logical dimensions");
    }
    const uint64_t pixels64 = static_cast<uint64_t>(source.logical_width) * source.logical_height;
    if (pixels64 > std::numeric_limits<std::size_t>::max() / 4U) throw FormatError("GFX logical canvas is too large");
    RgbaImage canvas{source.logical_width, source.logical_height,
                     std::vector<uint8_t>(static_cast<std::size_t>(pixels64) * 4U, 0)};
    const auto decoded = decode(subresource_index, zero_alpha_is_opaque);
    for (uint32_t y = 0; y < decoded.height; ++y) {
        const int64_t destination_y = static_cast<int64_t>(source.origin_y) + y;
        if (destination_y < 0 || destination_y >= static_cast<int64_t>(canvas.height)) continue;
        for (uint32_t x = 0; x < decoded.width; ++x) {
            const int64_t destination_x = static_cast<int64_t>(source.origin_x) + x;
            if (destination_x < 0 || destination_x >= static_cast<int64_t>(canvas.width)) continue;
            const std::size_t source_offset = (static_cast<std::size_t>(y) * decoded.width + x) * 4U;
            const std::size_t destination_offset =
                (static_cast<std::size_t>(destination_y) * canvas.width + static_cast<std::size_t>(destination_x)) * 4U;
            std::copy_n(decoded.pixels.begin() + static_cast<std::ptrdiff_t>(source_offset), 4,
                        canvas.pixels.begin() + static_cast<std::ptrdiff_t>(destination_offset));
        }
    }
    return canvas;
}

PalAsset PalAsset::parse(std::span<const std::byte> data) {
    if (data.size() < kHeaderSize || read_u32(data, 0) != kPalMagic) throw FormatError("invalid PAL magic");
    const auto allocation_size = read_u32(data, 4);
    if (allocation_size != 0 && allocation_size > data.size()) throw FormatError("PAL allocation size exceeds payload");
    const auto entry_count = read_u32(data, 0xB0);
    const uint64_t end = kHeaderSize + static_cast<uint64_t>(entry_count) * 8ULL;
    if (entry_count == 0 || end > data.size()) throw FormatError("PAL entry count exceeds payload");
    PalAsset result;
    result.colors_argb_.reserve(entry_count);
    for (uint32_t i = 0; i < entry_count; ++i) result.colors_argb_.push_back(read_u32(data, kHeaderSize + i * 8));
    return result;
}

} // namespace thandor
