#pragma once

#include "thandor/effect.hpp"
#include "thandor/field.hpp"
#include "thandor/gfx.hpp"
#include "thandor/level.hpp"
#include "thandor/model.hpp"
#include "thandor/pck.hpp"
#include "thandor/simulation.hpp"
#include "thandor/technology.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace thandor {

enum class AssetKind { Level, Field, Texture, Palette, Army, Model, Effect, Shot, Sound, Text, Other };

struct AssetRecord {
    std::string archive_name;
    std::string path;
    std::string normalized_path;
    AssetKind kind{AssetKind::Other};
    std::size_t archive_index{};
    std::size_t entry_index{};
    uint32_t unpacked_size{};
};

enum class PlacementVisualKind {
    Standard,
    Tree,
    Rock,
    PlayerStart,
    PowerResource,
    BuildingResource,
    ResourceScenery
};
enum class PlacementBrowserKind { General, Element, Unit, Building };

const char* placement_visual_kind_name(PlacementVisualKind kind) noexcept;

// Accept both retail executable names when selecting a game installation root.
bool has_supported_game_executable(const std::filesystem::path& game_root);

inline constexpr uint32_t kArmyAssetNameTextPage = 0x18U;
inline constexpr uint32_t kArmyAssetNameTextBaseIndex = 0x4FU;
inline constexpr uint32_t kArmyAssetNameTextBaseResourceId = 0x18004FU;

constexpr uint32_t army_asset_name_text_index(uint32_t model_name_offset) noexcept {
    return kArmyAssetNameTextBaseIndex + model_name_offset;
}
constexpr uint32_t army_asset_name_text_resource_id(uint32_t model_name_offset) noexcept {
    return kArmyAssetNameTextBaseResourceId + model_name_offset;
}

struct TechnologyPanelEntry {
    // Exact serialized MDL table index, 1..28. MDL +0x1C4 is a separate linked
    // technology and is intentionally not represented as panel index zero.
    uint32_t serialized_index{};
    uint32_t direction_index{};
    uint32_t row_index{};
    uint32_t technology_id{};
    TechnologyCategory category{TechnologyCategory::Category0};
    std::string title;
};

struct EffectPipelineReferenceInfo {
    uint32_t effect_definition_id{};
    EffectLifecycleTransitionKind transition_kind{
        EffectLifecycleTransitionKind::NoAdditionalAction};
    std::string sprite_path;
};

struct WeaponEffectPipelineInfo {
    uint32_t model_definition_id{};
    uint32_t shot_definition_id{};
    ShotTrajectoryMode trajectory_kind{ShotTrajectoryMode::DirectLine};
    uint32_t launch_attachment_count{};
    uint32_t launch_attachment_selector_mask{};
    EffectPipelineReferenceInfo primary_effect;
    EffectPipelineReferenceInfo launch_effect;
    EffectPipelineReferenceInfo secondary_effect;
    std::vector<EffectPipelineReferenceInfo> terrain_impact_effects;
    std::vector<EffectPipelineReferenceInfo> target_class_impact_effects;
    std::string shot_sprite_path;
};

struct AssetTechnologyCostComponent {
    uint32_t model_definition_id{};
    uint32_t technology_id{};
    std::string title;
    uint32_t xenite_cost_q4{};
    uint32_t energy_cost_q4{};
    uint32_t research_duration_q5{};
};

struct EffectPipelineValidationSummary {
    uint32_t weapon_pipeline_count{};
    uint32_t shot_reference_count{};
    uint32_t effect_reference_count{};
    uint32_t sprite_reference_count{};
    uint32_t maximum_effect_depth{};
    uint32_t missing_reference_count{};
    uint32_t cycle_count{};
    std::vector<std::string> issues;

    bool ok() const noexcept { return issues.empty(); }
};

struct ModelTechnologyVariant {
    uint32_t variant_index{};
    uint32_t model_definition_id{};
    uint32_t technology_unlock_id{};
    std::string technology_title;
    uint32_t linked_technology_id{};
    std::string linked_technology_title;
    std::string model_source_path;
    std::string sprite_path;
    std::vector<WeaponEffectPipelineInfo> weapon_pipelines;
};

struct ArmyModelVariantGroup {
    uint32_t arm_node_offset{};
    uint32_t parent_group_index{std::numeric_limits<uint32_t>::max()};
    uint32_t child_slot{};
    uint32_t depth{};
    std::vector<ModelTechnologyVariant> variants;
};

// Editor-only preview of one globally selected ARM model-technology level. LEV records do not
// serialize this selection; the retail game resolves the active model from faction technologies.
struct ArmyTechnologyLevelPreview {
    uint32_t level_index{};
    uint32_t root_model_definition_id{};
    uint32_t technology_effect_id{};
    std::string technology_effect_title;
    std::vector<ModelVisualNode> resolved_model_hierarchy;
};

struct ArmyAssetPreset {
    uint32_t registry_id{};
    std::string source_path;
    std::string group_hint;
    uint32_t model_definition_id{};
    uint32_t model_name_text_offset{};
    uint32_t name_text_resource_id{};
    bool has_model_name_text_offset{};
    std::string localized_name;
    std::string localized_name_source_path;
    std::string localized_name_source_archive;
    uint32_t runtime_model_class_id{};
    // Selected root MDL +0x5C. SHT +0x90 is indexed by this field, not by
    // runtime_model_class_id (MDL +0x4C).
    uint32_t shot_impact_class_index{};
    uint32_t model_runtime_render_flags{};
    bool casts_generated_shadow{true};
    std::string model_source_path;
    std::string sprite_path;
    std::vector<ModelVisualNode> model_hierarchy;
    // Complete runtime composition after game.exe resolves deferred MDL placeholder nodes
    // through the recursive linked ARM definition tree. Rendering must use this hierarchy.
    std::vector<ModelVisualNode> resolved_model_hierarchy;
    uint32_t linked_model_runtime_count{};
    uint32_t deferred_model_attachment_count{};
    uint32_t placement_occupancy_class{};
    uint32_t traversal_low_band_index{};
    uint32_t traversal_high_band_index{};
    bool has_placement_occupancy_class{};
    bool has_traversal_profile{};
    std::string display_name;
    std::string short_name;
    // PCK-backed disambiguation for multiple ARM records sharing the same localized chassis name.
    // This is derived only from the exact ARM variant -> MDL technology chain.
    std::string loadout_name;
    std::string technology_summary;
    std::string asset_search_text;
    // Selected root MDL +0x1C4, separate from both the ARM variant-unlock key (+0x1C0) and
    // the 28-entry panel table (+0x1C8..+0x234).
    uint32_t linked_technology_id{};
    std::string linked_technology_title;
    uint32_t technology_branch_count{};
    std::array<std::string, 7> technology_branch_names{};
    std::vector<ArmyModelVariantGroup> model_variant_groups;
    std::vector<ArmyTechnologyLevelPreview> technology_level_previews;
    std::vector<TechnologyPanelEntry> technology_panel_entries;
    PlacementVisualKind visual_kind{PlacementVisualKind::Standard};
    PlacementBrowserKind browser_kind{PlacementBrowserKind::Element};
    uint32_t primary_animated_subresource{};
    uint32_t secondary_animated_subresource{};
    bool has_primary_texture_animation{};
    bool has_secondary_texture_animation{};
    // Runtime class 2 advances the two selected root texture U offsets independently. ARM +0x14
    // is the class-specific distance-to-texture scale; it must not be treated as child[2] motion.
    int32_t track_texture_travel_scale{};
    // Runtime class 3 paired-foot support fields copied from the selected root MDL.
    int32_t articulated_root_height_q12{};
    int32_t articulated_contact_interpolation_rate_q12{};
    int32_t articulated_contact_lift_q12{};
    uint32_t timed_effect_id{};
    uint32_t timed_effect_interval_ticks{};
    uint32_t timed_effect_random_ticks{};
    // ArmyRuntime_UpdateAnimatedModelSubnodes reads these exact ARM record fields for
    // root child slots 0, 1, and 2. Rotation steps are turn16 units per simulation tick;
    // the vertical channel is Q12 and bounces between its serialized limits.
    int32_t child0_rotation_step_turn16{};
    int32_t child1_rotation_step_turn16{};
    int32_t child2_vertical_step_q12{};
    int32_t child2_vertical_min_q12{};
    int32_t child2_vertical_max_q12{};
    bool has_model_subnode_animation{};
    // A linked runtime-class-10 radar node owns continuously rotating child[0]/child[1]
    // subnodes. This callback is independent from weapon target acquisition.
    bool has_continuous_radar_subnode_animation{};
    // Only runtime-update classes 4, 11, 13, and 14 call
    // ArmyRuntime_UpdateAnimatedModelSubnodes. Other classes reuse the same ARM offsets for
    // movement or class state; treating those values as fan/child animation corrupts Hornet and
    // hover-unit hierarchies.
    bool runtime_uses_generic_model_subnode_animation{};
    // Runtime class 17 owns the dedicated movement/banking callback and must not be routed through
    // the generic child[0]/child[1] spin + child[2] bounce helper.
    bool runtime_uses_class17_movement_banking{};
    // Editor simulation profiles are recovered from the same MDL fields used by the 20 Hz
    // movement/aim/fire callbacks and from kind-2 launch attachments in the selected SPR LOD.
    SimulationMotionProfile simulation_motion_profile;
    std::vector<SimulationWeaponProfile> simulation_weapon_profiles;
    // Binary/PCK-proven asset statistics used by the browser and simulated fog of war.
    // MDL +0x48 is max-aggregated over the resolved model hierarchy by the retail runtime.
    int32_t fog_uncover_radius_q12{};
    uint32_t fog_uncover_scan_steps{};
    // MDL +0x60 is the initial/maximum model-node integrity. The total follows the runtime's
    // recursive hierarchy aggregation, while root keeps the selected chassis/building value.
    int32_t root_maximum_integrity{};
    int64_t total_maximum_integrity{};
    // Stock ARM +0x28 is the runtime purchase value but is zero in serialized stock records.
    // These PCK-backed base-configuration costs are therefore retained as exact TEC component
    // sums rather than mislabelled as a directly serialized ARM price.
    std::vector<AssetTechnologyCostComponent> base_cost_components;
    uint64_t base_xenite_cost_q4{};
    uint64_t base_energy_cost_q4{};
    EffectPipelineValidationSummary effect_pipeline_validation;
    // Canonical browser placement ID. Stock maps also contain compatible active variants
    // (810 for energy and 814 for building material), but new placements use 811/815.
    uint32_t canonical_registry_id{};
    uint32_t stock_placement_count{};
    bool visual_chain_complete{};
    bool direct_placement_proven{};
    bool game_placeable{};
    bool browser_visible{true};
    // The same runtime registry ID can be defined by multiple campaign/tutorial ARM files.
    // Preserve every proven ARM and MDL alternative so placement availability is checked
    // against the current LEV's mounted resource families rather than one globally chosen preview.
    std::vector<std::string> compatible_arm_paths;
    std::vector<std::string> compatible_model_paths;
    std::vector<uint32_t> compatible_registry_ids;
    std::vector<uint32_t> shared_sprite_effect_ids;
};

const char* placement_browser_kind_name(PlacementBrowserKind kind) noexcept;
bool placement_is_resource(const ArmyAssetPreset& preset) noexcept;
bool placement_is_resource_scenery(const ArmyAssetPreset& preset) noexcept;
// Energy deposits (810/811) require occupancy class 1 / FLD bit 0x1000.
// Building-material deposits (814/815) require class 0 / FLD bit 0x0800.
std::optional<uint32_t> resource_field_occupancy_class(uint32_t registry_id) noexcept;
std::size_t stamp_resource_field_masks(FieldGrid& field,
                                       std::span<const LevelPlacement> placements,
                                       float stock_radius_cells = 2.0F);
uint32_t canonical_placement_registry_id(const ArmyAssetPreset& preset) noexcept;
PlacementBrowserKind classify_placement_browser_kind(const ArmyAssetPreset& preset) noexcept;
bool placement_uses_neutral_source_color(PlacementVisualKind kind) noexcept;
bool placement_uses_neutral_source_color(const ArmyAssetPreset& preset) noexcept;
// game.exe selects an army GFX/PAL colour family from the frontend faction record;
// it is not the raw faction index.  This stock default reproduces the standard
// playable order (1 blue, 2 red, 3 green, 4 yellow, 5 orange, 6 brown, 7 purple).
uint32_t default_faction_graphics_family(uint32_t faction_index) noexcept;
const char* default_faction_color_name(uint32_t faction_index) noexcept;

struct PlacementCompatibilityIssue {
    std::size_t placement_index{};
    uint32_t registry_id{};
    std::string message;
};

struct AssetValidationIssue {
    std::string path;
    std::string message;
};

struct AssetValidationReport {
    std::size_t catalog_entries{};
    std::size_t decoded_entries{};
    std::size_t level_files{};
    std::size_t field_files{};
    std::size_t texture_files{};
    std::size_t palette_files{};
    std::size_t army_files{};
    std::size_t model_files{};
    std::size_t effect_files{};
    std::size_t effect_definitions{};
    std::vector<AssetValidationIssue> issues;
    bool ok() const noexcept { return issues.empty(); }
};

struct MapAssetGroup {
    std::string display_name;
    std::string level_path;
    std::string field_path;
    std::vector<std::size_t> asset_indices;
    std::vector<uint32_t> placement_registry_ids;
};

class AssetCatalog {
public:
    static AssetCatalog scan_game_root(const std::filesystem::path& game_root);

    const std::filesystem::path& game_root() const noexcept { return game_root_; }
    std::span<const AssetRecord> assets() const noexcept { return assets_; }
    std::span<const MapAssetGroup> map_groups() const noexcept { return map_groups_; }
    std::span<const std::size_t> general_assets() const noexcept { return general_assets_; }
    std::span<const ArmyAssetPreset> army_presets() const noexcept { return army_presets_; }
    std::span<const uint32_t> general_placement_ids() const noexcept { return general_placement_ids_; }
    const ArmyAssetPreset* army_preset(uint32_t registry_id) const noexcept;
    const EffectDefinitionRecord* effect_definition(uint32_t definition_id) const noexcept;
    const ShotDefinitionRecord* shot_definition(uint32_t definition_id) const noexcept;
    const TechnologyRecord* technology(uint32_t technology_id) const noexcept;
    std::span<const TechnologyRecord> technologies() const noexcept { return technologies_; }
    bool placement_available_in_level(const ArmyAssetPreset& preset,
                                      const LevelAsset& level) const noexcept;
    std::string placement_compatibility_diagnostic(const ArmyAssetPreset& preset,
                                                   const LevelAsset& level) const;
    std::vector<PlacementCompatibilityIssue> validate_level_placements(
        const LevelAsset& level) const;
    AssetValidationReport validate_supported_assets() const;

    const AssetRecord* find(std::string_view normalized_path) const;
    std::vector<std::byte> load(const AssetRecord& record) const;
    std::vector<std::byte> load(std::string_view normalized_path) const;
    std::optional<std::size_t> find_asset_index(std::string_view normalized_path) const;

private:
    std::filesystem::path game_root_;
    std::vector<std::shared_ptr<PckArchive>> archives_;
    std::vector<AssetRecord> assets_;
    std::vector<MapAssetGroup> map_groups_;
    std::vector<std::size_t> general_assets_;
    std::vector<ArmyAssetPreset> army_presets_;
    std::vector<uint32_t> general_placement_ids_;
    std::vector<TechnologyRecord> technologies_;
    std::unordered_map<uint32_t, EffectDefinitionRecord> effect_definitions_;
    std::unordered_map<uint32_t, ShotDefinitionRecord> shot_definitions_;
    std::unordered_map<std::string, std::size_t> lookup_;
};

AssetKind classify_asset(std::string_view normalized_path);
const char* asset_kind_name(AssetKind kind) noexcept;

} // namespace thandor
