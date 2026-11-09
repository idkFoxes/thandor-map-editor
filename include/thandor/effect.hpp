#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace thandor {

enum class ShotTrajectoryMode : int32_t {
    DirectLine = 0,
    Ballistic = 1,
    FixedRange = 2,
    LeadAdjusted = 3,
};

const char* shot_trajectory_mode_name(ShotTrajectoryMode mode) noexcept;

struct ShotLineOfFirePolicy {
    bool test_terrain{};
    bool test_models{};
};

// V530 ArmyWeaponRuntime_TestTargetLineOfFireCf surface selection. Ballistic shots test model
// candidates but do not terrain-raycast; fixed-range shots bypass both collision queries; direct
// and lead-adjusted shots use both. Angular/range acceptance remains handled by weapon simulation.
ShotLineOfFirePolicy shot_line_of_fire_policy(ShotTrajectoryMode mode) noexcept;

enum class EffectLifecycleTransitionKind : uint32_t {
    SpawnLinkedEffectAfterCountdown = 0,
    AdvancePeriodicEmissionAndCompletionAction = 1,
    IntegrateLinearMotionAndShadingPosition = 2,
    NoAdditionalAction = 3,
    AdvanceTerrainRelativeMotionAndTerminateOnContact = 4,
};

const char* effect_lifecycle_transition_kind_name(
    EffectLifecycleTransitionKind kind) noexcept;

struct ShotDefinitionRecord {
    int32_t trajectory_mode{};
    uint32_t definition_id{};
    int32_t launch_speed_q12{};
    int32_t ballistic_divisor_q12{};
    // SHT +0xD8 rotates the projectile model about its authored forward axis each tick.
    int32_t model_spin_step_turn16{};
    uint32_t primary_effect_id{};
    uint32_t launch_effect_id{};
    uint32_t secondary_effect_id{};
    // SHT +0x90 is the exact eight-entry target-impact EFF table indexed by the hit
    // model definition's shot-impact class at MDL +0x5C. It is independent from the
    // runtime callback class at MDL +0x4C and distinct from terrain impacts.
    std::array<uint32_t, 8> target_impact_effect_ids{};
    std::array<int32_t, 8> target_impact_damage_q12{};
    uint32_t shading_color_argb{};
    int32_t shading_transition_ticks{};
    // SHT +0x264 is the release/termination transition duration. It is distinct from
    // the ordinary +0x260 shading transition and is used by projectile unlink/impact paths.
    int32_t shading_release_transition_ticks{};
    // SHT +0xD0 is a projectile-relative lifetime in simulation ticks. Multiplying this
    // tick count by launchSpeedQ12 yields the direct-line distance used by retail range math.
    uint32_t projectile_lifetime_ticks{};
    // Projectile texture animation is independent from effect animation. The runtime adds
    // 0x10 per simulation tick to a Q4 accumulator and wraps the SPR subresource base by
    // the serialized frame count at SHT +0x1DC.
    uint32_t animation_threshold_q4{};
    uint32_t animation_frame_count{};
    uint32_t secondary_effect_interval_ticks{};
    // SHT +0x270 is a trajectory-ramp duration in ticks. Modes 2 and 3 use it to
    // progressively install the current movement speed; it is not a Q12 distance scalar.
    uint32_t trajectory_ramp_duration_ticks{};
    // Mode 2 switches from its launch/ramp phase when projectile age reaches +0x274.
    uint32_t fixed_range_transition_age_threshold_ticks{};
    int32_t elevation_offset_angle16{};
    int32_t mode2_selection_range_q12{};
    int32_t terrain_grid_mask_index{};
    uint32_t state_tint_argb{};
    uint32_t positioned_sound_gain_q15{};
    uint32_t positioned_sound_maximum_distance_q12{};
    // SHT+0x290 is the raw guidance turn-limit angle. The legacy API name is
    // retained; nonzero also bypasses unguided LOS pitch/pre-lead checks.
    uint32_t mode3_lead_disabled{};
    std::array<uint32_t, 31> terrain_impact_effect_ids{};
    // SHT +0xE0 holds signed height amplitudes. Native passes the address of
    // the selected entry to the effect, with radius and material in parallel columns.
    std::array<uint32_t, 31> terrain_impact_effect_owner_slots{};
    std::array<int32_t, 31> terrain_impact_radii_q12{};
    // SHT +0x1E0 is a signed 31-entry terrain-material reference array. Negative values are
    // valid sentinels; non-negative values must be below the retail 26-material limit.
    std::array<int32_t, 31> terrain_material_indices{};
    std::string sprite_path;

    ShotTrajectoryMode trajectory_kind() const noexcept {
        return static_cast<ShotTrajectoryMode>(trajectory_mode);
    }
    bool is_repair_or_recovery_shot() const noexcept {
        return target_impact_damage_q12[0] < 0;
    }
    uint32_t positioned_sound_table_index() const noexcept {
        return terrain_grid_mask_index > 0 ? static_cast<uint32_t>(terrain_grid_mask_index) : 0U;
    }
    uint32_t terrain_impact_effect_id(uint32_t material_index) const noexcept {
        return material_index < terrain_impact_effect_ids.size()
            ? terrain_impact_effect_ids[material_index] : 0U;
    }
    uint32_t target_impact_effect_id(uint32_t impact_class_index) const noexcept {
        return impact_class_index < target_impact_effect_ids.size()
            ? target_impact_effect_ids[impact_class_index] : 0U;
    }
};

// Exact trajectory-mode-dependent range contribution used by the game when building
// target-selection information. The result remains in the game's Q12 distance domain.
int32_t shot_selection_range_q12(const ShotDefinitionRecord& definition) noexcept;
float shot_selection_range_world(const ShotDefinitionRecord& definition) noexcept;

// V530 ArmyWeaponRuntime_TestTargetLineOfFireCf uses a distinct direct/lead acceptance range:
// launchSpeedQ12 * (projectileLifetimeTicks + rampBiasTicks - 1). Ballistic and fixed-range
// trajectories do not use this branch, so they return std::nullopt rather than a fabricated limit.
std::optional<int32_t> shot_line_of_fire_range_q12(const ShotDefinitionRecord& definition) noexcept;

struct ShotTerrainMaterialValidation {
    uint32_t sentinel_count{};
    uint32_t valid_reference_count{};
    uint32_t invalid_reference_count{};
};

// V415 validation: negative entries are sentinels; non-negative entries must fit the 26-material
// terrain texture domain. This validates references only and does not remap impact EFF selection.
ShotTerrainMaterialValidation validate_shot_terrain_material_references(
    const ShotDefinitionRecord& definition, uint32_t loaded_material_count = 26U) noexcept;

class ShotAsset {
public:
    static ShotAsset parse(std::span<const std::byte> data);

    std::span<const ShotDefinitionRecord> definitions() const noexcept { return definitions_; }

private:
    std::vector<ShotDefinitionRecord> definitions_;
};

struct EffectDefinitionRecord {
    uint32_t transition_kind{};
    uint32_t definition_id{};
    uint32_t runtime_value_0c{};
    uint32_t linked_effect_present{};
    uint32_t linked_effect_id{};
    uint32_t linked_shot_present{};
    uint32_t linked_shot_id{};
    // Transition-state movement speed loaded from EFF +0x20. State 2 advances the
    // effect model in the randomized azimuth/elevation direction by this Q12 value each tick.
    int32_t movement_speed_q12{};
    uint32_t completion_countdown_ticks{}; // EFF +0x24 owner completion countdown.
    int32_t terrain_grid_mask_index{};
    uint32_t creation_flags{};
    uint32_t shading_color_argb{};
    int32_t shading_transition_ticks{};
    int32_t shading_release_transition_ticks{};
    uint32_t frame_advance_threshold_q4{};
    uint32_t shading_start_frame{};
    uint32_t shading_stop_frame{};
    // EFF +0x48/+0x4C drive the periodic kind-3 attachment emitter in
    // EffectModelRuntimeMaintenance_UpdateLifecycleTintScaleAndTransitions.
    uint32_t periodic_effect_id{};
    uint32_t periodic_effect_interval_ticks{};
    // EffectModelRuntimeMaintenance applies these independent runtime alpha ramps.
    // They are tick counts, not frame indices.
    uint32_t alpha_fade_in_ticks{};
    uint32_t alpha_fade_out_ticks{};
    uint32_t terrain_pitch_drop_per_age_squared{};
    uint32_t terrain_contact_alpha_fade_step{};
    uint32_t state_tint_argb{};
    int32_t model_scale_x_q12{};
    int32_t model_scale_y_q12{};
    uint32_t positioned_sound_argument0{};
    uint32_t positioned_sound_argument1{};
    std::string sprite_path;

    uint32_t positioned_sound_table_index() const noexcept {
        return terrain_grid_mask_index > 0 ? static_cast<uint32_t>(terrain_grid_mask_index) : 0U;
    }
    uint32_t positioned_sound_gain_q15() const noexcept { return positioned_sound_argument0; }
    uint32_t positioned_sound_maximum_distance_q12() const noexcept { return positioned_sound_argument1; }

    EffectLifecycleTransitionKind lifecycle_transition_kind() const noexcept {
        return static_cast<EffectLifecycleTransitionKind>(transition_kind);
    }
};

// Runtime resource glows are GraphicsShadingRuntime records, not sprite geometry. The high byte
// of shadingColorArgb is converted to a Q12 target radius by << 8; the low 24 bits are added to
// nearby vertex lighting through the binary's linear 0..4095 radial lookup table.
struct ResourceShadingGlowProfile {
    uint32_t effect_id{};
    uint32_t packed_color_rgb{};
    float target_radius_world{};
    float grow_transition_ticks{};
    float release_transition_ticks{};
    float shading_start_tick{};
    float shading_stop_tick{};
    float effect_lifetime_ticks{};
};

ResourceShadingGlowProfile make_resource_shading_glow_profile(
    const EffectDefinitionRecord& definition);
float graphics_shading_radius_at_age(const ResourceShadingGlowProfile& profile, float age_ticks) noexcept;
float graphics_shading_radial_factor(float distance_world, float radius_world) noexcept;
// game.exe 0x004CCA90 divides (remaining squared radius >> 5) by
// (current squared radius >> 12), selecting indices 0..128 from the immutable qword table at
// 0x0041DE80. Resource light is added to terrain/model vertex lighting; it is not alpha overlay.
uint32_t graphics_shading_directdraw_lookup_index(float distance_world,
                                                   float radius_world) noexcept;
uint16_t graphics_shading_directdraw_raw_word(uint8_t color_channel,
                                                uint32_t lookup_index) noexcept;
uint8_t graphics_shading_directdraw_channel_addition(uint8_t color_channel,
                                                     uint32_t lookup_index) noexcept;
uint8_t graphics_shading_directdraw_modulated_channel(uint8_t base_light_channel,
                                                      uint32_t accumulated_shading_word) noexcept;
std::array<float, 3> graphics_shading_rgb_contribution(
    const ResourceShadingGlowProfile& profile, float age_ticks, float distance_world) noexcept;
std::array<float, 3> graphics_shading_saturating_add(
    std::array<float, 3> base, std::array<float, 3> addition) noexcept;
std::array<float, 3> graphics_shading_directdraw_vertex_light(
    std::array<float, 3> base, const ResourceShadingGlowProfile& profile,
    float age_ticks, float distance_world) noexcept;
// Compatibility spelling for older callers. G341 changes its behavior from translucent tinting to
// the binary-proven DirectDraw vertex-light addition.
std::array<float, 3> graphics_shading_generated_terrain_overlay(
    std::array<float, 3> base, const ResourceShadingGlowProfile& profile,
    float age_ticks, float distance_world) noexcept;
struct ResourceEmitterTiming {
    uint32_t base_interval_ticks{};
    uint32_t random_interval_ticks{};
    // The live runtime consumes this RNG draw only when kind-6 attachments exist and
    // MDL +0x68 bit 0 does not request the persistent attachment counter.
    bool consumes_attachment_random{true};
    // Transition state 2 consumes one additional draw for randomized effect orientation.
    bool consumes_orientation_random{};
};

struct ResourceEmitterPulse {
    uint32_t emitter_index{};
    uint32_t spawn_tick{};
    // The second shared-primary-RNG value consumed by
    // ArmyRuntime_UpdateTimedShotAndEffectEmitters. The runtime uses it to choose a type-6
    // attachment unless the definition requests its persistent counter instead.
    uint32_t attachment_random{};
    uint32_t orientation_random{};
};

struct ResourceEmitterSchedule {
    uint32_t cycle_ticks{};
    std::vector<ResourceEmitterPulse> pulses;
};

// DirectDraw/software-session preview of ArmyRuntime_UpdateTimedShotAndEffectEmitters.  Every
// initial map resource emits at tick zero.  Each later event consumes the shared primary RNG for
// its 80..89 tick interval and once more for the type-6 attachment selector, even when only one
// attachment exists.  The LEV does not persist the live RNG state, so the editor exposes the seed.
using ResourceEmitterOrientationPredicate =
    std::function<bool(uint32_t emitter_index, uint32_t spawn_tick, uint32_t attachment_selector)>;

ResourceEmitterSchedule build_resource_emitter_schedule(
    std::span<const ResourceEmitterTiming> emitters, uint32_t primary_rng_seed,
    uint32_t preferred_cycle_ticks = 2048U, uint32_t maximum_pulses = 256U,
    const ResourceEmitterOrientationPredicate& consumes_orientation_random_for_pulse = {});

struct DamageEffectTiming {
    uint32_t base_cooldown_ticks{};
    uint32_t random_cooldown_ticks{};
    uint32_t attachment_count{};
};

struct DamageEffectPulse {
    uint32_t spawn_tick{};
    uint32_t attachment_index{};
    // ArmyRuntime_EmitDamageThresholdEffect consumes one 32-bit RNG value and splits it into
    // runtime worldRotationAngle2=high16 and worldRotationAngle0=low16. A second RNG contributes
    // worldRotationAngle1=(low13 + 0x1FFF). Effect transition kind 2 then integrates from the exact
    // worldRotationAngle0/worldRotationAngle1 pair while angle1 eases toward 0x4000.
    uint32_t world_rotation_angle0{};
    uint32_t world_rotation_angle1{};
    uint32_t world_rotation_angle2{};
};

struct DamageEffectSchedule {
    uint32_t cycle_ticks{};
    std::vector<DamageEffectPulse> pulses;
};

// Deterministic editor replay of ArmyRuntime_EmitDamageThresholdEffect once the preview integrity
// is below MDL +0x250. The runtime emits immediately when its cooldown is nonpositive, sets
// cooldown to +0x258 + RNG % +0x25C (drawing RNG only when +0x25C is nonzero), advances
// model lookup key class 3 sequentially, and consumes two further RNG draws for effect orientation. LEV has no live RNG/cooldown state, so the editor
// seeds a repeatable preview schedule without changing PCK data.
DamageEffectSchedule build_damage_effect_schedule(
    DamageEffectTiming timing, uint32_t primary_rng_seed,
    uint32_t preferred_cycle_ticks = 2048U, uint32_t maximum_pulses = 256U);
// Exact class-3 lookup-index form. currentSelectionMetric4 starts at zero; a missing current key
// resets it to -1 and retries key zero, exactly matching ArmyRuntime_EmitDamageThresholdEffect.
// The returned attachment_index is the packed lookup key index, not an ordinal vector slot.
DamageEffectSchedule build_damage_effect_schedule_for_attachment_keys(
    DamageEffectTiming timing, std::span<const uint32_t> attachment_key_indices,
    uint32_t primary_rng_seed, uint32_t preferred_cycle_ticks = 2048U,
    uint32_t maximum_pulses = 256U);

// EffectModelRuntimeMaintenance adds 0x10 to the Q4 frame accumulator once per 20 Hz update and
// advances at most one texture frame per update. These helpers preserve that bounded recurrence.
uint32_t effect_frame_index_at_age(const EffectDefinitionRecord& definition,
                                   uint32_t age_ticks) noexcept;
std::optional<uint32_t> effect_model_completion_tick(const EffectDefinitionRecord& definition) noexcept;

uint32_t effect_lifetime_ticks(const EffectDefinitionRecord& definition) noexcept;
uint32_t effect_tick_for_completed_frames(const EffectDefinitionRecord& definition,
                                          uint32_t completed_frames) noexcept;
// Transition states 0 and 1 decrement linkedEffectPresent only on completed sprite-frame
// advances. The frame that exhausts animationFramesRemaining unlinks before transition dispatch,
// so an authored countdown that lands on or beyond the terminal frame produces no linked effect.
std::optional<uint32_t> effect_linked_effect_spawn_tick(
    const EffectDefinitionRecord& definition) noexcept;
// Exact runtime alpha used by EffectModelRuntimeMaintenance. The returned byte is multiplied
// with the serialized state-tint alpha before the texture alpha participates in blending.
uint8_t effect_alpha_at_age(const EffectDefinitionRecord& definition,
                            uint32_t age_ticks) noexcept;

// Exact packed-byte modulation used by retail effect/shot state-tint maintenance. i386 MMX
// duplicates each byte into a word, shifts Q8.8 by four, PMULHW-multiplies the two Q12 lanes,
// then PACKUSWB-saturates back to ARGB8. This is intentionally not generic float x/255 math.
uint32_t retail_modulate_argb_mmx(uint32_t first_argb, uint32_t second_argb) noexcept;

// UiNode_GetStateTintArgb @ 0x004BD160. Bits 0x04/0x08/0x10 select the exact packed state
// tint before model/effect-specific composition.
uint32_t retail_ui_state_tint_argb(uint32_t runtime_flags) noexcept;

// Non-transition-4 EffectModelRuntimeMaintenance_UpdateLifecycleTintScaleAndTransitions tint
// result. Bit 0x04 is the retail render-state gate: when clear the model becomes transparent
// white; when set, the runtime fade ARGB is MMX-modulated by EFF +0x60. Transition kind 4 has
// a contact-driven alpha state machine and is deliberately excluded from this helper.
std::optional<uint32_t> retail_effect_model_tint_argb(
    const EffectDefinitionRecord& definition, uint32_t age_ticks, uint32_t runtime_flags) noexcept;

// Transition state 2 calls the retail fixed-angle vector builder with runtime
// {worldRotationAngle0, worldRotationAngle1, EFF +0x20} every 20 Hz update, then eases
// worldRotationAngle1 toward 0x4000 with (angle*63 + 0x4000) >> 6. This overload is the exact
// angle-state form used by damage-state effects and other callers that know the runtime angles.
// ArmyRuntime_UpdateTimedShotAndEffectEmitters consumes one RNG dword for transition-kind-2
// timed EFF creation. game.exe uses its low 16 bits for worldRotationAngle0 and angle2, while
// worldRotationAngle1 is 0x4000 minus the high 12 bits.
std::array<uint32_t, 3> timed_effect_transition2_runtime_angles(uint32_t random_word) noexcept;

std::array<float, 3> effect_transition2_displacement_at_age_angles(
    const EffectDefinitionRecord& definition, uint32_t age_ticks,
    uint32_t world_rotation_angle0, uint32_t world_rotation_angle1) noexcept;
// The transform rebuild in transition kind 2 happens before the runtime stores the eased angle1,
// so the rendered pose after N maintenance updates uses N-1 easing steps (zero at ages 0 and 1).
uint32_t effect_transition2_world_rotation_angle1_at_pose_age(
    const EffectDefinitionRecord& definition, uint32_t age_ticks,
    uint32_t initial_world_rotation_angle1) noexcept;

// Deterministic compatibility seed form used by older editor-only preview call sites. New binary-
// grounded call sites should use effect_transition2_displacement_at_age_angles instead.
std::array<float, 3> effect_transition2_displacement_at_age(
    const EffectDefinitionRecord& definition, uint32_t age_ticks,
    uint32_t orientation_random) noexcept;

struct LinkedShotEmission {
    uint32_t parent_age_tick{};
    uint32_t attachment_index{};
};

// Transition state 1 decrements linkedShotPresent only when the Q4 animation accumulator
// advances a sprite frame. After each emission it reloads the frame-countdown, resolves the next
// type-2 attachment, and creates linkedShotId. The terminal frame unlinks before transition
// dispatch, so it cannot emit another linked shot. Invisible controller EFF records must still
// process this sequence even when their SPR contains no triangles.
std::vector<LinkedShotEmission> build_linked_shot_emissions(
    const EffectDefinitionRecord& definition);

// Resolve the absolute parent EffectRuntime spawn tick for a linked-shot emission observed on
// current_tick. The deterministic resource-emitter schedule repeats by cycle_ticks, while the
// EffectRuntime itself is detached and persists from its original pulse pose. A result is returned
// only when current_tick-parent_age_tick lands on this schedule pulse's spawn phase.
std::optional<uint32_t> linked_shot_parent_effect_spawn_tick(
    uint32_t current_tick, uint32_t cycle_ticks, uint32_t pulse_spawn_tick,
    uint32_t parent_age_tick) noexcept;

// Compatibility helpers retained for G339 source users.
uint32_t deterministic_resource_emitter_period(uint32_t base_ticks, uint32_t random_range_ticks,
                                               uint32_t stable_seed) noexcept;
uint32_t deterministic_resource_emitter_phase(uint32_t period_ticks, uint32_t stable_seed) noexcept;

class EffectAsset {
public:
    static EffectAsset parse(std::span<const std::byte> data);

    std::span<const EffectDefinitionRecord> definitions() const noexcept { return definitions_; }

private:
    std::vector<EffectDefinitionRecord> definitions_;
};

} // namespace thandor
