#pragma once

#include <array>
#include <cstdint>
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
    uint32_t state_tint_argb{};
    // SHT +0xD0 is consumed both as the runtime lifetime/range-scale scalar and by
    // ShotDefinition_ComputeSelectionRange for non-ballistic trajectories. Preserve both
    // semantic views without hiding the shared serialized field.
    uint32_t lifetime_ticks{};
    int32_t range_scale_d0_q12{};
    // Projectile texture animation is independent from effect animation. The runtime adds
    // 0x10 per simulation tick to a Q4 accumulator and wraps the SPR subresource base by
    // the serialized frame count at SHT +0x1DC.
    uint32_t animation_threshold_q4{};
    uint32_t animation_frame_count{};
    uint32_t secondary_effect_interval_ticks{};
    int32_t range_scale_270_q12{};
    int32_t elevation_offset_angle16{};
    int32_t mode2_selection_range_q12{};
    uint32_t mode3_lead_disabled{};
    std::array<uint32_t, 31> terrain_impact_effect_ids{};
    // SHT +0x1E0 is a parallel 31-entry material-index table retained exactly.
    // Most stock records use -1; it must not be confused with the effect pointer array.
    std::array<int32_t, 31> terrain_material_indices{};
    std::string sprite_path;

    ShotTrajectoryMode trajectory_kind() const noexcept {
        return static_cast<ShotTrajectoryMode>(trajectory_mode);
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
    uint32_t state_tint_argb{};
    int32_t model_scale_x_q12{};
    int32_t model_scale_y_q12{};
    uint32_t positioned_sound_argument0{};
    uint32_t positioned_sound_argument1{};
    std::string sprite_path;

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
ResourceEmitterSchedule build_resource_emitter_schedule(
    std::span<const ResourceEmitterTiming> emitters, uint32_t primary_rng_seed,
    uint32_t preferred_cycle_ticks = 2048U, uint32_t maximum_pulses = 256U);

// EffectModelRuntimeMaintenance adds 0x10 to the Q4 frame accumulator once per 20 Hz update and
// advances at most one texture frame per update. These helpers preserve that bounded recurrence.
uint32_t effect_frame_index_at_age(const EffectDefinitionRecord& definition,
                                   uint32_t age_ticks) noexcept;
uint32_t effect_lifetime_ticks(const EffectDefinitionRecord& definition) noexcept;
uint32_t effect_tick_for_completed_frames(const EffectDefinitionRecord& definition,
                                          uint32_t completed_frames) noexcept;
// Exact runtime alpha used by EffectModelRuntimeMaintenance. The returned byte is multiplied
// with the serialized state-tint alpha before the texture alpha participates in blending.
uint8_t effect_alpha_at_age(const EffectDefinitionRecord& definition,
                            uint32_t age_ticks) noexcept;

// Transition state 2 uses a randomized azimuth/elevation and moves the model every 20 Hz update.
// The elevation is smoothed toward vertical after every movement step. The editor returns the
// cumulative world-space displacement without changing the source EFF or LEV data.
std::array<float, 3> effect_transition2_displacement_at_age(
    const EffectDefinitionRecord& definition, uint32_t age_ticks,
    uint32_t orientation_random) noexcept;

struct LinkedShotEmission {
    uint32_t parent_age_tick{};
    uint32_t attachment_index{};
};

// Transition state 1 decrements linkedShotPresent once per 20 Hz maintenance tick, reloads the
// counter after each emission, resolves the next type-2 attachment, and creates linkedShotId.
// Sprite-frame timing is independent. Invisible controller EFF records must therefore continue
// processing this sequence even when their SPR contains no triangles.
std::vector<LinkedShotEmission> build_linked_shot_emissions(
    const EffectDefinitionRecord& definition);

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
