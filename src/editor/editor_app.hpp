#pragma once

#include "asset_preview.hpp"
#include "game_cursor.hpp"
#include "model_transform.hpp"
#include "terrain_renderer.hpp"
#include "viewport_picking.hpp"
#include "thandor/map_document.hpp"
#include "thandor/context_order.hpp"
#include "thandor/effect.hpp"
#include "thandor/fog_preview.hpp"
#include "thandor/simulation.hpp"
#include "thandor/terrain_texture.hpp"

#include <memory>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace editor {

enum class Tool { Select, Raise, Lower, Smooth, Flatten, Water, ResourceField, Texture, Resource, PlayerStart, EffectScenery, SimulationTarget, SimulationMove };
enum class GizmoMode { Translate, Rotate };
enum class WaterSelectionStage { SelectArea, PickReference, AdjustLevel };
enum class GizmoHandle { None, MoveX, MoveY, MoveTerrainZ, RotateZ };

class EditorApp {
public:
    EditorApp(GLFWwindow* window, VulkanContext& vulkan, std::filesystem::path game_root);
    void run();

private:
    struct ArmyTextureFamily {
        thandor::GfxAsset graphics;
        thandor::PalAsset palette;
    };

    struct Snapshot {
        std::vector<std::byte> field;
        std::vector<std::byte> level;
        std::vector<thandor::PlacementTag> tags;
        std::string map_name;
        std::string display_name;
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

    struct PendingTechnologyPlacement {
        uint32_t cell_x{};
        uint32_t cell_y{};
        uint32_t asset_id{};
        PreviewVec2 popup_screen_position{};
    };

    GLFWwindow* window_{};
    VulkanContext& vulkan_;
    thandor::AssetCatalog catalog_;
    TerrainRenderer renderer_;
    std::unique_ptr<thandor::MapDocument> document_;
    std::size_t current_group_{};
    Camera camera_;
    GameCursorAsset game_cursor_;
    bool game_cursor_enabled_{true};
    uint32_t game_cursor_frame_{GameCursorAsset::kDefaultFrame};
    bool camera_turn_drag_active_{};
    bool right_pointer_pressed_{};
    bool right_pointer_dragged_{};
    PreviewVec2 right_pointer_press_{};
    Tool tool_{Tool::Select};
    thandor::Brush brush_{};
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
    uint32_t selected_orientation_{};
    bool draw_water_{true};
    bool animate_surface_{true};
    bool animate_buildings_{};
    bool animate_units_{};
    bool animate_timed_effects_{true};
    bool simulation_mode_{};
    bool simulation_selected_only_{true};
    bool simulation_show_ranges_{true};
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
    std::unordered_map<std::string, std::shared_ptr<ArmyTextureFamily>> army_texture_cache_;
    std::size_t rendered_object_count_{};
    std::size_t marker_only_object_count_{};
    std::size_t object_texture_layer_count_{};
    std::size_t power_resource_decal_count_{};
    std::size_t building_resource_decal_count_{};
    std::size_t resource_body_triangle_count_{};
    std::size_t resource_luminous_particle_triangle_count_{};
    std::size_t resource_shading_emitter_count_{};
    std::size_t projected_shadow_triangle_count_{};
    std::size_t generated_shadow_object_count_{};
    std::size_t generated_shadow_texture_count_{};
    std::size_t unsupported_primitive_selector_count_{};
    std::size_t tree_object_count_{};
    std::size_t rock_object_count_{};
    std::size_t related_effect_definition_count_{};
    std::size_t timed_effect_controller_count_{};
    std::size_t timed_effect_sprite_triangle_count_{};
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
    uint32_t current_presentation_tick_{};
    uint32_t current_building_animation_tick_{};
    uint32_t last_dynamic_scene_tick_{std::numeric_limits<uint32_t>::max()};
    uint32_t last_simulation_tick_{std::numeric_limits<uint32_t>::max()};
    std::optional<Vec3f> simulation_target_point_;
    std::unordered_map<std::size_t,thandor::SimulationActorState> simulation_actor_states_;
    // Contextual in-game-style orders are editor-only runtime state. Entity targets are kept
    // separate from point targets so moving enemy actors are followed every 20 Hz tick.
    std::unordered_map<std::size_t,std::size_t> simulation_attack_placement_targets_;
    std::unordered_map<std::size_t,Vec3f> simulation_attack_point_targets_;
    // Editor-only production drive/rally destinations. Retail LEV has no persistent field for
    // this runtime order, so the marker remains transient like move and attack commands.
    std::unordered_map<std::size_t,Vec3f> simulation_production_rally_points_;
    // Static object geometry only tags placements that currently have a transient simulation
    // replacement. Membership changes therefore request one static rebuild; ordinary 20 Hz
    // aiming/movement updates remain dynamic-only.
    bool simulation_static_replacement_dirty_{};
    std::vector<thandor::SimulationProjectileState> simulation_projectiles_;
    uint64_t next_simulation_projectile_serial_{1U};
    std::size_t simulation_active_actor_count_{};
    std::size_t simulation_projectile_count_{};
    std::size_t simulation_volley_count_{};

    struct TimedEffectControllerPreview {
        uint32_t effect_id{};
        std::size_t placement_index{std::numeric_limits<std::size_t>::max()};
        uint32_t root_model_node_index{};
        uint32_t emitter_model_node_index{};
        std::vector<Vec3f> local_type6_attachments;
        Vec3f root_position{};
        ModelHierarchyTransform root_orientation{};
        ModelHierarchyTransform emitter_orientation{};
        std::optional<thandor::ResourceShadingGlowProfile> shading_profile;
        thandor::ResourceEmitterTiming timing;
        bool owner_is_building{};
        float visibility_radius{24.0F};
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
        std::size_t placement_index{std::numeric_limits<std::size_t>::max()};
        Vec3f anchor_position{};
        uint32_t anchor_yaw_turn16{};
        std::vector<ObjectVertex> receiver_vertices;
        std::vector<uint32_t> receiver_indices;
    };
    std::vector<TimedEffectControllerPreview> timed_effect_controllers_;
    std::vector<std::size_t> animated_building_placements_;
    std::vector<std::size_t> animated_unit_placements_;
    std::vector<std::size_t> continuous_radar_placements_;
    std::vector<std::size_t> simulation_actor_placements_;
    std::vector<std::size_t> visible_animated_buildings_;
    std::vector<std::size_t> visible_timed_effect_controllers_;
    std::unordered_map<uint64_t,uint32_t> object_texture_layer_lookup_;
    std::vector<DynamicShadowTemplate> dynamic_shadow_templates_;
    DynamicSceneCpu dynamic_scene_cpu_;
    uint64_t dynamic_scene_revision_{1U};
    std::array<uint64_t,VulkanContext::kFramesInFlight> uploaded_dynamic_scene_revisions_{};
    std::string first_object_build_error_;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    bool stroke_active_{};
    std::vector<AxisAlignedBounds> placement_pick_bounds_;
    std::vector<std::vector<TransformedBoundsHull>> placement_pick_hulls_;
    std::optional<std::size_t> selected_placement_;
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
    // Editor-only current-visibility preview. LEV/FLD do not serialize explored fog history.
    // 0=off, 1=one faction, 2=union of all active factions.
    int fog_preview_mode_{};
    int fog_preview_faction_{1};
    float fog_preview_radius_scale_{1.0F};
    std::vector<thandor::FogPreviewObserver> fog_preview_observers_;
    std::vector<uint8_t> fog_preview_visible_cells_;
    std::size_t fog_preview_visible_cell_count_{};
    std::size_t fog_preview_observer_count_{};
    GizmoMode gizmo_mode_{GizmoMode::Translate};
    GizmoHandle gizmo_hovered_{GizmoHandle::None};
    GizmoDragState gizmo_drag_{};
    std::optional<uint32_t> drag_preview_asset_id_;
    std::optional<PendingTechnologyPlacement> pending_technology_placement_;
    std::unordered_map<uint64_t, AssetPreviewMesh> asset_preview_cache_;
    std::unordered_map<uint64_t, thandor::RgbaImage> asset_preview_texture_cache_;
    std::string status_;
    thandor::AssetValidationReport startup_validation_;
    std::string asset_filter_;
    char new_map_name_[64]{"newmap"};
    char new_display_name_[128]{"New map"};
    int new_width_{91};
    int new_height_{91};
    float new_initial_height_{};
    int new_initial_material_{};
    bool new_empty_map_{true};
    int template_group_{};
    int texture_source_group_{};
    int texture_source_material_{};
    int texture_destination_material_{};
    char texture_import_project_path_[512]{};
    char texture_exchange_path_[512]{"terrain_layer.tga"};
    bool show_new_dialog_{};
    bool show_load_dialog_{};
    bool show_save_dialog_{};
    char load_map_path_[512]{};
    char save_project_path_[512]{};
    std::filesystem::path current_project_path_;
    std::array<std::array<char, 2048>, 15> scenario_text_buffers_{};
    bool scenario_text_buffers_dirty_{};
    std::vector<std::string> scenario_planet_labels_;
    std::vector<std::string> scenario_difficulty_labels_;
    std::vector<std::string> scenario_size_labels_;

    void frame();
    void draw_main_menu();
    void draw_left_panel();
    void draw_right_panel();
    void draw_status_bar();
    void draw_new_map_dialog();
    void draw_load_map_dialog();
    void draw_save_map_dialog();
    void draw_material_palette();
    void load_scenario_catalog_labels();
    void refresh_scenario_text_buffers();
    void apply_scenario_text_buffers();
    void draw_selected_object_editor();
    void draw_asset_group(const char* label, std::span<const std::size_t> indices);
    void draw_placement_markers();
    void draw_water_selection_overlay();
    void clear_water_selection();
    void ensure_water_selection_size();
    std::size_t water_selection_count() const noexcept;
    void paint_water_selection(uint32_t x, uint32_t y, bool enabled);
    void apply_flat_water_selection(bool enabled, bool checkpoint_if_needed = true);
    void adjust_water_selection_level(float delta);
    void draw_global_faction_selector(const char* widget_id);
    void draw_faction_camera_start_cards();
    void draw_simulation_target_card();
    void draw_ingame_selection_panel();
    void draw_placement_presets(std::span<const uint32_t> registry_ids,
                                thandor::PlacementBrowserKind browser_kind);
    void draw_placement_card(const thandor::ArmyAssetPreset& preset, float card_width,
                             float card_height, bool available_in_current_level);
    void draw_transform_gizmo();
    void draw_drag_drop_preview();
    void draw_game_cursor();
    void update_canvas_interaction();
    std::optional<std::pair<uint32_t,uint32_t>> pick_cell(float mouse_x,float mouse_y) const;
    std::optional<std::size_t> pick_placement(float mouse_x,float mouse_y) const;
    Vec3f placement_command_target_point(std::size_t placement_index) const;
    std::optional<Vec3f> simulation_attack_target_point(std::size_t attacker_index) const;
    thandor::ContextOrderKind resolve_context_order_at_pointer(float mouse_x,float mouse_y);
    void issue_context_order_at_pointer(float mouse_x,float mouse_y);
    void clear_selected_simulation_order();
    float terrain_height_near(float world_x, float world_y) const;
    PlacementContactPose placement_contact_pose(const thandor::ArmyAssetPreset& preset,
                                                 float world_x, float world_y,
                                                 float sprite_height_offset) const;
    bool mouse_in_canvas(float mouse_x, float mouse_y) const;
    AssetPreviewMesh& asset_preview(const thandor::ArmyAssetPreset& preset);
    TransformGizmoProjection selected_gizmo_projection() const;
    GizmoHandle hit_test_gizmo(PreviewVec2 mouse) const;
    void update_active_gizmo(PreviewVec2 mouse);
    thandor::PlacementRole placement_role_for_preset(const thandor::ArmyAssetPreset& preset) const noexcept;
    const thandor::ArmyAssetPreset* terrain_overlay_profile() const noexcept;
    std::span<const thandor::ModelVisualNode> placement_hierarchy(
        const thandor::ArmyAssetPreset& preset, std::size_t placement_index) const noexcept;
    const thandor::ArmyTechnologyLevelPreview* technology_level_preview(
        const thandor::ArmyAssetPreset& preset, uint32_t level_index) const noexcept;
    void draw_technology_placement_popup();
    void add_asset_placement(uint32_t x, uint32_t y, uint32_t asset_id,
                             uint32_t technology_level = 0U);
    void load_map(std::size_t group_index);
    void load_map_from_path(const std::filesystem::path& path);
    void save_project(const std::filesystem::path& path);
    void create_new_map();
    void load_materials();
    void apply_texture_family_from_map(std::size_t group_index);
    void rebuild_mesh();
    void rebuild_objects();
    void rebuild_dynamic_scene_cpu();
    void rebuild_fog_preview_visibility();
    bool fog_preview_point_visible(Vec3f point) const noexcept;
    float fog_preview_observer_radius(const thandor::ArmyAssetPreset& preset) const noexcept;
    void update_simulation(uint32_t presentation_tick);
    void reset_simulation(bool keep_target = true);
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
    std::vector<ModelHierarchyTransform> simulation_hierarchy_transforms(
        std::size_t placement_index, const thandor::ArmyAssetPreset& preset,
        thandor::SimulationActorState& actor);
    bool simulation_line_of_fire_clear(Vec3f source, Vec3f target) const;
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
