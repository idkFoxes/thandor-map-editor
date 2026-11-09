#pragma once

#include "thandor/field.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace thandor {

// Editor replay of the retail FieldGrid projected-occlusion visibility pass. The game stores
// one visibility byte per faction in FieldGridCell.occupancyMask (+0x70): bit 7 persists explored
// history, while one of bits 3..6 is rebuilt for current visibility according to runtime class.
struct FogPreviewObserver {
    int32_t world_x_q12{};
    int32_t world_y_q12{};
    int32_t reference_height_q12{};
    uint32_t radius_world_units{};
    uint64_t faction_visibility_mask{};
};

struct FogPreviewCell {
    uint32_t x{};
    uint32_t y{};
};

// Original six-ray occupancy classification, two bits per faction (seen/current).
uint32_t retail_model_neighborhood_mask(const FieldGrid&,std::span<const uint64_t> occupancy,
    int32_t x_q12,int32_t y_q12,int32_t radius_q12);
struct RetailModelOccupancy {uint32_t primary{},history{};};
RetailModelOccupancy retail_resolve_model_occupancy(uint32_t primary,uint32_t history,bool not_remembered);

// Runtime class -> visibility marker table at immutable game.exe 0x0051FC38. Every marker carries
// persistent explored bit 7 plus exactly one current-visibility class bit.
uint8_t retail_visibility_marker_for_model_class(uint32_t runtime_class_id) noexcept;

// Mirrors the callback's packed 4-bit faction-relation test. Faction byte 0 is unused; for
// factions 1..7, relation nibble bit 3 authorizes sharing the owner's marker with that faction.
uint64_t retail_faction_visibility_mask(uint32_t packed_relation_states,
                                        uint8_t marker) noexcept;

// Self-only mask used when the editor has no live faction-relation state to replay.
uint64_t retail_self_faction_visibility_mask(uint32_t faction_index,
                                             uint8_t marker) noexcept;

// Exact 256-dword terrain projection table at game.exe 0x00505FA0. Fresh V530 memory
// verification shows every entry is nearest-integer 0x4000/(step+4). Steps above 255 clamp to
// the final retail entry because the directional scan limit itself is capped at 0xFF.
int32_t retail_fog_projection_scale_q12(uint32_t scan_step) noexcept;

// Exact FieldGrid_WorldToGridQ12 + the projected-occlusion function's hex-cell rounding.
std::optional<FogPreviewCell> retail_fog_world_to_cell(const FieldGrid& field,
                                                       int32_t world_x_q12,
                                                       int32_t world_y_q12) noexcept;

// Exact nearest-cell transform used by the timed-emitter water lookup and the class-21 mapped
// terrain sound gate. Unlike projected-fog visibility it does not apply hex-cell rounding.
std::optional<FogPreviewCell> retail_nearest_world_to_cell(const FieldGrid& field,
                                                           int32_t world_x_q12,
                                                           int32_t world_y_q12) noexcept;

// Exact nearest-cell water selector used by ArmyRuntime_UpdateTimedShotAndEffectEmitters. Unlike
// the projected-fog hex rounding, this path independently rounds the transformed column/row Q12
// coordinates with +0x800 >> 12 and returns FieldGridCell.waterSurfaceDelta.
int32_t retail_nearest_water_delta_q12(const FieldGrid& field, int32_t world_x_q12,
                                       int32_t world_y_q12) noexcept;

// Rebuilds the current retail occupancy bits for all supplied observers. The returned qword per
// cell uses the same byte-per-faction layout as FieldGridCell.occupancyMask. This is a current-pass
// result; callers preserve previous bit-7 history separately when simulating multiple tick-wheel
// rebuilds.
std::vector<uint64_t> build_projected_fog_preview_occupancy(
    const FieldGrid& field,
    std::span<const FogPreviewObserver> observers);

// Retail state endpoints produced by FieldGrid_ClassifyCellFlagsToRuntimeByte.
uint8_t retail_fog_runtime_visibility_byte(uint8_t occupancy_byte) noexcept;

// Exact one-call result of FieldGrid_ApplyByteClampLookupToCells. The generated 64-KiB table
// moves the runtime byte by 0x15 toward the occupancy-selected target (0, 0x87, or 0xFF).
uint8_t retail_fog_clamp_runtime_byte(uint8_t current_runtime_byte,
                                      uint8_t occupancy_byte) noexcept;

// Number of clamp calls made by the retail eight-tick wheel for the visibility channel. Fresh
// V530 control flow proves owner-list dependence: case 3 always clamps once and clamps a second
// time only when at least one runtime owner exists; case 7 clamps only when an owner exists after
// occupancy rebuild. Other phases do not change the runtime visibility byte.
uint8_t retail_fog_clamp_calls_for_simulation_tick(uint32_t simulation_tick,
                                                    bool has_runtime_owners) noexcept;

bool retail_fog_currently_visible(uint8_t occupancy_byte) noexcept;
bool retail_fog_explored(uint8_t occupancy_byte) noexcept;
// MDL +0x68 bit 0x10 prevents remembering a model after sight is lost.
uint8_t retail_model_fog_intensity(uint8_t occupancy_byte, uint32_t model_definition_flags) noexcept;

uint8_t fog_occupancy_byte(uint64_t occupancy_mask, uint32_t faction_index) noexcept;

} // namespace thandor
