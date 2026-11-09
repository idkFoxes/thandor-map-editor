#include "thandor/model.hpp"
#include "thandor/simulation_terrain_damage.hpp"
#include "thandor/binary.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace thandor {
namespace {
constexpr std::size_t kHeaderSize = 0x200;
constexpr std::size_t kMeshRecordHeaderSize = 0x20;
constexpr std::size_t kSpriteVertexStride = 0x40;
constexpr std::size_t kSpriteTriangleStride = 0x40;
constexpr float kSpriteTextureBasis = 256.0F;

void require_range(std::span<const std::byte> data, std::size_t offset, std::size_t size,
                   const char* label) {
    if (offset > data.size() || size > data.size() - offset) {
        throw FormatError(std::string(label) + " exceeds payload");
    }
}

Vec3f read_q12_vec3(std::span<const std::byte> data, std::size_t offset) {
    return {q12_to_float(read_i32(data, offset)), q12_to_float(read_i32(data, offset + 4)),
            q12_to_float(read_i32(data, offset + 8))};
}

Vec3f normalized_or_up(Vec3f value) {
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length < 0.00001F) return {0.0F, 0.0F, 1.0F};
    return {value.x / length, value.y / length, value.z / length};
}

Vec3f subtract(Vec3f lhs, Vec3f rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Vec3f cross(Vec3f lhs, Vec3f rhs) {
    return {lhs.y * rhs.z - lhs.z * rhs.y,
            lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x};
}

float dot(Vec3f lhs, Vec3f rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

std::string find_first_sprite_path(std::span<const std::byte> record) {
    for (std::size_t offset = 0; offset + 8 <= record.size(); offset += 2) {
        std::string text;
        for (std::size_t cursor = offset; cursor + 2 <= record.size(); cursor += 2) {
            const auto value = read_u16(record, cursor);
            if (value < 0x20 || value >= 0x7F) break;
            text.push_back(static_cast<char>(value));
        }
        auto normalized = normalize_asset_path(text);
        if (normalized.starts_with("spr/") && normalized.size() > 4) {
            if (!normalized.ends_with(".spr")) normalized += ".spr";
            return normalized;
        }
    }
    return {};
}

std::string read_serialized_sprite_path(std::span<const std::byte> data, std::size_t path_offset) {
    if (path_offset >= data.size() || (path_offset & 1U) != 0U) return {};
    std::string text;
    for (std::size_t cursor = path_offset; cursor + 2U <= data.size(); cursor += 2U) {
        const uint16_t value = read_u16(data, cursor);
        if (value == 0U) break;
        if (value < 0x20U || value >= 0x7FU) return {};
        text.push_back(static_cast<char>(value));
        if (text.size() > 260U) return {};
    }
    auto normalized = normalize_asset_path(text);
    if (!normalized.starts_with("spr/") || normalized.size() <= 4U) return {};
    if (!normalized.ends_with(".spr")) normalized += ".spr";
    return normalized;
}

void parse_model_visual_node_recursive(std::span<const std::byte> data, std::size_t node_offset,
                                       uint32_t parent_index, uint32_t child_slot,
                                       std::vector<ModelVisualNode>& output,
                                       std::unordered_set<std::size_t>& active_offsets) {
    constexpr std::size_t kNodeHeaderSize = 0x3CU;
    require_range(data, node_offset, kNodeHeaderSize, "MDL model-node definition");
    if (!active_offsets.insert(node_offset).second) throw FormatError("MDL model-node hierarchy contains a cycle");
    const uint32_t child_count = read_u32(data, node_offset + 0x14U);
    if (child_count > 6U) throw FormatError("MDL model-node child count exceeds the serialized six-slot array");
    require_range(data, node_offset + 0x18U, static_cast<std::size_t>(child_count) * 4U,
                  "MDL model-node child offsets");

    ModelVisualNode node;
    node.serialized_offset = static_cast<uint32_t>(node_offset);
    node.parent_index = parent_index;
    node.child_slot = child_slot;
    node.flags = read_u32(data, node_offset + 4U);
    node.local_rotation_angle0 = read_u32(data, node_offset + 8U);
    node.local_rotation_angle1 = read_u32(data, node_offset + 0x0CU);
    node.local_rotation_angle2 = read_u32(data, node_offset + 0x10U);
    // Serialized MDL nodes store the UTF-16 SPR path inline at +0x38. The first
    // dword is the node byte size; runtime registration later replaces the separate
    // +0x30 resource slot with the loaded SpriteAsset pointer.
    node.sprite_path = read_serialized_sprite_path(data, node_offset + 0x38U);
    const uint32_t node_index = static_cast<uint32_t>(output.size());
    output.push_back(std::move(node));

    for (uint32_t slot = 0U; slot < child_count; ++slot) {
        const uint32_t child_offset = read_u32(data, node_offset + 0x18U + slot * 4U);
        if (child_offset == 0U) continue;
        parse_model_visual_node_recursive(data, child_offset, node_index, slot, output, active_offsets);
    }
    active_offsets.erase(node_offset);
}

void append_mesh_record(std::span<const std::byte> data, std::size_t record_offset,
                        std::size_t group_end, SpriteMesh& destination,
                        uint32_t& winding_correction_count,
                        std::vector<SpriteRaycastTriangle>* raycast_triangles=nullptr) {
    require_range(data, record_offset, kMeshRecordHeaderSize, "SPR mesh record header");
    const auto record_size = read_u32(data, record_offset);
    const auto mesh_group_selector_mask = read_u32(data, record_offset + 4);
    const auto vertex_count = read_u32(data, record_offset + 8);
    const auto triangle_count = read_u32(data, record_offset + 0x0C);
    const auto mesh_record_flags = read_u32(data, record_offset + 0x10);
    if (record_size < kMeshRecordHeaderSize) throw FormatError("SPR mesh record size is too small");
    if (record_offset > group_end || record_size > group_end - record_offset) {
        throw FormatError("SPR mesh record exceeds its LOD group");
    }
    if (vertex_count > 1'000'000U || triangle_count > 1'000'000U) {
        throw FormatError("SPR mesh count is unreasonable");
    }

    const std::size_t vertex_bytes = static_cast<std::size_t>(vertex_count) * kSpriteVertexStride;
    const std::size_t triangle_bytes = static_cast<std::size_t>(triangle_count) * kSpriteTriangleStride;
    const std::size_t vertex_start = record_offset + kMeshRecordHeaderSize;
    const std::size_t triangle_start = vertex_start + vertex_bytes;
    if (triangle_start < vertex_start || triangle_start + triangle_bytes < triangle_start ||
        triangle_start + triangle_bytes > record_offset + record_size) {
        throw FormatError("SPR mesh payload exceeds its record");
    }

    destination.vertices.reserve(destination.vertices.size() + static_cast<std::size_t>(triangle_count) * 3U);
    destination.indices.reserve(destination.indices.size() + static_cast<std::size_t>(triangle_count) * 3U);
    constexpr std::array<std::size_t, 3> pointer_fields{0x00, 0x0C, 0x18};
    constexpr std::array<std::size_t, 3> u_fields{0x04, 0x10, 0x1C};
    constexpr std::array<std::size_t, 3> v_fields{0x08, 0x14, 0x20};
    for (uint32_t triangle = 0; triangle < triangle_count; ++triangle) {
        const auto offset = triangle_start + static_cast<std::size_t>(triangle) * kSpriteTriangleStride;
        const uint32_t texture_subresource = read_u32(data, offset + 0x30);
        const uint32_t render_flags = read_u32(data, offset + 0x34);
        std::array<SpriteMeshVertex, 3> corners{};
        for (std::size_t corner = 0; corner < 3; ++corner) {
            const auto serialized_vertex_offset =
                static_cast<std::size_t>(read_u32(data, offset + pointer_fields[corner]));
            if (serialized_vertex_offset < vertex_start || serialized_vertex_offset >= triangle_start ||
                (serialized_vertex_offset - vertex_start) % kSpriteVertexStride != 0) {
                throw FormatError("SPR triangle vertex offset does not select a vertex in its mesh record");
            }
            const auto local_index =
                static_cast<uint32_t>((serialized_vertex_offset - vertex_start) / kSpriteVertexStride);
            if (local_index >= vertex_count) throw FormatError("SPR triangle vertex index exceeds mesh record");
            const auto source_offset = vertex_start + static_cast<std::size_t>(local_index) * kSpriteVertexStride;
            corners[corner] = {
                read_q12_vec3(data, source_offset),
                normalized_or_up(read_q12_vec3(data, source_offset + 0x10)),
                q12_to_float(read_i32(data, offset + u_fields[corner])),
                q12_to_float(read_i32(data, offset + v_fields[corner])),
                texture_subresource,
                render_flags,
                mesh_record_flags,
                mesh_group_selector_mask,
            };
        }

        // game.exe evaluates triangle visibility from the serialized plane normal at +0x24 and
        // initializes Direct3D with CULLMODE=NONE.  SPR corner order is therefore not a reliable
        // facing source.  Canonicalize only the order (including its UV corner) when it opposes
        // the authoritative plane normal; all positions, materials, flags, and triangle counts
        // remain unchanged.
        const Vec3f plane_normal = read_q12_vec3(data, offset + 0x24U);
        if(raycast_triangles){
            const auto to_q12=[](Vec3f v){
                return std::array{float_to_q12(v.x),float_to_q12(v.y),float_to_q12(v.z)};
            };
            raycast_triangles->push_back({{to_q12(corners[0].position),to_q12(corners[1].position),
                                           to_q12(corners[2].position)},to_q12(plane_normal)});
        }
        const Vec3f geometric_normal =
            cross(subtract(corners[1].position, corners[0].position),
                  subtract(corners[2].position, corners[0].position));
        if (dot(geometric_normal, plane_normal) < 0.0F) {
            std::swap(corners[1], corners[2]);
            ++winding_correction_count;
        }

        for (const auto& corner : corners) {
            destination.vertices.push_back(corner);
            destination.indices.push_back(static_cast<uint32_t>(destination.vertices.size() - 1U));
        }
    }
}
} // namespace

SpriteTextureUv normalize_sprite_texture_uv(float serialized_u, float serialized_v,
                                             uint32_t pixel_width, uint32_t pixel_height) {
    if (pixel_width == 0 || pixel_height == 0) throw FormatError("SPR texture has zero physical dimensions");
    if (!std::has_single_bit(pixel_width) || !std::has_single_bit(pixel_height)) {
        throw FormatError("SPR texture physical dimensions are not powers of two");
    }
    const uint32_t maximum = std::max(pixel_width, pixel_height);
    const float u_aspect = static_cast<float>(maximum / pixel_width);
    const float v_aspect = static_cast<float>(maximum / pixel_height);
    return {serialized_u / (kSpriteTextureBasis * u_aspect),
            serialized_v / (kSpriteTextureBasis * v_aspect)};
}

float normalize_model_texture_offset_u(int32_t fixed_texture_offset,
                                       uint32_t pixel_width,uint32_t pixel_height) {
    if (pixel_width == 0U || pixel_height == 0U)
        throw FormatError("SPR animated texture has zero physical dimensions");
    if (!std::has_single_bit(pixel_width) || !std::has_single_bit(pixel_height))
        throw FormatError("SPR animated texture dimensions are not powers of two");
    const uint32_t maximum = std::max(pixel_width,pixel_height);
    const float u_aspect = static_cast<float>(maximum / pixel_width);
    // ModelRuntimeNode stores packet-coordinate deltas in Q12 texture-coordinate units. The
    // renderer's normalized UV basis is 256 serialized units per repeat.
    return static_cast<float>(fixed_texture_offset) /
        (4096.0F * kSpriteTextureBasis * u_aspect);
}

float normalize_model_texture_offset_v(int32_t fixed_texture_offset,
                                       uint32_t pixel_width,uint32_t pixel_height) {
    if (pixel_width == 0U || pixel_height == 0U)
        throw FormatError("SPR animated texture has zero physical dimensions");
    if (!std::has_single_bit(pixel_width) || !std::has_single_bit(pixel_height))
        throw FormatError("SPR animated texture dimensions are not powers of two");
    const uint32_t maximum = std::max(pixel_width,pixel_height);
    const float v_aspect = static_cast<float>(maximum / pixel_height);
    return static_cast<float>(fixed_texture_offset) /
        (4096.0F * kSpriteTextureBasis * v_aspect);
}

RgbaImage make_sprite_texture_square_canvas(const RgbaImage& source) {
    if (source.width == 0 || source.height == 0 ||
        source.pixels.size() != static_cast<std::size_t>(source.width) * source.height * 4U) {
        throw FormatError("SPR texture image dimensions do not match its RGBA payload");
    }
    if (!std::has_single_bit(source.width) || !std::has_single_bit(source.height)) {
        throw FormatError("SPR texture image dimensions are not powers of two");
    }
    const uint32_t extent = std::max(source.width, source.height);
    if (source.width == extent && source.height == extent) return source;
    RgbaImage result{extent, extent, std::vector<uint8_t>(
        static_cast<std::size_t>(extent) * extent * 4U, 0U)};
    for (uint32_t y = 0; y < source.height; ++y) {
        const std::size_t source_offset = static_cast<std::size_t>(y) * source.width * 4U;
        const std::size_t destination_offset = static_cast<std::size_t>(y) * extent * 4U;
        std::copy_n(source.pixels.begin() + static_cast<std::ptrdiff_t>(source_offset),
                    static_cast<std::size_t>(source.width) * 4U,
                    result.pixels.begin() + static_cast<std::ptrdiff_t>(destination_offset));
    }
    return result;
}

SpriteAsset SpriteAsset::parse(std::span<const std::byte> data) {
    if (data.size() < kHeaderSize) throw FormatError("SPR image is smaller than its 0x200-byte header");
    if (read_u32(data, 0) != fourcc("spr")) throw FormatError("SPR magic mismatch");
    const auto declared_size = read_u32(data, 4);
    if (declared_size < kHeaderSize || declared_size > data.size()) {
        throw FormatError("SPR declared size exceeds payload");
    }
    if (read_u32(data, 0x0C) != 0x00020007U) throw FormatError("unsupported SPR converter version");

    SpriteAsset result;
    const uint32_t group_count = read_u32(data, 0xB0);
    result.registry_id_ = read_u32(data, 0xB8);
    result.local_bounds_min_ = read_q12_vec3(data, 0xC0U);
    result.local_bounds_max_ = read_q12_vec3(data, 0xCCU);
    result.bounding_radius_q12_ = read_i32(data, 0xD8U);
    result.bounding_radius_ = q12_to_float(result.bounding_radius_q12_);
    result.placement_height_offset_ = q12_to_float(read_i32(data, 0xDCU));
    result.lighting_scale_q12_ = read_i32(data, 0xE0U);
    result.has_valid_header_bounds_ =
        result.local_bounds_min_.x <= result.local_bounds_max_.x &&
        result.local_bounds_min_.y <= result.local_bounds_max_.y &&
        result.local_bounds_min_.z <= result.local_bounds_max_.z &&
        std::isfinite(result.bounding_radius_) && result.bounding_radius_ >= 0.0F;

    // The SPR header publishes a serialized lookup table at +0xE4/+0xE8.  Keys whose low nibble
    // is six are the model attachment points used by ArmyRuntime_UpdateTimedShotAndEffectEmitters.
    // Resource SPRs contain one such point, so preserving it fixes the effect origin without
    // inventing a per-resource offset.
    const uint32_t lookup_offset = read_u32(data, 0xE4);
    const uint32_t lookup_count = read_u32(data, 0xE8);
    if (lookup_count > 0x10000U) throw FormatError("SPR lookup count is unreasonable");
    if (lookup_offset != 0U || lookup_count != 0U) {
        require_range(data, lookup_offset, static_cast<std::size_t>(lookup_count) * 0x10U,
                      "SPR lookup table");
        for (uint32_t index = 0; index < lookup_count; ++index) {
            const std::size_t offset = lookup_offset + static_cast<std::size_t>(index) * 0x10U;
            const uint32_t key = read_u32(data, offset);
            const std::array<int32_t, 3> translation_q12{
                read_i32(data, offset + 4U), read_i32(data, offset + 8U), read_i32(data, offset + 0x0CU)};
            const Vec3f translation{q12_to_float(translation_q12[0]),
                                    q12_to_float(translation_q12[1]),
                                    q12_to_float(translation_q12[2])};
            result.lookup_records_.push_back(SpriteLookupRecord{key, translation, translation_q12});
            if ((key & 0xFU) == 6U) result.class6_lookup_points_.push_back(translation);
        }
    }
    if (group_count == 0 || group_count > 0xFFFU) {
        throw FormatError("SPR LOD group count is outside the game-validated range");
    }
    result.lod_groups_.reserve(group_count);

    std::size_t group_offset = kHeaderSize;
    for (uint32_t group_index = 0; group_index < group_count; ++group_index) {
        require_range(data, group_offset, kMeshRecordHeaderSize, "SPR LOD group header");
        const auto group_size = read_u32(data, group_offset);
        const auto record_count = read_u32(data, group_offset + 4);
        const auto group_flags = read_u32(data, group_offset + 0x0C);
        if (group_size < kMeshRecordHeaderSize) throw FormatError("SPR LOD group size is too small");
        require_range(data, group_offset, group_size, "SPR LOD group");
        const std::size_t group_end = group_offset + group_size;

        SpriteLodGroup lod;
        lod.flags = group_flags;
        std::size_t record_offset = group_offset + kMeshRecordHeaderSize;
        for (uint32_t record = 0; record < record_count; ++record) {
            require_range(data, record_offset, kMeshRecordHeaderSize, "SPR mesh record header");
            const auto record_size = read_u32(data, record_offset);
            append_mesh_record(data, record_offset, group_end, lod.mesh,
                               lod.winding_correction_count,&lod.raycast_triangles);
            record_offset += record_size;
        }
        if (record_offset > group_end) throw FormatError("SPR LOD records exceed group size");
        result.lod_groups_.push_back(std::move(lod));
        group_offset = group_end;
    }
    if (group_offset > declared_size) throw FormatError("SPR LOD groups exceed declared image size");

    // The renderer's generated-shadow path follows the dedicated table referenced by
    // SPR header +0xEC. It does not reuse any visible distance-selected LOD group.
    const uint32_t shadow_table_offset = read_u32(data, 0xECU);
    result.has_shadow_group_=shadow_table_offset!=0U;
    if (shadow_table_offset != 0U) {
        require_range(data, shadow_table_offset, kMeshRecordHeaderSize,
                      "SPR generated-shadow table header");
        const uint32_t shadow_table_size = read_u32(data, shadow_table_offset);
        const uint32_t shadow_record_count = read_u32(data, shadow_table_offset + 4U);
        if (shadow_table_size < kMeshRecordHeaderSize || shadow_record_count > 0x10000U) {
            throw FormatError("SPR generated-shadow table shape is invalid");
        }
        require_range(data, shadow_table_offset, shadow_table_size,
                      "SPR generated-shadow table");
        const std::size_t shadow_table_end =
            static_cast<std::size_t>(shadow_table_offset) + shadow_table_size;
        std::size_t shadow_record_offset =
            static_cast<std::size_t>(shadow_table_offset) + kMeshRecordHeaderSize;
        for (uint32_t record = 0; record < shadow_record_count; ++record) {
            require_range(data, shadow_record_offset, kMeshRecordHeaderSize,
                          "SPR generated-shadow mesh record header");
            const uint32_t record_size = read_u32(data, shadow_record_offset);
            uint32_t ignored_winding_corrections = 0U;
            append_mesh_record(data, shadow_record_offset, shadow_table_end,
                               result.generated_shadow_mesh_, ignored_winding_corrections);
            shadow_record_offset += record_size;
        }
        if (shadow_record_offset > shadow_table_end) {
            throw FormatError("SPR generated-shadow records exceed shadow table");
        }
        result.generated_shadow_record_count_ = shadow_record_count;
    }
    return result;
}

const SpriteLookupRecord* SpriteAsset::find_lookup_record(
    ModelLookupKeyIndex key_index, ModelLookupKeyClass key_class) const noexcept {
    const uint32_t packed = pack_model_lookup_key(key_index, key_class);
    for (const auto& record : lookup_records_) {
        if (record.packed_key == packed) return &record;
    }
    return nullptr;
}

const SpriteLookupRecord* SpriteAsset::child_attachment(uint32_t child_index) const noexcept {
    const ModelLookupKeyIndex index{child_index};
    if (const auto* class0 = find_lookup_record(index, ModelLookupKeyClass{0U})) return class0;
    return find_lookup_record(index, ModelLookupKeyClass{1U});
}

const SpriteMesh& SpriteAsset::lod_mesh(std::size_t index) const {
    if (index >= lod_groups_.size()) throw std::out_of_range("SPR LOD group index");
    return lod_groups_[index].mesh;
}

uint32_t SpriteAsset::lod_flags(std::size_t index) const {
    if (index >= lod_groups_.size()) throw std::out_of_range("SPR LOD group index");
    return lod_groups_[index].flags;
}

std::size_t SpriteAsset::generated_shadow_soft_triangle_count() const noexcept {
    std::size_t result = 0U;
    for (std::size_t triangle = 0; triangle + 2U < generated_shadow_mesh_.indices.size(); triangle += 3U) {
        const uint32_t index = generated_shadow_mesh_.indices[triangle];
        if (index < generated_shadow_mesh_.vertices.size() &&
            (generated_shadow_mesh_.vertices[index].mesh_record_flags & 1U) != 0U) ++result;
    }
    return result;
}

std::size_t SpriteAsset::generated_shadow_hard_triangle_count() const noexcept {
    const std::size_t total = generated_shadow_mesh_.indices.size() / 3U;
    const std::size_t soft = generated_shadow_soft_triangle_count();
    return total >= soft ? total - soft : 0U;
}

std::size_t SpriteAsset::alternate_lod_triangle_count() const noexcept {
    std::size_t result = 0;
    for (std::size_t index = 1; index < lod_groups_.size(); ++index) {
        result += lod_groups_[index].mesh.indices.size() / 3U;
    }
    return result;
}

std::size_t SpriteAsset::winding_correction_count() const noexcept {
    std::size_t result = 0;
    for (const auto& lod : lod_groups_) result += lod.winding_correction_count;
    return result;
}

ResourceEnergyKind classify_resource_energy(const SpriteMesh& mesh) noexcept {
    bool power = false;
    bool building = false;
    for (const auto& vertex : mesh.vertices) {
        power = power || vertex.texture_subresource == 26U || vertex.texture_subresource == 27U;
        building = building || vertex.texture_subresource == 32U || vertex.texture_subresource == 33U;
    }
    if (power && !building) return ResourceEnergyKind::Power;
    if (building && !power) return ResourceEnergyKind::Building;
    return ResourceEnergyKind::None;
}

uint32_t resource_body_subresource(ResourceEnergyKind kind) noexcept {
    switch (kind) {
        case ResourceEnergyKind::Power: return 26U;
        case ResourceEnergyKind::Building: return 32U;
        case ResourceEnergyKind::None: return std::numeric_limits<uint32_t>::max();
    }
    return std::numeric_limits<uint32_t>::max();
}

uint32_t resource_luminous_particle_subresource(ResourceEnergyKind kind) noexcept {
    switch (kind) {
        case ResourceEnergyKind::Power: return 27U;
        case ResourceEnergyKind::Building: return 33U;
        case ResourceEnergyKind::None: return std::numeric_limits<uint32_t>::max();
    }
    return std::numeric_limits<uint32_t>::max();
}

uint32_t resource_effect_subresource(ResourceEnergyKind kind) noexcept {
    return resource_luminous_particle_subresource(kind);
}

SpriteMesh extract_sprite_subresource_mesh(const SpriteMesh& mesh, uint32_t texture_subresource) {
    SpriteMesh result;
    if (texture_subresource == std::numeric_limits<uint32_t>::max()) return result;
    if (mesh.indices.size() % 3U != 0U) throw FormatError("SPR mesh index stream is not triangle aligned");
    for (std::size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3U) {
        const uint32_t ia = mesh.indices[triangle];
        const uint32_t ib = mesh.indices[triangle + 1U];
        const uint32_t ic = mesh.indices[triangle + 2U];
        if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() || ic >= mesh.vertices.size()) {
            throw FormatError("SPR mesh index exceeds expanded vertex stream");
        }
        const auto& a = mesh.vertices[ia];
        const auto& b = mesh.vertices[ib];
        const auto& c = mesh.vertices[ic];
        if (a.texture_subresource != texture_subresource ||
            b.texture_subresource != texture_subresource ||
            c.texture_subresource != texture_subresource) continue;
        const uint32_t base = static_cast<uint32_t>(result.vertices.size());
        result.vertices.push_back(a);
        result.vertices.push_back(b);
        result.vertices.push_back(c);
        result.indices.insert(result.indices.end(), {base, base + 1U, base + 2U});
    }
    return result;
}

SpriteMesh extract_resource_body_mesh(const SpriteMesh& mesh, ResourceEnergyKind kind) {
    return extract_sprite_subresource_mesh(mesh, resource_body_subresource(kind));
}

SpriteMesh extract_resource_luminous_particle_mesh(const SpriteMesh& mesh,
                                                       ResourceEnergyKind kind) {
    return extract_sprite_subresource_mesh(mesh, resource_luminous_particle_subresource(kind));
}

SpriteMesh extract_resource_effect_mesh(const SpriteMesh& mesh, ResourceEnergyKind kind) {
    return extract_resource_luminous_particle_mesh(mesh, kind);
}

std::vector<ModelVisualRecord> parse_model_visual_records(std::span<const std::byte> data,
                                                           std::string_view source_path) {
    if (data.size() < kHeaderSize) throw FormatError("MDL image is smaller than its 0x200-byte header");
    if (read_u32(data, 0) != fourcc("mdl")) throw FormatError("MDL magic mismatch");
    const auto declared_size = read_u32(data, 4);
    if (declared_size < kHeaderSize || declared_size > data.size()) throw FormatError("MDL declared size exceeds payload");
    const auto record_count = read_u32(data, 0xB0);
    std::vector<ModelVisualRecord> result;
    result.reserve(record_count);
    std::size_t offset = kHeaderSize;
    for (uint32_t record = 0; record < record_count; ++record) {
        require_range(data, offset, 0x0C, "MDL record header");
        const auto size = read_u32(data, offset);
        if (size < 0x0C) throw FormatError("MDL record size is too small");
        require_range(data, offset, size, "MDL record");
        const auto slice = data.subspan(offset, size);
        ModelVisualRecord visual;
        visual.definition_id = read_u32(data, offset + 8);
        // InGameSelectionDetailPanel_Rebuild reads the model-definition dword at +0x04
        // and resolves resource 0x18004F + value from texte/help.str page 0x18.
        visual.name_text_offset = read_u32(data, offset + 4);
        visual.has_name_text_offset = true;
        visual.source_path = normalize_asset_path(source_path);
        if (size >= 0x50U) {
            visual.runtime_class_id = read_u32(data, offset + 0x4CU);
            // ShotModelRuntimeMaintenance dereferences the hit model definition and uses
            // MDL +0x5C to index the SHT +0x90 eight-entry target-impact EFF table.
            if (size >= 0x60U) visual.shot_impact_class_index = read_u32(data, offset + 0x5CU);
            visual.simulation.class_step_q12 = read_i32(data, offset + 0x0CU);
            visual.simulation.yaw_max_velocity_turn16 = read_i32(data, offset + 0x10U);
            visual.simulation.pitch_max_velocity_turn16 = read_i32(data, offset + 0x14U);
            visual.simulation.runtime_field_18_raw = read_i32(data, offset + 0x18U);
            visual.simulation.yaw_acceleration_turn16 = read_i32(data, offset + 0x1CU);
            visual.simulation.pitch_acceleration_turn16 = read_i32(data, offset + 0x20U);
            visual.simulation.pitch_min_turn16 = read_i32(data, offset + 0x24U);
            visual.simulation.pitch_max_turn16 = read_i32(data, offset + 0x28U);
            visual.simulation.shot_definition_id = read_u32(data, offset + 0x2CU);
            visual.simulation.reload_ticks = read_u32(data, offset + 0x30U);
            visual.simulation.recoil_rotation_step_turn16 = read_i32(data, offset + 0x34U);
            visual.simulation.recoil_duration_ticks = read_i32(data, offset + 0x38U);
            visual.simulation.recoil_translation_q12 = read_i32(data, offset + 0x3CU);
            visual.simulation.action_vector0 = read_i32(data, offset + 0x40U);
            visual.simulation.action_vector1 = read_i32(data, offset + 0x44U);
            if (size >= 0x4CU)
                visual.simulation.fog_uncover_radius_q12 = read_i32(data, offset + 0x48U);
            if (size >= 0x5CU)
                visual.simulation.timed_water_effect_id = read_u32(data, offset + 0x58U);
            if (size >= 0x64U)
                visual.simulation.maximum_integrity = read_i32(data, offset + 0x60U);
            if (size >= 0x54U)
                visual.simulation.aim_height_offset_q12 = read_i32(data, offset + 0x50U);
            if (size >= 0x58U)
                visual.simulation.placement_height_offset_q12 = read_i32(data, offset + 0x54U);
            if (size >= 0x74U)
                visual.simulation.fog_reference_height_offset_q12 = read_i32(data, offset + 0x70U);
            if (size >= 0x78U)
                visual.simulation.destroyed_replacement_army_asset_id = read_i32(data, offset + 0x74U);
            if (size >= 0x80U) {
                visual.simulation.positioned_sound_gain_q15 = read_u32(data, offset + 0x78U);
                visual.simulation.positioned_sound_maximum_distance_q12 = read_u32(data, offset + 0x7CU);
            }
            if (size >= 0xC0U) {
                for (std::size_t channel = 0; channel < 8U; ++channel) {
                    const std::size_t pair_offset = 0x80U + channel * 8U;
                    visual.simulation.terminal_attachment_effect_ids[channel] =
                        read_u32(data, offset + pair_offset);
                    visual.simulation.terminal_attachment_delay_ticks[channel] =
                        read_i32(data, offset + pair_offset + 4U);
                }
            }
            if (size >= 0x190U) {
                visual.simulation.retail_build_time_q5 = read_u32(data, offset + 0x180U);
                visual.simulation.retail_xenite_cost_q4 = read_u32(data, offset + 0x184U);
                visual.simulation.retail_build_energy_q4 = read_u32(data, offset + 0x188U);
                visual.simulation.retail_energy_demand_q4 = read_u32(data, offset + 0x18CU);
            }
            if (size >= 0x194U)
                visual.simulation.construction_destruction_effect_id = read_u32(data, offset + 0x190U);
            if (size >= 0x19CU && simulation_has_water_damage_fields(visual.runtime_class_id)) {
                visual.simulation.water_damage_multiplier = read_i32(data, offset + 0x194U);
                visual.simulation.water_damage_threshold_q12 = read_i32(data, offset + 0x198U);
            }
            if (visual.runtime_class_id == 23U) {
                visual.simulation.vertical_deployment_step_q12_per_tick =
                    visual.simulation.class_step_q12;
                visual.simulation.vertical_deployment_travel_limit_q12 =
                    visual.simulation.pitch_min_turn16;
            }
            if (size >= 0x1A0U)
                visual.simulation.class_specific_19c_raw = read_i32(data, offset + 0x19CU);
            if (size >= 0x1ACU)
                visual.simulation.class_specific_1a8_raw = read_i32(data, offset + 0x1A8U);
            if (size >= 0x58U)
                visual.simulation.articulated_root_height_q12 = read_i32(data, offset + 0x54U);
        }
        if (size >= 0x27CU) {
            visual.simulation.placement_contact_kind = read_i32(data, offset + 0x278U);
        }
        if (size >= 0x280U)
            visual.simulation.occupancy_mark_radius_units = read_u32(data, offset + 0x27CU);
        if (size >= 0x6CU) {
            visual.runtime_render_flags = read_u32(data, offset + 0x68U);
            visual.simulation.runtime_render_flags = visual.runtime_render_flags;
            visual.casts_generated_shadow = (visual.runtime_render_flags & 0x40U) == 0U;
        }
        if (size >= 0x68U) {
            const uint32_t root_node_offset = read_u32(data, offset + 0x64U);
            if (root_node_offset != 0U) {
                std::unordered_set<std::size_t> active_offsets;
                parse_model_visual_node_recursive(data, root_node_offset,
                    std::numeric_limits<uint32_t>::max(), 0U, visual.hierarchy_nodes, active_offsets);
            }
        }
        for (auto& node : visual.hierarchy_nodes) {
            node.model_definition_id = visual.definition_id;
            node.runtime_class_id = visual.runtime_class_id;
            node.shot_impact_class_index = visual.shot_impact_class_index;
            node.simulation = visual.simulation;
        }
        if (!visual.hierarchy_nodes.empty()) visual.sprite_path = visual.hierarchy_nodes.front().sprite_path;
        if (visual.sprite_path.empty()) visual.sprite_path = find_first_sprite_path(slice);
        if (visual.sprite_path.empty()) throw FormatError("MDL definition has no serialized SPR path");
        if (size >= 0xC4U) {
            visual.simulation.class_specific_c0_raw = read_i32(data, offset + 0xC0U);
            visual.simulation.articulated_contact_interpolation_rate_q12 =
                visual.simulation.class_specific_c0_raw;
            if (visual.runtime_class_id == 16U)
                visual.simulation.energy_generation_capacity_q4 = read_u32(data, offset + 0xC0U);
        }
        if (size >= 0xC8U) {
            visual.simulation.articulated_contact_lift_q12 = read_i32(data, offset + 0xC4U);
            visual.simulation.class_specific_c4_raw = read_u32(data, offset + 0xC4U);
        }
        if (size >= 0xCCU) {
            visual.simulation.class_specific_c8_raw = read_i32(data, offset + 0xC8U);
        }
        if (size >= 0xD0U) {
            visual.simulation.class_specific_cc_raw = read_i32(data, offset + 0xCCU);
            if (visual.runtime_class_id == 3U)
                visual.simulation.articulated_contact_sound_asset_id = read_u32(data, offset + 0xCCU);
        }
        if (size >= 0xDCU) {
            visual.simulation.projected_sound_asset_id_d0 = read_u32(data, offset + 0xD0U);
            visual.simulation.projected_sound_asset_id_d8 = read_u32(data, offset + 0xD8U);
        }
        if (size >= 0xE0U) {
            visual.simulation.movement_collision_radius_q12 = read_i32(data, offset + 0xDCU);
        }
        if (size >= 0x1A4U)
            visual.simulation.attachment_proximity_radius_q12 = read_i32(data, offset + 0x1A0U);
        if (size >= 0x1B8U)
            visual.simulation.health_regeneration_per_step = read_i32(data, offset + 0x1B4U);
        if (size >= 0x1B0U) {
            visual.simulation.projected_sound_asset_id_primary = read_u32(data, offset + 0x1ACU);
        }
        if (size >= 0x270U) {
            visual.simulation.runtime_class_effect_sound_asset_id_26c = read_u32(data, offset + 0x26CU);
            visual.simulation.class13_completion_sound_asset_id =
                visual.simulation.runtime_class_effect_sound_asset_id_26c;
        }
        if (size >= 0x274U) {
            visual.simulation.creation_sound_asset_id_270 = read_u32(data, offset + 0x270U);
        }
        if (size >= 0x278U) {
            visual.simulation.projected_sound_asset_id_secondary = read_u32(data, offset + 0x274U);
        }
        if (size >= 0xCCU) {
            if (visual.runtime_class_id == 1U || visual.runtime_class_id == 2U ||
                visual.runtime_class_id == 17U || visual.runtime_class_id == 18U ||
                visual.runtime_class_id == 19U) {
                visual.simulation.ground_heading_interpolation_distance_q12 =
                    visual.simulation.class_specific_c0_raw;
                visual.simulation.ground_far_heading_error_limit_turn16 =
                    visual.simulation.class_specific_c4_raw & 0xFFFFU;
                visual.simulation.ground_near_heading_error_limit_turn16 =
                    static_cast<uint32_t>(visual.simulation.class_specific_c8_raw) & 0xFFFFU;
            }
            if (visual.runtime_class_id == 14U || visual.runtime_class_id == 15U) {
                visual.simulation.resource_storage_selector =
                    static_cast<uint32_t>(visual.simulation.class_specific_c0_raw);
                visual.simulation.resource_storage_capacity_contribution_q4 =
                    visual.simulation.class_specific_c4_raw;
                visual.simulation.has_resource_storage_capacity_contribution = true;
            }
            if (visual.runtime_class_id == 3U) {
                // V471 ArmyArticulatedRuntime_UpdateSelectedTerrainContact compares the signed
                // steering delta against MDL +0xC8 on both the positive and wrapped-negative path.
                visual.simulation.articulated_contact_steering_limit_turn16 =
                    read_u32(data, offset + 0xC8U) & 0xFFFFU;
            }
            if (visual.runtime_class_id == 14U) {
                // ArmyRuntimeClass_UpdateGridBoundEffectsAndModels shifts MDL +0xC8 left by 24
                // before packing it into FieldGridCell.runtime7C. Only the serialized low byte can
                // survive that operation and is consumed by the connected-region economy pass.
                visual.simulation.resource_extraction_weight_u8 =
                    read_u32(data, offset + 0xC8U) & 0xFFU;
            }
        }
        if (size >= 0xC4U) {
            visual.placement_occupancy_class = read_u32(data, offset + 0xC0U);
            visual.has_placement_occupancy_class =
                model_runtime_class_uses_resource_field_support(visual.runtime_class_id) &&
                visual.placement_occupancy_class < 21U;
            // The class-initialization callback selected for runtime class 13 copies
            // MDL +0xC0 into the root node's primary animated texture subresource.
            if (visual.runtime_class_id == 13U && visual.placement_occupancy_class != 0U) {
                visual.primary_animated_subresource = visual.placement_occupancy_class;
                visual.has_primary_texture_animation = true;
            }
        }
        if (size >= 0x268U) {
            visual.traversal_low_band_index = read_u32(data, offset + 0x260U);
            visual.traversal_high_band_index = read_u32(data, offset + 0x264U);
            // ModelDefinition_CopyTerrainClassValues resolves MDL +0x260 through
            // g_GridInfluenceRadiusOffset before any runtime owns this definition.
            // Stock movers serialize zero at +0xDC; retaining that zero disables
            // ArmyCollision_FindBlockingRuntimeForCurrentUnit entirely. Negative
            // classes retain the serialized radius (buildings use that path).
            // Unsupported custom classes must not emulate retail's out-of-bounds read.
            constexpr std::array<int32_t, 8> footprint_radii_q12{
                1000, 1250, 1500, 1600, 1920, 2240, 2600, 4100};
            if (visual.traversal_low_band_index < footprint_radii_q12.size()) {
                visual.simulation.movement_collision_radius_q12 =
                    footprint_radii_q12[visual.traversal_low_band_index];
                visual.simulation.attachment_proximity_radius_q12 =
                    footprint_radii_q12[visual.traversal_low_band_index];
            }
            visual.has_traversal_profile = visual.traversal_low_band_index < 24U &&
                                           visual.traversal_high_band_index < 7U;
        }
        if (size >= 0x26CU) {
            auto& p = visual.simulation;
            p.ground_slope_threshold_turn16 = p.pitch_min_turn16;
            p.ground_secondary_slope_threshold_turn16 = read_i32(data, offset + 0x268U);
            // ModelDefinition_CopyTerrainClassValues indexes this single table
            // from different bases. Bound custom definitions rather than reading
            // past it; negative terrain classes retain their serialized values.
            constexpr std::array<int32_t, 17> thresholds{
                0,11500,10500,10500,10500,14000,15000,15500,
                500,500,500,10500,10500,10500,12500,13500,14500};
            const int32_t terrain = std::bit_cast<int32_t>(visual.traversal_high_band_index);
            const auto resolve = [&](int64_t index, int32_t& value) {
                if (index >= 0 && index < static_cast<int64_t>(thresholds.size()))
                    value = thresholds[static_cast<size_t>(index)];
            };
            if (terrain >= 0) {
                if (p.placement_contact_kind == 1) resolve(11LL + terrain, p.ground_slope_threshold_turn16);
                else if (p.placement_contact_kind == 4) {
                    resolve(1LL + terrain, p.ground_slope_threshold_turn16);
                    resolve(4LL + terrain, p.ground_secondary_slope_threshold_turn16);
                } else {
                    resolve(4LL + terrain, p.water_damage_threshold_q12);
                    resolve(7LL + terrain, p.ground_slope_threshold_turn16);
                    resolve(10LL + terrain, p.ground_secondary_slope_threshold_turn16);
                }
            }
        }
        // ModelRuntimeSlotClassInit_ApplyDefinitionTextureAnimationIndices reads the exact
        // MDL +0x1B8/+0x1BC pair. Runtime class 2 consumes both selectors as independent tank
        // track U channels. Other classes retain the values as evidence but do not animate them.
        if (size >= 0x1C0U && visual.runtime_class_id == 2U) {
            visual.primary_animated_subresource = read_u32(data, offset + 0x1B8U);
            visual.secondary_animated_subresource = read_u32(data, offset + 0x1BCU);
            visual.has_primary_texture_animation = visual.primary_animated_subresource != 0U;
            visual.has_secondary_texture_animation = visual.secondary_animated_subresource != 0U;
        }
        if (size >= 0x168U) {
            visual.simulation.first_creation_notification_movie_id = read_u32(data, offset + 0x160U);
            visual.simulation.repeat_creation_notification_movie_id = read_u32(data, offset + 0x164U);
        }
        if (size >= 0x180U) {
            visual.simulation.timed_shot_definition_id = read_i32(data, offset + 0x168U);
            visual.simulation.timed_shot_interval_ticks = read_u32(data, offset + 0x16CU);
            visual.simulation.timed_shot_random_ticks = read_u32(data, offset + 0x170U);
            visual.simulation.timed_effect_id = read_u32(data, offset + 0x174U);
            visual.simulation.timed_effect_interval_ticks = read_u32(data, offset + 0x178U);
            visual.simulation.timed_effect_random_ticks = read_u32(data, offset + 0x17CU);
            visual.timed_shot_definition_id = visual.simulation.timed_shot_definition_id;
            visual.timed_shot_interval_ticks = visual.simulation.timed_shot_interval_ticks;
            visual.timed_shot_random_ticks = visual.simulation.timed_shot_random_ticks;
            visual.timed_water_effect_id = visual.simulation.timed_water_effect_id;
            visual.timed_effect_id = visual.simulation.timed_effect_id;
            visual.timed_effect_interval_ticks = visual.simulation.timed_effect_interval_ticks;
            visual.timed_effect_random_ticks = visual.simulation.timed_effect_random_ticks;
        }
        if (size >= 0x1C4U) {
            visual.technology_unlock_id = read_u32(data, offset + 0x1C0U);
        }
        if (size >= 0x1CCU) visual.simulation.timed_effects_require_research = read_u32(data, offset + 0x1C8U);
        if (size >= 0x1C8U) {
            visual.linked_technology_id = read_u32(data, offset + 0x1C4U);
        }
        if (size >= 0x238U) {
            for (uint32_t slot = 0U; slot < visual.technology_panel_ids.size(); ++slot) {
                visual.technology_panel_ids[slot] = read_u32(data, offset + 0x1C8U + slot * 4U);
            }
        }
        if (size >= 0x250U) {
            for (uint32_t slot = 0U; slot < visual.faction_technology_variant_model_ids.size(); ++slot) {
                visual.faction_technology_variant_model_ids[slot] =
                    read_u32(data, offset + 0x238U + slot * 4U);
            }
        }
        if (size >= 0x260U) {
            visual.simulation.damage_effect_health_threshold_percent =
                read_i32(data, offset + 0x250U);
            visual.simulation.damage_effect_id = read_u32(data, offset + 0x254U);
            visual.simulation.damage_effect_min_cooldown_ticks = read_u32(data, offset + 0x258U);
            visual.simulation.damage_effect_random_cooldown_ticks = read_u32(data, offset + 0x25CU);
            for (auto& node : visual.hierarchy_nodes) node.simulation = visual.simulation;
        }
        // Several runtime-only MDL fields are serialized after the root hierarchy pointer in the
        // record. Re-synchronize every node after the complete definition has been parsed so linked
        // runtime roots carry their own timed emitters, damage profile, fog and render flags.
        for (auto& node : visual.hierarchy_nodes) {
            node.simulation = visual.simulation;
            node.research_technology_ids = visual.technology_panel_ids;
        }
        result.push_back(std::move(visual));
        offset += size;
    }
    return result;
}

} // namespace thandor
