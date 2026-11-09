#include "thandor/level.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace thandor {
namespace {
constexpr uint32_t kLevelMagic = 0x0076656CU; // lev\0
constexpr std::size_t kPlacementSize = 0x20;
constexpr std::array<std::size_t, 10> kPathFields{
    0xB0, 0xB4, 0xB8, 0xBC, 0xC0, 0xC4, 0xC8, 0xCC, 0xD0, 0xD4
};
constexpr std::array<std::size_t, 8> kLightingColorOffsets{
    0x30C, 0x308, 0x304, 0x300, 0x2F8, 0x2F4, 0x2E8, 0x2E4
};

std::size_t path_capacity(const std::array<uint32_t, 10>& offsets, std::size_t index,
                          std::size_t fixed_size) {
    const auto start = offsets[index];
    std::size_t end = fixed_size;
    for (const auto candidate : offsets) {
        if (candidate > start) end = std::min<std::size_t>(end, candidate);
    }
    return end > start ? end - start : 0;
}

} // namespace


uint32_t LevelScheduledCondition::operand(std::size_t index) const {
    if (index >= 3U) throw std::out_of_range("scheduled-condition operand index");
    const std::size_t base = 3U + index * 4U;
    return static_cast<uint32_t>(payload_bytes[base]) |
        (static_cast<uint32_t>(payload_bytes[base + 1U]) << 8U) |
        (static_cast<uint32_t>(payload_bytes[base + 2U]) << 16U) |
        (static_cast<uint32_t>(payload_bytes[base + 3U]) << 24U);
}

void LevelScheduledCondition::set_operand(std::size_t index, uint32_t value) {
    if (index >= 3U) throw std::out_of_range("scheduled-condition operand index");
    const std::size_t base = 3U + index * 4U;
    payload_bytes[base] = static_cast<uint8_t>(value);
    payload_bytes[base + 1U] = static_cast<uint8_t>(value >> 8U);
    payload_bytes[base + 2U] = static_cast<uint8_t>(value >> 16U);
    payload_bytes[base + 3U] = static_cast<uint8_t>(value >> 24U);
}

LevelAsset LevelAsset::parse(std::span<const std::byte> data) {
    if (data.size() < 0x200 || read_u32(data, 0) != kLevelMagic) throw FormatError("invalid LEV magic");
    LevelAsset level;
    const auto count = read_u32(data, 0xD8);
    level.placement_offset_ = read_u32(data, 0xDC);
    if (level.placement_offset_ < 0x200 || level.placement_offset_ > data.size()) throw FormatError("invalid LEV placement offset");
    const uint64_t expected = static_cast<uint64_t>(level.placement_offset_) + static_cast<uint64_t>(count) * kPlacementSize;
    if (expected != data.size()) throw FormatError("LEV placement count does not match file size");
    level.fixed_image_.assign(data.begin(), data.begin() + level.placement_offset_);
    for (std::size_t i = 0; i < level.path_offsets_.size(); ++i) {
        level.path_offsets_[i] = read_u32(data, kPathFields[i]);
        if (level.path_offsets_[i] >= level.placement_offset_) throw FormatError("LEV path offset exceeds fixed image");
    }
    level.placements_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const auto offset = static_cast<std::size_t>(level.placement_offset_) + i * kPlacementSize;
        LevelPlacement p;
        p.army_asset_id = read_u32(data, offset + 0x00);
        p.faction_index = read_u32(data, offset + 0x04);
        p.world_y_q12 = read_i32(data, offset + 0x08);
        p.world_x_q12 = read_i32(data, offset + 0x0C);
        p.orientation_turn16 = read_u32(data, offset + 0x10);
        for (std::size_t j = 0; j < 3; ++j) p.preserved_tail_dwords[j] = read_u32(data, offset + 0x14 + j * 4);
        level.placements_.push_back(p);
    }
    level.read_paths();
    level.read_lighting();
    level.read_resource_lists();
    return level;
}

LevelAsset LevelAsset::from_template(const LevelAsset& source, std::string_view new_level_path,
                                     bool preserve_placements) {
    LevelAsset result = source;
    if (!preserve_placements) result.placements_.clear();
    result.paths_.level = normalize_asset_path(new_level_path);
    // InGameRuntime_InitializeNewSession unconditionally prepares, opens, and advances the
    // level movie before loading terrain and placements. Every shipped skirmish LEV carries a
    // non-empty flm\\{erde,eis,wüste,lava} base. Preserve the compatibility shell value.
    return result;
}

void LevelAsset::read_paths() {
    std::array<std::string*, 10> destinations{
        &paths_.level, &paths_.ground_textures, &paths_.surface_textures, &paths_.sky_textures,
        &paths_.army_textures, &paths_.shot_textures, &paths_.effect_textures,
        &paths_.ending_movie, &paths_.sounds, &paths_.technology
    };
    for (std::size_t i = 0; i < destinations.size(); ++i) {
        const auto capacity = path_capacity(path_offsets_, i, fixed_image_.size());
        *destinations[i] = normalize_asset_path(read_utf16le_z(fixed_image_, path_offsets_[i], capacity));
    }
}

void LevelAsset::read_lighting() {
    if (fixed_image_.size() <= kLightingColorOffsets.front() + 4) return;
    lighting_.color_13c = read_u32(fixed_image_, kLightingColorOffsets[0]);
    lighting_.color_138 = read_u32(fixed_image_, kLightingColorOffsets[1]);
    lighting_.color_134 = read_u32(fixed_image_, kLightingColorOffsets[2]);
    lighting_.color_130 = read_u32(fixed_image_, kLightingColorOffsets[3]);
    lighting_.ramp_12c = read_u32(fixed_image_, kLightingColorOffsets[4]);
    lighting_.color_128 = read_u32(fixed_image_, kLightingColorOffsets[5]);
    lighting_.ramp_124 = read_u32(fixed_image_, kLightingColorOffsets[6]);
    lighting_.base_color = read_u32(fixed_image_, kLightingColorOffsets[7]);
    if (fixed_image_.size() >= 0x2F4U) {
        // V537 LevelAssetRuntimeTail90 separates the packed light/field origin pair at +0x2E0
        // from the packed field-region dimensions at +0x2F0. Both recovered level loaders,
        // InGameLevelRuntime_LoadResourcesAfterDefaultResetCf and
        // InGameLevelRuntime_LoadResourcesAfterExternalTablesCf, feed this runtime tail; +0x2E0
        // is forwarded to WorldRuntime_RecomputeFieldRegionNormalsAndLighting as azimuth/elevation.
        const auto packed_direction = read_u32(fixed_image_, 0x2E0U);
        lighting_.azimuth = static_cast<uint16_t>(packed_direction & 0xFFFFU);
        lighting_.elevation = static_cast<uint16_t>(packed_direction >> 16U);
        const auto packed_dimensions = read_u32(fixed_image_, 0x2F0U);
        lighting_.field_region_width = static_cast<uint16_t>(packed_dimensions & 0xFFFFU);
        lighting_.field_region_height = static_cast<uint16_t>(packed_dimensions >> 16U);
        lighting_.cycle_duration_ticks = read_u32(fixed_image_, 0x2ECU);
    }
    if (fixed_image_.size() >= 0x350U) {
        const auto alternate_direction = read_u32(fixed_image_, 0x328U);
        lighting_.alternate_azimuth = static_cast<uint16_t>(alternate_direction & 0xFFFFU);
        lighting_.alternate_elevation = static_cast<uint16_t>(alternate_direction >> 16U);
        const auto alternate_dimensions = read_u32(fixed_image_, 0x32CU);
        lighting_.alternate_field_region_width = static_cast<uint16_t>(alternate_dimensions & 0xFFFFU);
        lighting_.alternate_field_region_height = static_cast<uint16_t>(alternate_dimensions >> 16U);
        lighting_.alternate_base_color = read_u32(fixed_image_, 0x330U);
        lighting_.alternate_ramp_124 = read_u32(fixed_image_, 0x334U);
        lighting_.alternate_color_128 = read_u32(fixed_image_, 0x338U);
        lighting_.alternate_ramp_12c = read_u32(fixed_image_, 0x33CU);
        lighting_.alternate_color_130 = read_u32(fixed_image_, 0x340U);
        lighting_.alternate_color_134 = read_u32(fixed_image_, 0x344U);
        lighting_.alternate_color_138 = read_u32(fixed_image_, 0x348U);
        lighting_.alternate_color_13c = read_u32(fixed_image_, 0x34CU);
    }
}

void LevelAsset::read_resource_lists() {
    resource_paths_.clear();
    model_resource_paths_.clear();
    army_resource_paths_.clear();
    effect_resource_paths_.clear();
    shot_resource_paths_.clear();
    for (std::size_t pair = 0; pair < 4; ++pair) {
        const auto count_offset = 0xE0 + pair * 8;
        const auto list_count = read_u32(fixed_image_, count_offset);
        const auto list_offset = read_u32(fixed_image_, count_offset + 4);
        if (list_count == 0) continue;
        const uint64_t end = static_cast<uint64_t>(list_offset) + static_cast<uint64_t>(list_count) * 0x40ULL;
        if (list_offset < 0x100 || end > fixed_image_.size()) throw FormatError("LEV resource path list exceeds fixed image");
        for (uint32_t i = 0; i < list_count; ++i) {
            auto path = normalize_asset_path(read_utf16le_z(fixed_image_, list_offset + i * 0x40, 0x40));
            if (!path.empty()) {
                if (pair == 1U) model_resource_paths_.push_back(path);
                if (pair == 0U) army_resource_paths_.push_back(path);
                if (pair == 2U) effect_resource_paths_.push_back(path);
                if (pair == 3U) shot_resource_paths_.push_back(path);
                resource_paths_.push_back(std::move(path));
            }
        }
    }
    std::sort(resource_paths_.begin(), resource_paths_.end());
    resource_paths_.erase(std::unique(resource_paths_.begin(), resource_paths_.end()), resource_paths_.end());
}

void LevelAsset::write_paths(std::span<std::byte> data) const {
    const std::array<std::string_view, 10> values{
        paths_.level, paths_.ground_textures, paths_.surface_textures, paths_.sky_textures,
        paths_.army_textures, paths_.shot_textures, paths_.effect_textures,
        paths_.ending_movie, paths_.sounds, paths_.technology
    };
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto capacity = path_capacity(path_offsets_, i, placement_offset_);
        if (capacity < 2) throw FormatError("LEV path field has no writable capacity");
        std::string windows_path(values[i]);
        std::replace(windows_path.begin(), windows_path.end(), '/', '\\');
        write_utf16le_fixed(data, path_offsets_[i], capacity, windows_path);
    }
}

void LevelAsset::write_lighting(std::span<std::byte> data) const {
    if (data.size() <= kLightingColorOffsets.front() + 4) return;
    const std::array<uint32_t, 8> values{
        lighting_.color_13c, lighting_.color_138, lighting_.color_134, lighting_.color_130,
        lighting_.ramp_12c, lighting_.color_128, lighting_.ramp_124, lighting_.base_color
    };
    for (std::size_t i = 0; i < values.size(); ++i) write_u32(data, kLightingColorOffsets[i], values[i]);
    write_u32(data, 0x2E0U, static_cast<uint32_t>(lighting_.azimuth) |
                                  static_cast<uint32_t>(lighting_.elevation) << 16U);
    write_u32(data, 0x2ECU, lighting_.cycle_duration_ticks);
    write_u32(data, 0x2F0U, static_cast<uint32_t>(lighting_.field_region_width) |
                                  static_cast<uint32_t>(lighting_.field_region_height) << 16U);
    if (data.size() >= 0x350U) {
        write_u32(data, 0x328U, static_cast<uint32_t>(lighting_.alternate_azimuth) |
                                      static_cast<uint32_t>(lighting_.alternate_elevation) << 16U);
        write_u32(data, 0x32CU, static_cast<uint32_t>(lighting_.alternate_field_region_width) |
                                      static_cast<uint32_t>(lighting_.alternate_field_region_height) << 16U);
        write_u32(data, 0x330U, lighting_.alternate_base_color);
        write_u32(data, 0x334U, lighting_.alternate_ramp_124);
        write_u32(data, 0x338U, lighting_.alternate_color_128);
        write_u32(data, 0x33CU, lighting_.alternate_ramp_12c);
        write_u32(data, 0x340U, lighting_.alternate_color_130);
        write_u32(data, 0x344U, lighting_.alternate_color_134);
        write_u32(data, 0x348U, lighting_.alternate_color_138);
        write_u32(data, 0x34CU, lighting_.alternate_color_13c);
    }
}

uint32_t LevelAsset::compatibility_alias_base() const {
    if (fixed_image_.size() < 0x174U) throw FormatError("LEV fixed image lacks compatibility alias field +0x170");
    return read_u32(fixed_image_, 0x170U);
}

void LevelAsset::set_compatibility_alias_base(uint32_t value) {
    if (fixed_image_.size() < 0x174U) throw FormatError("LEV fixed image lacks compatibility alias field +0x170");
    write_u32(fixed_image_, 0x170U, value);
}

namespace {
uint32_t decode_ascii_count(std::span<const std::byte> data, std::size_t offset,
                            const char* field_name) {
    if (data.size() < offset + 4U) throw FormatError(std::string("LEV fixed image lacks ") + field_name);
    const uint32_t value = read_u32(data, offset);
    if (value < static_cast<uint32_t>('0') || value > static_cast<uint32_t>('9')) {
        throw FormatError(std::string(field_name) + " is not an ASCII player-count digit");
    }
    return value - static_cast<uint32_t>('0');
}
}

uint32_t LevelAsset::frontend_assignable_faction_count() const {
    if (fixed_image_.size() < 0x314U) throw FormatError("LEV fixed image lacks frontend count +0x310");
    return read_u32(fixed_image_, 0x310U);
}

uint32_t LevelAsset::frontend_active_faction_count() const {
    if (fixed_image_.size() < 0x318U) throw FormatError("LEV fixed image lacks frontend count +0x314");
    return read_u32(fixed_image_, 0x314U);
}

uint32_t LevelAsset::relation_ui_flags() const {
    if (fixed_image_.size() < 0x31CU) throw FormatError("LEV fixed image lacks relation UI flags +0x318");
    return read_u32(fixed_image_, 0x318U);
}

uint32_t LevelAsset::relation_state8_faction_group_masks() const {
    if (fixed_image_.size() < 0x320U) throw FormatError("LEV fixed image lacks relation-state-8 groups +0x31C");
    return read_u32(fixed_image_, 0x31CU);
}

uint32_t LevelAsset::relation_state4_faction_group_masks() const {
    if (fixed_image_.size() < 0x324U) throw FormatError("LEV fixed image lacks relation-state-4 groups +0x320");
    return read_u32(fixed_image_, 0x320U);
}

void LevelAsset::set_initial_relation_setup(uint32_t ui_flags, uint32_t state8_group_masks,
                                             uint32_t state4_group_masks) {
    if (fixed_image_.size() < 0x324U)
        throw FormatError("LEV fixed image lacks initial relation setup through +0x320");
    write_u32(fixed_image_, 0x318U, ui_flags);
    write_u32(fixed_image_, 0x31CU, state8_group_masks);
    write_u32(fixed_image_, 0x320U, state4_group_masks);
}

uint32_t LevelAsset::embedded_minimum_player_count() const {
    return decode_ascii_count(fixed_image_, 0x140U, "embedded scenario minimum count +0x140");
}

uint32_t LevelAsset::embedded_maximum_player_count() const {
    return decode_ascii_count(fixed_image_, 0x148U, "embedded scenario maximum count +0x148");
}

void LevelAsset::set_frontend_faction_counts(uint32_t assignable_count,
                                              uint32_t active_count) {
    if (assignable_count == 0U || assignable_count > active_count || active_count > 7U) {
        throw FormatError("frontend faction counts must satisfy 1 <= human <= active <= 7");
    }
    if (fixed_image_.size() < 0x318U) {
        throw FormatError("LEV fixed image lacks the frontend faction-count fields through +0x314");
    }
    write_u32(fixed_image_, 0x310U, assignable_count);
    write_u32(fixed_image_, 0x314U, active_count);
}

void LevelAsset::set_embedded_player_count_range(uint32_t minimum_count,
                                                  uint32_t maximum_count) {
    if (minimum_count == 0U || minimum_count > maximum_count || maximum_count > 7U) {
        throw FormatError("embedded player-count range must satisfy 1 <= minimum <= maximum <= 7");
    }
    if (fixed_image_.size() < 0x14CU) {
        throw FormatError("LEV fixed image lacks embedded scenario player-count digits");
    }
    write_u32(fixed_image_, 0x140U, static_cast<uint32_t>('0') + minimum_count);
    write_u32(fixed_image_, 0x148U, static_cast<uint32_t>('0') + maximum_count);
}

void LevelAsset::set_skirmish_player_count(uint32_t count) {
    set_frontend_faction_counts(count, count);
    set_embedded_player_count_range(count, count);
}

namespace {
constexpr std::size_t kScheduledConditionImageOffset = 0x380U;
constexpr std::size_t kScheduledConditionRecordSize = 0x10U;
constexpr std::size_t kEndConditionTriggerImageOffset = 0x780U;
constexpr std::size_t kEndConditionTriggerRecordSize = 0x08U;
}

LevelScheduledCondition LevelAsset::scheduled_condition(uint32_t index) const {
    if (index >= kScheduledConditionCount) throw FormatError("scheduled-condition index must be 0..63");
    const std::size_t offset = kScheduledConditionImageOffset +
        static_cast<std::size_t>(index) * kScheduledConditionRecordSize;
    if (fixed_image_.size() < offset + kScheduledConditionRecordSize)
        throw FormatError("LEV fixed image lacks scheduled-condition image");
    LevelScheduledCondition condition;
    condition.status_and_kind = static_cast<uint8_t>(fixed_image_[offset]);
    for (std::size_t i = 0; i < condition.payload_bytes.size(); ++i)
        condition.payload_bytes[i] = static_cast<uint8_t>(fixed_image_[offset + 1U + i]);
    return condition;
}

void LevelAsset::set_scheduled_condition(uint32_t index, const LevelScheduledCondition& condition) {
    if (index >= kScheduledConditionCount) throw FormatError("scheduled-condition index must be 0..63");
    const std::size_t offset = kScheduledConditionImageOffset +
        static_cast<std::size_t>(index) * kScheduledConditionRecordSize;
    if (fixed_image_.size() < offset + kScheduledConditionRecordSize)
        throw FormatError("LEV fixed image lacks scheduled-condition image");
    // Satisfied bit is runtime state. Stock serialized LEVs keep it clear; never persist a
    // condition as already satisfied when the editor changes a record.
    fixed_image_[offset] = static_cast<std::byte>(condition.status_and_kind & 0xFEU);
    for (std::size_t i = 0; i < condition.payload_bytes.size(); ++i)
        fixed_image_[offset + 1U + i] = static_cast<std::byte>(condition.payload_bytes[i]);
}

LevelEndConditionTrigger LevelAsset::end_condition_trigger(uint32_t index) const {
    if (index >= kEndConditionTriggerCount) throw FormatError("end-condition trigger index must be 0..15");
    const std::size_t offset = kEndConditionTriggerImageOffset +
        static_cast<std::size_t>(index) * kEndConditionTriggerRecordSize;
    if (fixed_image_.size() < offset + kEndConditionTriggerRecordSize)
        throw FormatError("LEV fixed image lacks end-condition trigger image");
    LevelEndConditionTrigger trigger;
    const auto* p = fixed_image_.data() + static_cast<std::ptrdiff_t>(offset);
    trigger.state_flags = static_cast<uint8_t>(p[0]);
    trigger.movie_variant_selector = static_cast<uint8_t>(p[1]);
    trigger.skip_army_disable_when_one = static_cast<uint8_t>(p[2]);
    trigger.reserved03 = static_cast<uint8_t>(p[3]);
    trigger.faction_runtime_index = static_cast<uint8_t>(p[4]);
    trigger.end_movie_selection_index = static_cast<uint8_t>(p[5]);
    trigger.condition_index = static_cast<uint8_t>(p[6]);
    trigger.reserved07 = static_cast<uint8_t>(p[7]);
    return trigger;
}

void LevelAsset::set_end_condition_trigger(uint32_t index, const LevelEndConditionTrigger& trigger) {
    if (index >= kEndConditionTriggerCount) throw FormatError("end-condition trigger index must be 0..15");
    if (trigger.condition_index >= kScheduledConditionCount) throw FormatError("trigger condition index exceeds 63");
    if (trigger.faction_runtime_index >= 8U) throw FormatError("trigger faction must be 0..7");
    if (trigger.end_movie_selection_index >= 8U) throw FormatError("trigger outcome must be 0..7");
    const std::size_t offset = kEndConditionTriggerImageOffset +
        static_cast<std::size_t>(index) * kEndConditionTriggerRecordSize;
    if (fixed_image_.size() < offset + kEndConditionTriggerRecordSize)
        throw FormatError("LEV fixed image lacks end-condition trigger image");
    // Bit 1 is runtime 'processed' state and must not be serialized as pre-fired.
    fixed_image_[offset + 0U] = static_cast<std::byte>(trigger.state_flags & 1U);
    fixed_image_[offset + 1U] = static_cast<std::byte>(trigger.movie_variant_selector);
    fixed_image_[offset + 2U] = static_cast<std::byte>(trigger.skip_army_disable_when_one);
    fixed_image_[offset + 3U] = static_cast<std::byte>(trigger.reserved03);
    fixed_image_[offset + 4U] = static_cast<std::byte>(trigger.faction_runtime_index);
    fixed_image_[offset + 5U] = static_cast<std::byte>(trigger.end_movie_selection_index);
    fixed_image_[offset + 6U] = static_cast<std::byte>(trigger.condition_index);
    fixed_image_[offset + 7U] = static_cast<std::byte>(trigger.reserved07);
}

uint32_t LevelAsset::active_scheduled_condition_count() const {
    uint32_t count = 0U;
    for (uint32_t i = 0; i < kScheduledConditionCount; ++i)
        if ((static_cast<uint8_t>(scheduled_condition(i).kind()) & 0xFEU) != 0U) ++count;
    return count;
}

uint32_t LevelAsset::active_end_condition_trigger_count() const {
    uint32_t count = 0U;
    for (uint32_t i = 0; i < kEndConditionTriggerCount; ++i)
        if (end_condition_trigger(i).active()) ++count;
    return count;
}

LevelFactionCameraStart LevelAsset::faction_camera_start(uint32_t faction_index) const {
    if (faction_index == 0U || faction_index > kFactionCameraStartCount) {
        throw FormatError("faction camera index must be in the range 1..7");
    }
    const std::size_t offset = 0x200U + static_cast<std::size_t>(faction_index - 1U) * 0x20U;
    if (fixed_image_.size() < offset + 0x14U) {
        throw FormatError("LEV fixed image lacks faction camera record");
    }
    LevelFactionCameraStart camera;
    camera.position_x_q12 = read_i32(fixed_image_, offset + 0x00U);
    camera.position_y_q12 = read_i32(fixed_image_, offset + 0x04U);
    camera.position_z_q12 = read_i32(fixed_image_, offset + 0x08U);
    camera.motion_magnitude_q12 = read_u32(fixed_image_, offset + 0x0CU);
    const uint32_t packed_angles = read_u32(fixed_image_, offset + 0x10U);
    camera.heading_turn16 = static_cast<uint16_t>(packed_angles & 0xFFFFU);
    camera.pitch_turn16 = static_cast<int16_t>(packed_angles >> 16U);
    return camera;
}

void LevelAsset::set_faction_camera_start(uint32_t faction_index,
                                          const LevelFactionCameraStart& camera) {
    if (faction_index == 0U || faction_index > kFactionCameraStartCount) {
        throw FormatError("faction camera index must be in the range 1..7");
    }
    const std::size_t offset = 0x200U + static_cast<std::size_t>(faction_index - 1U) * 0x20U;
    if (fixed_image_.size() < offset + 0x14U) {
        throw FormatError("LEV fixed image lacks faction camera record");
    }
    write_i32(fixed_image_, offset + 0x00U, camera.position_x_q12);
    write_i32(fixed_image_, offset + 0x04U, camera.position_y_q12);
    write_i32(fixed_image_, offset + 0x08U, camera.position_z_q12);
    write_u32(fixed_image_, offset + 0x0CU,
              std::max<uint32_t>(camera.motion_magnitude_q12, 0x400U));
    const uint32_t packed_angles = static_cast<uint32_t>(camera.heading_turn16) |
        (static_cast<uint32_t>(static_cast<uint16_t>(camera.pitch_turn16)) << 16U);
    write_u32(fixed_image_, offset + 0x10U, packed_angles);
}

uint32_t LevelAsset::faction_xenite_q4(uint32_t faction_index) const {
    if (faction_index < 1U || faction_index > kFactionCameraStartCount)
        throw FormatError("faction resource index must be 1..7");
    const std::size_t base = 0x200U + static_cast<std::size_t>(faction_index - 1U) * 0x20U;
    if (fixed_image_.size() < base + 0x20U) throw FormatError("LEV faction resource record is truncated");
    return read_u32(fixed_image_, base + 0x14U);
}

uint32_t LevelAsset::faction_tritium_q4(uint32_t faction_index) const {
    if (faction_index < 1U || faction_index > kFactionCameraStartCount)
        throw FormatError("faction resource index must be 1..7");
    const std::size_t base = 0x200U + static_cast<std::size_t>(faction_index - 1U) * 0x20U;
    if (fixed_image_.size() < base + 0x20U) throw FormatError("LEV faction resource record is truncated");
    return read_u32(fixed_image_, base + 0x18U);
}

void LevelAsset::set_faction_starting_resources_q4(uint32_t faction_index,
                                                    uint32_t building_resource_q4,
                                                    uint32_t energy_resource_q4) {
    if (faction_index < 1U || faction_index > kFactionCameraStartCount)
        throw FormatError("faction resource index must be 1..7");
    const std::size_t base = 0x200U + static_cast<std::size_t>(faction_index - 1U) * 0x20U;
    if (fixed_image_.size() < base + 0x20U) throw FormatError("LEV faction resource record is truncated");
    write_u32(fixed_image_, base + 0x14U, building_resource_q4);
    write_u32(fixed_image_, base + 0x18U, energy_resource_q4);
}

LevelPlayerSlotRecord LevelAsset::faction_player_slot(uint32_t faction_index) const {
    if (faction_index < 1U || faction_index > kFactionCameraStartCount)
        throw FormatError("faction player-slot index must be 1..7");
    const std::size_t base = 0x200U + static_cast<std::size_t>(faction_index - 1U) * 0x20U;
    if (fixed_image_.size() < base + 0x20U) throw FormatError("LEV faction player-slot record is truncated");
    LevelPlayerSlotRecord slot;
    slot.start_camera_x_q12 = read_i32(fixed_image_, base + 0x00U);
    slot.start_camera_y_q12 = read_i32(fixed_image_, base + 0x04U);
    slot.start_camera_z_q12 = read_i32(fixed_image_, base + 0x08U);
    slot.start_camera_magnitude_q12 = read_u32(fixed_image_, base + 0x0CU);
    slot.packed_heading_low16_pitch_high16 = read_u32(fixed_image_, base + 0x10U);
    slot.start_xenite_q4 = read_u32(fixed_image_, base + 0x14U);
    slot.start_tritium_q4 = read_u32(fixed_image_, base + 0x18U);
    slot.ai_class_or_mode = read_u32(fixed_image_, base + 0x1CU);
    return slot;
}

void LevelAsset::set_faction_player_slot(uint32_t faction_index,
                                         const LevelPlayerSlotRecord& slot) {
    if (faction_index < 1U || faction_index > kFactionCameraStartCount)
        throw FormatError("faction player-slot index must be 1..7");
    const std::size_t base = 0x200U + static_cast<std::size_t>(faction_index - 1U) * 0x20U;
    if (fixed_image_.size() < base + 0x20U) throw FormatError("LEV faction player-slot record is truncated");
    write_i32(fixed_image_, base + 0x00U, slot.start_camera_x_q12);
    write_i32(fixed_image_, base + 0x04U, slot.start_camera_y_q12);
    write_i32(fixed_image_, base + 0x08U, slot.start_camera_z_q12);
    write_u32(fixed_image_, base + 0x0CU, slot.start_camera_magnitude_q12);
    write_u32(fixed_image_, base + 0x10U, slot.packed_heading_low16_pitch_high16);
    write_u32(fixed_image_, base + 0x14U, slot.start_xenite_q4);
    write_u32(fixed_image_, base + 0x18U, slot.start_tritium_q4);
    write_u32(fixed_image_, base + 0x1CU, slot.ai_class_or_mode);
}

uint32_t LevelAsset::faction_graphics_family_offset_raw(uint32_t faction_index) const {
    return faction_player_slot(faction_index).ai_class_or_mode;
}

void LevelAsset::set_faction_graphics_family_offset_raw(uint32_t faction_index, uint32_t value) {
    if (faction_index < 1U || faction_index > kFactionCameraStartCount)
        throw FormatError("faction AI class/mode index must be 1..7");
    const std::size_t base = 0x200U + static_cast<std::size_t>(faction_index - 1U) * 0x20U;
    if (fixed_image_.size() < base + 0x20U) throw FormatError("LEV faction AI class/mode record is truncated");
    write_u32(fixed_image_, base + 0x1CU, value);
}

uint32_t LevelAsset::faction_graphics_family_selector(uint32_t faction_index) const {
    if (faction_index == 0U) return 0U;
    if (faction_index > kFactionCameraStartCount)
        throw FormatError("faction graphics-family index must be 0..7");
    // Intentionally use uint32 wraparound.  Retail does a 32-bit ADD and the
    // frontend stores negative family offsets (for example 4 + -2 => army2).
    return resolve_faction_graphics_family(faction_index, faction_graphics_family_offset_raw(faction_index));
}

void LevelAsset::add_placement(LevelPlacement placement) {
    if (placement.faction_index > 255) throw FormatError("faction index exceeds byte-sized game domain");
    placements_.push_back(placement);
}

void LevelAsset::erase_placement(std::size_t index) {
    if (index >= placements_.size()) throw std::out_of_range("LEV placement index");
    placements_.erase(placements_.begin() + static_cast<std::ptrdiff_t>(index));
}

std::vector<std::byte> LevelAsset::serialize() const {
    const uint64_t size64 = static_cast<uint64_t>(placement_offset_) + placements_.size() * kPlacementSize;
    if (size64 > std::numeric_limits<uint32_t>::max()) throw FormatError("LEV exceeds 32-bit allocation size");
    std::vector<std::byte> data(static_cast<std::size_t>(size64));
    std::copy(fixed_image_.begin(), fixed_image_.end(), data.begin());
    write_u32(data, 0, kLevelMagic);
    write_u32(data, 4, static_cast<uint32_t>(data.size()));
    write_u32(data, 0xD8, static_cast<uint32_t>(placements_.size()));
    write_u32(data, 0xDC, placement_offset_);
    write_paths(data);
    write_lighting(data);
    for (std::size_t i = 0; i < placements_.size(); ++i) {
        const auto offset = static_cast<std::size_t>(placement_offset_) + i * kPlacementSize;
        const auto& p = placements_[i];
        write_u32(data, offset + 0x00, p.army_asset_id);
        write_u32(data, offset + 0x04, p.faction_index);
        write_i32(data, offset + 0x08, p.world_y_q12);
        write_i32(data, offset + 0x0C, p.world_x_q12);
        write_u32(data, offset + 0x10, p.orientation_turn16);
        for (std::size_t j = 0; j < 3; ++j) write_u32(data, offset + 0x14 + j * 4, p.preserved_tail_dwords[j]);
    }
    return data;
}

} // namespace thandor
