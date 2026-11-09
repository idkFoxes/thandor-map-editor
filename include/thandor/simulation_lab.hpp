#pragma once
#include "thandor/simulation_army_pool.hpp"
#include "thandor/simulation_lab_environment_types.hpp"
#include "thandor/simulation_lab_world_pool.hpp"
#include "thandor/purchase_affordability_v600.hpp"

#include "thandor/ai_knowledge.hpp"
#include "thandor/field.hpp"
#include "thandor/level.hpp"
#include "thandor/model.hpp"
#include "thandor/model_animation.hpp"
#include "thandor/simulation.hpp"
#include "thandor/technology.hpp"
#include "thandor/v528_pathing.hpp"
#include "thandor/pioneer_command_chain.hpp"
#include "thandor/pioneer_movement_gate.hpp"
#include "thandor/pioneer_site_selector_trace.hpp"
#include "thandor/pioneer_gate_trace.hpp"
#include "thandor/military_anchor_v599.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <limits>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace thandor {

struct SimulationLabRuntimeCatalog;
struct SimulationLabPlanningWorkspaceSnapshot;
struct SimulationLabLiveShotHull;

enum class SimulationLabSweepMode : std::uint8_t {
    Fixed,
    Linear,
    MinMiddleMax,
};

inline constexpr std::size_t kSimulationLabFactionCount = 8U;
inline constexpr std::size_t kSimulationLabPrimaryQueueCapacity = 64U;
// V537 retail faction-runtime bootstrap defaults. Direct/synthetic callers remain opt-in by
// leaving the corresponding seed fields at zero; the map editor seeds these exact values.
inline constexpr std::uint32_t kSimulationLabRetailInitialStorageLimitQ4 = 4000U;
inline constexpr std::uint32_t kSimulationLabRetailInitialEnergySupplyQ4 = 0x280U;
inline constexpr std::uint32_t kSimulationLabRetailInitialEnergyGenerationCapacityQ4 = 0x280U;

struct SimulationLabPrimaryQueueOverride {
    bool enabled{};
    std::uint32_t count{};
    std::array<std::uint32_t, kSimulationLabPrimaryQueueCapacity> army_asset_ids{};
};

struct SimulationLabConfig {
    std::uint32_t run_count{10U};
    std::uint32_t duration_ticks{20U * 180U};
    std::uint32_t base_seed{0x54484E44U};
    bool vary_seed_per_run{};
    std::uint32_t parameter_index{1U};
    std::int32_t fixed_value{};
    std::int32_t minimum_value{};
    std::int32_t maximum_value{0x1000};
    SimulationLabSweepMode sweep_mode{SimulationLabSweepMode::Linear};
    // Replay input for the recovered class-23 g_InGameSimulationStepTicks multiplier.
    // Only the class-23 deployment callback currently consumes this; unrelated world
    // callbacks still run at the ordinary one-tick Lab cadence.
    std::uint32_t class23_callback_step_ticks{1U};
    // Replay input1..5 for the serviced movement/projectile/production/research/energy callbacks.
    // Other world callbacks still have single-step cadence; this is not a complete speed mode.
    // Independent from coordinator/worker tick batching.
    // Default one preserves the normal20Hz Lab clock and minutes selector.
    std::uint32_t runtime_callback_step_ticks{1U};
    bool fast_build_cheat{};
    // Editor experiment input only. Retail stores this inventory in faction runtime/save state,
    // not in the LEV faction block. When enabled for a faction this replaces the recovered
    // class18/no-Group-A four-entry startup queue for Simulation Lab runs only.
    std::array<SimulationLabPrimaryQueueOverride, kSimulationLabFactionCount>
        primary_queue_overrides{};
    // Scheduling only; zero selects a hardware-sized pool with editor headroom.
    // Does not change a run's tick order, inputs or simulation state.
    std::uint32_t worker_thread_limit{};
};

inline constexpr std::size_t kSimulationLabNoComponent = std::numeric_limits<std::size_t>::max();

struct SimulationLabFactionEconomySeed {
    std::uint32_t xenite_current_q4{};
    std::uint32_t tritium_current_q4{};
    std::optional<std::uint32_t> xenite_storage_limit_q4;
    std::optional<std::uint32_t> tritium_storage_limit_q4;
    // Retail bootstrap starts both resource limits at 4000 Q4 before class-14/15 components
    // contribute their live capacities. Keep the baseline explicit so legacy/synthetic callers
    // can retain zero-origin arithmetic while the map editor seeds the recovered retail value.
    std::uint32_t baseline_xenite_storage_limit_q4{};
    std::uint32_t baseline_tritium_storage_limit_q4{};
    // Legacy/synthetic Simulation Lab callers predate V537 storage semantics. The map editor
    // opts into deriving limits from live class 14/15 model components; tests and external
    // callers can leave this false when their seed does not contain those semantic fields.
    bool derive_storage_limits_from_components{};
    // Manual experiment overrides remain available and take precedence when locked.
    bool xenite_storage_limit_locked{};
    bool tritium_storage_limit_locked{};
    // V537 faction energy allocation. Retail bootstrap initializes both baseline supply and
    // generation capacity to 0x280 Q4; live class-16 components add generation capacity. Keep
    // these baselines explicit/opt-in so legacy synthetic callers preserve their old zero image.
    bool derive_energy_allocation_from_components{};
    std::uint32_t baseline_energy_supply_q4{};
    std::uint32_t baseline_energy_generation_capacity_q4{};
    // Original faction-runtime terrainContributionScaleQ8, multiplied by MDL +0xC8.
    // Zero preserves legacy/synthetic no-extraction behavior; editor inputs must seed
    // the recovered value explicitly (the GUI currently uses a documented unity proxy).
    std::uint32_t terrain_contribution_scale_q8{};
};

// Exact inputs consumed by GameFactionRuntime_RecomputeProgressAndScoreMetrics. Extraction totals
// remain explicit because current stock is not the same quantity as lifetime extracted resource.

struct SimulationLabScenarioRuntimeSeed {
    std::vector<SimulationArmyPlacementSeed> initial_army_placements;
    bool terrain_lighting_cycle_active{};
    bool enabled{};
    std::uint32_t assignable_faction_count{};
    std::uint32_t active_faction_count{};
    // Simulation Lab explicitly runs every active faction autonomously, including faction 1.
    // False preserves original LEV single-/multiplayer assignment rules for direct API callers.
    bool simulate_all_active_factions_as_ai{};
    // Explicit replay input: frontend player ownership is not stored in LEV assignable count.
    std::optional<std::uint32_t> human_faction_mask;
    // Explicit forensic input for captures whose legacy hook corrupted planner calls.
    // This filters planner dispatch only; AI ownership, income scaling and combat stay native.
    // Unset for ordinary runs. Never infer it from out-of-range recorded faction IDs.
    std::optional<std::uint32_t> ai_planner_dispatch_mask;
    // Optional frontend replay counts. LEV alone cannot represent multiple players per faction.
    std::optional<std::array<std::uint32_t,8>> frontend_player_counts;
    std::uint32_t game_speed_q8{0x100U};
    bool ai_planning_enabled{true};
    bool reduced_update{};
    // Explicit replay of UI_COMMAND_RUNTIME_FLAG_LOCAL_FACTION_ENDED (0x08).
    // Frontend state, separate from the per-faction lifecycle records.
    bool local_faction_ended{};
    std::uint32_t active_view_faction_index{1U};
    // Level sound-table entries actually resolved before workers start. Empty means no alerts.
    std::vector<std::uint32_t> mapped_terrain_sound_asset_ids;
    std::uint32_t relation_ui_flags{};
    std::uint32_t relation_state8_faction_group_masks{};
    std::uint32_t relation_state4_faction_group_masks{};
    std::array<LevelScheduledCondition, LevelAsset::kScheduledConditionCount> scheduled_conditions{};
    std::array<LevelEndConditionTrigger, LevelAsset::kEndConditionTriggerCount> end_condition_triggers{};
    // Omitted static map models remain collision owners without entering AI/economy workspaces.
    std::vector<V528PathRuntimeBlocker> static_navigation_blockers;
    std::vector<SimulationLabScenerySeed> crushable_scenery;
    std::shared_ptr<const SimulationLabRuntimeCatalog> runtime_catalog;
    std::vector<SimulationLabEnvironmentalEmitter> environmental_emitters;
    std::vector<SimulationLabStaticPlacementOwner> static_placement_owners;
    std::vector<SimulationLabStaticShotHull> static_shot_hulls;
};

struct SimulationLabFactionScoreSeed {
    std::uint32_t xenite_extracted_total_q4{};
    std::uint32_t tritium_extracted_total_q4{};
};

struct SimulationLabFactionScoreState {
    std::uint32_t xenite_extracted_total_q4{};
    std::uint32_t tritium_extracted_total_q4{};
    std::uint32_t explored_terrain_percent{};
    std::uint32_t unlocked_technology_count_beyond_baseline{};
    std::int32_t primary_resource_component{};
    std::int32_t secondary_resource_component{};
    std::int32_t economy_progress_score{};
    std::int32_t relation_score{};
    std::int32_t points{};
    std::int32_t relation_counter_a{};
    std::int32_t relation_counter_b{};
    std::int32_t relation_counter_c{};
    std::int32_t relation_counter_d{};
    std::int32_t relation_counter_e{};
    std::int32_t relation_counter_f{};
};

struct SimulationLabTechnologyAsset {
    std::uint32_t technology_id{};
    std::array<std::uint32_t, 8> prerequisite_masks{};
    std::uint32_t xenite_cost_q4{};
    std::uint32_t energy_demand_q4{};
    std::uint32_t research_duration_ticks{};
    std::uint32_t dependency_technology_id{};
    std::int32_t ai_base_candidate_score{};
    // TEC +0x34 category (0..3). Research kind 5 gates categories 2/3 on the
    // faction already owning at least one unlocked technology of that category.
    std::uint32_t category{};
    // ModelDefinitionRegistry_FindByRuntimeClassId(TEC id) actually searches
    // MDL +0x1C0 (requiredTechnologyBit) in registry order, NOT runtime class.
    // The GUI resolves this once, before workers start, for score kinds 3/5.
    bool compatible_model_resolved{};
    // True only after the complete current LEV model table was read successfully.
    // A miss then scores zero; it must not use the legacy metadata-free base fallback.
    bool compatible_model_lookup_complete{};
    std::uint32_t compatible_model_definition_id{};
    std::uint32_t compatible_model_runtime_class_id{};
    std::uint32_t compatible_model_registry_position{};
};

struct SimulationLabWeaponSeed {
    SimulationWeaponProfile profile{};
    ShotDefinitionRecord shot{};
};

struct SimulationLabAiArmyWeaponScoreSeed {
    bool valid{};
    std::int32_t reload_divisor30{};
    std::array<std::int32_t, 8> target_class_impact_damage_q12{};
};

struct SimulationLabAiArmyScoreSeed {
    bool resolved{};
    bool all_selected_model_technologies_unlocked{};
    std::int32_t root_definition_value0c{};
    std::int32_t root_definition_value18{};
    std::int32_t root_definition_value60{};
    std::uint32_t root_category_maximum_index5c{};
    std::uint32_t child_list_count{};
    std::array<SimulationLabAiArmyWeaponScoreSeed, 2> child_weapons{};
};

struct SimulationLabLinkedModelTechnology {
    std::uint32_t variant_index{};
    std::uint32_t required_technology_id{}; // MDL +0x1C0
    std::uint32_t granted_technology_id{};  // MDL +0x1C4, independent of requirement
    SimulationLabAiArmyScoreSeed score;
    SimulationLabAiArmyWeaponScoreSeed weapon;
};
struct SimulationLabLinkedModelGroup {
    std::uint32_t parent_index{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t child_slot{};
    std::vector<SimulationLabLinkedModelTechnology> variants;
};

struct SimulationLabResearchPanel {
    std::uint32_t model_definition_id{};
    std::uint32_t runtime_class_id{};
    std::array<std::uint32_t, 28> ids{}; // serialized slots 1..28
};
struct SimulationLabResearchReplacement {
    std::uint32_t required_technology_id{};
    SimulationLabResearchPanel panel;
};
struct SimulationLabResearchVariant {
    std::uint32_t variant_index{};
    std::uint32_t required_technology_id{};
    SimulationLabResearchPanel panel;
    std::vector<SimulationLabResearchReplacement> replacements;
};

struct SimulationLabPendingAssetRequest {
    std::uint32_t army_asset_id{};
    std::uint32_t faction_index{};
    std::uint32_t created_tick{};
    std::uint32_t placement_attempts{};
};

// Explicit player inputs, serialized with stepping. These are commands rather than
// sampled world-state overrides; placement still passes the ordinary live validator.
enum class SimulationLabPlayerCommandKind {
    TakeStock, ReturnStock, PlaceStock, MovePioneer, StopMovement,
    MoveSelection, QueueSelectionWaypoint,
    TargetArmySelection, TargetPositionSelection
};
struct SimulationLabPlayerCommand {
    SimulationLabPlayerCommandKind kind{};
    std::uint32_t player_index{}, faction_index{}, army_asset_id{};
    std::size_t entity_index{kSimulationLabNoComponent};
    // SelectionRuntime_StopMovement consumes 32 nullable entries (0xE30 / Shift+S).
    // An empty slot stays empty; duplicates are legal, as in the native array.
    std::array<std::optional<std::size_t>,32> selected_entity_indices{};
    std::int32_t world_x_q12{}, world_y_q12{}, world_z_q12{};
    std::uint32_t heading_turn16{};
};
struct SimulationLabPlayerPlacement {
    std::uint32_t army_asset_id{}, faction_index{};
};
struct SimulationLabPlayerCommandResult {
    SimulationLabPlayerCommand command;
    bool accepted{};
    std::optional<MovementPoint> placed_point;
};

struct SimulationLabModelComponentSeed {
    std::size_t source_model_node_index{kSimulationLabNoComponent};
    std::uint32_t model_definition_id{};
    std::optional<std::uint32_t> definition_flags68;
    std::uint32_t runtime_class_id{};
    std::int32_t placement_contact_kind{};
    std::int32_t maximum_integrity{};
    std::uint32_t model_rotation_angle0{};
    // Class-14 MDL +0xC0 resource support selector, +0xC8 low-byte share weight.
    std::uint32_t resource_support_class{32U};
    std::uint32_t resource_extraction_weight_u8{};
    // Static resolved hierarchy offset from the initial ArmyRuntime actor root.
    // Unset for synthetic seeds and unresolved factory-produced hierarchies.
    std::optional<Vec3f> extraction_model_world_offset_at_seed;
    std::uint32_t extraction_model_seed_yaw_turn16{};
    std::uint32_t resource_storage_selector{};
    std::uint32_t resource_storage_capacity_contribution_q4{};
    bool has_resource_storage_capacity_contribution{};
    std::uint32_t retail_energy_demand_q4{};
    std::uint32_t energy_generation_capacity_q4{};
    std::array<std::uint32_t, 8> terminal_attachment_effect_ids{};
    std::array<std::int32_t, 8> terminal_attachment_delay_ticks{};
    std::optional<std::size_t> parent_component_index;
    std::int32_t health_regeneration_per_step{};
    std::int32_t health_regeneration_delay_ticks{};
};

struct SimulationLabModelComponentState {
    std::size_t source_model_node_index{kSimulationLabNoComponent};
    std::uint32_t model_definition_id{};
    std::optional<std::uint32_t> definition_flags68;
    std::uint32_t runtime_class_id{};
    std::int32_t placement_contact_kind{};
    std::int32_t maximum_integrity{};
    std::int32_t integrity{};
    std::uint32_t model_rotation_angle0{};
    // Class-14 MDL +0xC0 resource support selector, +0xC8 low-byte share weight.
    std::uint32_t resource_support_class{32U};
    std::uint32_t resource_extraction_weight_u8{};
    // Static resolved hierarchy offset from the initial ArmyRuntime actor root.
    // Unset for synthetic seeds and unresolved factory-produced hierarchies.
    std::optional<Vec3f> extraction_model_world_offset_at_seed;
    std::uint32_t extraction_model_seed_yaw_turn16{};
    // Class-14 runtime signedScalarState: last phase-0 contribution (Q4).
    std::uint32_t last_resource_payout_q4{};
    bool resource_cell_registered{};
    int32_t retained_damage_effect_cooldown{};
    std::uint32_t resource_storage_selector{};
    std::uint32_t resource_storage_capacity_contribution_q4{};
    bool has_resource_storage_capacity_contribution{};
    std::uint32_t retail_energy_demand_q4{};
    std::uint32_t energy_generation_capacity_q4{};
    std::array<std::uint32_t, 8> terminal_attachment_effect_ids{};
    std::array<std::int32_t, 8> terminal_attachment_countdowns{};
    std::uint32_t runtime_flags_ec{};
    std::uint32_t raw_impact_state_f8_ff{};
    bool terminal_runtime_flag8{};
    bool terminal_self_reference{};
    uint32_t last_model_callback_tick{};
    uint32_t terminal_effect_not_before_tick{};
    // Native effect-emitter state advances only when this model callback is serviced.
    uint32_t timed_effect_countdown{1U};
    uint32_t timed_effect_point_index{};
    bool runtime_removed{}; // Tombstone: runtime vector indices remain stable.
    bool terminal_attachment_metadata_prepared{};
    uint32_t terminal_attachment_point_mask{};
    std::array<uint32_t, 8> terminal_attachment_completion_ticks{};
    std::optional<uint32_t> destruction_complete_tick;
    // Last shot already created when the completing EFF entered the native
    // newest-first owner list. Later shots run before this completion.
    std::optional<uint64_t> destruction_effect_after_projectile_serial;
    std::size_t destruction_effect_world_handle{kSimulationLabNoWorldSlot};
    std::int32_t last_impact_overshoot{};
    std::optional<std::size_t> parent_component_index;
    std::int32_t health_regeneration_per_step{};
    std::int32_t health_regeneration_delay_ticks{};
    std::int32_t dismantle_countdown{};
    uint32_t animated_subnode_ticks{};
    std::size_t native_model_pool_slot{kSimulationLabNoComponent};
    // Class-15 callback pose, retained until that MODEL owner runs again.
    std::optional<std::int32_t> storage_child_z_q12;
};

struct SimulationLabSeedEntity {
    std::size_t source_placement_index{};
    std::uint32_t army_asset_id{};
    std::uint32_t faction_index{};
    std::uint32_t runtime_model_class_id{};
    // Selected root MDL +0x74. Retail spawns this authored ARM after the root
    // model hierarchy is released; -1 means no replacement.
    std::int32_t destroyed_replacement_army_asset_id{-1};
    bool building{};
    bool has_traversal_profile{};
    std::uint32_t traversal_low_band_index{};
    std::uint32_t traversal_high_band_index{};
    SimulationMotionProfile motion{};
    std::int32_t water_damage_multiplier{};
    std::int32_t water_damage_threshold_q12{};
    std::int32_t vertical_deployment_step_q12_per_tick{};
    std::int32_t vertical_deployment_travel_limit_q12{};
    // MDL +0x26C: class-23 deployed/retracted endpoint one-shot sound.
    std::uint32_t class23_deployment_sound_asset_id{};
    // V543 carries the class-specific presentation inputs required by the same 20 Hz callbacks
    // used by ordinary simulation. These are prepared by the editor/catalog so worker runs stay
    // asset-I/O free and deterministic.
    bool tank_track_animation_enabled{};
    float tank_track_local_bounds_y0{};
    std::int32_t tank_track_texture_travel_scale{};
    std::int32_t articulated_contact_interpolation_rate_q12{};
    std::uint32_t articulated_contact_sound_asset_id{};
    // V544 moves class-3 contact endpoint ownership into the worker.  The editor resolves the
    // authored terminal-foot XY offsets once when the Lab seed is created; workers then freeze
    // each new target against their own historical root/yaw and FieldGrid without asset I/O.
    bool articulated_contact_geometry_enabled{};
    std::array<Vec3f, 2> articulated_contact_local_offsets{};
    std::int32_t articulated_root_height_q12{};
    std::int32_t articulated_contact_lift_q12{};
    std::uint32_t articulated_contact_steering_limit_turn16{};
    float articulated_shared_foot_placement_offset{};
    // Root MDL movement-ground timed EFF channel. Runtime classes 2/3 service this only when the
    // root actually translates or turns. Carry it into each worker run so the snapshot event journal
    // can preserve pulses that happen between UI polls instead of recreating them at the newest pose.
    std::uint32_t movement_ground_effect_interval_ticks{};
    std::uint32_t movement_ground_land_effect_id{};
    std::uint32_t movement_ground_water_effect_id{};
    SimulationRuntimeSoundProfile runtime_sound_profile{};
    std::uint32_t positioned_sound_gain_q15{};
    std::uint32_t positioned_sound_maximum_distance_q12{};
    // V562 producer inputs are resolved by the editor before the worker starts. Runtime class 11
    // uses the fixed ARM +0x14 bit 0x10 gate, class 13 intersects candidate flags with root MDL
    // +0xC4, and class 22 uses root MDL +0xC4 as its persistent linked-child slot capacity.
    std::uint32_t production_compatibility_mask{};
    std::uint32_t production_storage_capacity{};
    SimulationAircraftDefinition aircraft_definition;
    int32_t airport_platform_step_q12{},airport_platform_limit_q12{},airport_platform_baseline_q12{};
    // V567 keeps the selected root MDL raw fields needed by the class-18 deployment/conversion
    // lifecycle. +0xC0 is the replacement ARM id, +0xC4 the conversion EFF id, +0x19C the
    // candidate-local proximity extent, and +0x1A8 the Group-A owner padding. The EFF lifetime is
    // resolved by the editor before worker start so the Lab remains asset-I/O free.
    std::int32_t runtime_class_specific_c0_raw{};
    std::uint32_t runtime_class_specific_c4_raw{};
    std::int32_t runtime_class_specific_19c_raw{};
    std::int32_t runtime_class_specific_1a8_raw{};
    std::uint32_t class18_conversion_effect_lifetime_ticks{};
    std::uint32_t runtime_flags{};
    std::optional<Vec3f> production_spawn_world;
    std::optional<Vec3f> production_exit_world;
    std::optional<Vec3f> production_drive_target_world; // class13 stored destination; exit is fallback point
    std::optional<Vec3f> production_spawn_local;
    std::optional<Vec3f> production_exit_local;
    float production_root_sprite_height_offset{};
    std::int32_t production_door_step_q12{}; // class13 MDL +0x0C, separate from actor movement
    bool class17_root_child_deployment_enabled{};
    // V537 expands the V534 root-only damage surface into the actual linked ModelRuntime tree.
    // Every component corresponds to one runtime-root MDL node and therefore owns its own
    // ModelRuntimeSlot +0x3C integrity initialized from MDL +0x60.
    std::vector<SimulationLabModelComponentSeed> model_components;
    // The compact Lab has one XY collision proxy per entity. It defaults to the primary root
    // component; tests/tools may select another recovered component without claiming that the Lab
    // reproduces retail per-model-node spatial hit selection.
    std::size_t collision_model_component_index{kSimulationLabNoComponent};
    // Compatibility fallback for older seeds that predate V537 component graphs.
    std::int32_t root_model_maximum_integrity{};
    // Authored linked definitions, in ARM depth-first order. Creation flag 4 grants
    // the selected definition's +0x1C4 technology; selection tests +0x1C0.
    std::vector<SimulationLabLinkedModelGroup> linked_model_technologies;
    // Exact 28-slot MDL technology panel exposure for research-capable runtime entities.
    std::vector<std::uint32_t> research_technology_ids;
    std::vector<SimulationLabResearchVariant> research_model_variants;
    // Worker-owned combat copies. Profiles and SHT records are resolved before thread start so
    // Lab weapon timing/damage/effects never perform catalog I/O.
    std::vector<SimulationLabWeaponSeed> combat_weapons;
    std::uint32_t shot_impact_class_index{};
    std::int32_t fog_uncover_radius_q12{};
    // Max live MDL +0x27C, separate from the fog radius and Pioneer build reach.
    std::uint32_t occupancy_mark_radius_units{};
    // Recovered ArmyRuntime visibility eye-height (+0x48), derived from live MDL +0x70.
    std::int32_t fog_reference_height_offset_q12{};
    std::vector<int32_t> runtime_model_node_height_offsets_q12;
    // Editor-prepared live state seeds full hierarchy weapon/nozzle/class state without making
    // the batch runner depend on catalog or renderer types. Each run receives its own copy.
    SimulationActorState actor{};
};

struct SimulationLabProductionAsset {
    std::uint32_t army_asset_id{};
    std::uint32_t army_runtime_flags14_raw{};
    // Exact raw dwords consumed by AiArmyCandidate_ComputeFactionWeightedScore.
    std::uint32_t ai_definition_class_value70{};
    std::uint32_t ai_definition_class_value74{};
    std::uint32_t ai_definition_class_value78{};
    // The editor resolves the ARM linked-definition lists against each faction's current
    // technology mask before the worker starts.  Direct API tests may leave this false and supply
    // one fallback seed in slot zero.
    bool ai_faction_score_seeds_prepared{};
    std::array<SimulationLabAiArmyScoreSeed, 8> ai_faction_score_seeds{};
    // Baseline slot-zero root values used by TechnologyRuntime_RebuildDerivedLimitsAndCategoryMasks
    // to build the global +0x0C and per-category +0x60 normalizers.
    std::int32_t ai_baseline_root_definition_value0c{};
    std::int32_t ai_baseline_root_definition_value18{};
    std::int32_t ai_baseline_root_definition_value60{};
    std::uint32_t ai_baseline_root_category_maximum_index5c{};
    // First three ARM linked-list nodes (root, child 0, child 1), up to 8
    // definitions each, prepared at catalog load. Kind-5 TEC scoring requires
    // *all* linked variants, rather than the single selected visual hierarchy.
    std::array<std::vector<std::uint32_t>, 3> ai_linked_model_definition_ids{};
    std::uint32_t ai_root_first_variant_required_technology_bit{};
    // Root MDL +0xC0. Retail level initialization distinguishes the two class-14 starter
    // definitions by whether this field is zero.
    std::int32_t runtime_class_specific_c0_raw{};
    std::uint32_t xenite_cost_q4{};
    std::uint32_t build_duration_ticks{};
    std::int32_t energy_demand_q4{};
    // Root MDL +0x190. ArmyRuntime_CreateInstanceFromAssetCf first resolves the faction's
    // technology-selected model definition, so placement must use that selected root's effect.
    // placement_effect_id remains the direct-API/fallback value for callers that do not prepare
    // per-faction technology variants.
    std::uint32_t placement_effect_id{};
    bool placement_effect_ids_prepared{};
    std::array<std::uint32_t, 8> placement_effect_ids_by_faction{};
    // Asset-I/O-free runtime template used when class 11 placement or class 13 completion creates
    // a real Lab entity. Faction and world position are supplied by the owning run at creation.
    SimulationLabSeedEntity entity_seed{};
};

struct SimulationLabGroundAimSnapshot {
    Vec3f position;
    std::optional<Vec3f> route_point;
    bool stationary{};
    std::optional<MovementPoint> path_coordinate;
    std::optional<MovementPoint> damage_tracked_coordinate;
};

struct SimulationLabEntity {
    bool runtime_removed{};
    // Retail runtime class 12: the wreck root advances vertically every service
    // tick; classLinkState's travelled distance is measured against SPR height.
    // Values remain zero unless a selected class-12 MDL and SPR resolve.
    std::int32_t class12_vertical_step_q12_per_tick{};
    std::int32_t class12_model_height_q12{};
    std::int32_t class12_vertical_offset_q12{};
    std::int32_t class12_travel_distance_q12{};
    // MDL +0x190 is construction/dismantle-only in Open Thandor. A combat
    // death must not turn it into an extra explosion.
    // Copy-on-upgrade hierarchy: snapshots share geometry while runtime definitions stay historical.
    std::shared_ptr<const std::vector<ModelVisualNode>> runtime_model_hierarchy;
    std::vector<std::size_t> native_world_model_slots;
    bool native_world_bank_pose_initialized{};
    std::size_t native_army_pool_slot{SIZE_MAX};
    std::size_t source_placement_index{};
    std::uint32_t army_asset_id{};
    std::uint32_t faction_index{};
    std::uint64_t weapon_volleys_fired{};
    std::uint32_t last_weapon_fire_tick{};
    bool articulated_contact_geometry_prepared{};
    std::uint32_t runtime_model_class_id{};
    // Selected root MDL +0x74. Retail spawns this authored ARM after the root
    // model hierarchy is released; -1 means no replacement.
    std::int32_t destroyed_replacement_army_asset_id{-1};
    bool building{};
    uint32_t model_primary_occupancy_mask{},model_occupancy_history{};
    bool model_terrain_state_initialized{}; // retained native visibility, valid even when the mask is zero
    std::optional<std::uint32_t> placement_normal_angles;
    bool synthetic{};
    bool construction_complete{true};
    std::uint32_t construction_start_tick{};
    std::uint32_t construction_complete_tick{};
    bool has_traversal_profile{};
    std::uint32_t traversal_low_band_index{};
    std::uint32_t traversal_high_band_index{};
    SimulationMotionProfile motion{};
    std::int32_t water_damage_multiplier{};
    std::int32_t water_damage_threshold_q12{};
    std::int32_t vertical_deployment_step_q12_per_tick{};
    std::int32_t vertical_deployment_travel_limit_q12{};
    // MDL +0x26C: class-23 deployed/retracted endpoint one-shot sound.
    std::uint32_t class23_deployment_sound_asset_id{};
    std::int32_t vertical_deployment_travel_q12{};
    std::int32_t vertical_deployment_child_local_z_q12{};
    bool tank_track_animation_enabled{};
    float tank_track_local_bounds_y0{};
    std::int32_t tank_track_texture_travel_scale{};
    std::int32_t articulated_contact_interpolation_rate_q12{};
    std::uint32_t articulated_contact_sound_asset_id{};
    bool articulated_contact_geometry_enabled{};
    std::array<Vec3f, 2> articulated_contact_local_offsets{};
    std::int32_t articulated_root_height_q12{};
    std::int32_t articulated_contact_lift_q12{};
    std::uint32_t articulated_contact_steering_limit_turn16{};
    float articulated_shared_foot_placement_offset{};
    // Root MDL movement-ground timed EFF channel. Runtime classes 2/3 service this only when the
    // root actually translates or turns. Carry it into each worker run so the snapshot event journal
    // can preserve pulses that happen between UI polls instead of recreating them at the newest pose.
    std::uint32_t movement_ground_effect_interval_ticks{};
    std::uint32_t movement_ground_land_effect_id{};
    std::uint32_t movement_ground_water_effect_id{};
    SimulationRuntimeSoundProfile runtime_sound_profile{};
    std::uint32_t positioned_sound_gain_q15{};
    std::uint32_t positioned_sound_maximum_distance_q12{};
    // Last root delta is published with each snapshot so the selected run can drive the exact
    // class-specific positioned-loop sound gate without touching worker-owned state.
    std::optional<SimulationLabGroundAimSnapshot> ground_aim_before_movement;
    std::optional<MovementPoint> group_route_pending_point;
    uint32_t group_route_pending_tick{};
    int32_t damage_effect_cooldown{};
    int32_t damage_effect_point{};
    bool last_translated{};
    bool last_turned{};
    SimulationActorState actor{};
    // Native entity pathCoordinate0/1 are not live model translation. The retail
    // workspace-01 distance selector consumes the stored pair, while 00/02 use
    // model-root positions. Absent for legacy/directly constructed fixtures.
    std::optional<MovementPoint> ai_path_coordinate_q12;
    std::optional<MovementPoint> navigation_influence_position_q12;
    // GameEntityRuntime::common.trackedCoordinate{0,1} is a third, distinct
    // pair. Stop Movement and selection relocation update all three together.
    std::optional<MovementPoint> ai_common_tracked_coordinate_q12;
    // GameEntityRuntime::damageState.trackedCoordinate{0,1} are independent
    // from pathCoordinate{0,1}. Only explicit native-equivalent anchor/reset
    // operations and movement commands write it (the stored steering pair).
    // It must not be inferred from a moving root pose.
    std::optional<MovementPoint> ai_damage_tracked_coordinate_q12;
    std::vector<SimulationLabModelComponentState> model_components;
    std::size_t root_model_component_index{kSimulationLabNoComponent};
    std::size_t collision_model_component_index{kSimulationLabNoComponent};
    // Compatibility summaries mirror the primary root component so the existing V534 UI/tests do
    // not silently change meaning while V537 exposes the full component graph.
    std::int32_t root_model_maximum_integrity{};
    std::int32_t root_model_integrity{};
    bool root_model_terminal{};
    std::int32_t root_model_last_impact_overshoot{};
    std::optional<std::size_t> target_entity_index;
    // Native commandTarget.targetFlags bit 2 carries an explicit world point
    // independently of an entity target.  The recovered writer stores its
    // arguments as {Z, X, Y}; this normalized world-space representation keeps
    // that point available to weapon aim and command-marker presentation.
    std::optional<Vec3f> combat_command_target_position_world;
    // Runtime class 0x17 (decimal 23) collision/deployment state. This is distinct from the
    // editor's decimal class-17 banking/deployment preview.
    uint32_t collision_state_bits{};
    uint32_t collision_owner_timer{};
    std::optional<std::size_t> linked_entity_index;

    // Movement +0x18 state is separate from model/class +0xEC runtime_flags.
    PioneerCommandState pioneer_movement{};
    bool pioneer_movement_initialized{};
    // ArmyRuntimeSlot +0x8C / aiUnitState; counted per scheduled busy-unit AI callback.
    std::int32_t pioneer_ai_command_cooldown_ticks{};
    std::uint32_t pioneer_ai_busy_skipped_calls{};
    std::uint32_t pioneer_ai_command_count{};
    std::uint32_t pioneer_ai_last_command_tick{};
    // V589: distinguish a request accepted by the route solver from one for which
    // V528 returned Blocked. An unusable request must not count as a movement order.
    std::uint32_t pioneer_route_rejection_count{};
    std::int32_t pioneer_last_rejected_target_x_q12{};
    std::int32_t pioneer_last_rejected_target_y_q12{};
    // V590: runtime-step/skip telemetry. Distinct from movement-state flags:
    // class-18 may be held (0x08) or immobile before its movement callback runs.
    std::uint32_t pioneer_runtime_step_count{};
    std::uint32_t pioneer_runtime_last_step_tick{};
    std::uint32_t pioneer_hold_skipped_ticks{};
    std::uint32_t pioneer_immobile_skipped_ticks{};
    std::uint32_t pioneer_last_gate_action{}; // 0=not yet sampled, 1..5 enum+1
    PioneerSiteSelectorTrace pioneer_site_selector{}; // V593: real SimLab selector-time scoring only
    PioneerGateTrace pioneer_gate_trace{}; // V594: source gate input/outcome provenance, diagnostic only


    // V599 army +0x8C/0x94 command state. Source fields ARM +0x70/+0x74/+0x78
    // feed the three independent military action scorers.
    std::int32_t military_site_weight{};
    std::int32_t military_anchor_weight{};
    std::int32_t military_secondary_weight{};
    std::int32_t military_ai_busy_cooldown{};
    std::uint32_t military_ai_command_kind{};
    bool military_ai_group_assigned{};
    bool military_ai_source_dispatch{};
    // Represented native commandModeFlags / commandGeneration, separate from
    // local weapon selection and the faction behavior cooldown.
    std::uint32_t combat_command_mode_flags{};
    std::int32_t combat_command_generation{};
    std::optional<std::size_t> combat_assigned_target_entity_index;
    std::uint32_t combat_command_acquisitions{},combat_command_retains{},combat_command_fallbacks{};

    // Native target-follow state, distinct from the transient solver waypoint.
    PioneerCommandState combat_movement;
    bool combat_movement_initialized{},combat_follow_owned{};
    std::uint32_t combat_follow_starts{},combat_follow_stops{},combat_follow_blocked_callbacks{};
    std::uint32_t combat_follow_route_resolves{},combat_follow_route_failures{};
    MilitaryChoiceV599 military_ai_last_choice{}; // score provenance, no planning feedback

    // V565 path scheduler cache. Retail movement consumes a runtime route/waypoint over multiple
    // updates; rebuilding the complete 4x GridScratch image for every moving entity on every 20 Hz
    // Lab tick was editor-only work. Cache the destination scratch cell and retain a proven direct
    // route (or active primary waypoint) until the destination cell changes or a bounded recheck is
    // due. This keeps path semantics deterministic while removing redundant full-grid solves.
    bool navigation_target_cell_valid{};
    V519PathScratchCoordinate navigation_target_cell{};
    std::uint32_t navigation_recheck_tick{};
    bool navigation_direct_route_valid{};

    // V562 producer state. The job is removed from the faction secondary queue and paid only when
    // one eligible producer accepts it. active_energy_demand_q4 is temporary load for that job.
    std::uint32_t production_compatibility_mask{};
    std::uint32_t production_storage_capacity{};
    SimulationAircraftDefinition aircraft_definition;
    int32_t airport_platform_step_q12{},airport_platform_limit_q12{},airport_platform_baseline_q12{};
    SimulationClass22AirportState airport;
    bool airport_initialized{};
    SimulationAircraftFlight aircraft;
    std::int32_t runtime_class_specific_c0_raw{};
    std::uint32_t runtime_class_specific_c4_raw{};
    std::int32_t runtime_class_specific_19c_raw{};
    std::int32_t runtime_class_specific_1a8_raw{};
    std::uint32_t class18_conversion_effect_lifetime_ticks{};
    // Mirrors ArmyRuntimeSlot +0xEC for the bits the Lab currently models. V567 uses 0x08 as the
    // conversion/deployment hold and honors the exact 0x18 eligibility mask from the Group-A
    // class-command handler.
    std::uint32_t runtime_flags{};
    std::uint32_t class18_conversion_effect_start_tick{};
    std::uint32_t class18_conversion_effect_complete_tick{};
    std::uint32_t class18_conversion_effect_remaining_ticks{};
    std::size_t class18_conversion_world_handle{kSimulationLabNoWorldSlot};
    std::optional<Vec3f> production_spawn_world;
    std::optional<Vec3f> production_exit_world;
    std::optional<Vec3f> production_drive_target_world;
    std::optional<Vec3f> production_spawn_local;
    std::optional<Vec3f> production_exit_local;
    float production_root_sprite_height_offset{};
    std::int32_t production_door_step_q12{};
    bool production_building{};
    bool production_child3_removed{};
    // Runtime class-13 door cycle. 0 idle, 1 building, 2 opening,
    // 3 waiting for the spawned army to clear the factory, 4 closing.
    std::uint32_t production_behavior_state{};
    std::uint32_t production_door_texture_offset_v{};
    std::uint32_t production_animated_subnode_ticks{};
    std::optional<std::size_t> production_linked_child_entity_index;
    // Persistent asset IDs stay on the pad. A physical class-21 instance exists only during a sortie.
    bool production_parked_on_pad{};
    std::uint32_t production_asset_id{};
    std::uint32_t production_start_tick{};
    std::uint32_t production_exit_command_tick{};
    std::uint32_t production_elapsed_ticks{};
    std::uint32_t production_duration_ticks{};
    std::int32_t production_active_energy_demand_q4{};
    std::array<std::uint32_t, kSimulationClass22StoredSlotCount> production_linked_child_asset_ids{};
    // Matches class-22 linkedChildSlotMask: a successful physical spawn sets
    // its slot bit. A reserved ARM id does not itself mean a launched child.
    std::uint32_t production_linked_child_slot_mask{};
    std::uint32_t production_linked_child_count{};

    // Class 13 creates a physical child at completion, links it to the factory, marks it active,
    // then gives it the recovered exit move. The link is cleared after that move completes.
    std::optional<std::size_t> production_parent_entity_index;
    bool production_linked_to_parent{};
    bool production_movement_unlock_pending{};

    // Research state mirrors runtime flags 0x80 (pending payment) and 0x40 (active). A source may
    // expose exactly the root MDL's 28 technology-panel ids and can own one pending/active job.
    std::vector<std::uint32_t> research_technology_ids;
    std::vector<SimulationLabResearchVariant> research_model_variants;
    bool research_pending_payment{};
    bool research_active{};
    std::uint32_t research_technology_id{};
    std::uint32_t research_elapsed_ticks{};
    std::uint32_t research_duration_ticks{};
    std::uint32_t research_xenite_due_q4{};
    std::uint32_t research_energy_demand_q4{};

    std::vector<SimulationLabWeaponSeed> combat_weapons;
    std::uint32_t shot_impact_class_index{};
    std::int32_t fog_uncover_radius_q12{};
    // Max live MDL +0x27C, separate from the fog radius and Pioneer build reach.
    std::uint32_t occupancy_mark_radius_units{};
    // Recovered ArmyRuntime visibility eye-height (+0x48), derived from live MDL +0x70.
    std::int32_t fog_reference_height_offset_q12{};
    std::vector<int32_t> runtime_model_node_height_offsets_q12;
};

enum class SimulationLabOneShotEventKind : std::uint8_t {
    ArticulatedContact = 1U,
    MovementGroundEffect = 2U,
    WeaponLaunchEffect = 3U,
    WeaponImpactEffect = 4U,
    PioneerConversionEffect = 5U,
    TerminalAttachmentEffect = 6U,
    ConstructionPlacementEffect = 7U,
    Class23DeploymentSound = 8U,
    ModelTimedEffect = 9U,
    PositionedEffectSound = 10U, // Sound-only journal entry; ambient geometry already has an owner.
    GlobalMappedTerrainSound = 11U,
};

// Immutable event payload retained through persistent linked nodes. SimulationLabExperiment is
// copied for every published worker generation, so a shared immutable chain avoids repeatedly
// deep-copying the whole historical event stream while still preserving every skipped worker tick.
struct SimulationLabTerminalEffectPoint {
    Vec3f position{};
    std::array<float,9> basis{1,0,0,0,1,0,0,0,1};
};
inline std::size_t simulation_lab_world_model_handle(const SimulationLabEntity& entity) noexcept {
    return entity.native_world_model_slots.empty()?kSimulationLabNoWorldSlot:entity.native_world_model_slots.front();
}

struct SimulationLabOneShotEvent {
    std::uint64_t sequence{};
    std::uint32_t tick{};
    std::size_t source_placement_index{};
    std::uint32_t army_asset_id{};
    std::uint32_t faction_index{};
    SimulationLabOneShotEventKind kind{SimulationLabOneShotEventKind::ArticulatedContact};
    std::uint32_t sound_asset_id{};
    std::uint32_t sound_listener_faction_index{};
    std::optional<std::array<uint32_t,2>> sound_gain_and_distance; // Historical live MDL, including technology variant.
    std::uint32_t completed_side{};
    std::uint32_t effect_id{};
    std::size_t source_model_node_index{kSimulationLabNoComponent};
    std::uint32_t attachment_channel{};
    // Prepared on the simulation tick: synthetic actors have no LEV placement to look up
    // later, and removed/replaced hierarchies must not redirect historical effects.
    std::vector<SimulationLabTerminalEffectPoint> terminal_attachment_world_points;
    Vec3f root_world_position{};
    std::optional<std::array<float,9>> effect_world_basis;
    std::optional<std::array<uint32_t,3>> effect_world_angles;
    // Historical actor state lets the editor reconstruct the exact hierarchy/foot pose from the
    // event tick rather than from whichever newer snapshot happened to reach the render thread.
    SimulationActorState actor{};
};

struct SimulationLabOneShotEventNode {
    SimulationLabOneShotEvent event{};
    std::shared_ptr<const SimulationLabOneShotEventNode> previous;

    SimulationLabOneShotEventNode(SimulationLabOneShotEvent event,
                                  std::shared_ptr<const SimulationLabOneShotEventNode> previous);
    ~SimulationLabOneShotEventNode();
};

// V755 bounded command-issue evidence for the four military AI writers that
// still require matched retail traces.  The compact Lab keeps only the latest
// records because SimulationLabRun is copied into worker/UI snapshots.  Every
// record carries a monotonic sequence so repeated diagnostic samples can be
// deduplicated without depending on vector identity or snapshot cadence.
inline constexpr std::size_t kSimulationLabMilitaryOrderTraceCapacity = 64U;

enum class SimulationLabMilitaryOrderWorkspace : std::uint32_t {
    General05 = 5U,
    Visible07 = 7U,
    BoundedUnseen03 = 3U,
    FactionAnchor = 0xFFFFFFFEU,
};

struct SimulationLabMilitaryOrderTrace {
    std::uint64_t sequence{};
    std::uint32_t tick{};
    std::uint32_t faction_index{};
    std::uint32_t source_entity_index{UINT32_MAX};
    SimulationLabMilitaryOrderWorkspace workspace{SimulationLabMilitaryOrderWorkspace::General05};
    std::uint32_t workspace_entry_index{UINT32_MAX};
    std::uint32_t target_entity_index{UINT32_MAX};
    std::uint32_t collected_count{};
    std::uint32_t accumulated_strength{};
    std::int32_t selected_score{};
    std::int32_t best_site_score{};
    std::int32_t best_anchor_score{};
    std::int32_t best_hostile_score{};
    MovementPoint requested{};
    MovementPoint primary{};
    MovementPoint fallback{};
    MovementPoint stored{};
    std::uint32_t command_mode{};
    std::int32_t command_generation{};
    std::uint32_t movement_flags{};
    std::uint32_t cooldown{};
    bool route_started{};
    bool direct_route{};
    bool target_owner_live{};
    bool target_from_unseen{};
};

// A native MODEL owner disappears when either the ArmyRuntime tombstone is
// set or its linked root ModelRuntime has been released.  Attached component
// tombstones do not remove the root owner.  Metadata-only fixtures without a
// component image still represent one live root owner.
inline bool simulation_lab_has_live_root_model_owner(
    const SimulationLabEntity& entity) noexcept {
    if (entity.runtime_removed) return false;
    return entity.root_model_component_index >= entity.model_components.size() ||
           !entity.model_components[entity.root_model_component_index].runtime_removed;
}

inline bool simulation_lab_production_effect_active(const SimulationLabEntity& entity) noexcept {
    if(entity.runtime_removed || entity.root_model_terminal || !entity.construction_complete) return false;
    const auto flags = entity.root_model_component_index < entity.model_components.size()
        ? entity.model_components[entity.root_model_component_index].runtime_flags_ec : 0U;
    if(((flags | entity.runtime_flags) & 9U) != 0U) return false; // switched off / destroyed
    const bool class13=entity.runtime_model_class_id==13U;
    return ((entity.research_active || ((flags|entity.runtime_flags)&0x40U)!=0U) &&
            (!class13 || entity.production_behavior_state==0U)) ||
        ((entity.production_building || ((flags|entity.runtime_flags)&0x100U)!=0U) &&
            (!class13 || entity.production_behavior_state==1U));
}

inline bool simulation_lab_model_node_present(const SimulationLabEntity& entity, size_t node) noexcept {
    if (entity.runtime_removed ||
        (entity.runtime_model_hierarchy && node >= entity.runtime_model_hierarchy->size())) return false;
    if(entity.runtime_model_hierarchy){
        const auto& nodes=*entity.runtime_model_hierarchy;
        size_t cursor=node;
        for(size_t hops=0;cursor<nodes.size()&&hops<nodes.size();++hops){
            const auto parent=nodes[cursor].parent_index;
            if(parent>=nodes.size())break;
            const auto grandparent=nodes[parent].parent_index;
            const bool runtime_root=grandparent>=nodes.size()||
                nodes[grandparent].model_definition_id!=nodes[parent].model_definition_id;
            const auto cls=nodes[parent].runtime_class_id;
            if(runtime_root&&(cls==14U||cls==15U)&&
               !model_runtime_world_child_enabled(cls,nodes[cursor].child_slot))return false;
            cursor=parent;
        }
    }
    if(entity.production_child3_removed&&entity.runtime_model_hierarchy){
        const auto& nodes=*entity.runtime_model_hierarchy;
        size_t cursor=node;
        for(size_t hops=0;cursor<nodes.size()&&hops<nodes.size();++hops){
            const auto parent=nodes[cursor].parent_index;
            if(parent<nodes.size()&&nodes[parent].parent_index==UINT32_MAX&&nodes[cursor].child_slot==3U)
                return false;
            cursor=parent;
        }
    }
    for (const auto& component : entity.model_components) {
        if (!component.runtime_removed) continue;
        size_t cursor = node;
        for (size_t hops = 0; hops <= (entity.runtime_model_hierarchy ? entity.runtime_model_hierarchy->size() : 0U); ++hops) {
            if (cursor == component.source_model_node_index) return false;
            if (!entity.runtime_model_hierarchy || cursor >= entity.runtime_model_hierarchy->size()) break;
            cursor = (*entity.runtime_model_hierarchy)[cursor].parent_index;
        }
    }
    return true;
}

struct SimulationLabRunMetrics {
    uint64_t extended_placement_searches{},extended_placement_tests{},extended_placement_accepts{};
    uint64_t placement_connectivity_tests{},placement_connectivity_rejections{};
    uint32_t live_extractor_site_tests{}, live_extractor_site_accepts{};
    uint32_t special_site_reservation_tests{}, special_site_reservation_accepts{};
    uint32_t special_site_probe_owners{}, live_storage_site_bonus_events{};
    uint32_t model_hierarchies_removed{};
    uint32_t entities_removed{};
    uint32_t destroyed_replacement_armies_spawned{};
    uint32_t destroyed_replacement_armies_unresolved{};
    std::uint32_t planning_cycles{};
    std::uint32_t ai_candidate_cache_rebuilds{};
    std::uint32_t ai_candidate_cache_replays{};
    std::uint32_t ai_candidate_cache_saves{};
    std::uint32_t ai_purchase_candidates_applied{};
    std::uint32_t planned_builds{};
    std::uint32_t completed_builds{};
    std::array<std::uint32_t, 4> candidate_builds{}; // ARM 14A, 14C, 14B, 14D
    std::uint32_t target_assignments{};
    std::uint32_t military_ai_selector_calls{};
    std::uint32_t military_ai_site_orders{};
    std::uint32_t military_ai_anchor_orders{};
    std::uint32_t military_ai_group_orders{};
    std::uint32_t military_ai_busy_skips{};
    std::uint32_t military_ai_site_orders_with_live_target{};
    std::uint32_t military_ai_anchor_orders_with_live_target{};
    std::uint32_t military_ai_unseen_group_targets_bounded{};
    std::uint32_t military_ai_unseen_group_targets_unavailable{};
    std::uint32_t impact_anchor_updates{};
    std::uint32_t impact_anchor_decay_steps{};
    std::uint32_t impact_anchor_expirations{};
    std::uint32_t impact_anchor_notification_gates{};
    std::uint32_t collision_contacts{};
    std::uint32_t class23_links{};
    std::uint32_t class0_impact_events{};
    std::uint32_t class0_terminal_impacts{};
    std::uint64_t class0_impact_damage_applied{};
    std::uint64_t class0_impact_overshoot{};
    std::uint32_t component_terminal_events{};
    std::uint32_t child_component_terminal_events{};
    std::uint32_t terminal_attachment_effect_events{};
    std::uint32_t terminal_root_destruction_effect_events{};
    std::uint32_t terminal_attachment_world_points_prepared{};
    std::uint32_t component_parent_propagations{};
    std::uint32_t root_relation_counter_c_events{};
    std::uint32_t root_relation_counter_d_events{};
    std::uint32_t root_direct_impact_angle_updates{};
    std::uint32_t pending_requests_created{};
    std::uint32_t pending_requests_consumed{};
    std::uint32_t pending_placement_retries{};
    std::uint32_t pending_capacity_blocks{};
    std::uint32_t secondary_purchase_requests_created{};
    std::uint32_t secondary_purchase_requests_consumed{};
    std::uint32_t production_jobs_started{};
    std::uint32_t production_jobs_completed{};
    std::uint32_t class11_primary_completions{};
    std::uint32_t class13_units_spawned{};
    std::uint32_t class22_assets_stored{};
    std::uint32_t class22_aircraft_spawned{};
    std::uint32_t retail_initial_primary_queue_factions{};
    std::uint32_t retail_initial_primary_queue_assets{};
    std::uint32_t lab_primary_queue_override_factions{};
    std::uint32_t lab_primary_queue_override_assets{};
    std::uint32_t primary_queue_capacity_blocks{};
    std::uint32_t unknown_storage_capacity_blocks{};
    std::uint32_t xenite_full_weight_boosts{};
    std::uint32_t derived14d_weight_events{};
    std::uint32_t tank_track_updates{};
    std::uint32_t articulated_contact_completions{};
    std::uint32_t movement_ground_effect_events{};
    std::uint32_t research_jobs_queued{};
    std::uint32_t research_jobs_started{};
    std::uint32_t research_jobs_completed{};
    std::uint64_t runtime_technology_replacements{};
    std::uint64_t runtime_technology_invalid_health_scales{};
    std::uint64_t navigation_actual_step_blocks{};
    std::uint64_t factory_exit_route_retries{};
    std::uint32_t production_timed_effects_spawned{};
    std::uint32_t weapon_shots_fired{};
    std::uint32_t projectile_impacts{};
    // Contact telemetry distinguishes hits on detachable child MDLs from root hits.
    std::uint32_t projectile_root_component_hits{};
    std::uint32_t projectile_child_component_hits{};
    std::uint32_t projectile_repair_contacts_healed{};
    std::uint32_t projectile_repair_contacts_rejected{};
    std::uint64_t projectile_repair_integrity_restored{};
    std::uint32_t environmental_effects_spawned{};
    std::uint32_t environmental_shots_spawned{};
    std::uint64_t projectile_damage_applied{};
    std::uint64_t water_damage_applications{};
    std::uint64_t water_damage_integrity_lost{};
    std::uint32_t weapon_launch_effect_events{};
    std::uint32_t weapon_impact_effect_events{};
    std::uint32_t class17_pose_updates{};
    std::uint32_t pioneer_conversion_effects_started{};
    std::uint32_t pioneer_conversions_completed{};
    std::uint32_t navigation_route_resolves{};
    std::uint32_t navigation_routed_solves{};
    std::uint32_t navigation_deferred_route_solves{};
    std::uint32_t navigation_cached_ticks{};
    // Native overlapping-group route rebuild diagnostics. The fixed pair image
    // is independent from the Lab's generic route-solve budget.
    std::uint32_t navigation_overlap_group_rebuilds{};
    std::uint32_t navigation_overlap_group_pairs{};
    std::uint32_t navigation_overlap_group_capacity_reached{};
    std::uint32_t navigation_overlap_group_source_replans{};
    // V574: raw retail move commands are resolved/clipped before the actor owns a final target.
    std::uint32_t navigation_destination_retargets{};
    // V574: ARM301 is emitted by two recovered retail candidate families instead of being omitted.
    std::uint32_t factory301_candidate_events{};
    double movement_distance_world{};
    double military_movement_distance_world{};
};

// At most three packed workspace-13 entries survive each unsuccessful retail
// candidate purchase pass. Keeping id and kind separate avoids conflating ARM
// ids with TEC ids (both namespaces overlap).
struct SimulationLabCachedAiCandidate {
    std::uint32_t id{};
    std::int32_t packed_score{};
    std::uint32_t kind{}; // 1=ARM purchase, 2=TEC research
};

// V587 source-of-truth sample of the actual bounded workspace-08 records consumed by
// both Pioneer navigation and pending-construction placement. The preflight `in_build_reach`
// is a range-only observation, NOT a retail placement result (terrain, hull, mode, and
// reservation gates can still reject the cell).
struct SimulationLabResourceSiteSample {
    std::uint32_t army_asset_id{};
    Vec3f point{};
    std::int32_t priority{};
    std::uint32_t nearest_constructor_distance_q12{};
    bool in_build_reach{};
    // Unlike V587's center-only radius, mode 0 adds the extractor MDL +0x1A8
    // candidate extent to each constructor's class-specific build reach.
    std::int32_t placement_candidate_extent_q12{};
    bool mode0_radius_with_extent{};
    // Score against the first live class-18 owner; telemetry, not an admission
    // verdict. Retail mode-3 bucket and reservation queries are still pending.
    std::int32_t pioneer_weighted_score{};
    // Inputs to the score as observed on the faction's planning pass, not
    // inferred from the GUI's later render position.
    std::int64_t pioneer_secondary_distance_q12{INT32_MAX};
    std::int64_t pioneer_visible_hostile_distance_q12{INT32_MAX};
    std::int64_t pioneer_distance_q12{INT32_MAX};
    std::uint32_t pioneer_assigned_count{};
    // V593 source-field support-only preflight. NEVER a complete retail
    // ArmyPlacement_CanPlaceAssetAtFieldPoint (collision, terrain, field blocker,
    // per-MDL runtime owner list and temporary ARM333 insertion are missing).
    bool support_only_inputs_available{};
    std::uint32_t support_only_owner_count{};
    bool support_only_mode3_reached{};
    std::uint32_t support_only_mode3_clearance_q12{};
    bool support_only_mode0_reached{};
};

// Immutable after each faction's economy planning phase. Exposed by the GUI so idle
// factions can be diagnosed without conflating "planned" with "placed" structures.
struct SimulationLabAiWorkspaceDiagnostics {
    std::uint32_t last_planning_tick{};
    std::uint32_t own_entities{};
    std::uint32_t live_pioneers{};
    std::uint32_t live_structures{};
    std::uint32_t effective_presence_owners{};
    std::uint32_t general05{};
    std::uint32_t flagged06{};
    std::uint32_t resource08{};
    std::uint32_t base09{};
    std::uint32_t large10{};
    std::array<SimulationLabResourceSiteSample, 32> resource_site_samples{};
    std::uint32_t resource_site_sample_count{};
};

struct SimulationLabRetailScratchImage {
    // Native pathCost scratch for AI placement ring connectivity; not serialized.
    std::vector<uint32_t> placement_path_cost;
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint32_t> state;
    // Runtime occupancy is deliberately independent from fog and build-radius decisions.
    // Field cells carry one bit per faction; the case-7 writer rebuilds these bits.
    std::vector<std::uint8_t> field_presence_by_faction;
    // Original occupancy bit 1 comes from the model hex scan, but the
    // scratch-grid presence classifier uses mask 0xF9 (excludes bit 1).
    std::vector<std::uint8_t> field_occupancy_bit1_by_faction;
    // Retail FLD occupancyMask: eight packed faction bytes per field cell.
    // Phase 7 clears bits 0..6, then LOS/other owner callbacks repopulate
    // current bits while explored bit 7 persists. Bit 1 is kept distinct
    // from the 0xF9 predicate used by the AI scratch-site scanner.
    std::vector<std::uint64_t> field_occupancy_masks;
    std::uint32_t influence_update_tick{};
    std::uint32_t presence_update_tick{};

    std::uint32_t read(std::uint32_t x, std::uint32_t y) const noexcept {
        return x < width && y < height
            ? state[static_cast<std::size_t>(y) * width + x] : 0x80000000U;
    }
    // Retail uses linear scratch-pointer offsets. Do not normalize cross-row
    // offsets to hex x/y pairs: the original arithmetic permits row carry.
    std::uint32_t six(std::uint32_t x, std::uint32_t y,
                      std::uint32_t row_stride, std::uint32_t step) const noexcept {
        const auto base=static_cast<std::int64_t>(y)*width+x;
        const auto get=[&](std::int64_t index) noexcept -> std::uint32_t {
            return index>=0 && static_cast<std::uint64_t>(index)<state.size()
                ? state[static_cast<std::size_t>(index)] : 0x80000000U;
        };
        return get(base) | get(base+step) | get(base+row_stride-step) |
               get(base+row_stride+step) | get(base+row_stride*2U-step) |
               get(base+row_stride*2U);
    }
};

struct SimulationLabRun {
    SimulationLabWorldObjectPool native_world_objects;
    // Non-owning view scoped to a planning dispatch that absorbed its own faction.
    const SimulationLabPlanningWorkspaceSnapshot* active_planning_snapshot{};
    SimulationArmyPool army_pool;
    std::array<bool,8> ai_active_entity_pressure_latched{};
    // Shared native WS03 retains its written prefix across faction rebuilds.
    std::size_t ai_unseen03_written_prefix{};
    std::vector<std::size_t> ai_unseen03_model_slots;
    struct DeferredFactoryExitRoute {
        size_t child{},parent{};
        MovementPoint exit{},rally{};
    };
    // Factory bookkeeping may be prepared early; commands take effect at the
    // factory's position in the captured native owner callback chain.
    bool defer_factory_exit_routes{};
    std::vector<DeferredFactoryExitRoute> deferred_factory_exit_routes;
    // Run-local terrain; copy before mutation when a worker snapshot shares it.
    std::shared_ptr<FieldGrid> runtime_field;
    bool terrain_lighting_cycle_active{};
    bool runtime_normals_dirty{};
    // Occupied entries are empty. Freed native model slots retain their smoke timer.
    std::vector<std::optional<std::int32_t>> vacant_model_smoke_cooldowns;
    // Model creation leaves the recycled timed-emitter point selector intact.
    std::vector<std::uint32_t> retained_model_effect_points;

    // Lab routing budget rotates independently of the stable owner movement order.
    std::size_t navigation_routed_solve_cursor{};
    std::uint32_t run_index{};
    std::uint32_t seed{};
    std::uint32_t rng_state{};
    std::uint32_t tick{};
    std::int32_t swept_value{};
    bool finished{};
    std::array<std::uint32_t, AiKnowledgeData::kParameterCount> ai_parameters{};
    std::vector<SimulationLabEntity> entities;
    // Number of linked static level owners held outside the mutable entity vector.
    // Retained in snapshots so editor fog and phase-7 world jobs agree about an empty world.
    std::size_t static_placement_owner_count{};
    std::array<SimulationLabPlayerPlacement, 256> player_pending_placements{};
    std::vector<SimulationLabPlayerCommand> player_commands;
    // Results for the most recently stepped tick, including rejected commands.
    std::vector<SimulationLabPlayerCommandResult> player_command_results;
    std::array<SimulationLabMilitaryOrderTrace,
               kSimulationLabMilitaryOrderTraceCapacity> military_order_trace{};
    std::uint64_t next_military_order_trace_sequence{1U};
    std::uint32_t military_order_trace_count{};
    std::uint32_t military_order_trace_write_index{};
    std::uint64_t military_order_trace_overwrites{};
    // Immutable, per-run phase-persistent scratch grid. Worker snapshots share the
    // old generation; world-job updates publish a new one (copy on write), avoiding
    // races with the GUI's SimulationLabRun copies.
    std::shared_ptr<const SimulationLabRetailScratchImage> retail_scratch;
    // Query cache belongs to this run; parallel runs never bind one another's
    // mutable snapshot selector. The underlying retained grid stays immutable.
    V565PathTerrainCache runtime_navigation_cache;
    // Direct API callers may edit FieldGrid between step() batches. This is the
    // last externally supplied field signature, NOT a normal retail tick event.
    std::uint64_t external_field_signature{};
    bool external_field_signature_valid{};


    // Isolated run-local image of the two faction terminal counters touched by 0x0052A200/2E0.
    std::array<std::uint32_t, 8> faction_relation_counter_c{};
    std::array<std::uint32_t, 8> faction_relation_counter_d{};
    // V572 level-runtime image. When enabled this mirrors the retail bootstrap relation rows,
    // authored LEV relation groups, faction lifecycle state, and mutable 64-condition/16-trigger
    // schedule used by InGameConditionRuntime_UpdateScheduledRecords.
    bool level_runtime_enabled{};
    std::uint32_t assignable_faction_count{};
    std::uint32_t active_faction_count{};
    std::uint32_t ai_controlled_faction_mask{};
    std::optional<std::uint32_t> ai_planner_dispatch_mask;
    std::uint32_t game_speed_q8{0x100U};
    bool ai_planning_enabled{true};
    bool reduced_update{};
    // Explicit replay of UI_COMMAND_RUNTIME_FLAG_LOCAL_FACTION_ENDED (0x08).
    // Frontend state, separate from the per-faction lifecycle records.
    bool local_faction_ended{};
    std::uint32_t active_view_faction_index{1U};
    std::array<std::uint32_t,8> faction_relation_transition_ticks{};
    std::vector<std::uint32_t> mapped_terrain_sound_asset_ids;
    std::uint32_t relation_ui_flags{};
    std::array<std::uint32_t, 8> faction_packed_relation_states{};
    std::array<std::uint32_t,8> faction_capability_flags{};
    std::array<std::array<std::uint32_t,8>,8> faction_pair_change_ticks{};
    std::array<std::array<std::uint32_t,8>,8> faction_pair_pressure{};
    std::array<std::uint32_t,8> faction_frontend_player_counts{};
    std::array<std::uint32_t,8> faction_merge_redirect{0,1,2,3,4,5,6,7};
    struct RelationTrace {uint32_t tick{},faction{},draws{},transitions{},merges{};} relation_trace;
    std::array<std::uint8_t, 8> faction_lifecycle_states{};
    std::array<std::uint32_t, 8> faction_xenite_extraction_rate_q4_per_tick{};
    std::array<std::uint32_t, 8> faction_tritium_extraction_rate_q4_per_tick{};
    std::array<LevelScheduledCondition, LevelAsset::kScheduledConditionCount> scheduled_conditions{};
    std::array<LevelEndConditionTrigger, LevelAsset::kEndConditionTriggerCount> end_condition_triggers{};
    std::optional<std::uint32_t> last_fired_end_trigger_index;
    std::array<SimulationLabFactionEconomySeed, 8> faction_economy{};
    std::array<SimulationLabFactionScoreState, 8> faction_score{};
    // V568 AiFactionRuntime_RebuildPlanningCapacityState image. Retail preserves eight signed
    // per-faction pressure channels across planning cycles and rebuilds the maximum after applying
    // decay and active hostile-runtime contributions. Army candidate scoring consumes the previous
    // completed image, then the planning dispatcher refreshes it for the next cycle.
    std::array<std::array<std::int32_t, 8>, 8> faction_ai_pressure_values{};
    std::array<std::int32_t, 8> faction_maximum_ai_pressure{};
    std::array<std::array<SimulationLabCachedAiCandidate, 3>, 8> ai_candidate_cache{};
    std::array<std::uint32_t, 8> ai_candidate_cache_count{};
    std::array<std::uint32_t, 8> ai_candidate_cache_reuse_state{};
    struct AiV601ArmyProfileTrace {
        std::uint32_t last_rebuild_tick{};
        std::uint32_t workspace11_count{};
        // One entry for each recovered A (defence), B (exploration), C (attack) scorer.
        std::array<std::uint32_t, 3> best_asset_id{};
        std::array<std::int32_t, 3> best_score{};
        std::array<std::uint32_t, 3> best_advanced_asset_id{};
        std::array<std::int32_t, 3> best_advanced_score{};
        std::array<std::uint32_t, 3> profile_invocations{};
        std::uint32_t eligible_tracked{};
        std::uint32_t eligible_walkers{};
        std::uint32_t eligible_gliders{};
        std::uint32_t eligible_aircraft{};
        std::uint32_t eligible_naval{};
        std::uint32_t eligible_turrets{};
    };
    std::array<AiV601ArmyProfileTrace, 8> ai_army_profile_trace{};
    struct ResearchCandidateTrace {
        uint32_t technology_id{},source_army_id{},source_class{},score{};
        uint32_t compatible_model_id{},compatible_model_class{},category{},reason{};
        uint32_t score_kind{},source_entity_index{},panel_slot{};
        int32_t base_score{};
        uint32_t xenite_cost_q4{},energy_demand_q4{},duration_ticks{};
        bool compatible_model_resolved{}, compatible_model_lookup_complete{};
        uint32_t compatible_arm_matches{},compatible_arm_positive{},compatible_arm_missing_metadata{};
        uint32_t locked_root_child_matches{},best_potential_arm_id{};
        int32_t best_potential_arm_score{};
        // reason: 0 evaluated; 1 nonpositive base; 2/3 category gate; 4 no model;
        // 5 native address score unavailable; 6 legacy base fallback; 7 class1 gate;
        // 8 minimum Xenite; 9 zero supply; 10 low/signed-negative pressure;
        // 11 no positive compatible army; 12 signed-nonpositive score; 13 unsafe quotient.
    };
    struct ResearchTrace {
        std::array<ResearchCandidateTrace,32> candidates{};
        uint32_t candidate_count{};
        std::uint32_t last_rebuild_tick{};
        bool arms_factory_present{};
        std::uint32_t source_count{};
        std::uint32_t admitted_candidates{};
        std::uint32_t positive_candidates{};
        bool candidate_cap_reached{};
        std::uint32_t best_technology_id{};
        std::uint32_t best_score{};
        std::uint32_t compatibility_index_entries_visited{};
        std::uint32_t potential_seed_resolutions{};
        std::uint32_t potential_score_evaluations{};
        std::uint32_t potential_score_cache_hits{};
        std::uint32_t last_purchase_tick{};
        std::uint32_t last_purchase_technology_id{};
        bool last_purchase_queued{};
        std::uint32_t last_completed_technology_id{};
        std::uint32_t last_completion_tick{};
    };
    std::array<ResearchTrace, 8> ai_research_trace{};
    struct SpecialSiteTrace {
        uint32_t tick{},workspace08_count{},cluster_tests{},cluster_accepts{},hostile_blocks{},probes_created{};
        uint32_t candidate_id{},candidate_kind{},weight{},site_index{},clearance_q12{},bucket_count{};
        bool selected{};
    };
    std::array<SpecialSiteTrace,8> ai_special_site_trace{};
    enum class PurchaseDecision : std::uint32_t {
        NotReached=0, Unaffordable=1, ResearchQueued=2, ResearchUnavailable=3,
        ProducerClassUsed=4, NoProducer=5, QueueFull=6, ArmyQueued=7, MissingAsset=8, ResearchNoOp=9
    };
    struct PurchaseCandidateTrace {
        std::uint32_t rank{},id{},kind{},cost_q4{},budget_before_q4{},budget_after_q4{};
        std::int32_t packed_score{};
        PurchaseDecision decision{};
    };
    struct PurchaseTrace {
        std::array<PurchaseCandidateTrace,16> candidates{};
        std::uint32_t tick{},candidate_count{},recorded_count{},initial_budget_q4{};
        bool cached{},truncated{};
    };
    std::array<PurchaseTrace,8> ai_purchase_trace{};
    struct StrategicConstructionTrace {
        std::uint32_t tick{},selected_factory{},existing_factory_count{},selected_lab{};
        std::array<std::uint32_t,3> free_percent{};
        bool has_resource330{},arms_factories_unlocked{},factory_capacity_available{},lab_capacity_available{};
    };
    std::array<StrategicConstructionTrace,8> ai_strategic_construction_trace{};
    std::array<AiPurchaseBlockDiagnosticV600, 8> ai_purchase_block_diagnostics{};
    std::array<SimulationLabAiWorkspaceDiagnostics, 8> ai_workspace_diagnostics{};
    // Projectile impact alerts update the primary/secondary anchor selected by
    // the hit definition's acceleration field and restart the retail 150-tick
    // cooldown. Army maintenance decrements both timers independently.
    std::array<std::uint32_t, 8> ai_primary_anchor_cooldown{};
    std::array<std::uint32_t, 8> ai_anchor_cooldown0{};
    std::array<MilitaryAnchorV599, 8> ai_primary_impact_anchor{};
    std::array<MilitaryAnchorV599, 8> ai_secondary_impact_anchor{};
    std::array<FactionTechnologyMask256, 8> faction_technology_masks{};
    // Persistent explored history compressed to one bit per field cell for each faction.
    std::array<std::vector<std::uint64_t>, 8> faction_explored_cell_bits;
    // Occupancy is reconstructed by the worker on the retail visibility callback (phase 7,
    // plus first-tick initialization). The editor must use this exact generation rather than
    // independently projecting a different set of live poses at every UI snapshot.
    std::vector<std::uint64_t> current_visibility_occupancy;
    std::uint32_t current_visibility_occupancy_tick{};
    // Retail has two independent 64-entry faction arrays. AI purchase selection appends here;
    // class 11 moves a finished building product into pending_asset_requests only after build time.
    std::vector<SimulationLabPendingAssetRequest> secondary_purchase_requests;
    std::vector<SimulationLabPendingAssetRequest> pending_asset_requests;
    // V564 mirrors InGameLevelRuntime_LoadResourcesAfterDefaultResetCf: factions that begin with
    // class 18 and no Group-A class receive the retail four-entry primary construction queue.
    // Bit N corresponds to faction N and records that this exact initialization branch fired.
    std::uint32_t retail_initial_primary_queue_faction_mask{};
    std::array<std::int64_t, 8> faction_active_production_energy_q4{};
    std::array<std::int64_t, 8> faction_active_research_energy_q4{};
    // V571 retail model-component allocation image. These are refreshed on the retail 8-tick
    // allocation cadence when the faction seed opts into recovered energy semantics.
    std::array<std::uint32_t, 8> faction_energy_generation_capacity_q4{};
    std::array<std::uint32_t, 8> faction_supplied_energy_demand_q4{};
    std::array<std::uint32_t, 8> faction_unpowered_energy_demand_q4{};
    std::array<std::uint32_t, 8> faction_last_tritium_energy_burn_q4{};
    std::vector<SimulationProjectileState> projectiles;
    std::vector<SimulationLabEnvironmentalEmitterState> environmental_emitter_states;
    std::vector<SimulationLabEnvironmentalEffect> environmental_effects;
    std::vector<std::size_t> environmental_emitter_world_handles;
    std::uint64_t next_projectile_serial{1U};
    // Monotonic, run-local one-shot journal. Nodes are persistent/immutable so published snapshots
    // share old history and only allocate for newly generated events.
    std::uint64_t next_one_shot_event_sequence{1U};
    std::shared_ptr<const SimulationLabOneShotEventNode> one_shot_event_tail;
    SimulationLabRunMetrics metrics{};
};

void simulation_lab_append_military_order_trace(
    SimulationLabRun& run, SimulationLabMilitaryOrderTrace trace) noexcept;

std::vector<std::int32_t> simulation_lab_sweep_values(const SimulationLabConfig& config);
void simulation_lab_update_faction_relations(SimulationLabRun&,uint32_t faction);
void simulation_lab_apply_relation_transition(SimulationLabRun&,uint32_t first,uint32_t second,
    uint32_t state_first,uint32_t state_second);
// Retail model bookkeeping; caller supplies the callback step batch.
void simulation_lab_advance_component_health(SimulationLabModelComponentState& component,
                                              std::uint32_t step_ticks = 1U) noexcept;

std::int32_t simulation_lab_hierarchy_condition_ratio_q12(const SimulationLabEntity& entity) noexcept;

std::vector<SimulationLabModelComponentSeed> simulation_lab_model_components_from_hierarchy(
    std::span<const ModelVisualNode> hierarchy);
bool simulation_lab_runtime_class_routes_terminal_to_relation_d(std::uint32_t runtime_class_id) noexcept;
bool simulation_lab_runtime_class_uses_group_a_command_handler(std::uint32_t runtime_class_id) noexcept;
enum class SimulationLabHostilePerception { Excluded, Unseen, Visible };
SimulationLabHostilePerception simulation_lab_ai_hostile_perception(
    const SimulationLabRun& run,const FieldGrid& field,std::uint32_t observing_faction,
    const SimulationLabEntity& entity) noexcept;
float simulation_lab_construction_progress(const SimulationLabRun& run,
                                           const SimulationLabEntity& entity) noexcept;
bool simulation_lab_parameter_effect_modeled(std::uint32_t parameter_index) noexcept;
// V553: exact V533 AiCandidateWorkspace_AddOrAccumulateWeightedEntry score delta.
// The caller owns RNG advancement; retail advances once even when weight_range <= 1.
std::optional<std::uint32_t> simulation_lab_ai_candidate_weighted_delta(
    std::uint32_t weight_range, std::uint32_t random_value) noexcept;
// Recovered workspace10 score, independently testable from workspace admission.
// Absence of ARM300 uses signed world X+Y, not a synthetic faction centroid.
std::int64_t simulation_lab_workspace10_candidate_score(
    Vec3f point, std::optional<Vec3f> asset300_anchor, std::uint32_t random_value) noexcept;
const char* simulation_lab_parameter_fidelity_label(std::uint32_t parameter_index) noexcept;
std::uint64_t simulation_lab_latest_one_shot_event_sequence(const SimulationLabRun& run) noexcept;
std::vector<SimulationLabOneShotEvent> simulation_lab_one_shot_events_after(
    const SimulationLabRun& run, std::uint64_t sequence);
// Exact frontend Points path: GameFactionRuntime_RecomputeProgressAndScoreMetrics followed by
// FrontendResultsTable_DrawPointsColumn.
void simulation_lab_recompute_points(SimulationLabFactionScoreState& score,
                                     const FactionTechnologyMask256& technology_mask) noexcept;

class SimulationLabExperiment {
public:
    void reset(const SimulationLabConfig& config,
               std::span<const std::uint32_t, AiKnowledgeData::kParameterCount> base_parameters,
               std::span<const SimulationLabSeedEntity> seed_entities);
    void reset(const SimulationLabConfig& config,
               std::span<const std::uint32_t, AiKnowledgeData::kParameterCount> base_parameters,
               std::span<const SimulationLabSeedEntity> seed_entities,
               std::span<const SimulationLabFactionEconomySeed> faction_economy);
    void reset(const SimulationLabConfig& config,
               std::span<const std::uint32_t, AiKnowledgeData::kParameterCount> base_parameters,
               std::span<const SimulationLabSeedEntity> seed_entities,
               std::span<const SimulationLabFactionEconomySeed> faction_economy,
               std::span<const SimulationLabProductionAsset> production_assets);
    void reset(const SimulationLabConfig& config,
               std::span<const std::uint32_t, AiKnowledgeData::kParameterCount> base_parameters,
               std::span<const SimulationLabSeedEntity> seed_entities,
               std::span<const SimulationLabFactionEconomySeed> faction_economy,
               std::span<const SimulationLabProductionAsset> production_assets,
               std::span<const SimulationLabTechnologyAsset> technology_assets,
               std::span<const FactionTechnologyMask256> faction_technology_masks,
               std::span<const SimulationLabFactionScoreSeed> faction_score_seeds,
               const SimulationLabScenarioRuntimeSeed& scenario_runtime = {});
    void clear() noexcept;
    // Publication-only copy used by the editor worker. Immutable snapshots need config + run
    // state, not the large seed/ARM/TEC catalogs that are only required by the mutable worker.
    // Keeping those catalogs out of every 20 Hz publication prevents snapshot churn from stealing
    // wall time after an otherwise-fast simulation generation.
    SimulationLabExperiment snapshot_copy() const;
    // Worker setup hook: classify immutable FLD traversal state before timed 20 Hz generations.
    // Mutable runtime blockers remain query-local during step_movement().
    void prepare_navigation_terrain(const FieldGrid& field);
    void step(const FieldGrid& field, std::uint32_t ticks = 1U);
    // Worker-oriented independent-run stepping. Runs never share mutable simulation state, so
    // callers may invoke this concurrently for distinct run indices while the runs_ vector itself
    // remains structurally unchanged. This is the primitive used by SimulationLabWorker's
    // persistent worker pool.
    void step_run(const FieldGrid& field, std::size_t run_index, std::uint32_t ticks = 1U);
    bool queue_airport_launch(std::size_t run_index,std::size_t pad_index,uint32_t lanes,
                              uint32_t heading,int32_t mission_x_q12,int32_t mission_y_q12);
    // Enqueue for the next full tick before owner callbacks. A true return means
    // queued; live admission is reported in player_command_results after stepping.
    bool queue_player_command(std::size_t run_index, const SimulationLabPlayerCommand& command);

    // Caller-owned experiment/replay control; change the callback batch without
    // resetting queues, timers, RNG or the world tick. Serialized with step_run.
    bool set_runtime_callback_step_ticks(uint32_t ticks) noexcept {
        if(ticks<1U || ticks>5U)return false;
        config_.runtime_callback_step_ticks=ticks;
        return true;
    }
    // Serialized with step_run by the owner or worker generation barrier.
    bool set_active_view_faction(uint32_t faction) noexcept {
        if(faction>=8U)return false;
        for(auto& run:runs_)run.active_view_faction_index=run.faction_merge_redirect[faction];
        return true;
    }
    const SimulationLabConfig& config() const noexcept { return config_; }
    std::span<const SimulationLabRun> runs() const noexcept { return runs_; }
    std::span<SimulationLabRun> runs() noexcept { return runs_; }
    bool active() const noexcept { return !runs_.empty(); }
    bool finished() const noexcept;

private:
    void apply_player_commands(SimulationLabRun& run, const FieldGrid& field);
    static void claim_model_smoke_slots(SimulationLabRun&,SimulationLabEntity&,size_t entity_index=SIZE_MAX);
    void step_airports(SimulationLabRun&,const FieldGrid&,std::optional<std::size_t> owner={});
    static std::uint32_t random_next(std::uint32_t& state) noexcept;
    static std::optional<std::size_t> nearest_enemy(const SimulationLabRun& run,
                                                     std::size_t entity_index) noexcept;
    std::optional<std::uint32_t> choose_construction_asset(
        SimulationLabRun& run, std::uint32_t faction, const FieldGrid& field,
        std::int32_t* selected_packed_score = nullptr,
        std::vector<SimulationLabCachedAiCandidate>* structural_candidates = nullptr,
        bool include_strategic = true);
    void append_strategic_construction_candidates(SimulationLabRun& run, std::uint32_t faction,
        std::vector<SimulationLabCachedAiCandidate>& candidates);
    struct ConstructionPlacement {
        Vec3f position{};
        std::uint32_t heading_turn16{};
    };
    std::optional<ConstructionPlacement> construction_position(SimulationLabRun& run,
        std::uint32_t faction, std::uint32_t candidate_asset_id, const FieldGrid& field);
    bool process_pending_construction(SimulationLabRun& run, std::uint32_t faction,
                                      const FieldGrid& field);
    void plan_construction(SimulationLabRun& run, const FieldGrid& field, std::uint32_t selected_faction);
    void plan_autonomous_army_purchases(SimulationLabRun& run, const FieldGrid& field, std::uint32_t selected_faction);
    void complete_construction(SimulationLabRun& run);
    SimulationLabEntity make_entity_from_seed(const SimulationLabSeedEntity& seed,
                                                      std::uint32_t faction, Vec3f position,
                                                      bool synthetic,
                                                      const FactionTechnologyMask256& mask);
    const SimulationLabProductionAsset* production_asset(std::uint32_t army_asset_id) const noexcept;
    bool producer_accepts(const SimulationLabEntity& producer,
                          const SimulationLabProductionAsset& candidate) const noexcept;
    bool producer_available_for_purchase(const SimulationLabEntity& producer,
                                         const SimulationLabProductionAsset& candidate) const noexcept;
    void initialize_retail_starting_primary_queue(SimulationLabRun& run);
    void start_production_jobs(SimulationLabRun& run,std::optional<std::size_t> owner={});
    void advance_production_jobs(SimulationLabRun& run,const FieldGrid& field,std::optional<std::size_t> owner={});
    const SimulationLabTechnologyAsset* technology_asset(std::uint32_t technology_id) const noexcept;
    bool technology_available(const SimulationLabRun& run, std::uint32_t faction,
                              std::uint32_t technology_id,bool include_pending=true) const noexcept;
    std::optional<std::uint32_t> select_research_candidate(
        SimulationLabRun& run, std::uint32_t faction) const;
    bool apply_research_purchase(SimulationLabRun& run, std::uint32_t faction,
                                 std::uint32_t technology_id,
                                 std::span<const size_t> unavailable={},
                                 std::optional<size_t>* selected_source=nullptr);
    void advance_research(SimulationLabRun& run,std::optional<std::size_t> owner={});
    void grant_creation_technologies(SimulationLabRun& run, std::uint32_t faction,
                                    const SimulationLabSeedEntity& seed);
    void unlock_technology_recursive(SimulationLabRun& run, std::uint32_t faction,
                                     std::uint32_t technology_id);
    void update_exploration_and_points(SimulationLabRun& run, const FieldGrid& field);
    static void update_scheduled_conditions(SimulationLabRun& run, const FieldGrid& field,uint32_t callback_step_ticks);
    struct DeferredEmitter { std::size_t index; bool root_timed_active; };
    void step_combat(SimulationLabRun& run, const FieldGrid& field,
                     std::span<const DeferredEmitter> deferred_emitters,
                     std::span<const SimulationLabLiveShotHull> ground_hulls_before_movement = {},
                     std::span<const std::size_t> deferred_ambient_emitters = {},
                     std::span<const SimulationLabLiveShotHull> animated_hulls_before_callbacks = {},
                     std::optional<std::size_t> only_owner = std::nullopt,
                     std::span<const std::size_t> serviced_owners = {});
    void advance_projectiles(SimulationLabRun& run, const FieldGrid& field,
        std::vector<std::size_t>* serviced_model_emitters = nullptr,
        std::vector<std::size_t>* serviced_model_callbacks = nullptr);
    void advance_environmental_emitters(SimulationLabRun& run,const FieldGrid& field,
                                        std::optional<std::size_t> emitter=std::nullopt);
    void emit_articulated_contact_effect(SimulationLabRun& run,const FieldGrid& field,
        const SimulationLabEntity& entity,Vec3f position,const std::array<uint32_t,3>& angles);
    void initialize_projectile_impact_effect(SimulationLabRun& run,const SimulationProjectileState& projectile,
        const ShotDefinitionRecord& shot,SimulationLabOneShotEvent& event);
    void advance_environmental_effects(SimulationLabRun& run,const FieldGrid& field);
    std::size_t link_environmental_effect(SimulationLabRun& run,uint32_t id,SimulationLabEffectPose pose,
        bool world_pool_only=false,std::optional<SimulationLabTerrainImpact> terrain_impact=std::nullopt,
        bool before_owner_body=false,bool after_owner_body=false);
    void claim_projectile_world_slot(SimulationLabRun& run,SimulationProjectileState& projectile);
    void emit_projectile_trail_effect(SimulationLabRun& run,const SimulationProjectileState& projectile,
                                      const ShotDefinitionRecord& shot,uint32_t age);
    void advance_environmental_effect(SimulationLabRun& run,const FieldGrid& field,std::size_t index,
                                      const std::function<void()>& before_linked_effect_random = {});
    void prune_environmental_effects(SimulationLabRun& run);
    void advance_damage_effects(SimulationLabRun& run,const FieldGrid& field,size_t owner,unsigned calls,
        std::optional<std::array<uint32_t,3>> retained_root_angles=std::nullopt);
    void advance_production_emitters(SimulationLabRun& run,const FieldGrid& field,
        std::optional<std::size_t> owner=std::nullopt,std::optional<bool> deferred_root_timed_active=std::nullopt,
        bool root_only=false,std::optional<std::array<uint32_t,3>> retained_root_angles=std::nullopt);
    static bool expanded_radius_overlap(const SimulationLabEntity& query, Vec3f query_position,
                                        const SimulationLabEntity& candidate) noexcept;
    void advance_class23_collision_state(SimulationLabRun& run, std::optional<size_t> owner = std::nullopt);
    static void recompute_resource_storage_limits(SimulationLabRun& run);
    static void recompute_energy_generation_capacity(SimulationLabRun& run);
    void update_energy_allocation(SimulationLabRun& run);
    static void apply_water_damage(SimulationLabRun& run, const FieldGrid& field,
        std::optional<std::size_t> only_owner = std::nullopt);
    static void sync_root_component_summary(SimulationLabEntity& entity) noexcept;
    static void apply_component_damage(SimulationLabRun& run, SimulationLabEntity& entity,
                                       std::size_t component_index, std::int32_t damage_amount,
                                       std::optional<std::uint32_t> direct_impact_angle_turn32,
                                       std::optional<std::uint32_t> source_faction = std::nullopt);
    static void process_collision_contacts(SimulationLabRun& run);
    void advance_terminal_attachment_effects(SimulationLabRun& run, const FieldGrid& field,
                                             bool release_completed_only = false,
                                             std::optional<uint64_t> before_projectile_serial = {},
                                             std::optional<std::size_t> only_owner = {},
                                             bool advance_wreck = false);
    void advance_pioneer_conversions(SimulationLabRun& run);
    void dispatch_group_a_class_command(SimulationLabRun& run, std::size_t owner_index);
    void step_military_ai(SimulationLabRun& run, const FieldGrid& field,
                          std::uint32_t faction);
    void step_movement(SimulationLabRun& run, const FieldGrid& field,
                       std::optional<size_t> only_owner=std::nullopt);

    SimulationLabConfig config_{};
    std::vector<SimulationLabSeedEntity> seed_entities_;
    std::vector<SimulationLabProductionAsset> production_assets_;
    std::unordered_map<std::uint32_t,std::size_t> production_asset_indices_;
    std::unordered_map<std::uint32_t,std::size_t> technology_asset_indices_;
    // Immutable catalog indexes are shared by worker runs and omitted from GUI snapshots.
    struct ResearchArmLink { std::size_t asset_index{}; bool root{},child{}; };
    std::unordered_map<std::uint32_t,std::vector<ResearchArmLink>> research_arm_index_;
    std::int32_t ai_flagged_definition_value0c_maximum_{1};
    std::array<std::int32_t,8> ai_category_value60_maxima_{1,1,1,1,1,1,1,1};
    void prepare_ai_scoring_catalog();
    std::vector<SimulationLabTechnologyAsset> technology_assets_;
    std::vector<SimulationLabRun> runs_;
    // Omitted by snapshot_copy(): publication only needs simulation state, while the mutable worker
    // owns this precomputed static navigation cache for the lifetime of its FLD snapshot.
    V565PathTerrainCache navigation_terrain_cache_;
    std::vector<V528PathRuntimeBlocker> static_navigation_blockers_;
    std::shared_ptr<const SimulationLabRuntimeCatalog> runtime_catalog_;
    std::vector<SimulationLabEnvironmentalEmitter> environmental_emitters_;
    std::vector<SimulationLabStaticPlacementOwner> static_placement_owners_;
    std::vector<SimulationLabStaticShotHull> static_shot_hulls_;
};

} // namespace thandor
