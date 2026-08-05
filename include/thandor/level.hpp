#pragma once

#include "thandor/field.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace thandor {

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



struct LevelPlayerSlotRecord {
    int32_t start_camera_x_q12{};
    int32_t start_camera_y_q12{};
    int32_t start_camera_z_q12{};
    uint32_t start_camera_magnitude_q12{0x1000U};
    uint32_t packed_heading_low16_pitch_high16{};
    uint32_t start_building_resource_q4{};
    uint32_t start_energy_resource_q4{};
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
    void set_embedded_player_count_range(uint32_t minimum_count, uint32_t maximum_count);
    // Compatibility helper for symmetric multiplayer/skirmish maps.
    void set_skirmish_player_count(uint32_t count);

    // game.exe copies seven 0x20-byte faction records between LEV +0x200..+0x2DF and
    // runtime state. The first five dwords of each record are the camera start: X/Y/Z Q12,
    // motion magnitude Q12, and packed heading/pitch turn16. The last three dwords contain
    // faction resource/mode metadata and are intentionally preserved by these accessors.
    static constexpr uint32_t kFactionCameraStartCount = 7U;
    // Stock midgard skirmish defaults. Stored values are Q4 (whole units * 16).
    static constexpr uint32_t kDefaultBuildingResourceQ4 = 6500U * 16U;
    static constexpr uint32_t kDefaultEnergyResourceQ4 = 3000U * 16U;
    LevelFactionCameraStart faction_camera_start(uint32_t faction_index) const;
    void set_faction_camera_start(uint32_t faction_index, const LevelFactionCameraStart& camera);
    uint32_t faction_building_resource_q4(uint32_t faction_index) const;
    uint32_t faction_energy_resource_q4(uint32_t faction_index) const;
    void set_faction_starting_resources_q4(uint32_t faction_index,
                                           uint32_t building_resource_q4,
                                           uint32_t energy_resource_q4);
    LevelPlayerSlotRecord faction_player_slot(uint32_t faction_index) const;
    void set_faction_player_slot(uint32_t faction_index, const LevelPlayerSlotRecord& slot);
    uint32_t faction_ai_class_or_mode(uint32_t faction_index) const;
    void set_faction_ai_class_or_mode(uint32_t faction_index, uint32_t value);

private:
    std::vector<std::byte> fixed_image_;
    uint32_t placement_offset_{};
    std::array<uint32_t, 10> path_offsets_{};
    LevelPaths paths_;
    TerrainLighting lighting_;
    std::vector<LevelPlacement> placements_;
    std::vector<std::string> resource_paths_;

    void read_paths();
    void read_lighting();
    void read_resource_lists();
    void write_paths(std::span<std::byte> data) const;
    void write_lighting(std::span<std::byte> data) const;
};

} // namespace thandor
