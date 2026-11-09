#pragma once

#include "thandor/field.hpp"
#include "thandor/path_diagnostics.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>
#include <memory>

namespace thandor {

// V530 closes the remaining retail GridScratch route-solver control flow used by the editor:
// source-side blocked-cell recovery, the integer hex-segment walker and the bounded six-pass
// weighted propagation frontier. Known MODEL owners use their class-specific native influence;
// geometry-only callers retain the circle adapter. Mutable owner history is reconstructed.

// Immutable 4x terrain lattice shared across actors/runs. Includes the original staggered
// FLD expansion and class-24 dilation; dynamic influence remains query-local. The worker
// prepares this before parallel stepping. The single-threaded API refreshes after FLD edits.
struct V565PathTerrainCache {
    const FieldGrid* field{};
    std::vector<uint32_t> states; // full 4x scratch lattice, including staggered halos
    // Optional immutable per-run classification/presence generation. Distance
    // bands are reconstructed query-locally so the moving owner is excluded.
    std::shared_ptr<const std::vector<uint32_t>> runtime_states;

    void reset(const FieldGrid& source);
    // Precompute immutable FLD traversal classification once before Simulation Lab worker
    // generations begin. After this call the cache is read-only and safe to share across the
    // experiment's independent run threads.
    void prepare(const FieldGrid& source);
};

struct V528PathRuntimeBlocker {
    int32_t world_x_q12{};
    int32_t world_y_q12{};
    int32_t collision_radius_q12{};
    // Known MODEL owners use their native low/high/no-op grid influence callback.
    // Three-field, geometry-only callers keep the explicit circle adapter.
    uint32_t runtime_class_id{UINT32_MAX};
    // Authored static owners share the native collision list with live armies.
    // Preserve their LEV order for first-blocker side effects (e.g. docking).
    size_t source_placement_index{SIZE_MAX};
};

enum class V528RouteDisposition : uint8_t {
    Direct,
    Routed,
    SourceRelocated,
    ReachableFallback,
    Blocked,
};

struct V528RouteResult {
    V528RouteDisposition disposition{V528RouteDisposition::Blocked};
    V519PathScratchCoordinate source_cell{};
    V519PathScratchCoordinate routing_source_cell{};
    V519PathScratchCoordinate requested_cell{};
    V519PathScratchCoordinate resolved_cell{};
    std::optional<V519PathScratchCoordinate> primary_cell;
    V519PathWorldPointQ12 resolved_world_q12{};
    std::optional<V519PathWorldPointQ12> primary_world_q12;
    uint32_t expanded_nodes{};
    bool source_cell_relocated{};
    bool requested_cell_retargeted{}; // Lab may resolve a blocked world point within the same cell.
    bool live_influence_bands_complete{}; // false until the runtime-only band workspace is modeled.

    bool usable() const noexcept { return disposition != V528RouteDisposition::Blocked; }
};

// Exact V530 carry polarity: false means the segment is acceptable/clear; true means it encounters
// a rejected GridScratch cell.  This wrapper returns the natural editor spelling (true == clear).
bool v528_path_segment_clear_static(const FieldGrid& field,
                                    V519PathScratchCoordinate start,
                                    V519PathScratchCoordinate end,
                                    uint32_t entity_class_mask,
                                    uint32_t caller_blocking_mask,
                                    uint32_t traversal_blocking_mask,
                                    std::span<const V528PathRuntimeBlocker> blockers = {},
                                    int32_t moving_radius_q12 = 0) noexcept;

// Simulation-Lab variant of the exact direct segment test that reuses one tick-local static terrain
// classification cache across actors.
bool v565_path_segment_clear_static_cached(const FieldGrid& field,
                                    V519PathScratchCoordinate start,
                                    V519PathScratchCoordinate end,
                                    uint32_t entity_class_mask,
                                    uint32_t caller_blocking_mask,
                                    uint32_t traversal_blocking_mask,
                                    std::span<const V528PathRuntimeBlocker> blockers,
                                    int32_t moving_radius_q12,
                                    V565PathTerrainCache& terrain_cache) noexcept;

// Resolve a group segment against the group's temporary retained influence image.
V519PathWorldPointQ12 v565_resolve_group_route_segment(const FieldGrid& field,
    V519PathWorldPointQ12 from, V519PathWorldPointQ12 to,
    uint32_t entity_class_mask, uint32_t blocking_mask, V565PathTerrainCache& cache);

// Retail-style route resolution over the state that is actually reconstructible in the editor.
// V530 proves that nearest-unblocked recovery is source-side (not destination snapping), and that
// weighted propagation is a FIFO frontier bounded by six 0x10000-entry pass windows. Lowest-cost
// backtracking and the exact integer line walker are preserved. Known MODEL owners are stamped
// through their native low/high/no-op influence callback; omitted owner history remains incomplete.
V528RouteResult v528_resolve_route(const FieldGrid& field,
                                   int32_t source_world_x_q12,
                                   int32_t source_world_y_q12,
                                   int32_t target_world_x_q12,
                                   int32_t target_world_y_q12,
                                   uint32_t entity_class_mask,
                                   uint32_t traversal_low_band_index,
                                   uint32_t traversal_high_band_index,
                                   std::span<const V528PathRuntimeBlocker> blockers = {},
                                   int32_t moving_radius_q12 = 0,
                                   V565PathTerrainCache* shared_terrain_cache = nullptr,
                                   bool movement_locked = false);

// Lab adapter: test the actual world-Q12 swept centre against expanded runtime
// circles. Grid-cell admission alone can miss an off-centre crossing.
bool simulation_lab_path_world_segment_clear(V519PathWorldPointQ12 from,
    V519PathWorldPointQ12 to, std::span<const V528PathRuntimeBlocker> blockers,
    int32_t moving_radius_q12) noexcept;

// Catalog-backed runs with retained native state use the FIFO resolver. The legacy
// synthetic-fixture adapter below preserves the recovered GridScratch
// admission masks, costs, source recovery, line walker and waypoint compression, but uses an exact
// shortest-cost A* search for the common routed case instead of retail's FIFO queue/tie order.
// Lab additionally validates actual world-position terrain and swept-hull edges and resolves blocked
// world endpoints. These adapter policies do not claim original influence/owner parity.
// Keep gameplay/editor parity audits on v528_resolve_route; this entry exists solely so 20 Hz Lab
// generations cannot be dominated by a full-map FIFO flood.
V528RouteResult v565_resolve_route_fast_for_simulation_lab(
                                   const FieldGrid& field,
                                   int32_t source_world_x_q12,
                                   int32_t source_world_y_q12,
                                   int32_t target_world_x_q12,
                                   int32_t target_world_y_q12,
                                   uint32_t entity_class_mask,
                                   uint32_t traversal_low_band_index,
                                   uint32_t traversal_high_band_index,
                                   std::span<const V528PathRuntimeBlocker> blockers = {},
                                   int32_t moving_radius_q12 = 0,
                                   V565PathTerrainCache* shared_terrain_cache = nullptr,
                                   bool direct_segment_already_rejected = false,
                                   bool movement_locked = false);

const char* v528_route_disposition_name(V528RouteDisposition disposition) noexcept;

} // namespace thandor
