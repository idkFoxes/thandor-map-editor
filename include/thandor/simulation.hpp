#pragma once

#include "thandor/effect.hpp"
#include "thandor/field.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <limits>
#include <span>
#include <vector>

namespace thandor {

inline constexpr uint32_t kSimulationTicksPerSecond = 20U;
inline constexpr uint32_t kSimulationAimToleranceTurn16 = 0x400U;

enum class SimulationLaunchPolicy : uint8_t {
    AllKind2Attachments,
    AlternateLaunchNodes,
    SequentialKind2Attachments
};

struct SimulationWeaponProfile {
    // The executable treats a weapon assembly as a hierarchy: the highest same-definition node
    // owns yaw, its child owns pitch, and one or more leaf nodes own kind-2 launch attachments and
    // recoil. Keep those roles separate so towers rotate their casing instead of only twisting the
    // muzzle leaf. Multiple launch leaves are one alternating weapon channel, not independent guns.
    uint32_t model_node_index{};       // compatibility alias for launch_model_node_index
    uint32_t yaw_model_node_index{};
    uint32_t pitch_model_node_index{};
    // Class 9 computes the target angles from root->child[0], but launches from the selected
    // kind-2 point after rebuilding the complete live hierarchy. Keep the aim pivot separate from
    // the muzzle node so the turret does not solve its own pose from a moving rocket attachment.
    uint32_t aim_model_node_index{std::numeric_limits<uint32_t>::max()};
    uint32_t launch_model_node_index{}; // compatibility alias for launch_model_node_indices.front()
    // The eight class-9 mesh selector bits live on root->child[0]->child[0]. That rack node is
    // independent from the node whose SPR owns the kind-2 launch points.
    uint32_t rack_mesh_model_node_index{std::numeric_limits<uint32_t>::max()};
    std::vector<uint32_t> launch_model_node_indices;
    SimulationLaunchPolicy launch_policy{SimulationLaunchPolicy::AllKind2Attachments};
    uint32_t model_definition_id{};
    uint32_t runtime_class_id{};
    uint32_t shot_definition_id{};
    uint32_t reload_ticks{};
    // Runtime class 9 uses MDL +0x38 as a shared interval between separate attachment
    // launches while MDL +0x30 remains an independent reload counter for each selector.
    uint32_t inter_shot_ticks{};
    int32_t maximum_range_q12{};
    int32_t yaw_max_velocity_turn16{};
    int32_t yaw_acceleration_turn16{};
    int32_t pitch_max_velocity_turn16{};
    int32_t pitch_acceleration_turn16{};
    int32_t pitch_min_turn16{};
    int32_t pitch_max_turn16{};
    int32_t recoil_rotation_step_turn16{};
    int32_t recoil_duration_ticks{};
    int32_t recoil_translation_q12{};
    uint32_t launch_attachment_count{};
    uint32_t launch_attachment_selector_mask{};

    bool valid() const noexcept {
        return shot_definition_id != 0U && reload_ticks != 0U && launch_attachment_count != 0U;
    }
};

bool simulation_model_node_is_weapon_aim_axis(
    std::span<const SimulationWeaponProfile> profiles,uint32_t model_node_index) noexcept;

struct SimulationMotionProfile {
    // Ground-movement interpretation of MDL +0x0C. Other runtime classes reuse the same
    // serialized field for class-specific work, including vertical effect/model teardown.
    int32_t speed_q12_per_tick{};
    // Placement-contact callback index from MDL +0x278. Kind 1 also dispatches ground-movement
    // variant B; every other value dispatches variant A. The editor records the exact value even
    // though its direct-drive preview still uses one shared kinematic approximation.
    int32_t placement_contact_kind{};
    int32_t yaw_max_velocity_turn16{};
    int32_t yaw_acceleration_turn16{};

    bool movable() const noexcept { return speed_q12_per_tick != 0; }
};

struct SimulationAxisState {
    uint32_t angle_turn16{};
    int32_t velocity_turn16{};
};

struct SimulationAimAngles {
    uint32_t yaw_turn16{};
    uint32_t pitch_turn16{};
    bool valid{true};
};

struct SimulationWeaponState {
    SimulationAxisState yaw{};
    SimulationAxisState pitch{0x4000U, 0};
    uint32_t cooldown_ticks{};
    uint32_t inter_shot_ticks{};
    std::array<int32_t, 8> attachment_reload_ticks{};
    // Enabled SPR mesh groups for the fixed class-9 rack node. The executable subtracts the
    // simulation step even from zero and restores a bit only after the signed timer becomes
    // negative, so a timer that has just reached zero stays hidden until the following tick.
    uint32_t launch_mesh_group_mask{0xFFFFFFFFU};
    uint32_t last_fired_attachment_selector{0xFFFFFFFFU};
    uint32_t recoil_rotation_ticks{};
    uint32_t recoil_translation_ticks{};
    uint32_t next_launch_attachment{};
    uint32_t recoil_launch_selector{};
    int32_t recoil_angle_offset_turn16{};
    int32_t recoil_translation_offset_q12{};
    bool target_in_range{};
    bool target_aligned{};
    bool line_of_fire_clear{true};
};

struct SimulationActorState {
    Vec3f position{};
    uint32_t body_yaw_turn16{};
    int32_t body_turn_velocity_turn16{};
    std::vector<SimulationWeaponState> weapons;
    std::optional<Vec3f> move_target;
    bool target_in_range{};
    bool target_aligned{};
    uint64_t last_updated_tick{};
    // Runtime class 17 starts with a 24-tick deployment state. Its first three root
    // children each advance localRotationAngle1 by 0x2AA per tick.
    uint32_t class17_rotor_deploy_ticks_remaining{};
    uint32_t class17_rotor_tilt_turn16{};
    uint32_t class17_bank_angle_turn16{0x4000U};
    uint32_t class17_bank_heading_turn16{};
    // Runtime class 2 root-node texture offsets use the signed +/-0x100000 period.
    int32_t primary_track_texture_offset_u{};
    int32_t secondary_track_texture_offset_u{};
    // Runtime class 3 alternates two terrain contacts. One foot remains anchored while the
    // opposite contact interpolates toward the authored terminal-foot position. Heading changes
    // also advance this state, allowing an in-place 180-degree turn instead of relying on the
    // class-3 root MDL's intentionally zero generic yaw fields.
    std::array<Vec3f, 2> articulated_foot_anchor_world{};
    std::array<uint32_t, 2> articulated_contact_heading_turn16{};
    uint32_t articulated_contact_progress_q12{};
    // G391 compatibility view: the low 0x1000 contact domain expanded to turn16.
    uint32_t articulated_gait_phase_turn16{};
    uint32_t articulated_swing_side{};
    uint32_t articulated_pose_swing_side{};
    bool articulated_contacts_initialized{};
    bool articulated_contact_transition_active{};
    bool articulated_was_moving{};
    bool articulated_turning{};
};

enum class SimulationImpactKind : uint8_t {
    None,
    TerrainSurface,
    SecondarySurface,
    TargetModelImpactClass,
};

struct SimulationProjectileState {
    uint64_t serial{};
    std::size_t owner_placement_index{};
    std::size_t weapon_profile_index{};
    uint32_t shot_definition_id{};
    Vec3f launch_position{};
    Vec3f target_position{};
    Vec3f launch_velocity{};
    uint32_t spawn_tick{};
    uint32_t travel_ticks{};
    uint32_t lifetime_ticks{};
    uint32_t launch_yaw_turn16{};
    uint32_t launch_pitch_turn16{};
    uint32_t fired_attachment_selector{0xFFFFFFFFU};
    SimulationImpactKind impact_kind{SimulationImpactKind::None};
    Vec3f impact_position{};
    uint32_t impact_material_index{};
    uint32_t impact_effect_id{};
    uint32_t impact_visual_lifetime_ticks{};
};

struct SimulationWeaponTickResult {
    bool turned{};
    bool fired{};
    bool in_range{};
    bool aligned{};
    bool line_of_fire_clear{};
    uint32_t fired_attachment_selector{0xFFFFFFFFU};
};

struct SimulationActorTickResult {
    bool moved{};
    bool turned{};
    bool fired{};
    bool in_range{};
    bool aligned{};
    std::vector<std::size_t> fired_weapon_indices;
};

int32_t shortest_turn16_delta(uint32_t current, uint32_t target) noexcept;
uint32_t wrap_turn16(int64_t value) noexcept;
float turn16_to_radians(uint32_t value) noexcept;
uint32_t radians_to_turn16(float radians) noexcept;

// Exact one-tick forms of the recovered yaw and pitch smoothers. The yaw path is modular;
// pitch is signed and clamped to the MDL bounds before acceleration/deceleration.
bool simulation_step_yaw(SimulationAxisState& state, uint32_t target_turn16,
                         int32_t maximum_velocity_turn16,
                         int32_t acceleration_turn16,
                         uint32_t tolerance_turn16 = kSimulationAimToleranceTurn16) noexcept;
bool simulation_step_pitch(SimulationAxisState& state, uint32_t target_turn16,
                           int32_t maximum_velocity_turn16,
                           int32_t acceleration_turn16,
                           int32_t minimum_pitch_turn16,
                           int32_t maximum_pitch_turn16,
                           uint32_t tolerance_turn16 = kSimulationAimToleranceTurn16) noexcept;

// Compatibility wrapper retained for earlier tests/callers; it uses the modular yaw contract.
bool simulation_step_axis(SimulationAxisState& state, uint32_t target_turn16,
                          int32_t maximum_velocity_turn16,
                          int32_t acceleration_turn16,
                          uint32_t tolerance_turn16 = kSimulationAimToleranceTurn16) noexcept;

SimulationAimAngles simulation_target_angles(Vec3f source, Vec3f target,
                                              int32_t elevation_offset_angle16 = 0) noexcept;
SimulationAimAngles simulation_shot_launch_angles(Vec3f source, Vec3f target,
                                                   const ShotDefinitionRecord& shot) noexcept;

bool simulation_target_within_range(Vec3f source, Vec3f target,
                                    const SimulationWeaponProfile& weapon) noexcept;

// Class-9 launch racks clear the complete low-bit prefix through the fired selector
// (`meshGroupMask &= 0xFFFFFFFE << selector`), not only the selected bit.
uint32_t simulation_class9_mesh_mask_after_fire(uint32_t current_mask,
                                                uint32_t selector) noexcept;
// Return the authoritative class-9 rack mask. Timer expiry mutates the mask in the 20 Hz callback;
// rendering must not synthesize visibility directly from timer==0 because that skips the binary's
// signed-underflow transition and can place restored rockets on the wrong hierarchy node.
uint32_t simulation_class9_render_mesh_mask(const SimulationWeaponState& state,
                                            const SimulationWeaponProfile& weapon) noexcept;

// Mode-zero SHT records are ray/beam models. The binary stores the nearest ray distance in
// ModelRuntimeNode +0xBC and the model renderer adds half that distance to local z==0 vertices
// and the full distance to local z>0 vertices. Negative-z geometry remains at the launch end.
float simulation_mode0_beam_length_world(const SimulationProjectileState& projectile,
                                         const ShotDefinitionRecord& shot) noexcept;
Vec3f simulation_mode0_stretch_local_position(Vec3f local_position,
                                              float beam_length_world) noexcept;

bool simulation_tick_actor_motion(SimulationActorState& actor,
                                  const SimulationMotionProfile& motion) noexcept;
// Runtime class 3 does not consume the generic MDL yaw velocity/acceleration pair. Its body
// heading advances through alternating contact transitions. This compatibility controller keeps
// that separate domain and uses the binary-proven 0x0800/0x2000 heading thresholds.
bool simulation_tick_articulated_actor_motion(
    SimulationActorState& actor, const SimulationMotionProfile& motion,
    int32_t contact_interpolation_rate_q12) noexcept;

// Recovered runtime-class-2 endpoint/chord update. The primary and secondary track channels use
// opposite lateral points on the root SPR and wrap independently in the signed 0x100000 period.
void simulation_update_tank_track_offsets(SimulationActorState& actor,
                                          Vec3f previous_position,
                                          uint32_t previous_yaw_turn16,
                                          Vec3f current_position,
                                          uint32_t current_yaw_turn16,
                                          float local_bounds_y0,
                                          int32_t texture_travel_scale) noexcept;

// Advances the class-3 alternating contact transition from both planar travel and body-yaw work.
// The 0x1000 transition domain mirrors runtimeStateA8/terrainContactMode in the original callback.
void simulation_update_articulated_contact_transition(
    SimulationActorState& actor, Vec3f previous_position, uint32_t previous_yaw_turn16,
    Vec3f current_position, uint32_t current_yaw_turn16,
    int32_t contact_interpolation_rate_q12) noexcept;
// Compatibility overload retained for G391 tests/callers.
void simulation_update_articulated_gait_phase(
    SimulationActorState& actor, Vec3f previous_position, Vec3f current_position,
    int32_t contact_interpolation_rate_q12) noexcept;
void simulation_initialize_class17_deployment(SimulationActorState& actor, bool freshly_placed) noexcept;
void simulation_tick_class17_deployment(SimulationActorState& actor) noexcept;
void simulation_tick_class17_banking(SimulationActorState& actor, bool moved) noexcept;
void simulation_tick_class17_pose(SimulationActorState& actor, bool moved) noexcept;
SimulationWeaponTickResult simulation_tick_weapon(
    SimulationActorState& actor,
    SimulationWeaponState& state,
    const SimulationWeaponProfile& weapon,
    const ShotDefinitionRecord& shot,
    Vec3f aim_origin,
    const Vec3f* target,
    bool line_of_fire_clear = true,
    uint32_t available_launch_attachment_selector_mask = 0xFFFFFFFFU) noexcept;

// Compatibility aggregate used by focused tests. Production editor simulation updates every
// recovered weapon profile independently through simulation_tick_weapon.
SimulationActorTickResult simulation_tick_actor(
    SimulationActorState& actor,
    const SimulationMotionProfile& motion,
    const SimulationWeaponProfile* weapon,
    const Vec3f* target,
    uint32_t tick_count = 1U) noexcept;

std::vector<SimulationProjectileState> simulation_create_volley(
    std::size_t owner_placement_index,
    std::size_t weapon_profile_index,
    const SimulationWeaponProfile& weapon,
    std::span<const Vec3f> launch_positions,
    Vec3f target_position,
    const ShotDefinitionRecord& shot,
    uint32_t spawn_tick,
    uint64_t& next_serial,
    uint32_t fired_attachment_selector = 0xFFFFFFFFU);

// Compatibility overload for the pre-G371 caller shape.
std::vector<SimulationProjectileState> simulation_create_volley(
    std::size_t owner_placement_index,
    const SimulationWeaponProfile& weapon,
    std::span<const Vec3f> launch_positions,
    Vec3f target_position,
    const ShotDefinitionRecord& shot,
    uint32_t spawn_tick,
    uint64_t& next_serial);

Vec3f simulation_projectile_position(const SimulationProjectileState& projectile,
                                     const ShotDefinitionRecord& shot,
                                     uint32_t current_tick) noexcept;
uint32_t simulation_shot_frame_index(const ShotDefinitionRecord& shot,
                                     uint32_t age_ticks) noexcept;
uint32_t simulation_mode3_elevation_offset_turn16(const ShotDefinitionRecord& shot,
                                                  uint32_t age_ticks) noexcept;
uint32_t simulation_mode3_model_pitch_turn16(const SimulationProjectileState& projectile,
                                              const ShotDefinitionRecord& shot,
                                              uint32_t age_ticks) noexcept;
bool simulation_projectile_impacted(const SimulationProjectileState& projectile,
                                    uint32_t current_tick) noexcept;
bool simulation_projectile_expired(const SimulationProjectileState& projectile,
                                   uint32_t current_tick) noexcept;

// Resolves the first terrain or secondary-water surface crossing along the authored
// projectile path. This mirrors the binary's nearest-distance comparison rather than
// classifying only the user-selected endpoint after the shot has already arrived.
void simulation_resolve_projectile_surface_impact(
    SimulationProjectileState& projectile, const ShotDefinitionRecord& shot,
    const FieldGrid& field) noexcept;

// The object/effect image array is sized from the selected Vulkan device. The old fixed 256
// limit was an editor-only truncation and could terminate valid stock effect combinations.
uint32_t simulation_validate_texture_layer_capacity(
    std::size_t requested_layers, uint32_t device_max_layers);

} // namespace thandor
