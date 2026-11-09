#pragma once

#include "thandor/field.hpp"
#include "thandor/gfx.hpp"
#include "thandor/semantic_types.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace thandor {

// The immutable class-dispatch tables bind runtime class 14 to
// ArmyPlacementCandidate_TestFieldOccupancyCf / ArmyRuntimeClass_UpdateGridBoundEffectsAndModels.
// MDL +0xC0 is therefore a resource-field support selector only for class 14; the same physical
// dword has unrelated meanings for other classes (for example class 16 Energy generation).
inline constexpr bool model_runtime_class_uses_resource_field_support(uint32_t runtime_class_id) noexcept {
    return runtime_class_id == 14U;
}

inline constexpr uint32_t resource_field_support_mask(uint32_t occupancy_class) noexcept {
    return occupancy_class < 21U ? (0x00000800U << occupancy_class) : 0U;
}

// SPR texture coordinates and material selectors are serialized per triangle corner, not per
// shared position. The editor therefore expands each triangle to three preview vertices instead
// of incorrectly sharing a vertex whose UV or texture subresource may differ on another face.
struct SpriteMeshVertex {
    Vec3f position;
    Vec3f normal;
    float texture_u{}; // serialized Q12 texel-space coordinate after q12_to_float
    float texture_v{}; // serialized Q12 texel-space coordinate after q12_to_float
    uint32_t texture_subresource{};
    uint32_t render_flags{};
    // SPR mesh-record +0x10. game.exe classifies generated-shadow triangles per
    // mesh record: bit 0 enters the blurred silhouette pass, clear enters the
    // later hard overwrite pass. This is not the top-level LOD-group flag.
    uint32_t mesh_record_flags{};
    // SPR mesh-record +0x04 is tested directly against ModelRuntimeNode.meshGroupMask.
    // Runtime class 9 clears one selector bit when a rocket leaves its rack and restores
    // it when that attachment-specific reload counter expires.
    uint32_t mesh_group_selector_mask{};
};

struct SpriteMesh {
    std::vector<SpriteMeshVertex> vertices;
    std::vector<uint32_t> indices;
};

struct SpriteLookupRecord {
    uint32_t packed_key{};
    Vec3f local_translation{};
    // Exact serialized Q12 translation retained for hierarchy-radius/depth-bin reconstruction.
    // The float view remains for renderer/editor callers.
    std::array<int32_t, 3> local_translation_q12{};

    ModelLookupKeyClass key_class() const noexcept {
        return ModelLookupKeyClass{packed_key & 0xFU};
    }
    ModelLookupKeyIndex key_index() const noexcept {
        return ModelLookupKeyIndex{packed_key >> 4U};
    }
    // Compatibility spellings for older tools.  The +0xE4/+0xE8 table is generic; callers
    // should prefer key_class()/key_index() and select the required class explicitly.
    uint32_t kind() const noexcept { return key_class().value(); }
    uint32_t child_index() const noexcept { return key_index().value(); }
};
using SpriteAttachmentRecord = SpriteLookupRecord;

// The top-level SPR groups are distance-selected model groups. They are alternatives, not pieces
// that may be flattened into one mesh. The static editor preview uses group zero, the game's
// highest-detail group, until camera-distance LOD switching is implemented.
struct SpriteRaycastTriangle {
    std::array<std::array<int32_t,3>,3> vertices_q12{};
    std::array<int32_t,3> normal_q12{};
};
struct SpriteLodGroup {
    uint32_t flags{};
    // Stock SPR triangle records carry an authoritative Q12 plane normal.  Some files, including
    // the Xenite mine root, serialize a subset of corner pointers in the opposite winding.  The
    // original game performs its facing test against the plane normal with Direct3D culling
    // disabled.  The Vulkan editor canonicalizes those triangles during parsing so ordinary GPU
    // back-face culling preserves the same visible surface.
    uint32_t winding_correction_count{};
    SpriteMesh mesh;
    std::vector<SpriteRaycastTriangle> raycast_triangles;
};

struct SpriteTextureUv {
    float u{};
    float v{};
};

// The stock renderer converts SPR Q12 texel coordinates through a universal 256-texel basis and
// then applies a power-of-two aspect correction from the physical GFX dimensions. This is not the
// same as dividing by the selected subresource's width or logical canvas size.
SpriteTextureUv normalize_sprite_texture_uv(float serialized_u, float serialized_v,
                                             uint32_t pixel_width, uint32_t pixel_height);
float normalize_model_texture_offset_u(int32_t fixed_texture_offset,
                                       uint32_t pixel_width, uint32_t pixel_height);
float normalize_model_texture_offset_v(int32_t fixed_texture_offset,
                                       uint32_t pixel_width, uint32_t pixel_height);
RgbaImage make_sprite_texture_square_canvas(const RgbaImage& source);

// Legacy renderer classifier names are preserved for source compatibility. V428 closes the
// blue/Power visual family as Tritium and the gold/Building family as Xenite; no game-facing
// UI should call either deposit generic Energy or Building resource.
enum class ResourceEnergyKind { None, Power, Building };

ResourceEnergyKind classify_resource_energy(const SpriteMesh& mesh) noexcept;
uint32_t resource_body_subresource(ResourceEnergyKind kind) noexcept;
uint32_t resource_luminous_particle_subresource(ResourceEnergyKind kind) noexcept;
uint32_t resource_effect_subresource(ResourceEnergyKind kind) noexcept; // compatibility alias
SpriteMesh extract_sprite_subresource_mesh(const SpriteMesh& mesh, uint32_t texture_subresource);
SpriteMesh extract_resource_body_mesh(const SpriteMesh& mesh, ResourceEnergyKind kind);
SpriteMesh extract_resource_luminous_particle_mesh(const SpriteMesh& mesh, ResourceEnergyKind kind);
SpriteMesh extract_resource_effect_mesh(const SpriteMesh& mesh, ResourceEnergyKind kind); // compatibility alias

class SpriteAsset {
public:
    static SpriteAsset parse(std::span<const std::byte> data);

    uint32_t registry_id() const noexcept { return registry_id_; }
    uint32_t group_count() const noexcept { return static_cast<uint32_t>(lod_groups_.size()); }
    std::span<const SpriteLodGroup> lod_groups() const noexcept { return lod_groups_; }
    const SpriteMesh& mesh() const noexcept { return lod_groups_.front().mesh; }
    const SpriteLodGroup& raycast_group() const noexcept {
        return lod_groups_[lod_groups_.size()-1U-(has_shadow_group_&&lod_groups_.size()>1U)];
    }
    const SpriteMesh& raycast_mesh() const noexcept { return raycast_group().mesh; }
    const SpriteMesh& lod_mesh(std::size_t index) const;
    uint32_t lod_flags(std::size_t index) const;
    // game.exe does not generate model shadows from the visible LOD mesh. SPR +0xEC
    // points to a separate variable-size shadow table whose records are rasterized in
    // two passes according to record flag bit 0.
    bool has_generated_shadow_mesh() const noexcept { return !generated_shadow_mesh_.indices.empty(); }
    const SpriteMesh& generated_shadow_mesh() const noexcept { return generated_shadow_mesh_; }
    uint32_t generated_shadow_record_count() const noexcept { return generated_shadow_record_count_; }
    std::size_t generated_shadow_soft_triangle_count() const noexcept;
    std::size_t generated_shadow_hard_triangle_count() const noexcept;
    std::size_t alternate_lod_triangle_count() const noexcept;
    std::size_t winding_correction_count() const noexcept;
    std::span<const Vec3f> class6_lookup_points() const noexcept { return class6_lookup_points_; }
    std::span<const SpriteLookupRecord> lookup_records() const noexcept { return lookup_records_; }
    const SpriteLookupRecord* find_lookup_record(ModelLookupKeyIndex key_index,
                                                  ModelLookupKeyClass key_class) const noexcept;
    const SpriteLookupRecord* child_attachment(uint32_t child_index) const noexcept;
    // Compatibility views retained for G386-era callers.
    std::span<const Vec3f> type6_attachment_points() const noexcept { return class6_lookup_points(); }
    std::span<const SpriteLookupRecord> attachment_records() const noexcept { return lookup_records(); }
    // SPR +0xC0..+0xDC are the runtime model-resource bounds and placement fields.
    // The placement-contact callbacks add +0xDC only for terrain-contact kinds 0 and 2.
    Vec3f local_bounds_min() const noexcept { return local_bounds_min_; }
    Vec3f local_bounds_max() const noexcept { return local_bounds_max_; }
    float bounding_radius() const noexcept { return bounding_radius_; }
    int32_t bounding_radius_q12() const noexcept { return bounding_radius_q12_; }
    float placement_height_offset() const noexcept { return placement_height_offset_; }
    // SPR +0xE0. Retail passes this signed Q12 value only to the render-flag 0x0800 scaled
    // model-vertex lighting path; the separate 0x0200 direct-tint bypass remains unchanged.
    int32_t lighting_scale_q12() const noexcept { return lighting_scale_q12_; }
    bool has_valid_header_bounds() const noexcept { return has_valid_header_bounds_; }

private:
    uint32_t registry_id_{};
    std::vector<SpriteLodGroup> lod_groups_;
    bool has_shadow_group_{};
    SpriteMesh generated_shadow_mesh_;
    uint32_t generated_shadow_record_count_{};
    std::vector<SpriteLookupRecord> lookup_records_;
    std::vector<Vec3f> class6_lookup_points_;
    Vec3f local_bounds_min_{};
    Vec3f local_bounds_max_{};
    float bounding_radius_{};
    int32_t bounding_radius_q12_{};
    float placement_height_offset_{};
    int32_t lighting_scale_q12_{};
    bool has_valid_header_bounds_{};
};

struct ModelRuntimeSimulationProfile {
    // Model-definition fields consumed by the 20 Hz movement/aim/fire callbacks.
    int32_t class_step_q12{};               // MDL +0x0C, runtime-class-specific step.
    // Keep the raw multi-role class fields alongside semantic class-specific aliases. Runtime
    // class 13 uses +0x0C as the factory texture-V open/close step and +0xC4 as the candidate
    // compatibility mask; class 3 interprets +0xC4 as articulated contact lift instead.
    int32_t class_specific_c0_raw{};           // MDL +0xC0, runtime-class-specific raw dword.
    uint32_t class_specific_c4_raw{};          // MDL +0xC4, runtime-class-specific raw dword.
    int32_t class_specific_c8_raw{};           // MDL +0xC8, runtime-class-specific raw dword.
    int32_t class_specific_cc_raw{};           // MDL +0xCC, runtime-class-specific raw dword.
    // V567: ArmyRuntime_ClassCommandHandlerGroupACf uses candidate MDL +0x19C as the
    // candidate-local proximity extent and the current Group-A owner's MDL +0x1A8 as padding.
    // The sum is squared and compared against XY distance before class-18 conversion is allowed.
    // Keep both fields raw because other runtime classes may reuse these offsets.
    int32_t class_specific_19c_raw{};          // MDL +0x19C, runtime-class-specific raw dword.
    int32_t class_specific_1a8_raw{};          // MDL +0x1A8, runtime-class-specific raw dword.
                                             // Ground callbacks use planar advance; the
                                             // effect/destroy callback uses vertical advance.
    int32_t yaw_max_velocity_turn16{};      // MDL +0x10
    int32_t pitch_max_velocity_turn16{};    // MDL +0x14
    // Raw MDL +0x18. AiCombatTarget_EvaluateCandidateScore tests this only after an
    // otherwise-best candidate fails ArmyWeaponRuntime_TestTargetLineOfFireCf: zero rejects the
    // obstructed candidate entirely; nonzero keeps the quartered obstruction score. Preserve the
    // raw field until a broader semantic name is proved.
    int32_t runtime_field_18_raw{};          // MDL +0x18
    // Class-dependent terrain registration overrides +0x24/+0x268. Keep these
    // separate from the weapon pitch range and preserve serialized fallbacks.
    int32_t ground_slope_threshold_turn16{};
    int32_t ground_secondary_slope_threshold_turn16{};
    int32_t yaw_acceleration_turn16{};      // MDL +0x1C
    int32_t pitch_acceleration_turn16{};    // MDL +0x20
    int32_t pitch_min_turn16{};             // MDL +0x24
    int32_t pitch_max_turn16{};             // MDL +0x28
    uint32_t shot_definition_id{};          // MDL +0x2C, relocated to ShotDefinition* at runtime
    uint32_t reload_ticks{};                // MDL +0x30
    int32_t recoil_rotation_step_turn16{};  // MDL +0x34
    int32_t recoil_duration_ticks{};        // MDL +0x38
    int32_t recoil_translation_q12{};       // MDL +0x3C
    int32_t action_vector0{};               // MDL +0x40
    int32_t action_vector1{};               // MDL +0x44
    // ArmyRuntime_RebuildDerivedSelectionMetrics and ModelRuntimePool_CreateInstanceByDefinitionIdCf
    // take the maximum MDL +0x48 across the resolved live model hierarchy. The projected-terrain
    // occlusion callback passes that exact value as its radius and converts it to terrain scan
    // steps with radius / 0x240, clamped to 1..255.
    int32_t fog_uncover_radius_q12{};        // MDL +0x48
    // MDL +0x27C, a distinct occupancy-bit-1 hex-scan radius. The army
    // retains the max across all live linked models; do not confuse with
    // fog (+0x48) or Group-A build proximity (+0x19C).
    std::uint32_t occupancy_mark_radius_units{};
    // ModelRuntimeHierarchy_AccumulateDerivedSelectionMetrics computes ArmyRuntime +0x48 as
    // max(nodeWorldZ-rootWorldZ + MDL +0x70) over the live model hierarchy. The projected fog
    // callback then adds that offset to the root node world Z to obtain the terrain-scan eye height.
    int32_t aim_height_offset_q12{}; // MDL +0x50, target model-origin aim offset
    int32_t placement_height_offset_q12{}; // MDL +0x54, contact callback offset
    int32_t fog_reference_height_offset_q12{}; // MDL +0x70
    // Metadata at MDL +0x74; retained for asset inspection. The Lab does not
    // spawn a ruin automatically when a combat-destroyed root is removed.
    // -1 means no replacement is specified; IDs are not damage-stage indexes.
    int32_t destroyed_replacement_army_asset_id{-1}; // MDL +0x74
    // Every retail positioned ArmyRuntime sound callback passes the selected root MDL +0x78
    // gain and +0x7C maximum distance alongside its class-owned sound-table index. Keep these
    // fields independent from D0/D8/1AC/274, which choose *which* sound slot is requested.
    uint32_t positioned_sound_gain_q15{};            // MDL +0x78
    uint32_t positioned_sound_maximum_distance_q12{}; // MDL +0x7C
    // V567 E1: ModelRuntimePool_CreateInstanceByDefinitionIdCf copies these eight MDL pairs
    // into ModelRuntimeSlot countdown state. Once a linked component reaches terminal integrity,
    // ArmyRuntime_ProcessReadyAttachmentChannels emits the paired EFF at lookup key
    // (channel << 4) | 3 exactly when that channel countdown reaches zero.
    std::array<uint32_t, 8> terminal_attachment_effect_ids{};      // MDL +0x80,+0x88,...,+0xB8
    std::array<int32_t, 8> terminal_attachment_delay_ticks{};      // MDL +0x84,+0x8C,...,+0xBC
    // ArmyRuntime_UpdateTimedShotAndEffectEmitters owns two independent 20 Hz countdowns on every
    // live ArmyRuntime/MDL root. The shot slot uses -1 as the serialized "none" sentinel. Timed
    // EFF selection is environment-sensitive: land/default comes from +0x174 and positive nearest
    // waterSurfaceDelta switches to +0x58 at the resolved class-6 emitter point.
    // Three independent ArmyRuntime creation/spawn paths increment the model-definition creation
    // counter at +0x1B0 and queue +0x160 for the first creation, +0x164 for later creations. These
    // are notification movie/resource ids; they are not LEV condition actions. Campaign maps can
    // therefore show notifications by placing/producing models that carry these fields.
    uint32_t first_creation_notification_movie_id{};  // MDL +0x160
    uint32_t repeat_creation_notification_movie_id{}; // MDL +0x164
    uint32_t timed_effects_require_research{}; // MDL +0x1C8, class 4 gate (also research slot 1)
    int32_t timed_shot_definition_id{-1};     // MDL +0x168
    uint32_t timed_shot_interval_ticks{};     // MDL +0x16C
    uint32_t timed_shot_random_ticks{};       // MDL +0x170
    uint32_t timed_water_effect_id{};         // MDL +0x058
    uint32_t timed_effect_id{};               // MDL +0x174
    uint32_t timed_effect_interval_ticks{};   // MDL +0x178
    uint32_t timed_effect_random_ticks{};     // MDL +0x17C
    // MDL +0x68 is also consumed by the timed emitter: bit 0 selects a persistent attachment
    // counter instead of RNG when choosing class-6 lookup points.
    uint32_t runtime_render_flags{};           // MDL +0x068
    // ModelRuntime creation copies MDL +0x60 into the live integrity field. Damage, technology
    // variant replacement, and hierarchy aggregation all preserve this as the initial/maximum
    // model-node integrity value.
    int32_t maximum_integrity{};             // MDL +0x60
    int32_t health_regeneration_per_step{};  // MDL +0x1B4
    // ArmyAssetRecord_RegisterAndRelocate resolves each base MDL and adds these values into
    // ARM runtime-record +0x24/+0x28. The hover card displays the accumulated values with >>5
    // and >>4 respectively. They are construction time and Xenite, not animation fields.
    uint32_t retail_build_time_q5{};          // MDL +0x180
    uint32_t retail_xenite_cost_q4{};         // MDL +0x184
    // MDL +0x18C is copied into ModelRuntimeSlot.energyDemandQ4 and priority-allocated by the
    // faction Energy scheduler. The selection panel recursively sums the same Q4 demand.
    uint32_t retail_build_energy_q4{};         // MDL +0x188, held by the producer
    uint32_t retail_energy_demand_q4{};        // MDL +0x18C
    // MDL +0x190 is the model-owned construction/deconstruction effect. Retail placement emits it
    // immediately after creating a placed army runtime. The dismantle completion path reuses the
    // same definition with EFFECT_RUNTIME_COMPLETION_DESTROY_MODEL_HIERARCHY. Keep this separate
    // from timed production smoke at +0x174 and damage-threshold effects at +0x254.
    uint32_t construction_destruction_effect_id{}; // MDL +0x190
    // Runtime class 16 contributes MDL +0xC0 to faction Energy generation capacity while alive.
    // Other runtime classes reuse +0xC0 for different class-specific meanings.
    uint32_t energy_generation_capacity_q4{};  // MDL +0xC0, runtime class 16 only
    // Runtime class 14 packs the low byte of MDL +0xC8 into FieldGridCell.runtime7C
    // bits 24..31. The connected Xenite/Tritium-region economy later multiplies that byte by
    // the region size per active extractor and the owning faction terrain contribution scale.
    // This is an extraction contribution weight, not a standalone per-second production rate.
    uint32_t resource_extraction_weight_u8{}; // MDL +0xC8 low byte, runtime class 14 only
    // V537 closes class 14/15 +0xC0/+0xC4 as faction resource-storage semantics.
    // Selector 0 contributes Xenite capacity; selector 1 contributes Tritium capacity.
    uint32_t resource_storage_selector{};
    uint32_t resource_storage_capacity_contribution_q4{};
    bool has_resource_storage_capacity_contribution{};
    // Ground movement classes audited in V537 apply interpolated water delta against these fields.
    int32_t water_damage_multiplier{};       // MDL +0x194
    int32_t water_damage_threshold_q12{};    // MDL +0x198
    // Ground steering interpolation recovered by V537. These are distinct from the generic
    // weapon yaw fields even though +0x10/+0x1C share their serialized offsets.
    int32_t ground_heading_interpolation_distance_q12{}; // MDL +0xC0
    uint32_t ground_far_heading_error_limit_turn16{};    // MDL +0xC4
    uint32_t ground_near_heading_error_limit_turn16{};   // MDL +0xC8
    // Runtime class 23 vertical deployment.
    int32_t vertical_deployment_step_q12_per_tick{}; // MDL +0x0C
    int32_t vertical_deployment_travel_limit_q12{};  // MDL +0x24
    // ArmyRuntime_EmitDamageThresholdEffect reads these four fields from the currently selected
    // MDL. The strict comparison is current integrity percent < threshold. Emission walks
    // model lookup key class 3 in index order and falls back to the model root if no point exists.
    int32_t damage_effect_health_threshold_percent{}; // MDL +0x250
    uint32_t damage_effect_id{};                      // MDL +0x254
    uint32_t damage_effect_min_cooldown_ticks{};      // MDL +0x258
    uint32_t damage_effect_random_cooldown_ticks{};   // MDL +0x25C
    // Runtime class 3 uses these model-definition fields for its paired articulated contacts.
    // +0x54 lifts the root above the two foot contacts. +0xC0 is the Q12 numerator used to
    // derive each contact interpolation step. +0xC4 is intentionally multi-role: it biases the
    // contact-distance denominator (4x in the initialization path) and also supplies the swing
    // contact Z arc in ArmyArticulatedRuntime_UpdateSuspensionHierarchy.
    int32_t articulated_root_height_q12{};  // MDL +0x54
    int32_t articulated_contact_interpolation_rate_q12{};  // MDL +0xC0
    int32_t articulated_contact_lift_q12{};  // MDL +0xC4
    // Runtime class 3 only. ArmyArticulatedRuntime_UpdateSelectedTerrainContact clamps the
    // signed contact steering angle against this turn16 magnitude before deriving the next
    // left/right terrain-contact target. Stock Emu uses 0x4000; Orang/Gorilla use 0x2000.
    uint32_t articulated_contact_steering_limit_turn16{}; // MDL +0xC8
    // ArmyCollision_FindBlockingRuntimeForCurrentUnitCf and the ordinary/articulated ground
    // movement owners use MDL +0xDC as the moving runtime radius. Candidate tests compare the
    // planar distance against this radius plus the other runtime's MDL +0xDC radius. This field
    // must never be sourced from SPR +0xDC, which is the separate placement-height offset.
    int32_t movement_collision_radius_q12{}; // MDL +0xDC
    int32_t attachment_proximity_radius_q12{}; // MDL +0x1A0, independent serialized radius
    // Completed class-3 left/right contacts use +0xCC as an index into the level runtime's
    // spatial-sound table. The LEV loader takes LevelPaths::sounds (stock sound/sound??), forces
    // the .sam extension, enumerates all matching package/filesystem entries, parses each matched
    // filename's trailing decimal number, and stores the created SpatialSoundSlot at that numeric
    // index. This is distinct from the internal
    // DirectSound voice-set registry, whose slots are assigned by first-free creation order.
    uint32_t articulated_contact_sound_asset_id{}; // MDL +0xCC, world spatial-sound table index
    // Runtime class 13 plays this positioned one-shot both when a produced unit is created and
    // again after it clears the producer link. Gain/distance still come from +0x78/+0x7C.
    // Runtime-class-specific sound/effect table slot at MDL +0x26C. Class 13 uses it for
    // production completion; class 21 passes the same raw slot to the mapped-terrain sound gate.
    uint32_t runtime_class_effect_sound_asset_id_26c{}; // MDL +0x26C
    uint32_t class13_completion_sound_asset_id{}; // compatibility alias for class 13
    // ArmyRuntimeSpawner_PlayCreationSound uses the independent +0x270 slot. Runtime class 21
    // calls it on its producer when the linked child releases class-22 state 3 -> 4. Do not
    // alias this to the class-13 completion sound at +0x26C.
    uint32_t creation_sound_asset_id_270{};    // MDL +0x270
    // Runtime audio method-D callbacks consume MDL +0xD0/+0xD8 with class-specific state gates.
    // Class 2/17 use +0xD0 for any movement/turn and +0xD8 additionally while turning;
    // classes 5..8 use +0xD8 as their movement-linked loop. Keep the raw slots distinct.
    uint32_t projected_sound_asset_id_d0{};        // MDL +0xD0
    uint32_t projected_sound_asset_id_d8{};        // MDL +0xD8
    uint32_t projected_sound_asset_id_primary{};   // MDL +0x1AC
    uint32_t projected_sound_asset_id_secondary{}; // MDL +0x274
    // MDL +0x278 indexes the army placement-contact callback table. Kind 1 also selects
    // ground-movement and positioned-sound variant B; other values select variant A.
    // AI class-12 behavior may replace the movement-dispatch value for the current tick.
    int32_t placement_contact_kind{};   // MDL +0x278

    bool has_weapon_definition() const noexcept {
        // SHT registry ID 0 is the stock basic laser, not a null weapon.
        return reload_ticks != 0U;
    }
};

struct ModelVisualNode {
    uint32_t serialized_offset{};
    uint32_t model_definition_id{};
    // The runtime callback class belongs to the linked MDL definition, not to the ARM root.
    // Keeping it on every resolved node prevents weapon-class behavior from being inferred
    // from an unrelated building or vehicle root.
    uint32_t runtime_class_id{};
    // MDL +0x5C selects the SHT +0x90 target-impact EFF entry after a ray hit.
    // It is independent from the runtime callback class at MDL +0x4C.
    uint32_t shot_impact_class_index{};
    ModelRuntimeSimulationProfile simulation;
    // MDL +0x1C8..+0x234: research slots 1..28, excluding the creation grant.
    std::array<uint32_t, 28> research_technology_ids{};
    uint32_t parent_index{std::numeric_limits<uint32_t>::max()};
    uint32_t child_slot{};
    uint32_t flags{};
    uint32_t local_rotation_angle0{};
    uint32_t local_rotation_angle1{};
    uint32_t local_rotation_angle2{};
    std::string sprite_path;

    bool creates_runtime_node() const noexcept { return (flags & 0xFU) == 0U; }
};

struct ModelVisualRecord {
    uint32_t definition_id{};
    // game.exe resolves model display text as texte/help.str page 0x18,
    // resource index 0x4F + this serialized MDL +0x04 value.
    uint32_t name_text_offset{};
    bool has_name_text_offset{};
    uint32_t runtime_class_id{};
    // MDL +0x5C is the target-impact class consumed by projectile maintenance.
    uint32_t shot_impact_class_index{};
    ModelRuntimeSimulationProfile simulation;
    // ModelNodeRuntime_CreateHierarchyRecursiveCf copies generated-shadow eligibility from
    // MDL +0x68. Bit 0x40 suppresses runtime flag 0x100 and therefore shadow generation.
    uint32_t runtime_render_flags{};
    bool casts_generated_shadow{true};
    std::string source_path;
    std::string sprite_path;
    std::vector<ModelVisualNode> hierarchy_nodes;
    // Runtime class 14 alone uses MDL +0x0C0 as the required resource-field support selector
    // (0 -> Xenite/0x0800, 1 -> Tritium/0x1000). Other runtime classes reuse +0xC0 for
    // unrelated state. MDL +0x260/+0x264 are the low/high grid-path classification selectors.
    uint32_t placement_occupancy_class{};
    uint32_t traversal_low_band_index{};
    uint32_t traversal_high_band_index{};
    bool has_placement_occupancy_class{};
    bool has_traversal_profile{};
    // Runtime class 13 copies MDL +0x0C0 into the root model-node primary animated
    // texture-subresource selector. The renderer applies the original 0..0x80000
    // V-offset range only to root-node triangles using this subresource.
    uint32_t primary_animated_subresource{};
    uint32_t secondary_animated_subresource{};
    bool has_primary_texture_animation{};
    bool has_secondary_texture_animation{};
    // Root-MDL compatibility summaries. Exact runtime replay uses ModelVisualNode::simulation so
    // linked MDL runtimes retain their own timed shot/EFF/environment selector state.
    int32_t timed_shot_definition_id{-1};
    uint32_t timed_shot_interval_ticks{};
    uint32_t timed_shot_random_ticks{};
    uint32_t timed_water_effect_id{};
    uint32_t timed_effect_id{};
    uint32_t timed_effect_interval_ticks{};
    uint32_t timed_effect_random_ticks{};
    // ModelDefinition_IsFactionTechnologyUnlockedCf reads MDL +0x1C0. ARM variant selection
    // scans up to eight linked model definitions and chooses the highest unlocked variant.
    uint32_t technology_unlock_id{};
    // ModelDefinition_UnlockLinkedTechnologyForFactionCf reads MDL +0x1C4. This is the
    // linked/base technology automatically forwarded to the faction unlock path; it is not the
    // first technology-panel slot.
    uint32_t linked_technology_id{};
    // InGameTechnologyPanel_Rebuild and the AI producer/purchase scans read exactly 28
    // TechnologyId values from MDL +0x1C8 through +0x234. Their serialized indices are 1..28,
    // distributed over seven direction/branch controls and four panel rows.
    std::array<uint32_t, 28> technology_panel_ids{};
    // ModelRuntimeHierarchy_ApplyFactionTechnologyVariantsRecursive scans these six IDs in
    // serialized order and selects the first nonzero candidate whose candidate MDL +0x1C0
    // technology is present in the faction's 256-bit runtime technology mask.
    std::array<uint32_t, 6> faction_technology_variant_model_ids{}; // MDL +0x238..+0x24C
};

std::vector<ModelVisualRecord> parse_model_visual_records(std::span<const std::byte> data,
                                                           std::string_view source_path = {});

} // namespace thandor
