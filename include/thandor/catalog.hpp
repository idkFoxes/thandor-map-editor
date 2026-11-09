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
#include <functional>
#include <utility>
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
    PowerResource,    // Legacy internal spelling: V428 closes this blue deposit as Tritium.
    BuildingResource, // Legacy internal spelling: V428 closes this gold deposit as Xenite.
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
    uint32_t xenite_cost_q4{};
    uint32_t energy_demand_q4{};
    uint32_t research_duration_q5{};
    std::string title;
    std::string special_features;
    std::string description;
};

struct EffectPipelineReferenceInfo {
    uint32_t effect_definition_id{};
    EffectLifecycleTransitionKind transition_kind{
        EffectLifecycleTransitionKind::NoAdditionalAction};
    std::string sprite_path;
    // Exact packed-key attachment classes in the EFF-owned SPR. Low nibble is class, upper bits
    // are selector index. Masks retain indices 0..31 without flattening class-2/3/4/6 semantics.
    std::array<uint32_t, 16> attachment_selector_masks{};
    std::array<uint32_t, 16> attachment_counts{};
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
    // Unique effect lists remain for compatibility, but the indexed tables below preserve the
    // actual runtime dispatch slots. SHT +0x90/+0xB0 are both indexed by hit MDL +0x5C (0..7).
    // Terrain EFF +0x14 has 31 independent raycast-result slots; +0x1E0 is a parallel signed
    // material-reference table and must not be falsely treated as a remap for the EFF table.
    std::vector<EffectPipelineReferenceInfo> terrain_impact_effects;
    std::vector<EffectPipelineReferenceInfo> target_class_impact_effects;
    std::array<uint32_t, 31> terrain_impact_effect_ids{};
    std::array<uint32_t, 8> target_class_impact_effect_ids{};
    std::array<int32_t, 8> target_class_impact_damage_q12{};
    std::array<int32_t, 31> terrain_material_indices{};
    std::string shot_sprite_path;
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

struct ModelFactionTechnologyReplacement {
    uint32_t slot_index{};
    uint32_t model_definition_id{};
    uint32_t technology_unlock_id{};
    std::string technology_title;
    std::string model_source_path;
    std::string sprite_path;
    std::vector<ModelVisualNode> model_hierarchy;
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
    // Full owning MDL hierarchy for this ARM variant. FactionTechnologyMask256 chooses the ARM
    // variant first, then the runtime scans faction_technology_replacements in ascending slot order
    // and uses the first unlocked candidate, exactly matching game.exe.
    std::vector<ModelVisualNode> model_hierarchy;
    std::vector<ModelFactionTechnologyReplacement> faction_technology_replacements;
    std::vector<WeaponEffectPipelineInfo> weapon_pipelines;
};

struct ArmyModelVariantGroup {
    uint32_t arm_node_offset{};
    uint32_t parent_group_index{std::numeric_limits<uint32_t>::max()};
    uint32_t child_slot{};
    uint32_t depth{};
    std::vector<ModelTechnologyVariant> variants;
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
    // Runtime class 14 only: low byte of MDL +0xC8. The game packs it into the field-cell
    // extractor occupancy record and resolves actual production over the connected resource
    // region. Keep this separate from placement_occupancy_class at MDL +0xC0.
    uint32_t resource_extraction_weight_u8{};
    uint32_t resource_storage_selector{};
    uint32_t resource_storage_capacity_contribution_q4{};
    bool has_resource_storage_capacity_contribution{};
    int32_t water_damage_multiplier{};
    int32_t water_damage_threshold_q12{};
    int32_t ground_heading_interpolation_distance_q12{};
    uint32_t ground_far_heading_error_limit_turn16{};
    uint32_t ground_near_heading_error_limit_turn16{};
    int32_t vertical_deployment_step_q12_per_tick{};
    int32_t vertical_deployment_travel_limit_q12{};
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
    std::vector<TechnologyPanelEntry> technology_panel_entries;
    PlacementVisualKind visual_kind{PlacementVisualKind::Standard};
    PlacementBrowserKind browser_kind{PlacementBrowserKind::Element};
    uint32_t primary_animated_subresource{};
    uint32_t secondary_animated_subresource{};
    bool has_primary_texture_animation{};
    bool has_secondary_texture_animation{};
    // V519 proves ARM +0x14 is also consumed as a raw AI-candidate flag word: bit 0 gates the
    // candidate scoring passes. Preserve the raw dword so this semantic view does not destroy the
    // already-proven class-2 signed track-texture interpretation of the same storage.
    uint32_t army_runtime_flags14_raw{};
    bool ai_candidate_eligible_bit0{};
    // V568: AiArmyCandidate_ComputeFactionWeightedScore reads these three raw ARM dwords
    // directly at +0x70/+0x74/+0x78.  Their domain is still unresolved; preserve the exact
    // serialized values instead of inventing gameplay names for them.
    uint32_t ai_definition_class_value70{};
    uint32_t ai_definition_class_value74{};
    uint32_t ai_definition_class_value78{};
    // Runtime class 2 advances the two selected root texture U offsets independently. ARM +0x14
    // is the class-specific signed distance-to-texture scale in that class-specific callback.
    int32_t track_texture_travel_scale{};
    // Selected root MDL raw runtime-class fields needed by class 13 production. They deliberately
    // coexist with the class-3 semantic aliases because +0xC4 is overloaded by runtime class.
    int32_t runtime_class_step_q12{};
    int32_t runtime_class_specific_c0_raw{};
    uint32_t runtime_class_specific_c4_raw{};
    int32_t runtime_class_specific_c8_raw{};
    int32_t runtime_class_specific_cc_raw{};
    int32_t runtime_class_specific_19c_raw{};
    int32_t runtime_class_specific_1a8_raw{};
    uint32_t runtime_class_effect_sound_asset_id_26c{};
    uint32_t class13_completion_sound_asset_id{};
    uint32_t creation_sound_asset_id_270{};
    // Runtime class 3 paired-foot support fields copied from the selected root MDL.
    int32_t articulated_root_height_q12{};
    int32_t articulated_contact_interpolation_rate_q12{};
    int32_t articulated_contact_lift_q12{};
    uint32_t articulated_contact_steering_limit_turn16{};
    uint32_t articulated_contact_sound_asset_id{};
    uint32_t positioned_sound_gain_q15{};
    uint32_t positioned_sound_maximum_distance_q12{};
    uint32_t projected_sound_asset_id_d0{};
    uint32_t projected_sound_asset_id_d8{};
    uint32_t projected_sound_asset_id_primary{};
    uint32_t projected_sound_asset_id_secondary{};
    // Selected-root compatibility summary of the two retail timed-emitter channels. Runtime
    // rendering walks ModelVisualNode::simulation so linked MDL roots keep their own slots too.
    int32_t timed_shot_definition_id{-1};
    uint32_t timed_shot_interval_ticks{};
    uint32_t timed_shot_random_ticks{};
    uint32_t timed_water_effect_id{};
    uint32_t timed_effect_id{};
    uint32_t timed_effect_interval_ticks{};
    uint32_t timed_effect_random_ticks{};
    uint32_t construction_destruction_effect_id{};
    int32_t damage_effect_health_threshold_percent{};
    uint32_t damage_effect_id{};
    uint32_t damage_effect_min_cooldown_ticks{};
    uint32_t damage_effect_random_cooldown_ticks{};
    // Compatibility summary of the selected root MDL animation profile. Runtime rendering uses
    // every parent ModelVisualNode::simulation recursively; these are not top-level ARM fields.
    // Rotation steps are turn16 units per 20 Hz simulation tick. The child[2] channel uses the
    // same integer fixed-point scale and bounded min/max values consumed by the game callback.
    int32_t child0_rotation_step_turn16{};
    int32_t child1_rotation_step_turn16{};
    int32_t child2_vertical_step_q12{};
    int32_t child2_vertical_min_q12{};
    int32_t child2_vertical_max_q12{};
    bool has_model_subnode_animation{};
    // A linked runtime-class-10 radar node owns continuously rotating child[0]/child[1]
    // subnodes. This callback is independent from weapon target acquisition.
    bool has_continuous_radar_subnode_animation{};
    // True when any resolved MDL runtime parent uses callback classes 4, 11, 13 or 14 and owns
    // an authored child channel. This is recursive: linked model runtimes do not inherit the ARM
    // root class or its animation profile.
    bool runtime_uses_generic_model_subnode_animation{};
    // Runtime class 17 owns the dedicated movement/banking callback and must not be routed through
    // the generic child[0]/child[1] spin + child[2] bounce helper.
    bool runtime_uses_class17_movement_banking{};
    // The class-17 initializer arms its three-child deployment state only when the live root owns
    // more than two children. This is false for every Dragonfly variant and most Hornets.
    bool runtime_uses_class17_root_child_deployment{};
    // Editor simulation profiles are recovered from the same MDL fields used by the 20 Hz
    // movement/aim/fire callbacks and from kind-2 launch attachments in the selected SPR LOD.
    SimulationMotionProfile simulation_motion_profile;
    std::vector<SimulationWeaponProfile> simulation_weapon_profiles;
    // Binary/PCK-proven asset statistics used by the browser and simulated fog of war.
    // MDL +0x48 is max-aggregated over the resolved model hierarchy by the retail runtime.
    int32_t fog_uncover_radius_q12{};
    uint32_t fog_uncover_scan_steps{};
    // Retail build-card statistics follow ArmyAssetRecord_RegisterAndRelocate and
    // InGameSelectionDetailPanel_Rebuild exactly. Each ARM hierarchy node contributes its selected
    // MDL once: +0x60 armour, +0x180 build time Q5, +0x184 Xenite Q4 and +0x18C Energy-demand Q4.
    // The loader accumulates +0x180/+0x184 into ARM runtime-record +0x24/+0x28 before the UI
    // applies >>5/>>4. Internal visual sprite nodes must not duplicate an owning MDL record.
    int32_t root_maximum_integrity{};
    int64_t retail_armour{};
    // ARM registration sums variant-0 MDL +0x188 over every ARM node, with dword wrap.
    // This construction load is separate from selected-model operating demand.
    uint32_t retail_build_energy_q4{};
    uint64_t retail_energy_demand_q4{};
    uint64_t retail_energy_generation_capacity_q4{};
    uint32_t retail_xenite_cost_q4{};
    uint32_t retail_build_time_q5{};
    uint32_t retail_stat_arm_node_count{};
    EffectPipelineValidationSummary effect_pipeline_validation;
    // Canonical browser placement ID. Stock maps also contain compatible active variants
    // (810 for Tritium and 814 for Xenite), but new placements use 811/815.
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
// Tritium deposits (810/811) require occupancy class 1 / FLD bit 0x1000.
// Xenite deposits (814/815) require class 0 / FLD bit 0x0800.
std::optional<uint32_t> resource_field_occupancy_class(uint32_t registry_id) noexcept;
std::size_t stamp_resource_field_masks(FieldGrid& field,
                                       std::span<const LevelPlacement> placements,
                                       float stock_radius_cells = 2.0F);
uint32_t canonical_placement_registry_id(const ArmyAssetPreset& preset) noexcept;
PlacementBrowserKind classify_placement_browser_kind(const ArmyAssetPreset& preset) noexcept;
bool placement_uses_neutral_source_color(PlacementVisualKind kind) noexcept;
bool placement_uses_neutral_source_color(const ArmyAssetPreset& preset) noexcept;
// Retail ArmyAssetRegistry_ResolveOrCreatePreviewTextureCf has a separate preview-only
// color boundary: registry IDs below 400 use the selected faction, while 400+ use
// neutral family 0. Keep this separate from world-placement source-color policy.
bool army_asset_preview_uses_neutral_source_color(uint32_t registry_id) noexcept;
bool army_asset_preview_uses_neutral_source_color(const ArmyAssetPreset& preset) noexcept;
// game.exe selects an army GFX/PAL graphics family from a LEV-authored per-faction
// offset; it is not a universal faction-index permutation. This fallback is used only
// when no LevelAsset is available and therefore assumes a zero raw LEV offset.
uint32_t default_faction_graphics_family(uint32_t faction_index) noexcept;
const char* default_faction_color_name(uint32_t faction_index) noexcept;
const char* graphics_family_color_name(uint32_t graphics_family) noexcept;
// Retail Single Player task-assignment name for the effective army graphics family.
// game.exe selects text 0x2173 + faction_index + the same LEV-authored family offset
// used by the armyN graphics selector, yielding Ares..Golrathen for families 1..8.
const char* graphics_family_single_player_faction_name(uint32_t graphics_family) noexcept;
// Retail Single Player task-assignment text color, encoded 0xRRGGBB.  The stock
// menue.str 0x2174..0x217B 0x8006 payload encodes BLUE/GREEN/RED/ALPHA nibbles;
// game.exe unpacks these in B,G,R order. Separate from the darker armyN swatch.
uint32_t graphics_family_single_player_text_rgb24(uint32_t graphics_family) noexcept;
// Representative UI swatch for the packaged armyN family, encoded 0xRRGGBB.
// Families 1..7 are the normal retail player-colour cycle; family 8 is the
// conditional/special gray family and is not a normal eighth player colour.
uint32_t graphics_family_representative_rgb24(uint32_t graphics_family) noexcept;
bool graphics_family_is_standard_player_color(uint32_t graphics_family) noexcept;

// Exact retail faction-technology model resolver. Each ARM hierarchy node starts at its first
// serialized nonzero MDL, then later ARM variants replace it when their candidate MDL +0x1C0 bit
// is set (last unlocked ARM candidate wins). The selected MDL then scans +0x238..+0x24C and
// replaces itself with the first unlocked candidate. Child ARM nodes are attached recursively to
// the deferred model placeholders selected by the owning MDL hierarchy.
std::vector<ModelVisualNode> resolve_faction_technology_hierarchy(
    const ArmyAssetPreset& preset, const FactionTechnologyMask256& technology_mask,
    uint32_t* selected_root_model_definition_id = nullptr,
    uint32_t* linked_runtime_count = nullptr,
    uint32_t* deferred_attachment_count = nullptr);

// Exact ARM linked-definition selection used by the AI workspace11 gate.  Each linked ARM node
// starts from serialized slot zero and is replaced by later slots whose selected MDL +0x1C0
// technology is unlocked.  The resulting selected MDL for every node must itself be unlocked.
bool army_asset_all_selected_model_technologies_unlocked(
    const ArmyAssetPreset& preset, const FactionTechnologyMask256& technology_mask) noexcept;

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

// V533 InGameLevelRuntime_LoadResourcesAfterDefaultResetCf scans the ARM registry and retains the
// last eligible definitions for class 11, class 14/+0xC0==0, class 14/+0xC0!=0 and class 16.  A
// faction that owns class 18 and no Group-A runtime receives these four definitions in its runtime
// primary/direct-place queue.  The queue itself is not LEV faction data; exposing the catalog
// selection separately lets the editor explain the retail startup result without inventing a map
// field.
std::optional<std::array<std::uint32_t, 4>> retail_starting_primary_asset_ids(
    std::span<const ArmyAssetPreset> presets) noexcept;

// Shared pure builder: editor loads resources; Simulation Lab reads prepared immutable metadata.
std::vector<SimulationWeaponProfile> simulation_weapon_profiles_from_hierarchy(
    std::span<const ModelVisualNode> hierarchy,
    const std::function<const ShotDefinitionRecord*(uint32_t)>& shot_lookup,
    const std::function<std::pair<uint32_t,uint32_t>(std::string_view)>& launch_lookup);

class AssetCatalog {
public:
    static AssetCatalog scan_game_root(const std::filesystem::path& game_root);
    // Read-only asset analysis without requiring an installed executable. The
    // editor's game-installation picker retains scan_game_root's executable gate.
    static AssetCatalog scan_archive_root(const std::filesystem::path& archive_root,
        std::span<const std::string> model_load_order = {},
        std::span<const std::string> army_load_order = {},
        std::span<const std::string> effect_load_order = {},
        std::span<const std::string> shot_load_order = {},
        std::string_view technology_path = {});
    static AssetCatalog scan_archive_root(const std::filesystem::path& archive_root, const LevelAsset& level);
    std::span<const std::string> model_load_order() const noexcept { return model_load_order_; }

    const std::filesystem::path& game_root() const noexcept { return game_root_; }
    std::span<const AssetRecord> assets() const noexcept { return assets_; }
    std::span<const MapAssetGroup> map_groups() const noexcept { return map_groups_; }
    std::span<const std::size_t> general_assets() const noexcept { return general_assets_; }
    std::span<const ArmyAssetPreset> army_presets() const noexcept { return army_presets_; }
    std::span<const uint32_t> general_placement_ids() const noexcept { return general_placement_ids_; }
    const ArmyAssetPreset* army_preset(uint32_t registry_id) const noexcept;
    const EffectDefinitionRecord* effect_definition(uint32_t definition_id) const noexcept;
    const ShotDefinitionRecord* shot_definition(uint32_t definition_id) const noexcept;
    // Build weapon channels from the exact hierarchy currently selected by faction technology.
    // This is deliberately hierarchy-scoped: base ARM models can carry class-7 placeholders with
    // SHT 0 while unlocked replacement MDLs carry the actual weapon definition.
    std::vector<SimulationWeaponProfile> simulation_weapon_profiles_for_hierarchy(
        std::span<const ModelVisualNode> hierarchy) const;
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
    // Physical PCK that supplied this mounted asset. Used by stock-map in-place saves; callers
    // still rewrite the archive transactionally rather than mutating the catalog's cached copy.
    std::filesystem::path backing_archive_path(const AssetRecord& record) const;

private:
    std::filesystem::path game_root_;
    std::vector<std::string> model_load_order_;
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
