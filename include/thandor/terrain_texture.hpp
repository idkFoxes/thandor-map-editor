#pragma once

#include "thandor/gfx.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace thandor {

enum class TerrainSurfaceFamily {
    Water,
    Lava,
    Ice,
    Other,
};

// The original renderer uses one animated primary-surface path for water, lava, and ice.
// This classifier is presentation metadata only; it never selects a synthetic material shader.
TerrainSurfaceFamily classify_terrain_surface_path(std::string_view path) noexcept;
const char* terrain_surface_family_name(TerrainSurfaceFamily family) noexcept;

struct Vec2f {
    float u{};
    float v{};
};

struct TerrainTriangleUvPair {
    std::array<Vec2f, 3> triangle0{};
    std::array<Vec2f, 3> triangle1{};
};

struct TerrainTextureRecord {
    std::array<int32_t, 6> uv_q12{};
    uint32_t material_index{};
    uint32_t palette_index{};
};

struct TerrainSoilPacketChoice {
    uint8_t source_vertex{};
    uint8_t variant{};
};

struct TerrainSoilPacketSelection {
    std::array<TerrainSoilPacketChoice, 3> choices{};
    uint8_t count{};
};

// Exact selector recovered from game.exe 0x00500A00..0x00500AF2. The first packet is always
// vertex 0 / variant 0. Equal-pair and all-distinct cases append the transition packets in the
// same order as the game's primitive queue.
TerrainSoilPacketSelection select_terrain_soil_packets(
    const std::array<uint8_t, 3>& vertex_materials) noexcept;

class TerrainSurfaceTextureLayout {
public:
    // The primary surface DAT begins with a 0x20-byte header.  Its first dword is the
    // number of low RNG bits used to select one of the following 0x20-byte records.
    static TerrainSurfaceTextureLayout parse(std::span<const std::byte> data);

    uint32_t selector_bit_count() const noexcept { return selector_bit_count_; }
    uint32_t selector_mask() const noexcept { return selector_mask_; }
    std::size_t record_count() const noexcept { return records_.size(); }
    const TerrainTextureRecord& record(uint32_t index) const;
    std::span<const TerrainTextureRecord> records() const noexcept { return records_; }

private:
    uint32_t selector_bit_count_{};
    uint32_t selector_mask_{};
    std::vector<TerrainTextureRecord> records_;
};

class TerrainTextureLayout {
public:
    static constexpr std::size_t random_groups_per_material = 8;
    static constexpr std::size_t variants_per_random_group = 8;
    static constexpr std::size_t records_per_material =
        random_groups_per_material * variants_per_random_group;
    static TerrainTextureLayout parse(std::span<const std::byte> data);

    std::size_t material_count() const noexcept { return materials_.size(); }
    bool has_material(uint8_t material) const noexcept { return material < materials_.size(); }
    std::span<const TerrainTextureRecord> records(uint8_t material) const;
    const TerrainTextureRecord& record(uint8_t material, uint8_t random_group,
                                       uint8_t variant) const;
    TerrainTriangleUvPair canonical_uvs(uint8_t material, uint32_t texture_width,
                                        uint32_t texture_height) const;

private:
    std::vector<std::array<TerrainTextureRecord, records_per_material>> materials_;
};

// The stock terrain tiles encode transparent transition borders. The editor does not yet
// reconstruct the game's per-cell transition selector, so preview layers close those borders by
// propagating the nearest visible texel and compositing partial alpha against it. Exported game
// assets are never modified.
RgbaImage make_terrain_preview_opaque(const RgbaImage& source);
// Builds a fully opaque 3D-preview diffuse tile from the largest proven opaque square in the
// projected stock terrain image. The original DAT coordinates remain parsed for correspondence,
// but are not misused as generic 3D mesh UVs.
RgbaImage make_terrain_preview_tileable(const RgbaImage& source, uint32_t output_size = 256);
RgbaImage make_missing_terrain_material(uint32_t material_index);

} // namespace thandor
