#include "thandor/effect.hpp"

#include "thandor/binary.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace thandor {
namespace {
constexpr std::size_t kHeaderSize = 0x200;
constexpr std::size_t kDefinitionSize = 0xC0;
constexpr std::size_t kPathOffset = 0x7C;
constexpr std::size_t kPathBytes = 68;
constexpr std::size_t kShotDefinitionSize = 0x2E0;
constexpr std::size_t kShotPathOffset = 0x29C;

std::string read_resource_path(std::span<const std::byte> data, std::size_t offset) {
    std::string raw;
    raw.reserve(kPathBytes / 2U);
    for (std::size_t cursor = 0; cursor + 2U <= kPathBytes; cursor += 2U) {
        const uint16_t value = read_u16(data, offset + cursor);
        if (value == 0U) break;
        if (value < 0x20U || value >= 0x7FU) throw FormatError("EFF sprite path is not narrow ASCII UTF-16");
        raw.push_back(static_cast<char>(value));
    }
    auto path = normalize_asset_path(raw);
    if (!path.empty() && !path.ends_with(".spr")) path += ".spr";
    return path;
}
} // namespace

const char* shot_trajectory_mode_name(ShotTrajectoryMode mode) noexcept {
    switch (mode) {
        case ShotTrajectoryMode::DirectLine: return "direct line";
        case ShotTrajectoryMode::Ballistic: return "ballistic";
        case ShotTrajectoryMode::FixedRange: return "fixed range";
        case ShotTrajectoryMode::LeadAdjusted: return "lead adjusted";
    }
    return "unknown";
}

ShotLineOfFirePolicy shot_line_of_fire_policy(ShotTrajectoryMode mode) noexcept {
    switch (mode) {
        case ShotTrajectoryMode::Ballistic: return {false, true};
        case ShotTrajectoryMode::FixedRange: return {false, false};
        case ShotTrajectoryMode::DirectLine:
        case ShotTrajectoryMode::LeadAdjusted: return {true, true};
    }
    return {true, true};
}

const char* effect_lifecycle_transition_kind_name(
    EffectLifecycleTransitionKind kind) noexcept {
    switch (kind) {
        case EffectLifecycleTransitionKind::SpawnLinkedEffectAfterCountdown:
            return "spawn linked effect after countdown";
        case EffectLifecycleTransitionKind::AdvancePeriodicEmissionAndCompletionAction:
            return "periodic emission/completion";
        case EffectLifecycleTransitionKind::IntegrateLinearMotionAndShadingPosition:
            return "linear motion/shading position";
        case EffectLifecycleTransitionKind::NoAdditionalAction:
            return "no additional action";
        case EffectLifecycleTransitionKind::AdvanceTerrainRelativeMotionAndTerminateOnContact:
            return "terrain-relative motion/contact termination";
    }
    return "unknown";
}

ShotAsset ShotAsset::parse(std::span<const std::byte> data) {
    if (data.size() < kHeaderSize || read_u32(data, 0) != fourcc("sht")) {
        throw FormatError("SHT magic mismatch");
    }
    const uint32_t declared_size = read_u32(data, 4);
    if (declared_size != data.size()) throw FormatError("SHT declared size does not match payload");
    if (read_u32(data, 0x0C) != 0x00060006U) throw FormatError("unsupported SHT converter version");
    const uint32_t count = read_u32(data, 0xB0);
    const uint64_t expected = kHeaderSize + static_cast<uint64_t>(count) * kShotDefinitionSize;
    if (expected != data.size()) throw FormatError("SHT definition count does not match payload size");

    ShotAsset result;
    result.definitions_.reserve(count);
    for (uint32_t index = 0; index < count; ++index) {
        const std::size_t offset = kHeaderSize + static_cast<std::size_t>(index) * kShotDefinitionSize;
        ShotDefinitionRecord record;
        record.trajectory_mode = read_i32(data, offset + 0x00);
        if (record.trajectory_mode < static_cast<int32_t>(ShotTrajectoryMode::DirectLine) ||
            record.trajectory_mode > static_cast<int32_t>(ShotTrajectoryMode::LeadAdjusted))
            throw FormatError("SHT trajectory mode exceeds recovered four-entry enum");
        record.definition_id = read_u32(data, offset + 0x08);
        record.launch_speed_q12 = read_i32(data, offset + 0x0C);
        record.model_spin_step_turn16 = read_i32(data, offset + 0xD8);
        record.ballistic_divisor_q12 = read_i32(data, offset + 0xDC);
        record.primary_effect_id = read_u32(data, offset + 0x10);
        record.launch_effect_id = read_u32(data, offset + 0xD4);
        record.shading_color_argb = read_u32(data, offset + 0x25C);
        record.shading_transition_ticks = read_i32(data, offset + 0x260);
        record.shading_release_transition_ticks = read_i32(data, offset + 0x264);
        record.secondary_effect_id = read_u32(data, offset + 0x268);
        for (uint32_t class_index = 0; class_index < record.target_impact_effect_ids.size();
             ++class_index) {
            record.target_impact_effect_ids[class_index] =
                read_u32(data, offset + 0x90 + class_index * 4U);
            record.target_impact_damage_q12[class_index] =
                read_i32(data, offset + 0xB0 + class_index * 4U);
        }
        record.projectile_lifetime_ticks = read_u32(data, offset + 0xD0);
        record.animation_threshold_q4 = read_u32(data, offset + 0x15C);
        record.animation_frame_count = read_u32(data, offset + 0x1DC);
        record.secondary_effect_interval_ticks = read_u32(data, offset + 0x26C);
        record.trajectory_ramp_duration_ticks = read_u32(data, offset + 0x270);
        record.fixed_range_transition_age_threshold_ticks = read_u32(data, offset + 0x274);
        record.elevation_offset_angle16 = read_i32(data, offset + 0x278);
        record.mode2_selection_range_q12 = read_i32(data, offset + 0x27C);
        record.terrain_grid_mask_index = read_i32(data, offset + 0x280);
        record.state_tint_argb = read_u32(data, offset + 0x284);
        record.positioned_sound_gain_q15 = read_u32(data, offset + 0x288);
        record.positioned_sound_maximum_distance_q12 = read_u32(data, offset + 0x28C);
        record.mode3_lead_disabled = read_u32(data, offset + 0x290);
        for (uint32_t effect_index = 0; effect_index < record.terrain_impact_effect_ids.size();
             ++effect_index) {
            record.terrain_impact_effect_ids[effect_index] =
                read_u32(data, offset + 0x14 + effect_index * 4U);
            record.terrain_impact_effect_owner_slots[effect_index] =
                read_u32(data, offset + 0xE0 + effect_index * 4U);
            record.terrain_impact_radii_q12[effect_index] =
                read_i32(data, offset + 0x160 + effect_index * 4U);
            record.terrain_material_indices[effect_index] =
                read_i32(data, offset + 0x1E0 + effect_index * 4U);
        }
        record.sprite_path = read_resource_path(data, offset + kShotPathOffset);
        if (record.sprite_path.empty()) throw FormatError("SHT definition has no sprite path");
        result.definitions_.push_back(std::move(record));
    }
    return result;
}


int32_t shot_selection_range_q12(const ShotDefinitionRecord& definition) noexcept {
    // game.exe ShotDefinition_ComputeSelectionRange. Keep the signed intermediate arithmetic
    // and clamp only the final editor-facing result to the representable positive Q12 domain.
    std::int64_t value = 0;
    if (definition.trajectory_mode == 1) {
        if (definition.ballistic_divisor_q12 == 0) return 0;
        const std::int64_t speed = definition.launch_speed_q12;
        value = ((speed * speed) / definition.ballistic_divisor_q12 * 9) >> 3;
    } else if (definition.trajectory_mode == 2) {
        value = definition.mode2_selection_range_q12;
    } else {
        const std::int64_t ramp_distance_bias =
            (static_cast<std::int64_t>(definition.trajectory_ramp_duration_ticks) * -0xAAA) >> 12;
        value = (ramp_distance_bias + static_cast<std::int64_t>(definition.projectile_lifetime_ticks)) *
                static_cast<std::int64_t>(definition.launch_speed_q12);
    }
    if (value <= 0) return 0;
    return static_cast<int32_t>(std::min<std::int64_t>(value,
        std::numeric_limits<int32_t>::max()));
}

float shot_selection_range_world(const ShotDefinitionRecord& definition) noexcept {
    return q12_to_float(shot_selection_range_q12(definition));
}

std::optional<int32_t> shot_line_of_fire_range_q12(
    const ShotDefinitionRecord& definition) noexcept {
    const auto mode = definition.trajectory_kind();
    if (mode != ShotTrajectoryMode::DirectLine && mode != ShotTrajectoryMode::LeadAdjusted)
        return std::nullopt;

    // Exact V530 integer expression from ArmyWeaponRuntime_TestTargetLineOfFireCf. This differs
    // from ShotDefinition_ComputeSelectionRange by the final -1 tick. Keep signed intermediates
    // because the executable does; invalid/non-positive ranges remain unusable in the editor.
    const std::int64_t ramp_bias_ticks =
        (static_cast<std::int64_t>(definition.trajectory_ramp_duration_ticks) * -0xAAA) >> 12;
    const std::int64_t effective_ticks =
        ramp_bias_ticks + static_cast<std::int64_t>(definition.projectile_lifetime_ticks) - 1;
    const std::int64_t value = effective_ticks * static_cast<std::int64_t>(definition.launch_speed_q12);
    if (value <= 0) return 0;
    return static_cast<int32_t>(std::min<std::int64_t>(
        value, std::numeric_limits<int32_t>::max()));
}

ShotTerrainMaterialValidation validate_shot_terrain_material_references(
    const ShotDefinitionRecord& definition, uint32_t loaded_material_count) noexcept {
    ShotTerrainMaterialValidation result;
    const uint32_t available_materials = std::min(loaded_material_count, 26U);
    for (const int32_t material_index : definition.terrain_material_indices) {
        if (material_index < 0) {
            ++result.sentinel_count;
            continue;
        }
        if (static_cast<uint32_t>(material_index) < available_materials) {
            ++result.valid_reference_count;
        } else {
            ++result.invalid_reference_count;
        }
    }
    return result;
}

EffectAsset EffectAsset::parse(std::span<const std::byte> data) {
    if (data.size() < kHeaderSize || read_u32(data, 0) != fourcc("eff")) {
        throw FormatError("EFF magic mismatch");
    }
    const uint32_t declared_size = read_u32(data, 4);
    if (declared_size != data.size()) throw FormatError("EFF declared size does not match payload");
    if (read_u32(data, 0x0C) != 0x00040007U) throw FormatError("unsupported EFF converter version");
    const uint32_t count = read_u32(data, 0xB0);
    const uint64_t expected = kHeaderSize + static_cast<uint64_t>(count) * kDefinitionSize;
    if (expected != data.size()) throw FormatError("EFF definition count does not match payload size");

    EffectAsset result;
    result.definitions_.reserve(count);
    for (uint32_t index = 0; index < count; ++index) {
        const std::size_t offset = kHeaderSize + static_cast<std::size_t>(index) * kDefinitionSize;
        EffectDefinitionRecord record;
        record.transition_kind = read_u32(data, offset + 0x00);
        if (record.transition_kind > 4U) throw FormatError("EFF transition kind exceeds recovered dispatch table");
        record.definition_id = read_u32(data, offset + 0x08);
        record.runtime_value_0c = read_u32(data, offset + 0x0C);
        record.linked_effect_present = read_u32(data, offset + 0x10);
        record.linked_effect_id = read_u32(data, offset + 0x14);
        record.linked_shot_present = read_u32(data, offset + 0x18);
        record.linked_shot_id = read_u32(data, offset + 0x1C);
        record.movement_speed_q12 = read_i32(data, offset + 0x20);
        record.completion_countdown_ticks = read_u32(data, offset + 0x24);
        record.terrain_grid_mask_index = read_i32(data, offset + 0x2C);
        record.creation_flags = read_u32(data, offset + 0x30);
        record.shading_color_argb = read_u32(data, offset + 0x34);
        record.shading_transition_ticks = read_i32(data, offset + 0x38);
        record.shading_release_transition_ticks = read_i32(data, offset + 0x3C);
        record.frame_advance_threshold_q4 = read_u32(data, offset + 0x28);
        record.shading_start_frame = read_u32(data, offset + 0x40);
        record.shading_stop_frame = read_u32(data, offset + 0x44);
        record.periodic_effect_id = read_u32(data, offset + 0x48);
        record.periodic_effect_interval_ticks = read_u32(data, offset + 0x4C);
        record.alpha_fade_in_ticks = read_u32(data, offset + 0x50);
        record.alpha_fade_out_ticks = read_u32(data, offset + 0x54);
        record.terrain_pitch_drop_per_age_squared = read_u32(data, offset + 0x58);
        record.terrain_contact_alpha_fade_step = read_u32(data, offset + 0x5C);
        record.state_tint_argb = read_u32(data, offset + 0x60);
        record.model_scale_x_q12 = read_i32(data, offset + 0x64);
        record.model_scale_y_q12 = read_i32(data, offset + 0x68);
        record.positioned_sound_argument0 = read_u32(data, offset + 0x6C);
        record.positioned_sound_argument1 = read_u32(data, offset + 0x70);
        record.sprite_path = read_resource_path(data, offset + kPathOffset);
        if (record.sprite_path.empty()) throw FormatError("EFF definition has no sprite path");
        result.definitions_.push_back(std::move(record));
    }
    return result;
}

ResourceShadingGlowProfile make_resource_shading_glow_profile(
    const EffectDefinitionRecord& definition) {
    if ((definition.shading_color_argb & 0x00FFFFFFU) == 0U) {
        throw FormatError("resource shading effect has no RGB contribution");
    }
    if (definition.shading_transition_ticks <= 0 ||
        definition.shading_release_transition_ticks <= 0) {
        throw FormatError("resource shading effect has invalid transition duration");
    }
    const float frame_ticks = definition.frame_advance_threshold_q4 == 0U
        ? 1.0F
        : static_cast<float>(definition.frame_advance_threshold_q4) / 16.0F;
    const uint32_t radius_q12 = ((definition.shading_color_argb >> 24U) & 0xFFU) << 8U;
    return {
        definition.definition_id,
        definition.shading_color_argb & 0x00FFFFFFU,
        q12_to_float(static_cast<int32_t>(radius_q12)),
        static_cast<float>(definition.shading_transition_ticks),
        static_cast<float>(definition.shading_release_transition_ticks),
        static_cast<float>(definition.shading_start_frame) * frame_ticks,
        static_cast<float>(definition.shading_stop_frame) * frame_ticks,
        static_cast<float>(definition.runtime_value_0c) * frame_ticks,
    };
}

float graphics_shading_radius_at_age(const ResourceShadingGlowProfile& profile,
                                     float age_ticks) noexcept {
    if (age_ticks < profile.shading_start_tick || profile.target_radius_world <= 0.0F) return 0.0F;
    const float local_age = age_ticks - profile.shading_start_tick;
    if (local_age < profile.grow_transition_ticks) {
        return profile.target_radius_world *
            std::clamp(local_age / profile.grow_transition_ticks, 0.0F, 1.0F);
    }
    if (age_ticks < profile.shading_stop_tick) return profile.target_radius_world;
    const float release_age = age_ticks - profile.shading_stop_tick;
    if (release_age >= profile.release_transition_ticks) return 0.0F;
    return profile.target_radius_world *
        std::clamp(1.0F - release_age / profile.release_transition_ticks, 0.0F, 1.0F);
}

float graphics_shading_radial_factor(float distance_world, float radius_world) noexcept {
    if (radius_world <= 0.0F || distance_world >= radius_world) return 0.0F;
    const float normalized = distance_world / radius_world;
    return std::clamp(1.0F - normalized * normalized, 0.0F, 1.0F);
}

uint32_t graphics_shading_directdraw_lookup_index(float distance_world,
                                                   float radius_world) noexcept {
    if (!std::isfinite(distance_world) || !std::isfinite(radius_world) ||
        radius_world <= 0.0F || distance_world >= radius_world) {
        return 0U;
    }
    const double radius_squared = static_cast<double>(radius_world) * radius_world;
    const double remaining_squared = radius_squared -
        static_cast<double>(distance_world) * distance_world;
    if (remaining_squared <= 0.0) return 0U;
    // 0x004CCA90: (remainingQ24 >> 5) / (radiusSquaredQ24 >> 12). The algebraic
    // quotient is floor(128 * remaining / radiusSquared), with center index 128.
    return static_cast<uint32_t>(std::clamp(
        std::floor(remaining_squared * 128.0 / radius_squared), 0.0, 128.0));
}

uint16_t graphics_shading_directdraw_raw_word(uint8_t color_channel,
                                                uint32_t lookup_index) noexcept {
    lookup_index = std::min(lookup_index, 128U);
    // 0x004CCB13-0x004CCB26: unpack byte, shift right by two, multiply the
    // selected 0x0041DE80 lookup word, and saturating-add the high word.
    const uint32_t source_word = (static_cast<uint32_t>(color_channel) * 257U) >> 2U;
    const uint32_t lookup_word = (lookup_index * 257U) >> 4U;
    return static_cast<uint16_t>((source_word * lookup_word) >> 16U);
}

uint8_t graphics_shading_directdraw_modulated_channel(
    uint8_t base_light_channel, uint32_t accumulated_shading_word) noexcept {
    // Terrain packet preparation starts from a white 0xFFFF lane shifted to Q10,
    // adds all resource words with saturation, then pmulhw-modulates that lane by
    // the ordinary packed terrain light (0x0050054C-0x00500588 and parallels).
    constexpr uint32_t white_source_word = (255U * 257U) >> 6U;
    const uint32_t lit_word = std::min(65535U, white_source_word + accumulated_shading_word);
    const uint32_t modulation_word =
        (static_cast<uint32_t>(base_light_channel) * 257U) >> 2U;
    return static_cast<uint8_t>(std::min(255U, (lit_word * modulation_word) >> 16U));
}

uint8_t graphics_shading_directdraw_channel_addition(uint8_t color_channel,
                                                     uint32_t lookup_index) noexcept {
    const uint32_t raw_word = graphics_shading_directdraw_raw_word(color_channel, lookup_index);
    constexpr uint32_t white_modulation_word = (255U * 257U) >> 2U;
    return static_cast<uint8_t>(std::min(255U,
        (raw_word * white_modulation_word) >> 16U));
}

std::array<float, 3> graphics_shading_rgb_contribution(
    const ResourceShadingGlowProfile& profile, float age_ticks, float distance_world) noexcept {
    const float radius = graphics_shading_radius_at_age(profile, std::floor(age_ticks));
    const uint32_t index = graphics_shading_directdraw_lookup_index(distance_world, radius);
    constexpr float inverse_byte = 1.0F / 255.0F;
    return {
        static_cast<float>(graphics_shading_directdraw_channel_addition(
            static_cast<uint8_t>((profile.packed_color_rgb >> 16U) & 0xFFU), index)) * inverse_byte,
        static_cast<float>(graphics_shading_directdraw_channel_addition(
            static_cast<uint8_t>((profile.packed_color_rgb >> 8U) & 0xFFU), index)) * inverse_byte,
        static_cast<float>(graphics_shading_directdraw_channel_addition(
            static_cast<uint8_t>(profile.packed_color_rgb & 0xFFU), index)) * inverse_byte,
    };
}

std::array<float, 3> graphics_shading_saturating_add(
    std::array<float, 3> base, std::array<float, 3> addition) noexcept {
    for (std::size_t component = 0; component < base.size(); ++component) {
        base[component] = std::clamp(base[component] + addition[component], 0.0F, 1.0F);
    }
    return base;
}

std::array<float, 3> graphics_shading_directdraw_vertex_light(
    std::array<float, 3> base, const ResourceShadingGlowProfile& profile,
    float age_ticks, float distance_world) noexcept {
    const float radius = graphics_shading_radius_at_age(profile, std::floor(age_ticks));
    const uint32_t index = graphics_shading_directdraw_lookup_index(distance_world, radius);
    const std::array<uint8_t, 3> source{
        static_cast<uint8_t>((profile.packed_color_rgb >> 16U) & 0xFFU),
        static_cast<uint8_t>((profile.packed_color_rgb >> 8U) & 0xFFU),
        static_cast<uint8_t>(profile.packed_color_rgb & 0xFFU),
    };
    std::array<float, 3> result{};
    for (std::size_t component = 0; component < result.size(); ++component) {
        const auto base_byte = static_cast<uint8_t>(std::clamp(
            std::lround(base[component] * 255.0F), 0L, 255L));
        result[component] = static_cast<float>(graphics_shading_directdraw_modulated_channel(
            base_byte, graphics_shading_directdraw_raw_word(source[component], index))) / 255.0F;
    }
    return result;
}

std::array<float, 3> graphics_shading_generated_terrain_overlay(
    std::array<float, 3> base, const ResourceShadingGlowProfile& profile,
    float age_ticks, float distance_world) noexcept {
    return graphics_shading_directdraw_vertex_light(base, profile, age_ticks, distance_world);
}

namespace {
uint32_t resource_random_next_primary(uint32_t& state) noexcept {
    const uint32_t first = state * 33U + 101U;
    state = first * 33U + 101U;
    return (first << 14U) ^ (state >> 2U);
}

uint32_t mix_resource_seed(uint32_t value) noexcept {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    return value ^ (value >> 16U);
}
}

ResourceEmitterSchedule build_resource_emitter_schedule(
    std::span<const ResourceEmitterTiming> emitters, uint32_t primary_rng_seed,
    uint32_t preferred_cycle_ticks, uint32_t maximum_pulses,
    const ResourceEmitterOrientationPredicate& consumes_orientation_random_for_pulse) {
    ResourceEmitterSchedule result;
    if (emitters.empty() || maximum_pulses == 0U) return result;
    if (preferred_cycle_ticks < 2U) preferred_cycle_ticks = 2U;

    std::vector<uint32_t> next_ticks(emitters.size(), 0U);
    std::vector<uint32_t> occurrence_counts(emitters.size(), 0U);
    uint32_t state = primary_rng_seed;
    uint32_t latest_spawn = 0U;
    while (result.pulses.size() < maximum_pulses) {
        uint32_t next_tick = preferred_cycle_ticks;
        for (const uint32_t value : next_ticks) next_tick = std::min(next_tick, value);
        if (next_tick >= preferred_cycle_ticks) break;

        bool emitted = false;
        for (std::size_t index = 0; index < emitters.size(); ++index) {
            if (next_ticks[index] != next_tick) continue;
            if (result.pulses.size() >= maximum_pulses) break;
            const auto& timing = emitters[index];
            const uint32_t interval_random = resource_random_next_primary(state);
            const uint32_t interval = std::max(1U, timing.base_interval_ticks +
                (timing.random_interval_ticks == 0U ? 0U
                 : interval_random % timing.random_interval_ticks));
            // ArmyRuntime_UpdateTimedShotAndEffectEmitters selects its kind-6 attachment from
            // either the shared primary RNG or the persistent per-runtime counter selected by
            // MDL +0x68 bit 0. Preserve the selected value and consume no fabricated RNG draw.
            const uint32_t attachment_random = timing.consumes_attachment_random
                ? resource_random_next_primary(state) : occurrence_counts[index];
            ++occurrence_counts[index];
            const bool consumes_orientation = consumes_orientation_random_for_pulse
                ? consumes_orientation_random_for_pulse(static_cast<uint32_t>(index), next_tick,
                      attachment_random)
                : timing.consumes_orientation_random;
            const uint32_t orientation_random = consumes_orientation
                ? resource_random_next_primary(state) : 0U;
            result.pulses.push_back({static_cast<uint32_t>(index), next_tick,
                                     attachment_random, orientation_random});
            latest_spawn = next_tick;
            emitted = true;
            next_ticks[index] = next_tick + interval;
        }
        if (!emitted) break;
    }

    // If the 256-record DirectDraw-era table bounded the preview before the requested horizon, end
    // the repeating window immediately after the last complete event rather than dropping records
    // in the middle of a nominal 2048-tick cycle.
    result.cycle_ticks = result.pulses.size() == maximum_pulses
        ? std::max(2U, latest_spawn + 1U) : preferred_cycle_ticks;
    return result;
}

DamageEffectSchedule build_damage_effect_schedule_for_attachment_keys(
    DamageEffectTiming timing, std::span<const uint32_t> attachment_key_indices,
    uint32_t primary_rng_seed, uint32_t preferred_cycle_ticks, uint32_t maximum_pulses) {
    DamageEffectSchedule result;
    if (maximum_pulses == 0U) return result;
    preferred_cycle_ticks = std::max(2U, preferred_cycle_ticks);
    uint32_t state = primary_rng_seed;
    uint32_t spawn_tick = 0U;
    int64_t selection_metric4 = 0;
    const auto has_key = [&](uint32_t key) {
        return std::find(attachment_key_indices.begin(), attachment_key_indices.end(), key) !=
            attachment_key_indices.end();
    };
    while (spawn_tick < preferred_cycle_ticks && result.pulses.size() < maximum_pulses) {
        uint32_t random_cooldown = 0U;
        if (timing.random_cooldown_ticks != 0U)
            random_cooldown = resource_random_next_primary(state) % timing.random_cooldown_ticks;
        const uint32_t interval = std::max(1U, timing.base_cooldown_ticks + random_cooldown);

        uint32_t emitted_attachment_key = 0U;
        if (!attachment_key_indices.empty()) {
            if (selection_metric4 >= 0 && has_key(static_cast<uint32_t>(selection_metric4))) {
                emitted_attachment_key = static_cast<uint32_t>(selection_metric4);
                ++selection_metric4;
            } else {
                // The retail function stores -1 before retrying lookup key zero, then increments
                // that stored value after a successful transform. Hence a wrap emission from key
                // zero leaves the next requested key at zero rather than one.
                selection_metric4 = -1;
                if (has_key(0U)) {
                    emitted_attachment_key = 0U;
                    ++selection_metric4;
                }
            }
        }

        const uint32_t orientation_word = resource_random_next_primary(state);
        const uint32_t orientation_elevation = resource_random_next_primary(state);
        result.pulses.push_back({spawn_tick, emitted_attachment_key,
            orientation_word & 0xFFFFU,
            (orientation_elevation & 0x1FFFU) + 0x1FFFU,
            (orientation_word >> 16U) & 0xFFFFU});
        if (std::numeric_limits<uint32_t>::max() - spawn_tick < interval) break;
        spawn_tick += interval;
    }
    result.cycle_ticks = result.pulses.size() == maximum_pulses && !result.pulses.empty()
        ? std::max(2U, result.pulses.back().spawn_tick + 1U) : preferred_cycle_ticks;
    return result;
}

DamageEffectSchedule build_damage_effect_schedule(
    DamageEffectTiming timing, uint32_t primary_rng_seed,
    uint32_t preferred_cycle_ticks, uint32_t maximum_pulses) {
    std::vector<uint32_t> contiguous_keys;
    contiguous_keys.reserve(timing.attachment_count);
    for (uint32_t key = 0U; key < timing.attachment_count; ++key) contiguous_keys.push_back(key);
    return build_damage_effect_schedule_for_attachment_keys(timing, contiguous_keys,
        primary_rng_seed, preferred_cycle_ticks, maximum_pulses);
}

uint32_t effect_frame_index_at_age(const EffectDefinitionRecord& definition,
                                   uint32_t age_ticks) noexcept {
    if (definition.runtime_value_0c == 0U) return 0U;
    const uint32_t threshold = std::max(1U, definition.frame_advance_threshold_q4);
    const uint64_t unconstrained = (static_cast<uint64_t>(age_ticks) * 16U) / threshold;
    const uint32_t bounded_rate = static_cast<uint32_t>(std::min<uint64_t>(age_ticks, unconstrained));
    return std::min(definition.runtime_value_0c - 1U, bounded_rate);
}

uint32_t effect_tick_for_completed_frames(const EffectDefinitionRecord& definition,
                                          uint32_t completed_frames) noexcept {
    if (completed_frames == 0U) return 0U;
    const uint32_t threshold = std::max(1U, definition.frame_advance_threshold_q4);
    // The maintenance loop advances no more than one frame per update. For thresholds at or above
    // Q4 one-frame (0x10), ceiling division reproduces its accumulator. Smaller thresholds still
    // need one complete presentation update per frame.
    const uint64_t scaled = static_cast<uint64_t>(completed_frames) * threshold;
    const uint64_t accumulator_ticks = (scaled + 15U) / 16U;
    return static_cast<uint32_t>(std::max<uint64_t>(completed_frames, accumulator_ticks));
}

std::optional<uint32_t> effect_model_completion_tick(const EffectDefinitionRecord& definition) noexcept {
    // Expiry unlinks the EFF before its callback; zero wraps on DEC. Kind 4 also
    // requires motion/contact state and must not use this frame-only timer.
    if (definition.transition_kind != 1U || definition.completion_countdown_ticks == 0U ||
        definition.completion_countdown_ticks >= definition.runtime_value_0c) return std::nullopt;
    return effect_tick_for_completed_frames(definition, definition.completion_countdown_ticks);
}

uint32_t effect_lifetime_ticks(const EffectDefinitionRecord& definition) noexcept {
    return effect_tick_for_completed_frames(definition, definition.runtime_value_0c);
}

std::optional<uint32_t> effect_linked_effect_spawn_tick(
    const EffectDefinitionRecord& definition) noexcept {
    if ((definition.transition_kind != 0U && definition.transition_kind != 1U) ||
        definition.linked_effect_present == 0U || definition.linked_effect_id == 0U ||
        definition.linked_effect_present >= definition.runtime_value_0c)
        return std::nullopt;
    return effect_tick_for_completed_frames(definition, definition.linked_effect_present);
}

uint8_t effect_alpha_at_age(const EffectDefinitionRecord& definition,
                            uint32_t age_ticks) noexcept {
    const uint32_t lifetime = effect_lifetime_ticks(definition);
    if (lifetime == 0U || age_ticks >= lifetime) return 0U;
    if (definition.alpha_fade_in_ticks != 0U && age_ticks < definition.alpha_fade_in_ticks) {
        return static_cast<uint8_t>(std::min<uint64_t>(255U,
            static_cast<uint64_t>(age_ticks) * 255U / definition.alpha_fade_in_ticks));
    }
    if (definition.alpha_fade_out_ticks != 0U) {
        const uint32_t fade_start = lifetime > definition.alpha_fade_out_ticks
            ? lifetime - definition.alpha_fade_out_ticks : 0U;
        if (age_ticks > fade_start) {
            const uint32_t remaining = lifetime - age_ticks;
            return static_cast<uint8_t>(std::min<uint64_t>(255U,
                static_cast<uint64_t>(remaining) * 255U / definition.alpha_fade_out_ticks));
        }
    }
    return 255U;
}

namespace {
uint8_t retail_mmx_modulate_byte(uint8_t first, uint8_t second) noexcept {
    const uint32_t first_q12 = (static_cast<uint32_t>(first) * 0x101U) >> 4U;
    const uint32_t second_q12 = (static_cast<uint32_t>(second) * 0x101U) >> 4U;
    return static_cast<uint8_t>(std::min(255U, (first_q12 * second_q12) >> 16U));
}
}

uint32_t retail_modulate_argb_mmx(uint32_t first_argb, uint32_t second_argb) noexcept {
    uint32_t result = 0U;
    for (unsigned shift : {0U, 8U, 16U, 24U}) {
        const auto first = static_cast<uint8_t>((first_argb >> shift) & 0xFFU);
        const auto second = static_cast<uint8_t>((second_argb >> shift) & 0xFFU);
        result |= static_cast<uint32_t>(retail_mmx_modulate_byte(first, second)) << shift;
    }
    return result;
}

uint32_t retail_ui_state_tint_argb(uint32_t runtime_flags) noexcept {
    if ((runtime_flags & 0x04U) != 0U) return 0xFFFFFFFFU;
    if ((runtime_flags & 0x08U) == 0U) return 0x00000000U;
    return (runtime_flags & 0x10U) == 0U ? 0xFF878787U : 0x00FFFFFFU;
}

std::optional<uint32_t> retail_effect_model_tint_argb(
    const EffectDefinitionRecord& definition, uint32_t age_ticks, uint32_t runtime_flags) noexcept {
    if (definition.transition_kind == 4U) return std::nullopt;
    if ((runtime_flags & 0x04U) == 0U) return 0x00FFFFFFU;
    const uint32_t runtime_tint =
        (static_cast<uint32_t>(effect_alpha_at_age(definition, age_ticks)) << 24U) | 0x00FFFFFFU;
    return retail_modulate_argb_mmx(runtime_tint, definition.state_tint_argb);
}

std::array<uint32_t, 3> timed_effect_transition2_runtime_angles(uint32_t random_word) noexcept {
    const uint32_t azimuth = random_word & 0xFFFFU;
    const uint32_t elevation = (0x4000U - (random_word >> 20U)) & 0xFFFFU;
    return {azimuth, elevation, azimuth};
}

std::array<float, 3> effect_transition2_displacement_at_age_angles(
    const EffectDefinitionRecord& definition, uint32_t age_ticks,
    uint32_t world_rotation_angle0, uint32_t world_rotation_angle1) noexcept {
    std::array<float, 3> result{};
    if (definition.transition_kind != 2U || definition.movement_speed_q12 == 0 || age_ticks == 0U)
        return result;
    const float speed = q12_to_float(definition.movement_speed_q12);
    uint32_t angle0 = world_rotation_angle0 & 0xFFFFU;
    uint32_t angle1 = world_rotation_angle1 & 0xFFFFU;
    constexpr float turn_to_radians = 6.28318530717958647692F / 65536.0F;
    // FixedMath_BuildVectorFromAnglesAndMagnitude3Regs at 0x00484770 is equivalent to
    // {cos(a0)*cos(a1), cos(a0)*sin(a1), sin(a0)} * magnitude. The runtime then updates
    // worldRotationAngle1 with (angle1*63 + 0x4000) >> 6 after each integration step.
    const uint32_t steps = std::min(age_ticks, effect_lifetime_ticks(definition));
    for (uint32_t tick = 0; tick < steps; ++tick) {
        const float a0 = static_cast<float>(angle0) * turn_to_radians;
        const float a1 = static_cast<float>(angle1) * turn_to_radians;
        const float horizontal = std::cos(a0) * speed;
        result[0] += horizontal * std::cos(a1);
        result[1] += horizontal * std::sin(a1);
        result[2] += std::sin(a0) * speed;
        angle1 = static_cast<uint32_t>((static_cast<uint64_t>(angle1) * 63U + 0x4000U) >> 6U)
            & 0xFFFFU;
    }
    return result;
}

uint32_t effect_transition2_world_rotation_angle1_at_pose_age(
    const EffectDefinitionRecord& definition, uint32_t age_ticks,
    uint32_t initial_world_rotation_angle1) noexcept {
    uint32_t angle1 = initial_world_rotation_angle1 & 0xFFFFU;
    if (definition.transition_kind != 2U || age_ticks <= 1U) return angle1;
    const uint32_t lifetime = effect_lifetime_ticks(definition);
    const uint32_t movement_steps = std::min(age_ticks, lifetime);
    const uint32_t easing_steps = movement_steps == 0U ? 0U : movement_steps - 1U;
    for (uint32_t tick = 0; tick < easing_steps; ++tick)
        angle1 = static_cast<uint32_t>((static_cast<uint64_t>(angle1) * 63U + 0x4000U) >> 6U)
            & 0xFFFFU;
    return angle1;
}

std::array<float, 3> effect_transition2_displacement_at_age(
    const EffectDefinitionRecord& definition, uint32_t age_ticks,
    uint32_t orientation_random) noexcept {
    // Preserve the deterministic editor seed mapping used before exact runtime angles were exposed.
    const uint32_t azimuth = orientation_random & 0xFFFFU;
    const uint32_t elevation = (0x4000U - (orientation_random >> 20U)) & 0xFFFFU;
    return effect_transition2_displacement_at_age_angles(definition, age_ticks, elevation, azimuth);
}

std::vector<LinkedShotEmission> build_linked_shot_emissions(
    const EffectDefinitionRecord& definition) {
    std::vector<LinkedShotEmission> result;
    // Presence/cadence is explicit; SHT registry ID0 is a valid laser.
    if (definition.transition_kind != 1U || definition.linked_shot_present == 0U) return result;
    // Retail transition kind 1 decrements linkedShotPresent only when the Q4 animation
    // accumulator advances a sprite frame (iVar32 != 0), not on every 20 Hz maintenance tick.
    // The frame that exhausts animationFramesRemaining unlinks and returns before transition
    // dispatch, so a linked shot is never emitted on the terminal frame advance.
    const uint32_t cadence_frames = definition.linked_shot_present;
    if (definition.runtime_value_0c <= 1U) return result;
    uint32_t attachment = 0U;
    for (uint64_t completed_frames = cadence_frames;
         completed_frames < definition.runtime_value_0c;
         completed_frames += cadence_frames, ++attachment) {
        const uint32_t tick = effect_tick_for_completed_frames(
            definition, static_cast<uint32_t>(completed_frames));
        result.push_back({tick, attachment});
        if (completed_frames > std::numeric_limits<uint32_t>::max() - cadence_frames) break;
    }
    return result;
}

std::optional<uint32_t> linked_shot_parent_effect_spawn_tick(
    uint32_t current_tick, uint32_t cycle_ticks, uint32_t pulse_spawn_tick,
    uint32_t parent_age_tick) noexcept {
    if (cycle_ticks == 0U || pulse_spawn_tick >= cycle_ticks || current_tick < parent_age_tick)
        return std::nullopt;
    const uint32_t parent_spawn_tick = current_tick - parent_age_tick;
    if (parent_spawn_tick % cycle_ticks != pulse_spawn_tick) return std::nullopt;
    return parent_spawn_tick;
}

uint32_t deterministic_resource_emitter_period(uint32_t base_ticks, uint32_t random_range_ticks,
                                               uint32_t stable_seed) noexcept {
    if (random_range_ticks == 0U) return std::max(1U, base_ticks);
    return std::max(1U, base_ticks + mix_resource_seed(stable_seed) % random_range_ticks);
}

uint32_t deterministic_resource_emitter_phase(uint32_t period_ticks, uint32_t stable_seed) noexcept {
    if (period_ticks == 0U) return 0U;
    return mix_resource_seed(stable_seed ^ 0xA511E9B3U) % period_ticks;
}

} // namespace thandor
