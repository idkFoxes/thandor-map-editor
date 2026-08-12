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
struct SpriteLodGroup {
    uint32_t flags{};
    // Stock SPR triangle records carry an authoritative Q12 plane normal.  Some files, including
    // the Xenite mine root, serialize a subset of corner pointers in the opposite winding.  The
    // original game performs its facing test against the plane normal with Direct3D culling
    // disabled.  The Vulkan editor canonicalizes those triangles during parsing so ordinary GPU
    // back-face culling preserves the same visible surface.
    uint32_t winding_correction_count{};
    SpriteMesh mesh;
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
RgbaImage make_sprite_texture_square_canvas(const RgbaImage& source);

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
    float placement_height_offset() const noexcept { return placement_height_offset_; }
    // SPR +0xE0. Retail passes this signed Q12 value only to the render-flag 0x0800 scaled
    // model-vertex lighting path; the separate 0x0200 direct-tint bypass remains unchanged.
    int32_t lighting_scale_q12() const noexcept { return lighting_scale_q12_; }
    bool has_valid_header_bounds() const noexcept { return has_valid_header_bounds_; }

private:
    uint32_t registry_id_{};
    std::vector<SpriteLodGroup> lod_groups_;
    SpriteMesh generated_shadow_mesh_;
    uint32_t generated_shadow_record_count_{};
    std::vector<SpriteLookupRecord> lookup_records_;
    std::vector<Vec3f> class6_lookup_points_;
    Vec3f local_bounds_min_{};
    Vec3f local_bounds_max_{};
    float bounding_radius_{};
    float placement_height_offset_{};
    int32_t lighting_scale_q12_{};
    bool has_valid_header_bounds_{};
};

struct ModelRuntimeSimulationProfile {
    // Model-definition fields consumed by the 20 Hz movement/aim/fire callbacks.
    int32_t class_step_q12{};               // MDL +0x0C, runtime-class-specific step.
                                             // Ground callbacks use planar advance; the
                                             // effect/destroy callback uses vertical advance.
    int32_t yaw_max_velocity_turn16{};      // MDL +0x10
    int32_t pitch_max_velocity_turn16{};    // MDL +0x14
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
    // ModelRuntime creation copies MDL +0x60 into the live integrity field. Damage, technology
    // variant replacement, and hierarchy aggregation all preserve this as the initial/maximum
    // model-node integrity value.
    int32_t maximum_integrity{};             // MDL +0x60
    // ArmyAssetRecord_RegisterAndRelocate resolves each base MDL and adds these values into
    // ARM runtime-record +0x24/+0x28. The hover card displays the accumulated values with >>5
    // and >>4 respectively. They are construction time and Xenite, not animation fields.
    uint32_t retail_build_time_q5{};          // MDL +0x180
    uint32_t retail_xenite_cost_q4{};         // MDL +0x184
    // InGameSelectionDetailPanel_Rebuild recursively sums this field once for the selected
    // faction-unlocked MDL at each ARM hierarchy node, then displays the Q4 value with >> 4.
    uint32_t retail_energy_cost_q4{};         // MDL +0x18C
    // ArmyRuntime_EmitDamageThresholdEffect reads these four fields from the currently selected
    // MDL. The strict comparison is current integrity percent < threshold. Emission walks
    // model lookup key class 3 in index order and falls back to the model root if no point exists.
    int32_t damage_effect_health_threshold_percent{}; // MDL +0x250
    uint32_t damage_effect_id{};                      // MDL +0x254
    uint32_t damage_effect_min_cooldown_ticks{};      // MDL +0x258
    uint32_t damage_effect_random_cooldown_ticks{};   // MDL +0x25C
    // Runtime class 3 uses these model-definition fields for its paired articulated contacts.
    // +0x54 lifts the root above the two foot contacts. +0xC0 is the Q12 numerator used to
    // derive each contact interpolation step; +0xC4 is the vertical contact lift/clearance term.
    int32_t articulated_root_height_q12{};  // MDL +0x54
    int32_t articulated_contact_interpolation_rate_q12{};  // MDL +0xC0
    int32_t articulated_contact_lift_q12{};  // MDL +0xC4
    // MDL +0x278 indexes the army placement-contact callback table. Kind 1 also selects
    // ground-movement and positioned-sound variant B; other values select variant A.
    // AI class-12 behavior may replace the movement-dispatch value for the current tick.
    int32_t placement_contact_kind{};   // MDL +0x278

    bool has_weapon_definition() const noexcept {
        return shot_definition_id != 0U && reload_ticks != 0U;
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
    // game.exe uses MDL +0x0C0 as the terrain/occupancy bit selector for structure
    // placement and MDL +0x260/+0x264 as the low/high grid-path classification selectors.
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
