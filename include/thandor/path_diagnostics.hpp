#pragma once

#include "thandor/field.hpp"

#include <cstdint>

namespace thandor {

// V519 pathing uses a runtime GridScratch lattice four times wider/taller than the serialized FLD.
// These helpers expose only the fixed-point coordinate and mask contracts proven in V519. They do
// not synthesize the runtime-only GridScratch state, route queue, or carry-flag return paths.
struct V519PathScratchCoordinate {
    int32_t column{};
    int32_t row{};

    friend bool operator==(const V519PathScratchCoordinate&, const V519PathScratchCoordinate&) = default;
};

struct V519PathWorldPointQ12 {
    int32_t x_q12{};
    int32_t y_q12{};
};

struct V519PathMasks {
    uint32_t faction_presence_mask{};
    uint32_t high_cost_mask{};
    uint32_t blocking_mask{};
    bool traversal_bands_valid{};
};

// Exact V519 world->GridScratch projection: FieldGrid_WorldToGridQ12 followed by nearest quarter-
// cell rounding (+0x800 >> 10). Physical X/Y naming follows the already-audited fog helper rather
// than the decompiler's axis-swapped call-site parameter names.
V519PathScratchCoordinate v519_path_world_to_scratch(int32_t world_x_q12,
                                                      int32_t world_y_q12) noexcept;

// EntityPathing_ResolveDestinationAndRebuildRoutes clamps both source and requested destination to
// the one-scratch-cell inner border before route work.
V519PathScratchCoordinate v519_path_clip_to_inner_scratch(const FieldGrid& field,
                                                          V519PathScratchCoordinate coordinate) noexcept;

// Exact integer center reconstruction used by the V519 route rebuild path. Scratch coordinates are
// cell centers, so projecting a serialized FLD vertex and converting back intentionally differs by
// up to roughly one quarter-cell.
V519PathWorldPointQ12 v519_path_scratch_center_to_world_q12(
    V519PathScratchCoordinate coordinate) noexcept;

// Exact masks established by EntityPathing_ResolveDestinationAndRebuildRoutes in V519.
V519PathMasks v519_path_masks(uint32_t faction_index, uint32_t traversal_low_band_index,
                              uint32_t traversal_high_band_index) noexcept;

} // namespace thandor
