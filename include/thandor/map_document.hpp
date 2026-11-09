#pragma once

#include "thandor/catalog.hpp"
#include "thandor/scenario_catalog.hpp"
#include "thandor/text_resource.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace thandor {

enum class PlacementRole { Generic, Resource, PlayerStart, EffectOrScenery };

struct Patch00ExportOptions {
    bool include_scenario_catalog{};
    bool include_level{};
    bool include_field{};
    bool include_text{};
    bool include_custom_assets{true};

    bool any() const noexcept {
        return include_scenario_catalog || include_level || include_field || include_text ||
               include_custom_assets;
    }
};

struct PlayableFactionSummary {
    uint32_t player_count{};
    uint32_t highest_faction_index{};
    uint32_t faction_mask{};
    uint32_t nonneutral_placement_count{};
    uint32_t player_start_count{};
    bool contiguous{};
    bool exportable{};
    std::string diagnostic;
};

struct PlayerAiConfiguration {
    uint32_t human_assignable_factions{1U};
    uint32_t active_factions{2U};
};

struct ScenarioFrontendMetadata {
    uint32_t planet_theme_index{};
    uint32_t difficulty_index{};
    uint32_t map_size_category_index{};
};

struct StockMapSaveResult {
    std::vector<std::filesystem::path> updated_archives;
    std::vector<std::filesystem::path> backup_archives;
    std::size_t replaced_entries{};
    std::size_t inserted_entries{};
};

struct PlacementTag {
    std::size_t placement_index{};
    PlacementRole role{PlacementRole::Generic};
    std::string display_name;
    // Editor-only integrity percentage for previewing ArmyRuntime_EmitDamageThresholdEffect.
    // Retail LEV placement records do not serialize current damage; a newly loaded map starts
    // every placement at 100 percent integrity.
    uint32_t preview_integrity_percent{100U};
};

class MapDocument {
public:
    static MapDocument open(const AssetCatalog& catalog, const MapAssetGroup& group);
    static MapDocument open_loose(const AssetCatalog& catalog,
                                  const std::filesystem::path& level_path,
                                  const std::filesystem::path& field_path = {});
    static MapDocument open_project(const AssetCatalog& catalog,
                                    const std::filesystem::path& project_path);
    static MapDocument create_from_template(const AssetCatalog& catalog, const MapAssetGroup& group,
                                            std::string map_name, uint32_t width, uint32_t height);
    static MapDocument create_empty(const AssetCatalog& catalog, const MapAssetGroup& compatibility_shell,
                                    std::string map_name, std::string display_name,
                                    uint32_t width, uint32_t height,
                                    int32_t initial_height_q12 = 0, uint8_t initial_material = 0);

    const std::string& map_name() const noexcept { return map_name_; }
    void set_map_name(std::string value);
    const std::string& display_name() const noexcept { return display_name_; }
    void set_display_name(std::string value);
    const std::string& author() const noexcept { return author_; }
    void set_author(std::string value);
    uint32_t default_human_faction() const;
    void set_default_human_faction(uint32_t faction);
    void resize_field(uint32_t width, uint32_t height, bool center_existing = true);
    FieldGrid& field() noexcept { return field_; }
    const FieldGrid& field() const noexcept { return field_; }
    LevelAsset& level() noexcept { return level_; }
    const LevelAsset& level() const noexcept { return level_; }
    uint32_t scenario_alias_base() const noexcept { return scenario_record_.alias_base(); }
    ScenarioFrontendMetadata scenario_frontend_metadata() const noexcept;
    void set_scenario_frontend_metadata(const ScenarioFrontendMetadata& metadata);
    std::array<std::string, 15> scenario_detail_strings(uint32_t preferred_country = 44U) const;
    void set_scenario_detail_string(uint32_t detail_index, std::string value);
    std::vector<std::byte> scenario_record_bytes() const { return scenario_record_.serialize(); }
    std::vector<std::byte> level_text_bytes() const { return level_text_.serialize(); }
    void restore_editor_metadata(std::span<const std::byte> scenario_record,
                                 std::span<const std::byte> level_text);
    PlayableFactionSummary playable_faction_summary() const;
    PlayerAiConfiguration player_ai_configuration() const;
    void set_player_ai_configuration(const PlayerAiConfiguration& configuration);
    bool dirty() const noexcept { return dirty_; }
    bool is_campaign_mission_project() const noexcept { return campaign_mission_project_; }
    void mark_dirty() noexcept { dirty_ = true; }
    void mark_clean() noexcept { dirty_ = false; }

    std::filesystem::path export_game_level_package(const std::filesystem::path& game_root,
                                                    unsigned slot, bool create_backup = true) const;
    std::filesystem::path update_patch00_overrides(const std::filesystem::path& game_root,
                                                   unsigned slot,
                                                   const Patch00ExportOptions& options,
                                                   bool create_backup = true) const;
    void export_loose(const std::filesystem::path& directory) const;
    // Rewrites the exact stock PCK archive(s) that supplied this map. Runtime simulation state is
    // intentionally not an input: only authored MapDocument LEV/FLD/text/catalog/custom assets
    // are emitted. Every touched archive is backed up and the whole operation rolls back on error.
    StockMapSaveResult save_stock_map_in_place(const AssetCatalog& catalog,
                                                const MapAssetGroup& group,
                                                bool create_backup = true) const;
    void save_project(const std::filesystem::path& project_path, bool preserve_mission_state = false) const;
    // Campaign missions retain authored faction counts, zero resources, scripts and shared terrain identity.
    std::vector<PckWriteEntry> campaign_package_entries() const;

    std::vector<PlacementTag>& placement_tags() noexcept { return placement_tags_; }
    std::span<const PlacementTag> placement_tags() const noexcept { return placement_tags_; }
    std::array<FactionTechnologyMask256, 8>& faction_technology_masks() noexcept {
        return faction_technology_masks_;
    }
    const std::array<FactionTechnologyMask256, 8>& faction_technology_masks() const noexcept {
        return faction_technology_masks_;
    }
    FactionTechnologyMask256& faction_technology_mask(uint32_t faction_index) noexcept {
        return faction_technology_masks_[std::min<uint32_t>(faction_index, 7U)];
    }
    const FactionTechnologyMask256& faction_technology_mask(uint32_t faction_index) const noexcept {
        return faction_technology_masks_[std::min<uint32_t>(faction_index, 7U)];
    }

    std::vector<std::byte> load_asset(const AssetCatalog& catalog, std::string_view path) const;
    bool has_custom_asset(std::string_view path) const;
    std::size_t custom_asset_count() const noexcept { return custom_assets_.size(); }
    void import_terrain_material(const AssetCatalog& catalog, uint8_t destination_material,
                                 std::string_view source_gfx_path);
    void import_terrain_material_bytes(const AssetCatalog& catalog, uint8_t destination_material,
                                       std::string provenance, std::vector<std::byte> gfx_bytes);
    std::span<const std::string, 26> terrain_material_sources() const noexcept {
        return terrain_material_sources_;
    }

private:
    std::string map_name_;
    std::string display_name_;
    std::string author_;
    uint32_t default_human_faction_{};
    FieldGrid field_;
    LevelAsset level_;
    std::vector<PlacementTag> placement_tags_;
    std::array<FactionTechnologyMask256, 8> faction_technology_masks_{};
    std::map<std::string, std::vector<std::byte>> custom_assets_;
    std::array<std::string, 26> terrain_material_sources_{};
    ScenarioCatalogRecord scenario_record_;
    TextResourceAsset level_text_;
    bool dirty_{};
    bool campaign_mission_project_{};

    static std::string sanitize_map_name(std::string value);
    static std::string sanitize_display_name(std::string value);
    static void initialize_placement_tags(MapDocument& document, const AssetCatalog& catalog);
    static std::vector<ScenarioCatalogRecord> load_merged_level_catalog(const AssetCatalog& catalog);
    static TextResourceAsset load_level_text_or_minimal(const AssetCatalog& catalog,
                                                        const MapAssetGroup* group,
                                                        std::string_view display_name);
    void validate_game_package_contract() const;
    void synchronize_gameplay_metadata(LevelAsset& level, ScenarioCatalogRecord& scenario) const;
    static std::string terrain_base_path(std::string_view source);
    static std::string terrain_material_path(std::string_view base, uint32_t material);
    std::string ensure_custom_terrain_family(const AssetCatalog& catalog);
};

} // namespace thandor
