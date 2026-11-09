#pragma once

#include "thandor/field.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace thandor {

// InGameWorldInput_UpdateDragSelectionAndCameraCf updates the live placement heading from
// horizontal pointer displacement with exactly deltaX * 0x40 and wraps to turn16. Preview and
// committed placement both consume the same heading global in the retail executable.
constexpr uint32_t placement_heading_after_pointer_delta(uint32_t heading_turn16,
                                                          int32_t delta_x_pixels) noexcept {
    const int64_t updated = static_cast<int64_t>(heading_turn16 & 0xFFFFU) +
        static_cast<int64_t>(delta_x_pixels) * 0x40LL;
    return static_cast<uint32_t>(updated) & 0xFFFFU;
}

struct LevelPlacement {
    uint32_t army_asset_id{};
    uint32_t faction_index{};
    int32_t world_y_q12{};
    int32_t world_x_q12{};
    uint32_t orientation_turn16{};
    // No stock placement uses these dwords, and game.exe does not assign them a role in the
    // placement walk. Preserve them exactly rather than inventing command or route semantics.
    std::array<uint32_t, 3> preserved_tail_dwords{};

    // LEV +0x08 is the game world-Y argument and +0x0C is world-X. The game model
    // transform writes world-Y to render X and world-X to render Y, so the editor must use
    // render_x/render_y rather than displaying the serialized pair directly.
    float world_x() const noexcept { return q12_to_float(world_x_q12); }
    float world_y() const noexcept { return q12_to_float(world_y_q12); }
    float render_x() const noexcept { return q12_to_float(world_y_q12); }
    float render_y() const noexcept { return q12_to_float(world_x_q12); }
    void set_render_position_q12(int32_t render_x_q12, int32_t render_y_q12) noexcept {
        world_y_q12 = render_x_q12;
        world_x_q12 = render_y_q12;
    }
};



enum class LevelScheduledConditionKind : uint8_t {
    NoneOrUnused = 0x00,
    NoActiveEntityWithDefinition = 0x02,
    NoActiveEntityWithDefinitionAndClassCommandGroupA = 0x04,
    NoActiveEntityWithDefinitionAndRuntimeId = 0x06,
    FactionInactiveOrRelationAtLeast8 = 0x08,
    XeniteCurrentAtLeast = 0x0A,
    TritiumCurrentAtLeast = 0x0C,
    ActiveArmyScaleValueAtLeast = 0x0E,
    MatchingDefinitionAndRuntimeIdActiveEntityCountAtLeast = 0x10,
    FactionTerrainOccupancyPercentAtLeast = 0x12,
    CountdownElapsed = 0x14,
    XeniteStorageLimitAtMost0FA0 = 0x16,
    NoActiveEntityWithClassIdOutsideClassCommandGroupA = 0x18,
    BooleanPostfixExpression = 0x1A,
};

struct LevelScheduledCondition {
    uint8_t status_and_kind{};
    std::array<uint8_t, 15> payload_bytes{};

    LevelScheduledConditionKind kind() const noexcept {
        return static_cast<LevelScheduledConditionKind>(status_and_kind & 0xFEU);
    }
    bool satisfied() const noexcept { return (status_and_kind & 1U) != 0U; }
    uint32_t operand(std::size_t index) const;
    void set_operand(std::size_t index, uint32_t value);
};

struct LevelEndConditionTrigger {
    uint8_t state_flags{};
    uint8_t movie_variant_selector{};
    uint8_t skip_army_disable_when_one{};
    uint8_t reserved03{};
    uint8_t faction_runtime_index{};
    uint8_t end_movie_selection_index{};
    uint8_t condition_index{};
    uint8_t reserved07{};

    bool active() const noexcept { return (state_flags & 1U) != 0U; }
    bool processed() const noexcept { return (state_flags & 2U) != 0U; }
};



constexpr uint32_t resolve_faction_graphics_family(uint32_t faction_index,
                                                    uint32_t raw_level_offset) noexcept {
    // Retail x86 does a 32-bit ADD. Negative frontend offsets are stored as their
    // two's-complement dword, so unsigned wraparound is the exact operation.
    return faction_index == 0U ? 0U : faction_index + raw_level_offset;
}

struct LevelPlayerSlotRecord {
    int32_t start_camera_x_q12{};
    int32_t start_camera_y_q12{};
    int32_t start_camera_z_q12{};
    uint32_t start_camera_magnitude_q12{0x1000U};
    uint32_t packed_heading_low16_pitch_high16{};
    // LEV player-slot +0x14/+0x18. V428 closes these as starting Xenite and Tritium stock.
    // They are not generic building resource / Energy-capacity fields.
    uint32_t start_xenite_q4{};
    uint32_t start_tritium_q4{};
    uint32_t ai_class_or_mode{};
};

struct LevelFactionCameraStart {
    int32_t position_x_q12{};
    int32_t position_y_q12{};
    int32_t position_z_q12{};
    uint32_t motion_magnitude_q12{0x1000U};
    uint16_t heading_turn16{};
    int16_t pitch_turn16{};

    float position_x() const noexcept { return q12_to_float(position_x_q12); }
    float position_y() const noexcept { return q12_to_float(position_y_q12); }
    float position_z() const noexcept { return q12_to_float(position_z_q12); }
};

struct LevelPaths {
    std::string level;
    std::string ground_textures;
    std::string surface_textures;
    std::string sky_textures;
    std::string army_textures;
    std::string shot_textures;
    std::string effect_textures;
    std::string ending_movie;
    std::string sounds;
    std::string technology;
};

class LevelAsset {
public:
    static LevelAsset parse(std::span<const std::byte> data);
    static LevelAsset from_template(const LevelAsset& source, std::string_view new_level_path,
                                    bool preserve_placements = false);

    const LevelPaths& paths() const noexcept { return paths_; }
    LevelPaths& paths() noexcept { return paths_; }
    const TerrainLighting& lighting() const noexcept { return lighting_; }
    TerrainLighting& lighting() noexcept { return lighting_; }
    std::span<const LevelPlacement> placements() const noexcept { return placements_; }
    std::span<LevelPlacement> placements() noexcept { return placements_; }
    std::span<const std::string> resource_paths() const noexcept { return resource_paths_; }
    // The native definition registries load their files in serialized table order.
    std::span<const std::string> model_resource_paths_in_load_order() const noexcept { return model_resource_paths_; }
    std::span<const std::string> army_resource_paths_in_load_order() const noexcept { return army_resource_paths_; }
    std::span<const std::string> effect_resource_paths_in_load_order() const noexcept { return effect_resource_paths_; }
    std::span<const std::string> shot_resource_paths_in_load_order() const noexcept { return shot_resource_paths_; }

    void add_placement(LevelPlacement placement);
    void erase_placement(std::size_t index);
    void clear_placements() noexcept { placements_.clear(); }
    std::vector<std::byte> serialize() const;

    std::span<const std::byte> fixed_image() const noexcept { return fixed_image_; }
    uint32_t placement_offset() const noexcept { return placement_offset_; }
    uint32_t compatibility_alias_base() const;
    void set_compatibility_alias_base(uint32_t value);

    // FrontendPlayerRuntime_InitializeFactionAssignments and
    // FrontendTaskAssignmentPage_Initialize read two distinct dwords: LEV +0x310 is the
    // human-assignable prefix and +0x314 is the total active-faction count. Factions after the
    // assignable prefix are initialized as computer-only rows. Stock campaign maps mirror this
    // exact range as ASCII digits at LEV +0x140 (minimum/human) and +0x148 (maximum/active).
    uint32_t frontend_assignable_faction_count() const;
    uint32_t frontend_active_faction_count() const;
    uint32_t embedded_minimum_player_count() const;
    uint32_t embedded_maximum_player_count() const;
    void set_frontend_faction_counts(uint32_t assignable_count, uint32_t active_count);
    // V537 LevelAssetRuntimeTail90 relation setup copied into faction runtime during level init.
    // These are authored initial relationship groups, not the mutable live diplomacy image.
    uint32_t relation_ui_flags() const;
    uint32_t relation_state8_faction_group_masks() const;
    uint32_t relation_state4_faction_group_masks() const;
    void set_initial_relation_setup(uint32_t ui_flags, uint32_t state8_group_masks,
                                    uint32_t state4_group_masks);
    void set_embedded_player_count_range(uint32_t minimum_count, uint32_t maximum_count);
    // Compatibility helper for symmetric multiplayer/skirmish maps.
    void set_skirmish_player_count(uint32_t count);

    // LEV +0x380 contains the exact 64 x 0x10 scheduled-condition image consumed by
    // InGameConditionRuntime_UpdateScheduledRecords. LEV +0x780 contains 16 x 0x08 end-condition
    // triggers. Campaign/tutorial maps use custom records here; ordinary skirmish maps carry the
    // stock elimination/end-movie condition set. These are serialized map-script state, unlike
    // notification playback history which exists only at runtime.
    static constexpr uint32_t kScheduledConditionCount = 64U;
    static constexpr uint32_t kEndConditionTriggerCount = 16U;
    LevelScheduledCondition scheduled_condition(uint32_t index) const;
    void set_scheduled_condition(uint32_t index, const LevelScheduledCondition& condition);
    LevelEndConditionTrigger end_condition_trigger(uint32_t index) const;
    void set_end_condition_trigger(uint32_t index, const LevelEndConditionTrigger& trigger);
    uint32_t active_scheduled_condition_count() const;
    uint32_t active_end_condition_trigger_count() const;

    // game.exe copies seven 0x20-byte faction records between LEV +0x200..+0x2DF and
    // runtime state. The first five dwords of each record are the camera start: X/Y/Z Q12,
    // motion magnitude Q12, and packed heading/pitch turn16. The last three dwords contain
    // The final three dwords are starting Xenite Q4, starting Tritium Q4, and AI class/mode.
    // game.exe copies +0x14/+0x18 directly into faction Xenite/Tritium stock on level load.
    static constexpr uint32_t kFactionCameraStartCount = 7U;
    // Stock midgard skirmish defaults. Stored values are Q4 (whole units * 16).
    static constexpr uint32_t kDefaultXeniteQ4 = 6500U * 16U;
    static constexpr uint32_t kDefaultTritiumQ4 = 3000U * 16U;
    LevelFactionCameraStart faction_camera_start(uint32_t faction_index) const;
    void set_faction_camera_start(uint32_t faction_index, const LevelFactionCameraStart& camera);
    uint32_t faction_xenite_q4(uint32_t faction_index) const;
    uint32_t faction_tritium_q4(uint32_t faction_index) const;
    void set_faction_starting_resources_q4(uint32_t faction_index,
                                           uint32_t xenite_q4,
                                           uint32_t tritium_q4);
    LevelPlayerSlotRecord faction_player_slot(uint32_t faction_index) const;
    void set_faction_player_slot(uint32_t faction_index, const LevelPlayerSlotRecord& slot);
    // LEV player-slot +0x1C. Older recovery called this aiClassOrMode, but retail
    // frontend code edits it as the signed offset used to choose armyN.gfx/armyN.pal.
    uint32_t faction_graphics_family_offset_raw(uint32_t faction_index) const;
    void set_faction_graphics_family_offset_raw(uint32_t faction_index, uint32_t value);
    // ArmyRuntime_InitializePoolAndGraphicsCf consumes runtime-record +0x38, which
    // LevelLoad seeds as faction_index + the raw LEV offset above. Frontend color
    // cycling mutates that same LEV dword and permits negative offsets by wraparound.
    uint32_t faction_graphics_family_selector(uint32_t faction_index) const;

private:
    std::vector<std::byte> fixed_image_;
    uint32_t placement_offset_{};
    std::array<uint32_t, 10> path_offsets_{};
    LevelPaths paths_;
    TerrainLighting lighting_;
    std::vector<LevelPlacement> placements_;
    std::vector<std::string> resource_paths_;
    std::vector<std::string> model_resource_paths_;
    std::vector<std::string> army_resource_paths_;
    std::vector<std::string> effect_resource_paths_;
    std::vector<std::string> shot_resource_paths_;

    void read_paths();
    void read_lighting();
    void read_resource_lists();
    void write_paths(std::span<std::byte> data) const;
    void write_lighting(std::span<std::byte> data) const;
};

} // namespace thandor
