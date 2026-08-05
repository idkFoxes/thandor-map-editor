#include "thandor/terrain_texture.hpp"

#include "thandor/binary.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <stdexcept>
#include <string>

namespace thandor {
namespace {
constexpr std::size_t kHeaderSize = 0x20;
constexpr std::size_t kRecordSize = 0x20;
constexpr std::size_t kMaterialBlockSize = TerrainTextureLayout::records_per_material * kRecordSize;
constexpr std::size_t kCanonicalTriangle0Record = 3;
constexpr std::size_t kCanonicalTriangle1Record = 2;

TerrainTriangleUvPair fallback_uvs() {
    return {{{Vec2f{0.5F, 0.0F}, Vec2f{0.0F, 1.0F}, Vec2f{1.0F, 1.0F}}},
             {{Vec2f{0.0F, 0.0F}, Vec2f{0.5F, 1.0F}, Vec2f{1.0F, 0.0F}}}};
}

TerrainTextureRecord parse_texture_record(std::span<const std::byte> data,
                                                std::size_t offset) {
    if (offset > data.size() || kRecordSize > data.size() - offset) {
        throw FormatError("terrain DAT record exceeds payload");
    }
    TerrainTextureRecord record;
    for (std::size_t component = 0; component < record.uv_q12.size(); ++component) {
        record.uv_q12[component] = read_i32(data, offset + component * 4U);
    }
    record.material_index = read_u32(data, offset + 0x18U);
    record.palette_index = read_u32(data, offset + 0x1CU);
    return record;
}

Vec2f record_uv(const TerrainTextureRecord& record, std::size_t vertex,
                uint32_t texture_width, uint32_t texture_height) {
    if (texture_width == 0 || texture_height == 0) throw FormatError("terrain texture has zero dimensions");
    return {q12_to_float(record.uv_q12[vertex * 2]) / static_cast<float>(texture_width),
            q12_to_float(record.uv_q12[vertex * 2 + 1]) / static_cast<float>(texture_height)};
}

} // namespace

TerrainSurfaceFamily classify_terrain_surface_path(std::string_view path) noexcept {
    std::string normalized;
    normalized.reserve(path.size());
    for (char value : path) {
        if (value == '\\') value = '/';
        if (value >= 'A' && value <= 'Z') value = static_cast<char>(value - 'A' + 'a');
        normalized.push_back(value);
    }
    const auto slash = normalized.find_last_of('/');
    std::string_view leaf(normalized);
    if (slash != std::string::npos) leaf.remove_prefix(slash + 1U);
    const auto dot = leaf.find('.');
    if (dot != std::string_view::npos) leaf = leaf.substr(0U, dot);
    if (leaf == "water" || leaf == "wasser") return TerrainSurfaceFamily::Water;
    if (leaf == "lava") return TerrainSurfaceFamily::Lava;
    if (leaf == "ice" || leaf == "eis") return TerrainSurfaceFamily::Ice;
    return TerrainSurfaceFamily::Other;
}

const char* terrain_surface_family_name(TerrainSurfaceFamily family) noexcept {
    switch (family) {
    case TerrainSurfaceFamily::Water: return "Water";
    case TerrainSurfaceFamily::Lava: return "Lava";
    case TerrainSurfaceFamily::Ice: return "Ice";
    case TerrainSurfaceFamily::Other: return "Custom surface";
    }
    return "Custom surface";
}

TerrainSoilPacketSelection select_terrain_soil_packets(
    const std::array<uint8_t, 3>& materials) noexcept {
    TerrainSoilPacketSelection result{};
    result.choices[0] = {0U, 0U};
    result.count = 1U;
    if (materials[0] == materials[1] && materials[1] == materials[2]) return result;
    if (materials[0] == materials[1]) {
        result.choices[result.count++] = {2U, 1U};
    } else if (materials[0] == materials[2]) {
        result.choices[result.count++] = {1U, 2U};
    } else if (materials[1] == materials[2]) {
        result.choices[result.count++] = {1U, 3U};
    } else {
        result.choices[result.count++] = {1U, 4U};
        result.choices[result.count++] = {2U, 5U};
    }
    return result;
}

TerrainSurfaceTextureLayout TerrainSurfaceTextureLayout::parse(
    std::span<const std::byte> data) {
    if (data.size() < kHeaderSize) {
        throw FormatError("surface DAT is smaller than its header");
    }
    if ((data.size() - kHeaderSize) % kRecordSize != 0U) {
        throw FormatError("surface DAT payload is not a whole number of records");
    }
    const uint32_t bit_count = read_u32(data, 0U);
    if (bit_count > 16U) throw FormatError("surface DAT selector bit count is invalid");
    const uint32_t required_records = 1U << bit_count;
    const std::size_t record_count = (data.size() - kHeaderSize) / kRecordSize;
    if (record_count < required_records || record_count == 0U || record_count > 4096U) {
        throw FormatError("surface DAT does not contain its complete selector range");
    }

    TerrainSurfaceTextureLayout result;
    result.selector_bit_count_ = bit_count;
    result.selector_mask_ = required_records - 1U;
    result.records_.reserve(record_count);
    for (std::size_t index = 0; index < record_count; ++index) {
        result.records_.push_back(parse_texture_record(data, kHeaderSize + index * kRecordSize));
    }
    return result;
}

const TerrainTextureRecord& TerrainSurfaceTextureLayout::record(uint32_t index) const {
    if (index >= records_.size()) throw std::out_of_range("surface DAT record index");
    return records_[index];
}

TerrainTextureLayout TerrainTextureLayout::parse(std::span<const std::byte> data) {
    if (data.size() < kHeaderSize) throw FormatError("terrain DAT is smaller than its header");
    const uint32_t material_count = read_u32(data, 0);
    if (material_count == 0 || material_count > 26) throw FormatError("terrain DAT material count is invalid");
    const uint64_t expected = kHeaderSize + static_cast<uint64_t>(material_count) * kMaterialBlockSize;
    if (expected != data.size()) throw FormatError("terrain DAT size does not match its material count");

    TerrainTextureLayout result;
    result.materials_.resize(material_count);
    for (uint32_t material = 0; material < material_count; ++material) {
        const std::size_t material_base = kHeaderSize + static_cast<std::size_t>(material) * kMaterialBlockSize;
        for (std::size_t record_index = 0; record_index < records_per_material; ++record_index) {
            const std::size_t offset = material_base + record_index * kRecordSize;
            auto& record = result.materials_[material][record_index];
            record = parse_texture_record(data, offset);
            if (record.material_index != material) {
                throw FormatError("terrain DAT record material index does not match its block");
            }
        }
    }
    return result;
}

std::span<const TerrainTextureRecord> TerrainTextureLayout::records(uint8_t material) const {
    if (!has_material(material)) throw std::out_of_range("terrain DAT material index");
    return materials_[material];
}

const TerrainTextureRecord& TerrainTextureLayout::record(uint8_t material,
                                                          uint8_t random_group,
                                                          uint8_t variant) const {
    if (!has_material(material)) throw std::out_of_range("terrain DAT material index");
    if (random_group >= random_groups_per_material)
        throw std::out_of_range("terrain DAT random group");
    if (variant >= variants_per_random_group)
        throw std::out_of_range("terrain DAT record variant");
    return materials_[material][static_cast<std::size_t>(random_group) *
                                    variants_per_random_group + variant];
}

TerrainTriangleUvPair TerrainTextureLayout::canonical_uvs(uint8_t material, uint32_t texture_width,
                                                           uint32_t texture_height) const {
    if (!has_material(material)) return fallback_uvs();
    const auto& block = materials_[material];
    const auto& triangle0 = block[kCanonicalTriangle0Record];
    const auto& triangle1 = block[kCanonicalTriangle1Record];
    TerrainTriangleUvPair result;
    for (std::size_t vertex = 0; vertex < 3; ++vertex) {
        result.triangle0[vertex] = record_uv(triangle0, vertex, texture_width, texture_height);
        result.triangle1[vertex] = record_uv(triangle1, vertex, texture_width, texture_height);
    }
    return result;
}

RgbaImage make_terrain_preview_opaque(const RgbaImage& source) {
    const uint64_t pixel_count64 = static_cast<uint64_t>(source.width) * source.height;
    if (source.width == 0 || source.height == 0 || pixel_count64 > std::numeric_limits<std::size_t>::max() ||
        source.pixels.size() != static_cast<std::size_t>(pixel_count64) * 4) {
        throw FormatError("terrain preview image dimensions do not match its RGBA payload");
    }

    RgbaImage result = source;
    const std::size_t pixel_count = static_cast<std::size_t>(pixel_count64);
    std::vector<int32_t> nearest(pixel_count, -1);
    std::deque<std::size_t> queue;
    for (std::size_t index = 0; index < pixel_count; ++index) {
        if (source.pixels[index * 4 + 3] == 255) {
            nearest[index] = static_cast<int32_t>(index);
            queue.push_back(index);
        }
    }
    if (queue.empty()) {
        for (std::size_t index = 0; index < pixel_count; ++index) {
            if (source.pixels[index * 4 + 3] != 0) {
                nearest[index] = static_cast<int32_t>(index);
                queue.push_back(index);
            }
        }
    }
    if (queue.empty()) {
        for (std::size_t index = 0; index < pixel_count; ++index) result.pixels[index * 4 + 3] = 255;
        return result;
    }

    constexpr std::array<std::array<int, 2>, 4> directions{{{{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}}}};
    while (!queue.empty()) {
        const std::size_t index = queue.front();
        queue.pop_front();
        const int x = static_cast<int>(index % source.width);
        const int y = static_cast<int>(index / source.width);
        for (const auto& direction : directions) {
            const int nx = x + direction[0];
            const int ny = y + direction[1];
            if (nx < 0 || ny < 0 || nx >= static_cast<int>(source.width) || ny >= static_cast<int>(source.height)) continue;
            const std::size_t neighbor = static_cast<std::size_t>(ny) * source.width + static_cast<std::size_t>(nx);
            if (nearest[neighbor] >= 0) continue;
            nearest[neighbor] = nearest[index];
            queue.push_back(neighbor);
        }
    }

    for (std::size_t index = 0; index < pixel_count; ++index) {
        const std::size_t offset = index * 4;
        const uint32_t alpha = source.pixels[offset + 3];
        const std::size_t nearest_index = static_cast<std::size_t>(nearest[index]);
        const std::size_t nearest_offset = nearest_index * 4;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const uint32_t foreground = source.pixels[offset + channel];
            const uint32_t background = source.pixels[nearest_offset + channel];
            result.pixels[offset + channel] = static_cast<uint8_t>((foreground * alpha + background * (255U - alpha) + 127U) / 255U);
        }
        result.pixels[offset + 3] = 255;
    }
    return result;
}

RgbaImage make_terrain_preview_tileable(const RgbaImage& source, uint32_t output_size) {
    const uint64_t pixel_count64 = static_cast<uint64_t>(source.width) * source.height;
    if (source.width == 0 || source.height == 0 || output_size == 0 || output_size > 4096 ||
        pixel_count64 > std::numeric_limits<std::size_t>::max() ||
        source.pixels.size() != static_cast<std::size_t>(pixel_count64) * 4U) {
        throw FormatError("terrain preview image dimensions do not match its RGBA payload");
    }

    // Largest-square dynamic programming over fully opaque source texels. Stock soil images are
    // projected diamonds with transparent transition space above them; sampling that whole image
    // as a conventional diffuse tile is what caused the compressed/mashed G308-G309 preview.
    std::vector<uint32_t> previous(source.width, 0), current(source.width, 0);
    uint32_t best_size = 0, best_right = 0, best_bottom = 0;
    for (uint32_t y = 0; y < source.height; ++y) {
        std::fill(current.begin(), current.end(), 0);
        for (uint32_t x = 0; x < source.width; ++x) {
            const bool opaque = source.pixels[(static_cast<std::size_t>(y) * source.width + x) * 4U + 3U] == 255U;
            if (!opaque) continue;
            current[x] = (x == 0 || y == 0) ? 1U
                : 1U + std::min({previous[x], current[x - 1], previous[x - 1]});
            if (current[x] > best_size) {
                best_size = current[x];
                best_right = x;
                best_bottom = y;
            }
        }
        previous.swap(current);
    }
    if (best_size < 8U) return make_terrain_preview_opaque(source);
    const uint32_t left = best_right + 1U - best_size;
    const uint32_t top = best_bottom + 1U - best_size;

    RgbaImage result{output_size, output_size, {}};
    result.pixels.resize(static_cast<std::size_t>(output_size) * output_size * 4U);
    for (uint32_t y = 0; y < output_size; ++y) {
        const uint32_t source_y = top + std::min(best_size - 1U,
            static_cast<uint32_t>((static_cast<uint64_t>(y) * best_size) / output_size));
        for (uint32_t x = 0; x < output_size; ++x) {
            const uint32_t source_x = left + std::min(best_size - 1U,
                static_cast<uint32_t>((static_cast<uint64_t>(x) * best_size) / output_size));
            const std::size_t source_offset =
                (static_cast<std::size_t>(source_y) * source.width + source_x) * 4U;
            const std::size_t destination_offset =
                (static_cast<std::size_t>(y) * output_size + x) * 4U;
            std::copy_n(source.pixels.begin() + static_cast<std::ptrdiff_t>(source_offset), 4,
                        result.pixels.begin() + static_cast<std::ptrdiff_t>(destination_offset));
            result.pixels[destination_offset + 3U] = 255U;
        }
    }
    return result;
}

RgbaImage make_missing_terrain_material(uint32_t material_index) {
    RgbaImage image{256, 256, {}};
    image.pixels.resize(256U * 256U * 4U);
    const uint8_t base = static_cast<uint8_t>(55U + (material_index * 23U) % 120U);
    for (uint32_t y = 0; y < image.height; ++y) {
        for (uint32_t x = 0; x < image.width; ++x) {
            const bool alternate = ((x / 16U) + (y / 16U)) % 2U != 0;
            const uint8_t value = static_cast<uint8_t>(std::min(255U, static_cast<uint32_t>(base) + (alternate ? 35U : 0U)));
            const std::size_t offset = (static_cast<std::size_t>(y) * image.width + x) * 4;
            image.pixels[offset + 0] = value;
            image.pixels[offset + 1] = static_cast<uint8_t>(std::min(255U, static_cast<uint32_t>(value) + 12U));
            image.pixels[offset + 2] = static_cast<uint8_t>(std::max(0, static_cast<int>(value) - 18));
            image.pixels[offset + 3] = 255;
        }
    }
    return image;
}

} // namespace thandor
