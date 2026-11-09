#pragma once
#include "editor_ui_scale.hpp"
#include "editor_asset_fallback.hpp"

#include "thandor/simulation_command_markers.hpp"

#include "thandor/simulation_lab_diagnostics.hpp"

#include "simulation_lab_diagnostic_writer.hpp"
#include "sprite_canvas_cache.hpp"
#include "simulation_lab_product_cache.hpp"
#include "simulation_lab_runtime_catalog_cache.hpp"
#include "object_texture_source_reuse.hpp"
#include "generated_shadow_reuse.hpp"
#include "shadow_receiver_pose_cache.hpp"
#include "editor_workspace.hpp"
#include "editor_user_settings.hpp"
#include "ai_knowledge_panel.hpp"
#include "editor_ai_profiles.hpp"
#include "asset_preview.hpp"
#include "game_cursor.hpp"
#include "model_transform.hpp"
#include "runtime_shadow.hpp"
#include "editor_performance_log.hpp"
#include "terrain_renderer.hpp"
#include "viewport_picking.hpp"
#include "thandor/audio.hpp"
#include "thandor/simulation_lab_audio.hpp"
#include "thandor/map_document.hpp"
#include "thandor/terrain_generator.hpp"
#include "thandor/new_map_terrain.hpp"
#include "thandor/campaign_document.hpp"
#include "thandor/context_order.hpp"
#include "thandor/effect.hpp"
#include "thandor/fog_preview.hpp"
#include "thandor/simulation.hpp"
#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_worker.hpp"
#include "thandor/terrain_texture.hpp"
#include "thandor/retail_selection_frame.hpp"
#include "thandor/retail_selection_metrics.hpp"
#include "thandor/simulation_command_markers.hpp"
#include "thandor/v528_pathing.hpp"

#include <memory>
#include <chrono>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace editor {

enum class Tool { Select, Raise, Lower, Smooth, Flatten, Water, ResourceField, Texture, Resource, PlayerStart, EffectScenery, SimulationTarget, SimulationMove, GenerateTerrain };
enum class GizmoMode { Translate, Rotate };
enum class WaterSelectionStage { SelectArea, PickReference, AdjustLevel };
enum class GizmoHandle { None, MoveX, MoveY, MoveTerrainZ, RotateZ };

class EditorApp {
public:
    EditorApp(GLFWwindow* window, VulkanContext& vulkan, std::filesystem::path game_root);
    void run();
    ~EditorApp();

private:
    friend struct EditorUiValidation;
    struct ArmyTextureFamily {
        thandor::GfxAsset graphics;
        thandor::PalAsset palette;
        // Parsed source assets already live for this family; decoded canvas data
        // can be reused across object/AI ownership rebuilds with bounded RAM.
        mutable SpriteCanvasCache sprite_canvases;
        std::function<void(const std::string&)> decode_failure;
        ArmyTextureFamily(thandor::GfxAsset gfx, thandor::PalAsset pal)
            : graphics(std::move(gfx)), palette(std::move(pal)) {}
        thandor::RgbaImage square_canvas(std::uint32_t subresource) const {
            return sprite_canvases.obtain(subresource, [&] {
                try{return graphics.decode(thandor::GraphicsSubresourceIndex{subresource}, false);}
                catch(const std::bad_alloc&){throw;}
                catch(const std::exception& error){
                    if(decode_failure)decode_failure("subresource "+std::to_string(subresource)+": "+error.what());
                    return missing_texture_placeholder();
                }
            });
        }
    };

    using RuntimeSpatialSoundRequest = thandor::SimulationLabSoundRequest;

    struct ActiveLoopingSound {
        std::string path;
        std::unique_ptr<thandor::LoopingPcmVoice> voice;
    };

    struct Snapshot {
        std::vector<std::byte> field;
        std::vector<std::byte> level;
        std::vector<thandor::PlacementTag> tags;
        std::array<thandor::FactionTechnologyMask256, 8> faction_technology_masks;
        std::string map_name;
        std::string display_name;
        std::string author;
        uint32_t default_human_faction{};
        std::vector<std::byte> scenario_record;
        std::vector<std::byte> level_text;
    };

    struct GizmoDragState {
        GizmoHandle handle{GizmoHandle::None};
        PreviewVec2 mouse_start{};
        PreviewVec2 mouse_previous{};
        thandor::LevelPlacement placement_start{};
        TransformGizmoProjection projection_start{};
        float rotation_screen_angle_start{};
    };


    GLFWwindow* window_{};
    VulkanContext& vulkan_;
    EditorPerformanceLog editor_performance_log_;
    thandor::AssetCatalog catalog_;
    std::optional<thandor::AiKnowledgeData> ai_knowledge_;
    EditorAiProfiles ai_profiles_;
    int ai_profile_parameter_index_{0x51};
    int ai_profile_draft_index_{-1};
    int32_t ai_profile_draft_value_{};
    void apply_ai_profile();
    void draw_ai_profile_controls();
    std::string ai_knowledge_load_error_;
    AiKnowledgeUiState ai_knowledge_ui_;
    TerrainRenderer renderer_;
    std::unique_ptr<thandor::MapDocument> document_;
    std::size_t current_group_{};
    Camera camera_;
    GameCursorAsset game_cursor_;
    bool game_cursor_enabled_{true};
    uint32_t game_cursor_frame_{GameCursorAsset::kDefaultFrame};
    bool show_left_panel_{true};
    bool show_right_panel_{true};
    bool compact_ribbon_{};
    bool overview_expanded_{true};
    bool overview_players_{true};
    bool overview_resources_{true};
    bool overview_heightmap_{};
    bool overview_grid_{};
    int asset_category_{};
    int requested_inspector_tab_{-1};
    int inspector_page_{};
    int workspace_transform_action_{};
    WorkspaceRect last_dynamic_canvas_{};
    EditorWorkspaceLayout workspace_layout() const noexcept;
    bool project_to_canvas(Vec3f point, const Mat4& vp, float& x, float& y, float& depth) const;
    ViewRay canvas_view_ray(float x, float y) const;
    void draw_workspace_ribbon();
    void draw_map_overview();
    void draw_selected_object_preview(const thandor::ArmyAssetPreset& preset, uint32_t faction);
    void draw_asset_categories(std::span<const uint32_t> registry_ids);
    bool begin_inspector_tab(const char* label, int index);
    void choose_workspace_tool(Tool tool, int tab);
    bool show_retail_debug_overlay_{};
    bool show_measured_fps_counter_{true};
    bool debug_show_render_counters_{true};
    bool debug_show_world_vectors_{true};
    bool debug_show_cursor_coordinates_{true};
    bool debug_show_memory_resources_{true};
    bool debug_show_simulation_clock_{true};
    uint32_t retail_debug_frame_accumulator_{};
    uint32_t retail_debug_sample_frames_{};
    uint64_t retail_debug_draw_packet_accumulator_{};
    uint64_t retail_debug_sample_draw_packets_{};
    bool measured_fps_initialized_{};
    double measured_fps_window_start_seconds_{};
    uint32_t measured_fps_presented_frames_{};
    double measured_present_fps_{};
    double measured_present_frame_ms_{};
    bool has_factory_drive_markers_{};
    std::array<float,6> last_marker_camera_{};
    bool camera_turn_drag_active_{};
    PreviewVec2 camera_turn_capture_center_{};
    PreviewVec2 placement_rotation_pointer_anchor_{};
    std::optional<std::pair<uint32_t,uint32_t>> placement_rotation_cell_;
    bool right_pointer_pressed_{};
    bool right_pointer_dragged_{};
    bool right_pointer_capture_active_{};
    PreviewVec2 right_pointer_press_{};
    PreviewVec2 right_pointer_capture_center_{};
    Tool tool_{Tool::Select};
    thandor::Brush brush_{};
    thandor::TerrainGeneratorSettings generator_settings_;
    std::vector<uint8_t> generator_selection_;
    std::vector<thandor::TerrainRegionPoint> generator_outline_;
    std::optional<thandor::TerrainGenerationResult> generator_preview_;
    std::vector<std::byte> generator_source_field_,generator_source_level_;
    std::vector<float> generator_weights_;
    bool generator_region_ready_{};
    bool generator_drawing_{};
    bool generator_edit_path_{};
    bool generator_stroke_is_path_{};
    bool generator_show_selection_{true};
    bool generator_protect_structures_{true};
    int generator_shape_{}; // Freehand lasso or rectangle.
    int generator_combine_{}; // Replace, add, subtract.
    std::string generator_message_;
    std::string generator_selection_label_{"Draw an area or choose Whole map."};
    thandor::TerrainWaterEditPolicy terrain_water_policy_{
        thandor::TerrainWaterEditPolicy::PreserveAbsoluteSurface};
    float flatten_height_{};
    float water_height_{};
    bool water_erase_mode_{};
    WaterSelectionStage water_selection_stage_{WaterSelectionStage::SelectArea};
    std::vector<uint8_t> water_selection_mask_;
    std::optional<std::pair<uint32_t,uint32_t>> water_reference_cell_;
    float water_level_step_{0.25F};
    bool water_selection_checkpointed_{};
    uint32_t resource_field_occupancy_class_{1U};
    bool resource_field_erase_mode_{};
    uint8_t selected_material_{};
    uint32_t selected_asset_id_{1};
    uint32_t selected_faction_{1};
    int mission_golrathen_ai_slot_{}; // GUI choice only; LEV holds the persistent army8 assignment
    uint32_t selected_orientation_{};
    bool draw_water_{true};
    bool animate_surface_{true};
    bool animate_buildings_{};
    bool animate_units_{};
    bool animate_timed_effects_{true};
    bool animate_resource_fields_{true};
    // Runtime class-2 tread marks and class-3 articulated footprints are independent EffectRuntime
    // instances. Keep them visible by default and age them from their spawn snapshots rather than
    // rebuilding historical pulses at the owner's current position.
    bool show_movement_ground_marks_{true};
    bool simulation_mode_{};
    // Editor-only audible preview. Retail initializes level effects enabled; keep playback on by
    // default while exposing an explicit View-menu toggle.
    bool sound_playback_enabled_{true};
    bool simulation_sound_at_map_focus_{true};
    // Corresponds to the retail g_SoundEffectsGainQ15 setting loaded from persistent-settings
    // offset 0x24. 0x8000 is unity; zero mutes/stops projected effects channels.
    uint32_t sound_effects_gain_q15_{0x8000U};
    bool simulation_selected_only_{true};
    bool simulation_show_ranges_{true};
    // Multi-run experiment state. Worlds stay logically isolated but are projected into the same
    // terrain/camera coordinate system; the selected run reuses the full dynamic model renderer.
    std::shared_ptr<const thandor::SimulationLabRuntimeCatalog> simulation_lab_audio_catalog_;
    std::vector<RuntimeSpatialSoundRequest> simulation_lab_loop_sound_requests_;
    mutable std::unordered_map<uint32_t,std::optional<std::string>> runtime_sound_path_cache_;
    mutable std::string runtime_sound_path_pattern_;
    uint64_t audio_one_shot_requests_{},audio_fog_filtered_{},audio_inaudible_{},audio_missing_samples_{},audio_backend_rejected_{},audio_submitted_{};
    uint64_t audio_loop_requests_{};
    mutable uint64_t audio_path_cache_hits_{};
    thandor::SimulationLabWorker simulation_lab_worker_;
    std::shared_ptr<const thandor::SimulationLabExperiment> simulation_lab_snapshot_;
    // UI/debug reads a deliberately slower immutable snapshot. The live snapshot above continues
    // driving render geometry, fog, sound/event transport and selected-run actor state at the
    // normal simulation cadence; large text/debug panels only advance twice per second.
    std::shared_ptr<const thandor::SimulationLabExperiment> simulation_lab_ui_snapshot_;
    thandor::SimulationLabConfig simulation_lab_config_{};
    bool simulation_lab_running_{};
    bool simulation_lab_paused_{};
    bool simulation_lab_show_all_runs_{};
    // When the Lab starts with fog preview off, enable selected-faction visibility while
    // it runs, then restore the previous off state only if the user did not change modes.
    bool simulation_lab_auto_fog_preview_{};
    bool retail_field_diagnostics_{};
    int retail_field_resource_selector_{};
    int simulation_lab_selected_run_{};
    bool simulation_lab_run_selector_hovered_{};
    // Highest one-shot sequence observed for every run. We advance this for unselected runs too so
    // switching runs never replays a backlog of historical audio; the selected run consumes every
    // event between UI polls from the immutable snapshot journal.
    std::vector<std::uint64_t> simulation_lab_last_observed_one_shot_sequence_;
    // One joined background formatter consumes immutable samples; the UI only captures scalars.
    std::unique_ptr<SimulationLabDiagnosticWriter> simulation_lab_diagnostic_writer_;
    std::string simulation_lab_diagnostic_path_;
    bool simulation_lab_worker_error_logged_{};
    std::chrono::steady_clock::time_point simulation_lab_last_log_time_{};
    bool simulation_lab_log_initialized_{}, simulation_lab_log_finished_{};
    double simulation_lab_last_log_milliseconds_{};
    struct SimulationLabUiDebugState {
        thandor::SimulationLabWorkerPerformance performance{};
        uint32_t run_worker_count{};
        uint32_t pending_ticks{};
        uint64_t completed_generations{};
        uint64_t last_generation_duration_us{};
        uint64_t maximum_generation_duration_us{};
    };
    SimulationLabUiDebugState simulation_lab_ui_debug_state_{};
    struct SimulationLabUiSelectedSummary {
        thandor::SimulationLabPlanSummary plans{};
        bool valid{};
        std::size_t entity_count{};
        std::size_t complete_entities{};
        std::size_t synthetic_entities{};
        std::size_t building_entities{};
        std::size_t movable_entities{};
        std::size_t driving_entities{};
        std::size_t factories_building{};
        std::size_t factories_opening{};
        std::size_t factories_waiting{};
        std::size_t factories_closing{};
        std::size_t parked_aircraft{};
        std::size_t vision_owners{};
        std::size_t mobile_vision_owners{};
        std::size_t zero_vision_radius{};
        std::size_t off_map_vision_owners{};
        uint32_t visibility_tick{};
    };
    SimulationLabUiSelectedSummary simulation_lab_ui_selected_summary_{};
    double simulation_lab_last_ui_refresh_time_seconds_{};
    bool simulation_lab_ui_refresh_initialized_{};
    int simulation_lab_semantic_row_{1};
    uint32_t simulation_lab_tick_duration_ms_{50U};
    // Ordinary experiments retain the historical all-AI/1.0x defaults. Retail-recording
    // comparisons can explicitly preserve the human faction mask and TICK game-speed Q8.
    bool simulation_lab_simulate_all_active_factions_as_ai_{true};
    uint32_t simulation_lab_human_faction_mask_{2U};
    uint32_t simulation_lab_game_speed_q8_{0x100U};
    bool simulation_lab_storage_limit_overrides_enabled_{};
    uint32_t simulation_lab_xenite_storage_limit_q4_{4000U};
    uint32_t simulation_lab_tritium_storage_limit_q4_{4000U};
    double simulation_lab_last_schedule_time_seconds_{};
    double simulation_lab_tick_accumulator_ms_{};
    bool simulation_lab_schedule_initialized_{};
    bool simulation_lab_snapshot_dirty_{};
    std::chrono::steady_clock::time_point last_lab_scene_update_{};
    // Hash of selected-run synthetic asset/faction membership plus faction technology masks. A
    // change means the static texture array may be missing a newly placed/researched Lab model and
    // requests exactly one object/texture rebuild; ordinary movement ticks remain dynamic-only.
    double simulation_lab_last_sync_milliseconds_{};
    std::uint64_t simulation_lab_render_asset_signature_{};
    std::unordered_set<uint32_t> simulation_lab_resident_effect_ids_;
    // A successful class-18 completion action replaces an authored LEV PlayerStart runtime with a
    // synthetic ARM while preserving the original placement as lineage for selection/overlay. Do
    // not draw both model hierarchies at once.
    std::unordered_set<std::size_t> simulation_lab_suppressed_placements_;
    // Editor presentation and simulation share one scaled 20 Hz timeline. This affects previews,
    // driving, weapon cadence, shots and effects, but never changes serialized ARM/LEV data.
    uint32_t animation_preview_multiplier_{1U};
    double last_presentation_time_seconds_{};
    double presentation_tick_fraction_{};
    bool presentation_timeline_initialized_{};
    bool wireframe_{};
    bool exact_soil_packets_{true};
    uint32_t soil_transition_rng_seed_{thandor::FieldGrid::kDefaultSoilRandomSeed};
    uint32_t resource_animation_rng_seed_{thandor::FieldGrid::kDefaultSoilRandomSeed};
    bool projected_object_shadows_{true};
    // Editor-only V537 lighting-cycle preview. These values never serialize into LEV.
    bool lighting_cycle_preview_enabled_{};
    uint32_t lighting_cycle_preview_tick_{};
    bool backup_existing_level_package_{true};
    bool backup_existing_patch00_{true};
    bool patch00_include_custom_assets_{true};
    unsigned level_package_slot_{1};
    std::vector<std::string> material_names_;
    std::vector<thandor::RgbaImage> material_images_;
    std::optional<thandor::TerrainTextureLayout> terrain_texture_layout_;
    std::optional<thandor::TerrainSurfaceTextureLayout> surface_texture_layout_;
    thandor::TerrainSurfaceFamily surface_family_{thandor::TerrainSurfaceFamily::Other};
    std::vector<uint32_t> surface_palette_argb_;
    std::vector<thandor::RgbaImage> surface_material_images_;
    std::size_t loaded_surface_material_count_{};
    std::size_t loaded_material_count_{};
    std::size_t missing_material_count_{};
    std::unordered_map<std::string, thandor::SpriteAsset> sprite_cache_;
    std::unordered_map<std::string,std::string> asset_load_failures_;
    void report_asset_failure(const std::string& path,const std::string& message);
    std::shared_ptr<ArmyTextureFamily> load_texture_family(const std::string& gfx,const std::string& pal);
    std::unordered_map<std::string, std::shared_ptr<thandor::SamPcmData>> sam_pcm_cache_;
    std::unordered_map<uint32_t, ActiveLoopingSound> active_looping_sounds_;
    std::unordered_set<uint32_t> looping_sound_start_failures_;
    std::string sound_playback_status_;
    std::unordered_map<std::string, std::shared_ptr<ArmyTextureFamily>> army_texture_cache_;
    std::optional<thandor::RgbaImage> local_player_texture_override_;
    std::string local_player_texture_override_status_;
    uint32_t local_player_runtime_faction_{1U};
    std::size_t rendered_object_count_{};
    std::size_t marker_only_object_count_{};
    std::size_t object_texture_layer_count_{};
    std::size_t lab_selected_render_variants_{};
    std::size_t power_resource_decal_count_{};
    std::size_t building_resource_decal_count_{};
    std::size_t resource_body_triangle_count_{};
    std::size_t resource_luminous_particle_triangle_count_{};
    std::size_t resource_shading_emitter_count_{};
    std::size_t projected_shadow_triangle_count_{};
    std::size_t generated_shadow_object_count_{};
    std::size_t generated_shadow_compaction_saved_vertices_{};
    std::uint64_t generated_shadow_surface_culled_triangles_{};
    std::uint64_t generated_shadow_dynamic_culled_triangles_{};
    uint64_t generated_shadow_pose_cache_hits_{};
    uint64_t generated_shadow_pose_cache_misses_{};
    uint64_t generated_shadow_pose_cache_reused_vertices_{};
    std::size_t generated_shadow_texture_count_{};
    std::size_t unsupported_primitive_selector_count_{};
    std::size_t tree_object_count_{};
    std::size_t rock_object_count_{};
    std::size_t related_effect_definition_count_{};
    std::size_t timed_effect_controller_count_{};
    std::size_t timed_effect_sprite_triangle_count_{};
    thandor::SimulationCommandMarkerDiagnostics command_marker_diagnostics_{};
    double command_marker_build_milliseconds_{};
    int command_marker_run_index_{-1};
    std::size_t command_marker_count_{};
    std::size_t command_marker_triangle_count_{};
    std::size_t lava_linked_shot_particle_count_{};
    std::size_t player_start_count_{};
    std::size_t object_build_error_count_{};
    std::size_t selected_lod_triangle_count_{};
    std::size_t discarded_alternate_lod_triangle_count_{};
    std::size_t untextured_triangle_count_{};
    std::size_t neutral_source_color_triangle_count_{};
    std::size_t terrain_overlay_safe_cell_count_{};
    std::size_t terrain_overlay_caution_cell_count_{};
    std::size_t terrain_overlay_blocked_cell_count_{};
    std::size_t recursive_model_node_count_{};
    std::size_t animated_model_node_count_{};
    std::size_t visible_animated_building_count_{};
    std::size_t visible_timed_effect_controller_count_{};
    std::size_t dynamic_triangle_count_{};
    double dynamic_scene_build_milliseconds_{};
    double dynamic_scene_pre_shadow_milliseconds_{},dynamic_scene_shadow_milliseconds_{};
    double dynamic_scene_fog_milliseconds_{},dynamic_scene_model_setup_milliseconds_{};
    double dynamic_scene_effects_milliseconds_{},dynamic_scene_sort_milliseconds_{};
    double generated_shadow_build_milliseconds_{};
    uint32_t current_presentation_tick_{};
    uint32_t current_building_animation_tick_{};
    uint32_t last_dynamic_scene_tick_{std::numeric_limits<uint32_t>::max()};
    uint32_t last_simulation_tick_{std::numeric_limits<uint32_t>::max()};
    std::optional<Vec3f> simulation_target_point_;
    std::unordered_map<std::size_t,thandor::SimulationActorState> simulation_actor_states_;
    // The LEV placement keeps its authored faction for save/edit semantics. During a live
    // Simulation Lab run, diplomacy state 11 can re-own the runtime model without rewriting LEV.
    // Keep that transient owner separately so rendering, texture residency and shadows use the
    // same surviving faction as the simulation.
    std::unordered_map<std::size_t,uint32_t> simulation_runtime_factions_;
    uint32_t simulation_runtime_owner_revision_{1U};
    // Contextual in-game-style orders are editor-only runtime state. Entity targets are kept
    // separate from point targets so moving enemy actors are followed every 20 Hz tick.
    std::unordered_map<std::size_t,std::size_t> simulation_attack_placement_targets_;
    std::unordered_map<std::size_t,Vec3f> simulation_attack_point_targets_;
    // Editor-only production drive/rally destinations. Retail LEV has no persistent field for
    // this runtime order, so the marker remains transient like move and attack commands.
    std::unordered_map<std::size_t,Vec3f> simulation_production_rally_points_;
    // Retail class-13 production consumes a faction-owned secondary queue rather than a producer-
    // local build choice. The editor exposes an explicit queue action, then keeps all resulting
    // economy, factory phase and produced-unit state transient: nothing is inserted into LEV.
    std::array<std::vector<uint32_t>,8> simulation_secondary_production_queues_{};
    std::array<uint32_t,8> simulation_faction_xenite_q4_{};
    std::array<uint32_t,8> simulation_relation_transition_ticks_{};
    bool simulation_economy_initialized_{};
    std::unordered_map<std::size_t,uint32_t> simulation_factory_candidate_selection_;
    std::unordered_map<std::size_t,thandor::SimulationClass13ProductionState> simulation_factory_states_;
    // Runtime class 22 consumes the same faction secondary queue as class 13, but completed
    // aircraft remain in a producer-owned persistent store and are not class-13 products.
    std::unordered_map<std::size_t,uint32_t> simulation_airport_candidate_selection_;
    std::unordered_map<std::size_t,thandor::SimulationClass22AirportState> simulation_airport_states_;
    std::unordered_map<std::size_t,uint32_t> simulation_airport_launch_lane_selection_;
    std::unordered_map<std::size_t,float> simulation_airport_mission_heading_degrees_;
    enum class SimulationTransientKind : uint8_t { ProducedClass13, LinkedClass21 };
    struct SimulationTransientRef {
        SimulationTransientKind kind{SimulationTransientKind::ProducedClass13};
        uint64_t serial{};
        friend bool operator==(const SimulationTransientRef&,const SimulationTransientRef&)=default;
    };
    struct SimulationLinkedChildEntity {
        uint64_t serial{};
        uint32_t army_asset_id{};
        uint32_t faction_index{};
        std::size_t producer_placement_index{std::numeric_limits<std::size_t>::max()};
        std::size_t storage_slot{std::numeric_limits<std::size_t>::max()};
        thandor::SimulationActorState actor{};
        thandor::SimulationClass21LinkedChildState runtime{};
        int32_t mission_world_x_q12{};
        int32_t mission_world_y_q12{};
        uint32_t mission_heading_turn16{};
        int32_t movement_accumulator_q12{};
        int32_t platform_reference_z_q12{};
        int32_t mission_arc_reference_z_q12{};
        int32_t child0_local_z_q12{};
        uint32_t child0_pitch_turn16{0x4000U};
        int32_t maximum_integrity{};
        int32_t integrity{};
        bool visible{true};
        bool landed{};
    };
    std::unordered_map<uint64_t,SimulationLinkedChildEntity> simulation_linked_child_entities_;
    uint64_t next_simulation_linked_child_serial_{1U};
    struct SimulationModelRaycastHit {
        float distance{};
        std::optional<std::size_t> placement_index;
        std::optional<SimulationTransientRef> transient;
        uint32_t faction_index{};
        uint32_t army_asset_id{};
    };
    struct SimulationProducedEntity {
        uint64_t serial{};
        uint32_t army_asset_id{};
        uint32_t faction_index{};
        std::size_t producer_placement_index{std::numeric_limits<std::size_t>::max()};
        thandor::SimulationActorState actor{};
        int32_t maximum_integrity{};
        int32_t integrity{};
        bool linked_to_producer{true};
        bool exit_command_issued{};
    };
    std::unordered_map<uint64_t,SimulationProducedEntity> simulation_produced_entities_;
    uint64_t next_simulation_produced_entity_serial_{1U};
    // Static object geometry only tags placements that currently have a transient simulation
    // replacement. Membership changes therefore request one static rebuild; ordinary 20 Hz
    // aiming/movement updates remain dynamic-only.
    bool simulation_static_replacement_dirty_{};
    struct SimulationPlacementIntegrityState {
        int32_t maximum_integrity{};
        int32_t integrity{};
    };
    // Ordinary editor simulation keeps damage transient. It mirrors ModelRuntimeSlot integrity
    // without mutating LEV/editor metadata; reset_simulation() restores every placement to its
    // authored maximum. Damage-state EFF preview consumes this state when present.
    std::unordered_map<std::size_t,SimulationPlacementIntegrityState> simulation_integrity_states_;
    std::vector<thandor::SimulationProjectileState> simulation_projectiles_;
    uint64_t next_simulation_projectile_serial_{1U};
    std::size_t simulation_active_actor_count_{};
    std::size_t simulation_projectile_count_{};
    std::size_t simulation_volley_count_{};

    struct TimedEffectControllerPreview {
        uint32_t land_effect_id{};
        uint32_t water_effect_id{};
        std::size_t placement_index{std::numeric_limits<std::size_t>::max()};
        uint32_t root_model_node_index{};
        uint32_t emitter_model_node_index{};
        uint32_t runtime_class_id{};
        std::vector<Vec3f> local_type6_attachments;
        Vec3f root_position{};
        ModelHierarchyTransform root_orientation{};
        ModelHierarchyTransform emitter_orientation{};
        std::optional<thandor::ResourceShadingGlowProfile> shading_profile;
        thandor::ResourceEmitterTiming timing;
        bool owner_is_building{};
        bool owner_has_authored_model_animation{};
        bool owner_is_resource_field{};
        float visibility_radius{24.0F};
    };
    struct DamageEffectControllerPreview {
        uint32_t effect_id{};
        std::size_t placement_index{std::numeric_limits<std::size_t>::max()};
        uint32_t model_node_index{};
        int32_t health_threshold_percent{};
        thandor::DamageEffectTiming timing;
        std::vector<std::pair<uint32_t,Vec3f>> local_type3_attachments;
        Vec3f root_position{};
        ModelHierarchyTransform root_orientation{};
        bool owner_is_building{};
        float visibility_radius{24.0F};
    };
    struct MovementGroundEffectInstance {
        std::size_t owner_placement_index{std::numeric_limits<std::size_t>::max()};
        uint32_t effect_id{};
        uint32_t spawn_tick{};
        Vec3f origin{};
        ModelHierarchyTransform orientation{};
        bool articulated_contact_footprint{};
        bool ground_mark{true};
        std::optional<std::array<uint32_t,3>> exact_runtime_angles{};
    };
    struct BuildingDestructionPreviewState {
        uint32_t start_tick{};
        uint32_t effect_id{};
    };
    struct DynamicSceneCpu {
        std::vector<ObjectVertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<ObjectDrawRun> draw_runs;
        std::vector<ObjectVertex> shadow_vertices;
        std::vector<uint32_t> shadow_indices;
        std::vector<ResourceShadingEmitter> shading_emitters;
    };
    struct DynamicShadowTemplate {
        RuntimeShadowOwner owner{};
        uint32_t model_runtime_render_flags{0x10U};
        std::size_t placement_index{std::numeric_limits<std::size_t>::max()};
        // Fog-only authored templates remain valid without a simulation actor. Runtime
        // replacement templates must disappear as soon as their owner is removed.
        bool requires_live_owner{};
        Vec3f anchor_position{};
        uint32_t anchor_yaw_turn16{};
        std::vector<ObjectVertex> receiver_vertices;
        std::vector<uint32_t> receiver_indices;
        std::vector<float> receiver_height_offsets;
        ShadowReceiverPoseCache<ObjectVertex> projected_pose_cache;
    };
    struct EnvironmentalEffectRuntimePoseSnapshot {
        uint32_t effect_id{};
        uint32_t spawn_tick{};
        uint32_t lifetime_ticks{};
        ModelHierarchyTransform effect_pose{};
    };
    std::vector<TimedEffectControllerPreview> timed_effect_controllers_;
    // Retail timed emitters create a detached EffectRuntime at the owner/attachment pose from the
    // pulse tick. Linked shots emitted later must transform their type-2 attachment through that
    // historical effect pose, not through the owner's newer simulation pose. Key is
    // (absolute parent-effect spawn tick << 32) | deterministic schedule pulse index.
    std::unordered_map<uint64_t,EnvironmentalEffectRuntimePoseSnapshot>
        environmental_effect_runtime_pose_history_;
    std::vector<DamageEffectControllerPreview> damage_effect_controllers_;
    std::vector<MovementGroundEffectInstance> movement_ground_effects_;
    std::unordered_map<std::size_t,BuildingDestructionPreviewState> building_destruction_states_;
    std::vector<std::size_t> animated_building_placements_;
    std::vector<std::size_t> animated_unit_placements_;
    std::vector<std::size_t> continuous_radar_placements_;
    std::vector<std::size_t> simulation_actor_placements_;
    std::vector<std::size_t> visible_animated_buildings_;
    std::vector<std::size_t> visible_timed_effect_controllers_;
    std::unordered_map<uint64_t,uint32_t> object_texture_layer_lookup_;
    ObjectTextureSourceReuse object_texture_source_reuse_;
    GeneratedShadowReuse generated_shadow_reuse_;
    std::array<uint64_t,4> object_layer_identity_totals_{};
    uint64_t lab_snapshot_dirty_coalesced_{};
    SimulationLabProductCache simulation_lab_product_cache_;
    SimulationLabRuntimeCatalogCache simulation_lab_runtime_catalog_cache_;
    std::optional<thandor::AssetCatalog> simulation_lab_level_catalog_cache_;
    std::string simulation_lab_level_catalog_key_;
    uint64_t simulation_lab_level_catalog_hits_{},simulation_lab_level_catalog_misses_{};
    std::vector<DynamicShadowTemplate> dynamic_shadow_templates_;
    // Set only while rebuilding the CPU object texture array. The normal dynamic
    // pass reuses the resulting atlas and never uploads textures per frame.
    std::function<void(RuntimeShadowOwner,Vec3f,uint32_t,uint32_t,
        std::span<const thandor::GeneratedShadowTriangle>)> runtime_shadow_collector_;
    // Reused only for camera-only rebuilds of the same presented state. Visibility is
    // rechecked, effect billboards are replayed, and the final draw queue is sorted anew.
    struct DynamicModelViewQuery {
        Vec3f center{};
        AxisAlignedBounds bounds{};
        float margin{};
        bool use_bounds{},visible{};
    };
    struct DynamicModelEffect {
        const thandor::EffectDefinitionRecord* definition{};
        Vec3f position{};
        uint32_t age{},orientation_random{},texture_faction{};
        float yaw{};
        std::optional<ModelHierarchyTransform> orientation;
        std::optional<uint32_t> forced_frame;
        std::optional<std::array<uint32_t,3>> exact_angles;
    };
    struct DynamicModelStep {
        std::size_t first_vertex{},end_vertex{},first_index{},end_index{};
        std::optional<DynamicModelEffect> effect;
    };
    struct DynamicModelCache {
        bool valid{};
        uint32_t visual_tick{},building_tick{};
        const void* document{};
        const void* snapshot{};
        std::array<uint32_t,15> settings{};
        DynamicSceneCpu scene;
        std::vector<DynamicModelViewQuery> views;
        std::vector<DynamicModelStep> steps;
        std::vector<std::size_t> visible_buildings;
        std::vector<SimulationLabRenderReceipt> receipts;
    } dynamic_model_cache_;
    uint64_t dynamic_model_cache_hits_{},dynamic_model_cache_misses_{},dynamic_plain_cache_hits_{};
    uint64_t dynamic_view_cache_misses_{},dynamic_fog_cache_hits_{},dynamic_shadow_cache_hits_{};
    uint64_t dynamic_texture_family_fast_hits_{},dynamic_texture_family_fast_misses_{};
    DynamicSceneCpu dynamic_scene_cpu_;
    std::vector<SimulationLabRenderReceipt> simulation_lab_render_receipts_;
    uint64_t dynamic_scene_revision_{1U};
    std::array<uint64_t,VulkanContext::kFramesInFlight> uploaded_dynamic_scene_revisions_{};
    std::string first_object_build_error_;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    bool stroke_active_{};
    std::vector<AxisAlignedBounds> placement_pick_bounds_;
    std::vector<std::vector<TransformedBoundsHull>> placement_pick_hulls_;
    std::vector<std::vector<TransformedBoundsHull>> placement_model_hulls_;
    std::optional<std::size_t> selected_placement_;
    // Runtime slots are stable within one experiment; run identity prevents a
    // selected index from highlighting an unrelated entity after switching runs.
    std::optional<std::pair<std::size_t,std::size_t>> selected_simulation_lab_entity_;
    std::optional<std::size_t> hovered_placement_;
    std::optional<Vec3f> hovered_terrain_point_;
    thandor::ContextOrderKind hovered_context_order_{thandor::ContextOrderKind::None};
    std::optional<std::size_t> selected_edit_index_;
    char selected_display_name_[128]{};
    uint32_t selected_edit_asset_id_{};
    int selected_edit_faction_{};
    std::array<float,3> selected_edit_position_{};
    float selected_edit_yaw_degrees_{};
    int asset_browser_group_{};
    int asset_browser_scope_{};
    int asset_file_group_{};
    int selected_camera_faction_{1};
    // 0=off, 1=selected/default vehicle traversal, 2=selected/default building placement,
    // 3=raw binary terrain-class bands.
    int terrain_compatibility_overlay_{};
    // Read-only V519 GridScratch diagnostics. This exposes the exact 4x coordinate/mask contract
    // but deliberately does not replace simulation movement while CF returns and runtime-only
    // scratch/route-queue state remain unresolved.
    bool v519_path_diagnostics_{};
    // Editor replay of retail projected terrain visibility. LEV/FLD do not serialize the runtime
    // per-faction occupancy history, so explored bit 7 is session-only while this preview runs.
    // 0=off, 1=one faction, 2=union of all active factions.
    int fog_preview_mode_{};
    int fog_preview_faction_{1};
    // Editor-only baseline for inspecting a map as though every cell has persistent explored bit
    // 7. Current visibility still comes exclusively from the retail projected-occlusion pass, so
    // dynamic units/effects remain hidden outside live visibility.
    bool fog_preview_all_explored_{};
    bool fog_preview_show_nominal_radius_{true};
    std::vector<thandor::FogPreviewObserver> fog_preview_observers_;
    std::vector<uint64_t> fog_preview_occupancy_history_;
    std::vector<uint8_t> fog_preview_runtime_cells_;
    std::size_t fog_preview_visible_cell_count_{};
    std::size_t fog_preview_explored_cell_count_{};
    std::size_t fog_preview_observer_count_{};
    const thandor::MapDocument* fog_preview_history_document_{};
    int fog_preview_history_mode_{};
    int fog_preview_history_faction_{};
    bool fog_preview_history_all_explored_{};
    int fog_preview_history_lab_run_{-1};
    uint32_t fog_preview_last_retail_tick_{std::numeric_limits<uint32_t>::max()};
    GizmoMode gizmo_mode_{GizmoMode::Translate};
    GizmoHandle gizmo_hovered_{GizmoHandle::None};
    GizmoDragState gizmo_drag_{};
    std::optional<uint32_t> drag_preview_asset_id_;
    std::optional<std::pair<uint32_t,uint32_t>> drag_preview_cell_;
    bool placement_rotation_capture_active_{};
    PreviewVec2 placement_rotation_capture_anchor_{};
    mutable std::unordered_map<std::string, std::vector<thandor::ModelVisualNode>>
        faction_technology_hierarchy_cache_;
    mutable std::unordered_map<std::string, std::vector<thandor::SimulationWeaponProfile>>
        faction_technology_weapon_profile_cache_;
    std::unordered_map<uint64_t, AssetPreviewMesh> asset_preview_cache_;
    std::unordered_map<uint64_t, thandor::RgbaImage> asset_preview_texture_cache_;
    std::string status_;
    thandor::AssetValidationReport startup_validation_;
    std::string asset_filter_;
    char new_map_name_[64]{"newmap"};
    char new_display_name_[128]{"New map"};
    int new_width_{91};
    int new_height_{91};
    thandor::NewMapTerrainSettings new_terrain_settings_;
    int new_surface_type_{}; // Water, ice, lava, or terrain style's original surface.
    int new_initial_material_{};
    bool new_empty_map_{true};
    bool new_use_template_dimensions_{true};
    std::string new_map_error_;
    int template_group_{};
    int texture_source_group_{};
    int texture_source_material_{};
    int texture_destination_material_{};
    char texture_import_project_path_[512]{};
    char texture_exchange_path_[512]{"terrain_layer.tga"};
    bool show_new_dialog_{};
    bool show_load_dialog_{};
    bool show_save_dialog_{};
    bool show_campaign_window_{};
    std::optional<thandor::CampaignDocument> campaign_document_;
    std::optional<int32_t> campaign_editing_mission_;
    std::filesystem::path campaign_project_path_;
    bool campaign_dirty_{};
    int campaign_selected_mission_{};
    int campaign_zone_faction_{1};
    bool campaign_show_exit_zones_{};
    int campaign_import_index_{};
    int campaign_package_slot_{};
    std::vector<std::string> campaign_native_paths_;
    std::vector<std::string> campaign_mission_labels_;
    thandor::CampaignValidation campaign_validation_;
    char campaign_file_path_[1024]{};
    char campaign_map_path_[1024]{};
    char campaign_export_path_[1024]{};
    char campaign_new_identifier_[64]{"my_campaign"};
    std::string campaign_message_;
    struct CustomMapEntry {
        std::filesystem::path path;
        std::string label;
    };
    std::vector<CustomMapEntry> custom_maps_;
    std::string load_map_error_;
    std::string save_map_error_;
    int resize_width_{},resize_height_{};
    char load_map_path_[512]{};
    char save_project_path_[512]{};
    std::filesystem::path current_project_path_;
    // Last successfully loaded file (including loose .lev) or stable stock LEV identifier.
    std::string last_opened_map_type_{"stock"};
    std::string last_opened_map_value_;
    std::filesystem::path user_settings_path_;
    std::string last_saved_user_settings_;
    std::chrono::steady_clock::time_point next_user_settings_check_{};
    bool user_settings_restoring_{};
    // Present only when the current document was opened from the stock-map catalog. This keeps
    // Save Current Map tied to the exact source PCK instead of guessing from map names.
    std::optional<std::size_t> current_stock_group_;
    std::array<std::array<char, 2048>, 15> scenario_text_buffers_{};
    bool scenario_text_buffers_dirty_{};
    std::vector<std::string> scenario_planet_labels_;
    std::vector<std::string> scenario_difficulty_labels_;
    std::vector<std::string> scenario_size_labels_;

    void frame();
    EditorUserSettings collect_user_settings() const;
    void save_user_settings(bool force=false) noexcept;
    void start_editor_performance_log();
    void set_logging_enabled(bool enabled);
    void update_ui_scale();
    void refresh_simulation_lab_ui_text();
    struct SimulationLabUiRunText {
        std::string label, value, tick, built, viewport_label;
    };
    std::vector<SimulationLabUiRunText> simulation_lab_ui_text_;
    std::string simulation_lab_progress_text_;
    std::string simulation_lab_objects_text_;
    std::string simulation_lab_results_text_;
    uint32_t simulation_lab_ui_maximum_tick_{};
    char measured_fps_label_[64]{"FPS ..."};
    bool logging_enabled_{};
    int ui_scale_percent_{};
    float applied_font_density_{};
    EditorPerfContext editor_performance_context() const;
    void finish_editor_performance_frame(bool presented,EditorPerformanceLog::Clock::time_point started);
    float left_panel_width() const noexcept;
    float right_panel_width() const noexcept;
    uint32_t estimated_debug_draw_packets() const noexcept;
    void reset_retail_debug_counter_sample() noexcept;
    void record_presented_frame(double now_seconds) noexcept;
    void draw_main_menu();
    void draw_retail_debug_overlay();
    void draw_left_panel();
    void draw_right_panel();
    void draw_status_bar();
    void draw_new_map_dialog();
    void draw_load_map_dialog();
    void draw_save_map_dialog();
    void draw_terrain_generator_panel();
    void update_terrain_generator_selection();
    void draw_terrain_generator_overlay();
    void reset_terrain_generator();
    void discard_terrain_generator_preview();
    void refresh_terrain_generator_selection(bool sample_height=false);
    void preview_generated_terrain();
    void apply_generated_terrain();
    std::vector<uint8_t> terrain_generator_protection() const;
    void draw_campaign_window();
    void draw_campaign_exit_zones();
    void refresh_campaign_labels();
    void open_campaign_mission(std::size_t index);
    void save_campaign_mission(const std::filesystem::path& path);
    void draw_campaign_trigger_destination(uint8_t outcome);
    void draw_material_palette(bool use_asset_filter = false);
    void load_scenario_catalog_labels();
    void refresh_scenario_text_buffers();
    void apply_scenario_text_buffers();
    void draw_selected_object_editor();
    void draw_asset_group(const char* label, std::span<const std::size_t> indices);
    void draw_placement_markers();
    void draw_water_selection_overlay();
    void draw_retail_field_diagnostics_overlay();
    void clear_water_selection();
    void ensure_water_selection_size();
    std::size_t water_selection_count() const noexcept;
    void paint_water_selection(uint32_t x, uint32_t y, bool enabled);
    void apply_flat_water_selection(bool enabled, bool checkpoint_if_needed = true);
    void adjust_water_selection_level(float delta);
    uint32_t graphics_family_for_faction(uint32_t faction_index) const;
    uint32_t runtime_faction_for_placement(std::size_t placement_index,
                                           uint32_t authored_faction) const noexcept;
    void draw_global_faction_selector(const char* widget_id);
    void draw_faction_research_mask_editor(uint32_t faction_index, const char* widget_id);
    void draw_faction_camera_start_cards();
    void draw_simulation_target_card();
    void draw_ingame_selection_panel();
    void draw_simulation_lab_panel();
    void draw_simulation_lab_run_switcher();
    bool select_simulation_lab_run(std::size_t index);
    void draw_simulation_lab_overlay();
    bool factory_placement_exit_clear(const thandor::ArmyAssetPreset& preset,
        uint32_t cell_x,uint32_t cell_y,uint32_t heading);
    void draw_retail_selection_frame(float left,float top,float right,float bottom,int32_t condition_q12,
        std::optional<uint32_t> faction_rgb=std::nullopt,
        const thandor::RetailSelectionFrameMetrics& metrics={});
    std::optional<thandor::RetailSelectionFrameAssets> retail_selection_frame_assets_;
    bool retail_selection_frame_load_attempted_{};
    void start_simulation_lab();
    void clear_simulation_lab();
    void update_simulation_lab(double now_seconds);
    void sync_selected_simulation_lab_run();
    uint32_t fog_preview_active_faction_count() const noexcept;
    void begin_simulation_lab_diagnostics();
    void append_simulation_lab_diagnostics(bool force=false);
    void end_simulation_lab_diagnostics(const char* reason);
    bool simulation_lab_active() const noexcept;
    bool simulation_lab_finished() const noexcept;
    std::span<const thandor::SimulationLabRun> simulation_lab_runs() const noexcept;
    std::span<const thandor::SimulationLabRun> simulation_lab_ui_runs() const noexcept;
    void refresh_simulation_lab_ui_snapshot(double now_seconds, bool force=false);
    bool poll_simulation_lab_snapshot();
    void draw_placement_presets(std::span<const uint32_t> registry_ids,
                                std::optional<thandor::PlacementBrowserKind> browser_kind);
    void draw_placement_card(const thandor::ArmyAssetPreset& preset, float card_width,
                             float card_height, bool available_in_current_level);
    void draw_transform_gizmo();
    void draw_drag_drop_preview();
    void draw_game_cursor();
    void update_canvas_interaction();
    struct PlacementPickHit {
        std::size_t placement_index{};
        float ray_distance{};
    };
    std::optional<std::pair<uint32_t,uint32_t>> pick_cell(float mouse_x,float mouse_y) const;
    std::optional<PlacementPickHit> pick_placement_hit(float mouse_x,float mouse_y) const;
    std::optional<std::size_t> pick_placement(float mouse_x,float mouse_y) const;
    std::optional<std::pair<std::size_t,float>> pick_simulation_lab_entity(float mouse_x,float mouse_y);
    Vec3f placement_command_target_point(std::size_t placement_index) const;
    std::optional<Vec3f> simulation_attack_target_point(std::size_t attacker_index) const;
    thandor::ContextOrderKind resolve_context_order_at_pointer(float mouse_x,float mouse_y);
    void issue_context_order_at_pointer(float mouse_x,float mouse_y);
    void clear_selected_simulation_order();
    float terrain_height_near(float world_x, float world_y) const;
    const thandor::SpriteAsset& cached_sprite(const std::string& path);
    PlacementContactPose placement_contact_pose(const thandor::ArmyAssetPreset& preset,
                                                 float world_x, float world_y,
                                                 float sprite_height_offset) const;
    bool mouse_in_canvas(float mouse_x, float mouse_y) const;
    AssetPreviewMesh& asset_preview(const thandor::ArmyAssetPreset& preset);
    TransformGizmoProjection selected_gizmo_projection() const;
    GizmoHandle hit_test_gizmo(PreviewVec2 mouse) const;
    void update_active_gizmo(PreviewVec2 mouse);
    thandor::PlacementRole placement_role_for_preset(const thandor::ArmyAssetPreset& preset) const noexcept;
    bool placement_has_required_resource_support(const thandor::ArmyAssetPreset& preset,
                                                 const thandor::LevelPlacement& placement) const noexcept;
    const thandor::ArmyAssetPreset* terrain_overlay_profile() const noexcept;
    std::span<const thandor::ModelVisualNode> placement_hierarchy(
        const thandor::ArmyAssetPreset& preset, std::size_t placement_index) const noexcept;
    std::span<const thandor::SimulationWeaponProfile> placement_weapon_profiles(
        const thandor::ArmyAssetPreset& preset, std::size_t placement_index) const;
    bool placement_has_recovery_weapon(std::size_t placement_index) const;
    bool placement_has_hostile_weapon(std::size_t placement_index) const;
    void add_asset_placement(uint32_t x, uint32_t y, uint32_t asset_id);
    void load_map(std::size_t group_index);
    bool load_map_from_path(const std::filesystem::path& path);
    void refresh_custom_maps();
    bool save_project(const std::filesystem::path& path);
    void save_current_map();
    bool create_new_map();
    void reset_new_map_editing_view();
    void load_materials();
    void apply_texture_family_from_map(std::size_t group_index);
    void rebuild_mesh();
    void rebuild_objects();
    void rebuild_dynamic_scene_cpu(bool camera_only=false);
    void activate_fog_on_driving_order(uint32_t faction_index);
    void rebuild_fog_preview_visibility();
    std::vector<uint64_t> simulation_visibility_occupancy_for_faction(uint32_t faction_index);
    bool fog_preview_point_visible(Vec3f point) const noexcept;
    uint8_t fog_preview_model_intensity(Vec3f point,uint32_t model_definition_flags) const noexcept;
    uint32_t fog_preview_observer_radius(const thandor::ArmyAssetPreset& preset) const noexcept;
    void update_simulation(uint32_t presentation_tick);
    void spawn_environmental_linked_shots(uint32_t simulation_tick);
    mutable thandor::V565PathTerrainCache simulation_navigation_terrain_cache_;
    uint64_t simulation_navigation_terrain_signature_{};
    void prepare_simulation_navigation_terrain();
    std::vector<thandor::V528PathRuntimeBlocker> simulation_movement_blockers(
        std::size_t moving_index,std::optional<uint64_t> produced_serial={}) const;
    bool simulation_preview_motion(thandor::SimulationActorState& actor,
        const thandor::ArmyAssetPreset& preset,std::size_t moving_index,
        std::optional<uint64_t> produced_serial={});
    bool simulation_path_segment_clear(std::size_t moving_index, Vec3f from, Vec3f to,
                                       const thandor::ArmyAssetPreset& moving_preset,
                                       std::optional<uint64_t> produced_serial={}) const;
    thandor::V528RouteResult simulation_resolve_v528_route(
        std::size_t moving_index, Vec3f from, Vec3f final_target,
        const thandor::ArmyAssetPreset& moving_preset,
                                       std::optional<uint64_t> produced_serial={}) const;
    std::optional<std::string> resolve_runtime_spatial_sound_path(uint32_t sound_table_index) const;
    thandor::SpatialAudioListener spatial_audio_listener() const noexcept;
    void play_runtime_spatial_sound(uint32_t sound_table_index, Vec3f world_position,
                                    uint32_t gain_q15, uint32_t maximum_distance_q12);
    void play_runtime_global_sound(uint32_t sound_table_index);
    void play_effect_positioned_sound(const thandor::EffectDefinitionRecord& effect,
                                      Vec3f world_position);
    void reconcile_runtime_looping_sounds(
        const std::unordered_map<uint32_t,RuntimeSpatialSoundRequest>& desired_requests);
    void stop_all_runtime_looping_sounds();
    void reset_simulation(bool keep_target = true);
    void ensure_simulation_economy_initialized();
    std::optional<std::pair<Vec3f,Vec3f>> simulation_class13_spawn_and_exit_points(
        std::size_t producer_index,const thandor::ArmyAssetPreset& producer_preset);
    std::optional<Vec3f> simulation_class13_default_drive_target(
        std::size_t producer_index,const thandor::ArmyAssetPreset& producer_preset);
    std::optional<Vec3f> simulation_class13_effective_drive_target(
        std::size_t producer_index,const thandor::ArmyAssetPreset& producer_preset);
    void update_class13_factory_production(std::size_t producer_index,
        const thandor::ArmyAssetPreset& producer_preset,uint32_t tick);
    std::optional<int32_t> simulation_class22_platform_baseline_q12(
        std::size_t producer_index,const thandor::ArmyAssetPreset& producer_preset);
    void update_class21_linked_children(uint32_t tick);
    void update_class22_airport_storage(std::size_t producer_index,
        const thandor::ArmyAssetPreset& producer_preset,uint32_t tick);
    void update_produced_simulation_entities(uint32_t tick);
    enum class SimulationStateOrigin { ExistingPlacement, FreshPlacement };
    thandor::SimulationActorState& simulation_state_for(
        std::size_t placement_index,
        SimulationStateOrigin origin = SimulationStateOrigin::ExistingPlacement);
    void update_idle_class17_deployment(uint32_t presentation_tick);
    std::vector<Vec3f> simulation_weapon_launch_points(
        std::size_t placement_index, const thandor::SimulationWeaponProfile& weapon,
        const thandor::SimulationWeaponState& weapon_state,
        const std::vector<ModelHierarchyTransform>& transforms,
        uint32_t* available_selector_mask = nullptr);
    void synchronize_simulated_actor_placement(
        std::size_t placement_index, const thandor::SimulationActorState& actor);
    std::optional<float> simulation_storage_child_z(const thandor::ModelVisualNode& parent,
        uint32_t slot,uint32_t faction,const thandor::SimulationLabRun* run=nullptr) const;
    std::vector<ModelHierarchyTransform> simulation_hierarchy_transforms(
        std::size_t placement_index, const thandor::ArmyAssetPreset& preset,
        thandor::SimulationActorState& actor, std::vector<uint8_t>* active_nodes = nullptr,
        bool commit_articulated_contact_state = false);
    std::vector<TransformedBoundsHull> simulation_transient_pick_hulls(
        uint32_t army_asset_id,uint32_t faction_index,thandor::SimulationActorState& actor,
        const SimulationLinkedChildEntity* linked_child = nullptr,
        const thandor::SimulationLabEntity* runtime_entity = nullptr);
    std::optional<SimulationModelRaycastHit> simulation_model_raycast_nearest(
        const ViewRay& ray,float maximum_distance,uint32_t query_tick,
        std::optional<std::size_t> excluded_placement = std::nullopt,
        std::optional<SimulationTransientRef> excluded_transient = std::nullopt);
    void simulation_refresh_mode0_projectile_raycast(
        thandor::SimulationProjectileState& projectile,const thandor::ShotDefinitionRecord& shot,
        uint32_t current_tick);
    void simulation_refresh_non_direct_projectile_model_collision(
        thandor::SimulationProjectileState& projectile,const thandor::ShotDefinitionRecord& shot,
        uint32_t current_tick);
    bool simulation_line_of_fire_clear(std::size_t shooter_index,
                                       std::optional<std::size_t> intended_target_index,
                                       Vec3f source, Vec3f target,
                                       const thandor::ShotDefinitionRecord& shot,
                                       bool support_or_repair_command,uint32_t query_tick,
                                       std::optional<SimulationTransientRef> excluded_transient = std::nullopt,
                                       std::optional<SimulationTransientRef> intended_transient = std::nullopt,
                                       bool exclude_shooter_placement = true);
    void commit_selected_simulation_position();
    void bake_selected_target_facing();
    void upload_dynamic_scene(uint32_t frame_index);
    bool placement_visible_in_canvas(Vec3f center, float radius) const;
    bool bounds_visible_in_canvas(const AxisAlignedBounds& bounds, float padding = 0.0F) const;
    Snapshot snapshot() const;
    void restore(const Snapshot& snapshot);
    void checkpoint();
    void undo();
    void redo();
    void add_placement(uint32_t x,uint32_t y,thandor::PlacementRole role);
    void delete_selected_placement();
    void export_game_level_package();
    void update_patch00_overrides();
    void center_camera();
    void capture_faction_camera_start(uint32_t faction_index);
    void focus_faction_camera_start(uint32_t faction_index);
    void aim_camera_at_faction_start_vehicle(uint32_t faction_index);
};

} // namespace editor
