#include "thandor/v528_pathing.hpp"
#include "thandor/retail_placement.hpp"
#include "thandor/simulation_lab.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <utility>
#include <unordered_map>

namespace thandor {
namespace {

constexpr uint32_t kHardBoundary = 0x80000000U;
constexpr uint32_t kInfiniteCost = 0x7FFFFFFFU;

uint32_t low_blocking_mask(uint32_t low_band) noexcept {
    return low_band < 8U ? static_cast<uint32_t>(0x00000100U << low_band) : 0U;
}

uint32_t high_cost_mask(uint32_t low_band) noexcept {
    return low_band < 8U ? static_cast<uint32_t>(0x00010000U << low_band) : 0U;
}

uint32_t high_blocking_mask(uint32_t high_band) noexcept {
    return high_band < 8U ? static_cast<uint32_t>(0x01000000U << high_band) : kHardBoundary;
}

struct ScratchCell {
    uint32_t state{};
    uint32_t cost{kInfiniteCost};
};

struct ScratchImage {
    uint32_t width{};
    uint32_t height{};
    std::vector<ScratchCell> cells;

    // Native path costs use offsets in the flat scratch allocation. On open
    // edge cells, a neighbor can cross a column boundary into the adjacent row.
    bool contains_index(int64_t index) const noexcept {
        return index >= 0 && static_cast<uint64_t>(index) < cells.size();
    }

    bool contains(int32_t row, int32_t column) const noexcept {
        return row >= 0 && column >= 0 && row < static_cast<int32_t>(height) &&
               column < static_cast<int32_t>(width);
    }
    std::size_t index(int32_t row, int32_t column) const noexcept {
        return static_cast<std::size_t>(row) * width + static_cast<std::size_t>(column);
    }
};

bool within_expanded_radius(const V519PathWorldPointQ12& point,
                            const V528PathRuntimeBlocker& blocker,
                            int32_t moving_radius_q12) noexcept {
    if (moving_radius_q12 == 0 || blocker.collision_radius_q12 == 0) return false;
    const int64_t dx = static_cast<int64_t>(point.x_q12) - blocker.world_x_q12;
    const int64_t dy = static_cast<int64_t>(point.y_q12) - blocker.world_y_q12;
    const int64_t radius = static_cast<int64_t>(moving_radius_q12) + blocker.collision_radius_q12;
    return dx * dx + dy * dy <= radius * radius;
}

struct ScratchTerrainStateCache {
    struct IndexedBlocker {
        V528PathRuntimeBlocker blocker{};
        int32_t min_column{};
        int32_t max_column{};
    };

    explicit ScratchTerrainStateCache(const FieldGrid& source,
                                      std::span<const V528PathRuntimeBlocker> blockers = {},
                                      int32_t moving_radius_q12 = 0,
                                      V565PathTerrainCache* shared_terrain_cache = nullptr)
        : field(source) {
        const std::size_t required = static_cast<std::size_t>(source.width()) * source.height() * 16U;
        if (shared_terrain_cache != nullptr) {
            if (shared_terrain_cache->field == &source && shared_terrain_cache->runtime_states &&
                shared_terrain_cache->runtime_states->size() == required) {
                shared_states = shared_terrain_cache->runtime_states.get();
                retained_faction_presence = true;
            } else {
                if (shared_terrain_cache->field != &source || shared_terrain_cache->states.size() != required)
                    shared_terrain_cache->prepare(source);
                shared_states = &shared_terrain_cache->states;
            }
        } else {
            owned_states = retail_build_terrain_scratch(source).state;
            shared_states = &owned_states;
        }
        if(!retained_faction_presence)for (const auto& blocker : blockers) {
            if (blocker.runtime_class_id == UINT32_MAX) continue;
            retail_visit_grid_influence(source.width()*4U,*shared_states,
                {blocker.world_x_q12,blocker.world_y_q12},blocker.collision_radius_q12,
                blocker.runtime_class_id,[&](size_t index,uint32_t bits) {
                    influence_states[index] |= bits;
                });
        }
        if (moving_radius_q12 == 0 || blockers.empty()) return;
        blocker_rows.resize(source.height()*4U);
        const int32_t scratch_width = static_cast<int32_t>(source.width() * 4U);
        const int32_t scratch_height = static_cast<int32_t>(source.height() * 4U);
        indexed_blockers.reserve(blockers.size());
        for (const auto& blocker : blockers) {
            if (blocker.collision_radius_q12 == 0) continue;
            const int64_t combined = static_cast<int64_t>(moving_radius_q12) +
                                     blocker.collision_radius_q12;
            if (combined <= 0) continue;
            const int32_t radius = static_cast<int32_t>(std::min<int64_t>(
                combined, std::numeric_limits<int32_t>::max()));
            int32_t min_column = std::numeric_limits<int32_t>::max();
            int32_t max_column = std::numeric_limits<int32_t>::min();
            int32_t min_row = std::numeric_limits<int32_t>::max();
            int32_t max_row = std::numeric_limits<int32_t>::min();
            for (const int sx : {-1, 1}) {
                for (const int sy : {-1, 1}) {
                    const int64_t wx = static_cast<int64_t>(blocker.world_x_q12) + sx * static_cast<int64_t>(radius);
                    const int64_t wy = static_cast<int64_t>(blocker.world_y_q12) + sy * static_cast<int64_t>(radius);
                    const auto scratch = v519_path_world_to_scratch(
                        static_cast<int32_t>(std::clamp<int64_t>(wx, std::numeric_limits<int32_t>::min(),
                                                                 std::numeric_limits<int32_t>::max())),
                        static_cast<int32_t>(std::clamp<int64_t>(wy, std::numeric_limits<int32_t>::min(),
                                                                 std::numeric_limits<int32_t>::max())));
                    min_column = std::min(min_column, scratch.column);
                    max_column = std::max(max_column, scratch.column);
                    min_row = std::min(min_row, scratch.row);
                    max_row = std::max(max_row, scratch.row);
                }
            }
            // The inverse lattice rounds; two scratch records of padding keep the row/column index
            // conservative while exact world-Q12 radius testing remains the final predicate.
            min_column = std::max(0, min_column - 2);
            max_column = std::min(scratch_width - 1, max_column + 2);
            min_row = std::max(0, min_row - 2);
            max_row = std::min(scratch_height - 1, max_row + 2);
            if (min_column > max_column || min_row > max_row) continue;
            const std::uint16_t index = static_cast<std::uint16_t>(indexed_blockers.size());
            indexed_blockers.push_back({blocker, min_column, max_column});
            for (int32_t row = min_row; row <= max_row; ++row)
                blocker_rows[static_cast<std::size_t>(row)].push_back(index);
        }
    }

    uint32_t state_at(uint32_t column, uint32_t row) const {
        const auto index = static_cast<std::size_t>(row) * field.width()*4U + column;
        const auto it = influence_states.find(index);
        const auto base=(*shared_states)[index];
        return base | (it == influence_states.end() ? 0U : it->second);
    }

    bool dynamic_blocked(int32_t row, int32_t column, int32_t moving_radius_q12) const noexcept {
        if (moving_radius_q12 == 0 || row < 0 ||
            row >= static_cast<int32_t>(blocker_rows.size())) return false;
        const auto& candidates = blocker_rows[static_cast<std::size_t>(row)];
        if (candidates.empty()) return false;
        const auto world = v519_path_scratch_center_to_world_q12({column, row});
        for (const auto index : candidates) {
            const auto& candidate = indexed_blockers[index];
            if (column < candidate.min_column || column > candidate.max_column) continue;
            if (candidate.blocker.runtime_class_id == UINT32_MAX &&
                within_expanded_radius(world, candidate.blocker, moving_radius_q12)) return true;
        }
        return false;
    }

    // Adjacent lattice centres may straddle a small hull without either
    // centre overlapping it. Reuse conservative row bins for the swept edge.
    bool edge_blocked(V519PathScratchCoordinate from,V519PathScratchCoordinate to,
                      int32_t moving_radius_q12) const noexcept {
        if(moving_radius_q12<=0||from.row<0||from.row>=static_cast<int32_t>(blocker_rows.size()))return false;
        const auto a=v519_path_scratch_center_to_world_q12(from);
        const auto b=v519_path_scratch_center_to_world_q12(to);
        for(const auto index:blocker_rows[static_cast<size_t>(from.row)]){
            const auto& candidate=indexed_blockers[index];
            if(std::min(from.column,to.column)>candidate.max_column||
               std::max(from.column,to.column)<candidate.min_column)continue;
            if(!simulation_lab_path_world_segment_clear(a,b,std::span(&candidate.blocker,1),moving_radius_q12))return true;
        }
        return false;
    }

    const FieldGrid& field;
    std::vector<uint32_t> owned_states;
    const std::vector<uint32_t>* shared_states{};
    bool retained_faction_presence{};
    std::unordered_map<size_t,uint32_t> influence_states;
    std::vector<IndexedBlocker> indexed_blockers;
    std::vector<std::vector<std::uint16_t>> blocker_rows;
};

uint32_t scratch_state_at(const FieldGrid& field, int32_t row, int32_t column,
                          uint32_t entity_class_mask, uint32_t dynamic_block_mask,
                          std::span<const V528PathRuntimeBlocker> blockers,
                          int32_t moving_radius_q12,
                          ScratchTerrainStateCache* terrain_cache = nullptr) {
    const int32_t scratch_width = static_cast<int32_t>(field.width() * 4U);
    const int32_t scratch_height = static_cast<int32_t>(field.height() * 4U);
    // GridScratch_RebuildTerrainAndRuntimeClassificationMasks ORs each FLD
    // classification into an asymmetric halo, then dilates class 24. A world
    // sample of floor((index-2)/4) loses these writes and shifts hill boundaries.
    if (row < 0 || column < 0 || row >= scratch_height || column >= scratch_width)
        return kHardBoundary;
    uint32_t state = terrain_cache
        ? terrain_cache->state_at(static_cast<uint32_t>(column), static_cast<uint32_t>(row))
        : retail_build_terrain_scratch(field).read(static_cast<uint32_t>(column), static_cast<uint32_t>(row));
    if ((state & kHardBoundary) == 0U &&
        (!terrain_cache || !terrain_cache->retained_faction_presence)) state |= entity_class_mask;
    if (dynamic_block_mask != 0U && moving_radius_q12 != 0 && !blockers.empty()) {
        if (terrain_cache && terrain_cache->dynamic_blocked(row, column, moving_radius_q12)) {
            state |= dynamic_block_mask;
        } else if (!terrain_cache) {
            const auto world = v519_path_scratch_center_to_world_q12({column, row});
            for (const auto& blocker : blockers) {
                if (within_expanded_radius(world, blocker, moving_radius_q12)) {
                    state |= dynamic_block_mask;
                    break;
                }
            }
        }
    }
    return state;
}

ScratchImage build_scratch(const FieldGrid& field, uint32_t entity_class_mask,
                           uint32_t low_band, uint32_t high_band,
                           std::span<const V528PathRuntimeBlocker> blockers,
                           int32_t moving_radius_q12,
                           ScratchTerrainStateCache* terrain_cache = nullptr) {
    (void)high_band;
    ScratchImage image;
    image.width = field.width() * 4U;
    image.height = field.height() * 4U;
    image.cells.resize(static_cast<std::size_t>(image.width) * image.height);
    const uint32_t low_block = low_blocking_mask(low_band);

    for (uint32_t row = 0; row < image.height; ++row) {
        for (uint32_t column = 0; column < image.width; ++column) {
            auto& cell = image.cells[static_cast<std::size_t>(row) * image.width + column];
            // V528's line test requires the active class bit in admitted cells. The retail live
            // buffer preserves/updates this namespace through occupancy/influence passes; the
            // editor reconstructs only the proven static terrain plus caller-owned blockers.
            cell.state = scratch_state_at(field, static_cast<int32_t>(row),
                static_cast<int32_t>(column), entity_class_mask, low_block, blockers,
                moving_radius_q12, terrain_cache);
        }
    }
    return image;
}

bool cell_rejected(const ScratchCell& cell, uint32_t entity_class_mask,
                   uint32_t traversal_blocking_mask, uint32_t caller_blocking_mask) noexcept {
    return (cell.state & kHardBoundary) != 0U ||
           (cell.state & entity_class_mask) == 0U ||
           (cell.state & traversal_blocking_mask) != 0U ||
           (cell.state & caller_blocking_mask) != 0U;
}

// V530 resolves the retail helper far enough to reproduce its integer pointer walk directly.
// The helper tests every crossed scratch record except the original start record and returns the
// carry/rejection polarity used by the executable.  Use signed record indices here so malformed
// editor inputs fail closed instead of reproducing retail pointer underflow.
bool segment_rejected(const ScratchImage& image, V519PathScratchCoordinate start,
                      V519PathScratchCoordinate end, uint32_t entity_class_mask,
                      uint32_t traversal_blocking_mask, uint32_t caller_blocking_mask) noexcept {
    if (!image.contains(start.row, start.column) || !image.contains(end.row, end.column)) return true;

    const int64_t width = static_cast<int64_t>(image.width);
    const int64_t cell_count = static_cast<int64_t>(image.cells.size());
    const int64_t original_start = static_cast<int64_t>(image.index(start.row, start.column));
    int64_t terminal = static_cast<int64_t>(image.index(end.row, end.column));
    int64_t scan = original_start;
    int32_t row_delta = end.row - start.row;
    int32_t column_delta = end.column - start.column;

    const auto rejected_at = [&](int64_t index) noexcept {
        if (index < 0 || index >= cell_count) return true;
        if (index == original_start) return false;
        return cell_rejected(image.cells[static_cast<std::size_t>(index)], entity_class_mask,
                             traversal_blocking_mask, caller_blocking_mask);
    };

    if (column_delta < 0) {
        row_delta = -row_delta;
        column_delta = -column_delta;
        scan = terminal;
        terminal = original_start;
    }

    if (row_delta < 0) {
        int32_t error = 0;
        const int32_t diagonal_threshold = -row_delta - column_delta;
        for (;;) {
            if (rejected_at(scan)) return true;
            if (scan == terminal) return false;
            if (error <= diagonal_threshold) {
                if (error != diagonal_threshold) {
                    error += column_delta * 2;
                    scan -= width;
                    continue;
                }
                error += column_delta * 2;
                scan -= width;
            }
            error += row_delta * 2;
            ++scan;
        }
    }

    int32_t vertical_error = 0;
    int32_t column_error = 0;
    int32_t columns_remaining = column_delta;
    int64_t column_bottom = scan;
    for (;;) {
        while (vertical_error < row_delta) {
            scan += width;
            vertical_error += column_delta;
            if (scan == terminal) break;
        }

        do {
            vertical_error -= row_delta;
            for (int64_t column_scan = scan;; column_scan -= width) {
                if (rejected_at(column_scan)) return true;
                if (column_scan == column_bottom) break;
                if (column_scan - width < 0) return true;
            }

            --columns_remaining;
            if (columns_remaining < 0) return false;
            ++scan;
            ++column_bottom;
            while (column_error <= -column_delta) {
                column_bottom += width;
                column_error += column_delta;
                if (column_bottom < 0 || column_bottom >= cell_count) return true;
            }
            column_error -= row_delta;
        } while (columns_remaining == 0);
    }
}

bool segment_rejected_on_demand(
    const FieldGrid& field, V519PathScratchCoordinate start, V519PathScratchCoordinate end,
    uint32_t entity_class_mask, uint32_t traversal_blocking_mask,
    uint32_t caller_blocking_mask, uint32_t dynamic_block_mask,
    std::span<const V528PathRuntimeBlocker> blockers, int32_t moving_radius_q12,
    ScratchTerrainStateCache* terrain_cache = nullptr) {
    const int64_t width = static_cast<int64_t>(field.width()) * 4;
    const int64_t height = static_cast<int64_t>(field.height()) * 4;
    const int64_t cell_count = width * height;
    const auto contains = [&](V519PathScratchCoordinate cell) noexcept {
        return cell.row >= 0 && cell.column >= 0 &&
               static_cast<int64_t>(cell.row) < height &&
               static_cast<int64_t>(cell.column) < width;
    };
    if (!contains(start) || !contains(end) || width <= 0 || height <= 0) return true;

    const int64_t original_start = static_cast<int64_t>(start.row) * width + start.column;
    int64_t terminal = static_cast<int64_t>(end.row) * width + end.column;
    int64_t scan = original_start;
    int32_t row_delta = end.row - start.row;
    int32_t column_delta = end.column - start.column;
    const auto rejected_at = [&](int64_t index) {
        if (index < 0 || index >= cell_count) return true;
        if (index == original_start) return false;
        const int32_t row = static_cast<int32_t>(index / width);
        const int32_t column = static_cast<int32_t>(index % width);
        const uint32_t state = scratch_state_at(field, row, column, entity_class_mask,
            dynamic_block_mask, blockers, moving_radius_q12, terrain_cache);
        return (state & kHardBoundary) != 0U || (state & entity_class_mask) == 0U ||
               (state & traversal_blocking_mask) != 0U ||
               (state & caller_blocking_mask) != 0U;
    };

    if (column_delta < 0) {
        row_delta = -row_delta;
        column_delta = -column_delta;
        scan = terminal;
        terminal = original_start;
    }
    if (row_delta < 0) {
        int32_t error = 0;
        const int32_t diagonal_threshold = -row_delta - column_delta;
        for (;;) {
            if (rejected_at(scan)) return true;
            if (scan == terminal) return false;
            if (error <= diagonal_threshold) {
                if (error != diagonal_threshold) {
                    error += column_delta * 2;
                    scan -= width;
                    continue;
                }
                error += column_delta * 2;
                scan -= width;
            }
            error += row_delta * 2;
            ++scan;
        }
    }

    int32_t vertical_error = 0;
    int32_t column_error = 0;
    int32_t columns_remaining = column_delta;
    int64_t column_bottom = scan;
    for (;;) {
        while (vertical_error < row_delta) {
            scan += width;
            vertical_error += column_delta;
            if (scan == terminal) break;
        }
        do {
            vertical_error -= row_delta;
            for (int64_t column_scan = scan;; column_scan -= width) {
                if (rejected_at(column_scan)) return true;
                if (column_scan == column_bottom) break;
                if (column_scan - width < 0) return true;
            }
            --columns_remaining;
            if (columns_remaining < 0) return false;
            ++scan;
            ++column_bottom;
            while (column_error <= -column_delta) {
                column_bottom += width;
                column_error += column_delta;
                if (column_bottom < 0 || column_bottom >= cell_count) return true;
            }
            column_error -= row_delta;
        } while (columns_remaining == 0);
    }
}

// Lab physical routes start at the actual model position, not the rounded
// GridScratch centre. Check every scratch record touched by that world segment.
// A small conservative border covers the staged integer projection's <2-Q12
// rounding error. The original start record stays exempt for source recovery.
bool world_terrain_segment_rejected(const FieldGrid& field,
    V519PathWorldPointQ12 from, V519PathWorldPointQ12 to,
    uint32_t entity_class_mask, uint32_t blocking_mask,
    ScratchTerrainStateCache* terrain_cache) {
    // This guard validates terrain crossed by the world-space chord. Runtime
    // bands are tested at path nodes; their initial overlap must not prevent
    // the outward source recovery that the physical guard explicitly admits.
    blocking_mask &= 0xFF000000U;
    const auto project=[](V519PathWorldPointQ12 p) {
        const double diagonal=double(p.y_q12)*-0x20C8CC/2097152.0;
        return std::array<double,2>{(double(p.x_q12)*0x1C6E9C/1048576.0-diagonal+2048.0)/1024.0,
                                    (2.0*diagonal+2048.0)/1024.0};
    };
    const auto a=project(from),b=project(to);
    const double dx=b[0]-a[0],dy=b[1]-a[1];
    const auto source=v519_path_world_to_scratch(from.x_q12,from.y_q12);
    int32_t column=static_cast<int32_t>(std::floor(a[0])),row=static_cast<int32_t>(std::floor(a[1]));
    const int32_t end_column=static_cast<int32_t>(std::floor(b[0])),end_row=static_cast<int32_t>(std::floor(b[1]));
    const int sx=dx>0?1:dx<0?-1:0,sy=dy>0?1:dy<0?-1:0;
    const double infinity=std::numeric_limits<double>::infinity();
    const double delta_x=sx?1.0/std::abs(dx):infinity,delta_y=sy?1.0/std::abs(dy):infinity;
    double next_x=sx?((sx>0?column+1.0:double(column))-a[0])/dx:infinity;
    double next_y=sy?((sy>0?row+1.0:double(row))-a[1])/dy:infinity;
    constexpr double border=2.0/1024.0+1e-9;
    const auto touches=[&](int32_t c,int32_t r) {
        double low=0,high=1;
        for(int axis=0;axis<2;++axis){
            const double start=a[axis],delta=axis?dy:dx,cell=axis?double(r):double(c);
            if(delta==0){if(start<cell-border||start>cell+1+border)return false;continue;}
            double first=(cell-border-start)/delta,last=(cell+1+border-start)/delta;
            if(first>last)std::swap(first,last);
            low=std::max(low,first);high=std::min(high,last);
            if(low>high)return false;
        }
        return true;
    };
    const int64_t maximum=int64_t(std::abs(end_column-column))+std::abs(end_row-row)+1;
    for(int64_t i=0;i<maximum;++i){
        for(int dr=-1;dr<=1;++dr)for(int dc=-1;dc<=1;++dc){
            const int32_t c=column+dc,r=row+dr;
            if((c==source.column&&r==source.row)||!touches(c,r))continue;
            // Integer projection can put the model just across a floating-point
            // cell border. Do not reject the tiny initial border strip behind
            // its actual start record when the segment moves away from it.
            if((c<source.column&&dx>=0)||(c>source.column&&dx<=0)||
               (r<source.row&&dy>=0)||(r>source.row&&dy<=0))continue;
            const auto state=scratch_state_at(field,r,c,entity_class_mask,0,{},0,terrain_cache);
            if((state&kHardBoundary)||(state&entity_class_mask)==0||(state&blocking_mask))return true;
        }
        if(column==end_column&&row==end_row)return false;
        if(next_x<next_y){column+=sx;next_x+=delta_x;}
        else {row+=sy;next_y+=delta_y;}
    }
    return true;
}

constexpr std::array<std::pair<int32_t, int32_t>, 6> kNeighbors{{
    {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}
}};
constexpr std::array<std::pair<int32_t, int32_t>, 6> kPropagationNeighbors{{
    {-1, 0}, {-1, 1}, {0, 1}, {0, -1}, {1, -1}, {1, 0}
}};

int32_t mixed_axial_distance(V519PathScratchCoordinate candidate,
                             V519PathScratchCoordinate reference) noexcept {
    // Exact on the small signed-delta hex-lattice domain used by routing.  The
    // recovered branch tree sums equal-sign delta magnitudes and otherwise takes
    // the larger. Proven equivalent in OPT001 on 3,263,169 coordinate pairs per
    // compiler; arithmetic outside int32 abs/subtract range is not supported.
    const int32_t dx = candidate.column - reference.column;
    const int32_t dy = candidate.row - reference.row;
    const int32_t ax = dx < 0 ? -dx : dx;
    const int32_t ay = dy < 0 ? -dy : dy;
    return (dx < 0) == (dy < 0) ? ax + ay : std::max(ax, ay);
}


struct FastRoutedPath {
    std::vector<V519PathScratchCoordinate> cells;
    uint32_t expanded_nodes{};
};

FastRoutedPath find_routed_path_on_demand(
    const FieldGrid& field, V519PathScratchCoordinate source,
    V519PathScratchCoordinate destination, uint32_t entity_class_mask,
    uint32_t high_cost_mask_value, uint32_t blocking_mask,
    uint32_t dynamic_block_mask, std::span<const V528PathRuntimeBlocker> blockers,
    int32_t moving_radius_q12, ScratchTerrainStateCache* terrain_cache, V519PathWorldPointQ12 actual_source) {
    FastRoutedPath result;
    const uint32_t width = field.width() * 4U;
    const uint32_t height = field.height() * 4U;
    if (width == 0U || height == 0U) return result;
    const std::size_t cell_count = static_cast<std::size_t>(width) * height;
    const auto contains = [&](V519PathScratchCoordinate cell) noexcept {
        return cell.row >= 0 && cell.column >= 0 &&
               cell.row < static_cast<int32_t>(height) &&
               cell.column < static_cast<int32_t>(width);
    };
    if (!contains(source) || !contains(destination)) return result;
    const auto index_of = [&](V519PathScratchCoordinate cell) noexcept {
        return static_cast<std::size_t>(cell.row) * width + static_cast<std::size_t>(cell.column);
    };
    const auto coordinate_of = [&](std::size_t index) noexcept {
        return V519PathScratchCoordinate{static_cast<int32_t>(index % width),
                                         static_cast<int32_t>(index / width)};
    };

    // Cache complete per-scratch state only for records actually visited by A*. The underlying
    // terrain cache already collapses the 4x lattice to one expensive normal classification per
    // FLD cell; this second cache avoids repeating dynamic-blocker tests while the open set relaxes
    // the same scratch records.
    std::vector<uint32_t> state_cache(cell_count, std::numeric_limits<uint32_t>::max());
    const auto state_at_index = [&](std::size_t index) -> uint32_t {
        auto& cached = state_cache[index];
        if (cached != std::numeric_limits<uint32_t>::max()) return cached;
        const auto cell = coordinate_of(index);
        cached = scratch_state_at(field, cell.row, cell.column, entity_class_mask,
            dynamic_block_mask, blockers, moving_radius_q12, terrain_cache);
        return cached;
    };
    const auto transition_from_state = [&](uint32_t state) noexcept {
        if ((state & kHardBoundary) != 0U) return kInfiniteCost;
        uint32_t cost = 4U;
        if ((state & entity_class_mask) != 0U) {
            if ((state & blocking_mask) != 0U) return kInfiniteCost;
            cost = (state & high_cost_mask_value) != 0U ? 12U : 3U;
        }
        return cost;
    };

    const std::size_t source_index = index_of(source);
    const std::size_t destination_index = index_of(destination);
    std::array<std::size_t, 7> goals{};
    std::size_t goal_count = 0U;
    goals[goal_count++] = source_index;
    for (const auto& [dr, dc] : kNeighbors) {
        const V519PathScratchCoordinate candidate{source.column + dc, source.row + dr};
        if (contains(candidate)) goals[goal_count++] = index_of(candidate);
    }
    const auto is_goal = [&](std::size_t index) noexcept {
        for (std::size_t i = 0; i < goal_count; ++i)
            if (goals[i] == index) return true;
        return false;
    };

    struct OpenNode {
        uint32_t cost{};
        uint32_t index{};
    };
    // With the exact mixed-axial distance, h=3*distance is a consistent lower bound: one hex step
    // changes h by at most three, while every admitted transition costs at least three. Therefore
    // A* f-values never decrease and a newly generated node is at most 15 f-units ahead
    // (12 transition + 3 heuristic increase). A 16-bucket monotone queue removes binary-heap
    // overhead without changing shortest-path semantics.
    constexpr uint32_t kOpenBucketCount = 16U;
    std::array<std::vector<OpenNode>, kOpenBucketCount> open;
    for (auto& bucket : open) bucket.reserve(1024U);
    std::vector<uint32_t> cost(cell_count, kInfiniteCost);
    std::vector<uint32_t> next_to_destination(cell_count,
                                               std::numeric_limits<uint32_t>::max());
    cost[destination_index] = 0U;
    const uint32_t initial_h = static_cast<uint32_t>(
        std::max(0, mixed_axial_distance(destination, source))) * 3U;
    open[initial_h % kOpenBucketCount].push_back(
        {0U, static_cast<uint32_t>(destination_index)});
    std::size_t open_count = 1U;
    uint32_t current_f = initial_h;

    std::size_t reached = std::numeric_limits<std::size_t>::max();
    while (open_count != 0U) {
        auto& bucket = open[current_f % kOpenBucketCount];
        if (bucket.empty()) {
            ++current_f;
            continue;
        }
        const auto current = bucket.back();
        bucket.pop_back();
        --open_count;
        const std::size_t current_index = current.index;
        if (current.cost != cost[current_index]) continue;
        ++result.expanded_nodes;
        if (is_goal(current_index) && simulation_lab_path_world_segment_clear(actual_source,
                v519_path_scratch_center_to_world_q12(coordinate_of(current_index)),
                blockers,moving_radius_q12) &&
            !world_terrain_segment_rejected(field,actual_source,
                v519_path_scratch_center_to_world_q12(coordinate_of(current_index)),
                entity_class_mask,blocking_mask,terrain_cache)) {
            reached = current_index;
            break;
        }
        const auto cell = coordinate_of(current_index);
        for (const auto& [dr, dc] : kPropagationNeighbors) {
            const V519PathScratchCoordinate neighbor{cell.column + dc, cell.row + dr};
            if (!contains(neighbor)) continue;
            const auto neighbor_index = index_of(neighbor);
            const uint32_t step = transition_from_state(state_at_index(neighbor_index));
            if (step == kInfiniteCost || current.cost > kInfiniteCost - step) continue;
            const uint32_t next = current.cost + step;
            if (next >= cost[neighbor_index]) continue;
            // Admitted endpoints alone do not admit an edge through a blocked
            // terrain corner or a small hull between the lattice centres.
            if(world_terrain_segment_rejected(field,
                v519_path_scratch_center_to_world_q12(neighbor),
                v519_path_scratch_center_to_world_q12(cell),entity_class_mask,
                blocking_mask,terrain_cache)||
               terrain_cache->edge_blocked(neighbor,cell,moving_radius_q12))continue;
            cost[neighbor_index] = next;
            next_to_destination[neighbor_index] = static_cast<uint32_t>(current_index);
            const uint32_t heuristic = static_cast<uint32_t>(
                std::max(0, mixed_axial_distance(neighbor, source))) * 3U;
            const uint32_t estimate = next > kInfiniteCost - heuristic
                ? kInfiniteCost : next + heuristic;
            // The consistent heuristic and 3/4/12 transition domain make this bound exact. Keep a
            // defensive slow clamp for malformed future state rather than corrupting a bucket.
            if (estimate < current_f || estimate - current_f >= kOpenBucketCount) {
                // This should be unreachable for the six recovered neighbors. Falling back to the
                // last bucket remains deterministic and only delays expansion; it does not admit a
                // blocked record or alter the recorded cost.
                open[(current_f + kOpenBucketCount - 1U) % kOpenBucketCount].push_back(
                    {next, static_cast<uint32_t>(neighbor_index)});
            } else {
                open[estimate % kOpenBucketCount].push_back(
                    {next, static_cast<uint32_t>(neighbor_index)});
            }
            ++open_count;
        }
    }
    if (reached == std::numeric_limits<std::size_t>::max()) return result;

    result.cells.reserve(static_cast<std::size_t>(mixed_axial_distance(source, destination)) + 2U);
    result.cells.push_back(source);
    if (reached != source_index) result.cells.push_back(coordinate_of(reached));
    std::size_t scan = reached;
    for (std::size_t guard = 0; guard < cell_count && scan != destination_index; ++guard) {
        const auto next = next_to_destination[scan];
        if (next == std::numeric_limits<uint32_t>::max()) {
            result.cells.clear();
            return result;
        }
        scan = next;
        const auto cell = coordinate_of(scan);
        if (result.cells.empty() || cell != result.cells.back()) result.cells.push_back(cell);
    }
    if (result.cells.empty() || result.cells.back() != destination) result.cells.clear();
    return result;
}

std::optional<V519PathScratchCoordinate> compressed_primary_on_demand(
    const FieldGrid& field, std::span<const V519PathScratchCoordinate> path,
    uint32_t entity_class_mask, uint32_t traversal_blocking_mask,
    uint32_t caller_blocking_mask, uint32_t dynamic_block_mask,
    std::span<const V528PathRuntimeBlocker> blockers, int32_t moving_radius_q12,
    ScratchTerrainStateCache* terrain_cache,V519PathWorldPointQ12 actual_source) {
    if (path.size() < 2U) return std::nullopt;
    const auto source = path.front();
    V519PathScratchCoordinate previous = source;
    uint32_t active_caller_mask = caller_blocking_mask;
    for (std::size_t i = 1U; i < path.size(); ++i) {
        const auto scan_state = scratch_state_at(field, previous.row, previous.column,
            entity_class_mask, dynamic_block_mask, blockers, moving_radius_q12, terrain_cache);
        if ((scan_state & caller_blocking_mask) != 0U) active_caller_mask = 0U;
        const auto next = path[i];
        const bool world_clear=simulation_lab_path_world_segment_clear(actual_source,
            v519_path_scratch_center_to_world_q12(next),blockers,moving_radius_q12) &&
            !world_terrain_segment_rejected(field,actual_source,
                v519_path_scratch_center_to_world_q12(next),entity_class_mask,
                traversal_blocking_mask,terrain_cache);
        if (!world_clear || segment_rejected_on_demand(field, source, next, entity_class_mask,
                traversal_blocking_mask, active_caller_mask, dynamic_block_mask, blockers,
                moving_radius_q12, terrain_cache)) {
            // When off-centre, first reach the admitted source centre rather than
            // repeating a physically blocked shortcut from the rounded cell.
            if(previous==source&&!world_clear)return source;
            return previous == source ? std::optional{next} : std::optional{previous};
        }
        previous = next;
    }
    return std::nullopt;
}

struct NearestUnblockedResult {
    V519PathScratchCoordinate cell{};
    bool relocation_required{};
    bool found{};
};

NearestUnblockedResult nearest_unblocked_source(const ScratchImage& image,
                                                 V519PathScratchCoordinate source,
                                                 uint32_t blocking_mask) noexcept {
    const uint32_t rejected_mask = blocking_mask | kHardBoundary;
    const auto clear = [&](V519PathScratchCoordinate cell) noexcept {
        return image.contains(cell.row, cell.column) &&
               (image.cells[image.index(cell.row, cell.column)].state & rejected_mask) == 0U;
    };

    // V530: if the source OR any immediate neighbor is clear, preserve the original source.  The
    // retail helper only relocates when the complete seven-cell source neighborhood is blocked.
    if (clear(source)) return {source, false, true};
    for (const auto& [dr, dc] : kNeighbors) {
        if (clear({source.column + dc, source.row + dr})) return {source, false, true};
    }

    const int32_t min_column = std::max(0, source.column - 16);
    const int32_t min_row = std::max(0, source.row - 16);
    const int32_t max_column = std::min(static_cast<int32_t>(image.width), source.column + 16);
    const int32_t max_row = std::min(static_cast<int32_t>(image.height), source.row + 16);
    int32_t best_distance = std::numeric_limits<int32_t>::max();
    std::optional<V519PathScratchCoordinate> best;
    for (int32_t row = min_row; row < max_row; ++row) {
        for (int32_t column = min_column; column < max_column; ++column) {
            const V519PathScratchCoordinate candidate{column, row};
            if (!clear(candidate)) continue;
            const int32_t distance = mixed_axial_distance(candidate, source);
            if (distance < best_distance) {
                best_distance = distance;
                best = candidate;
            }
        }
    }
    return best ? NearestUnblockedResult{*best, true, true}
                : NearestUnblockedResult{source, true, false};
}

NearestUnblockedResult nearest_unblocked_source_on_demand(
    const FieldGrid& field, V519PathScratchCoordinate source, uint32_t entity_class_mask,
    uint32_t blocking_mask, uint32_t dynamic_block_mask,
    std::span<const V528PathRuntimeBlocker> blockers, int32_t moving_radius_q12,
    ScratchTerrainStateCache* terrain_cache = nullptr,
    std::optional<V519PathWorldPointQ12> actual_source = {}) {
    const int32_t width = static_cast<int32_t>(field.width() * 4U);
    const int32_t height = static_cast<int32_t>(field.height() * 4U);
    const uint32_t rejected_mask = blocking_mask | kHardBoundary;
    const auto clear = [&](V519PathScratchCoordinate cell) {
        if (cell.row < 0 || cell.column < 0 || cell.row >= height || cell.column >= width)
            return false;
        return (scratch_state_at(field, cell.row, cell.column, entity_class_mask,
            dynamic_block_mask, blockers, moving_radius_q12, terrain_cache) & rejected_mask) == 0U;
    };
    if (clear(source)) return {source, false, true};
    for (const auto& [dr, dc] : kNeighbors) {
        if (clear({source.column + dc, source.row + dr})) return {source, false, true};
    }
    const int32_t min_column = std::max(0, source.column - 16);
    const int32_t min_row = std::max(0, source.row - 16);
    const int32_t max_column = std::min(width, source.column + 16);
    const int32_t max_row = std::min(height, source.row + 16);
    int32_t best_distance = std::numeric_limits<int32_t>::max();
    std::optional<V519PathScratchCoordinate> best;
    for (int32_t row = min_row; row < max_row; ++row) {
        for (int32_t column = min_column; column < max_column; ++column) {
            const V519PathScratchCoordinate candidate{column, row};
            if (!clear(candidate)) continue;
            const int32_t distance = mixed_axial_distance(candidate, source);
            if (distance < best_distance) {
                // A clear endpoint does not guarantee an escape from an
                // overlapping group. The Lab's physical guard only admits
                // outward motion; choosing a point through another hull
                // otherwise repeats the same rejected step every solve.
                if (actual_source &&
                    (!simulation_lab_path_world_segment_clear(*actual_source,
                        v519_path_scratch_center_to_world_q12(candidate), blockers,
                        moving_radius_q12) ||
                     world_terrain_segment_rejected(field, *actual_source,
                        v519_path_scratch_center_to_world_q12(candidate),
                        entity_class_mask, blocking_mask, terrain_cache))) continue;
                best_distance = distance;
                best = candidate;
            }
        }
    }
    return best ? NearestUnblockedResult{*best, true, true}
                : NearestUnblockedResult{source, true, false};
}

uint32_t transition_cost(const ScratchCell& destination, uint32_t entity_class_mask,
                         uint32_t high_cost, uint32_t blocking_mask) noexcept {
    if ((destination.state & kHardBoundary) != 0U) return kInfiniteCost;
    uint32_t cost = 4U;
    if ((destination.state & entity_class_mask) != 0U) {
        if ((destination.state & blocking_mask) != 0U) return kInfiniteCost;
        cost = (destination.state & high_cost) != 0U ? 12U : 3U;
    }
    return cost;
}

bool origin_or_neighbor_reached(const ScratchImage& image,
                                V519PathScratchCoordinate origin) noexcept {
    if (!image.contains(origin.row, origin.column)) return false;
    const auto index = static_cast<int64_t>(image.index(origin.row, origin.column));
    if (image.cells[index].cost < kInfiniteCost) return true;
    for (const auto& [dr, dc] : kNeighbors) {
        const int64_t neighbor = index + int64_t{dr} * image.width + dc;
        if (image.contains_index(neighbor) && image.cells[neighbor].cost < kInfiniteCost)
            return true;
    }
    return false;
}

uint32_t propagate_costs(ScratchImage& image, V519PathScratchCoordinate origin,
                         V519PathScratchCoordinate destination, uint32_t entity_class_mask,
                         uint32_t high_cost, uint32_t blocking_mask,
                         uint32_t remaining_passes = 6U) {
    constexpr std::size_t kRetailQueueEntries = 0x180000U / 4U; // 32-bit pointer queue.
    constexpr std::size_t kRetailPassBoundaryEntries = 0x10000U;
    for (auto& cell : image.cells) {
        cell.state &= ~1U; // GridScratch_ResetTraversalFlagsAndCosts clears traversal bit zero.
        cell.cost = kInfiniteCost;
    }
    if (!image.contains(destination.row, destination.column) || remaining_passes == 0U) return 0U;

    std::vector<std::size_t> queue;
    queue.reserve(std::min(kRetailQueueEntries, image.cells.size() * 4U));
    const std::size_t destination_index = image.index(destination.row, destination.column);
    image.cells[destination_index].cost = 0U;
    queue.push_back(destination_index);
    std::size_t read_cursor = 0U;
    std::size_t pass_boundary = kRetailPassBoundaryEntries;
    uint32_t expanded = 0U;

    while (read_cursor < queue.size()) {
        if (read_cursor >= pass_boundary) {
            pass_boundary += kRetailPassBoundaryEntries;
            if (origin_or_neighbor_reached(image, origin)) break;
            if (--remaining_passes == 0U) break;
        }

        const std::size_t current_index = queue[read_cursor++];
        auto& current = image.cells[current_index];
        if ((current.state & 1U) != 0U) continue;
        ++expanded;
        const uint32_t base_cost = current.cost;
        for (const auto& [dr, dc] : kPropagationNeighbors) {
            const int64_t index = static_cast<int64_t>(current_index) + int64_t{dr} * image.width + dc;
            if (!image.contains_index(index)) continue;
            auto& neighbor = image.cells[index];
            const uint32_t step = transition_cost(neighbor, entity_class_mask, high_cost, blocking_mask);
            if (step == kInfiniteCost || base_cost > kInfiniteCost - step) continue;
            const uint32_t next = base_cost + step;
            if (next < neighbor.cost && queue.size() < kRetailQueueEntries) {
                neighbor.cost = next;
                queue.push_back(static_cast<std::size_t>(index));
            }
        }
    }
    return expanded;
}

std::vector<uint8_t> reachable_from_source(const ScratchImage& image,
                                           V519PathScratchCoordinate source,
                                           uint32_t entity_class_mask,
                                           uint32_t high_cost,
                                           uint32_t blocking_mask) {
    std::vector<uint8_t> reachable(image.cells.size(), 0U);
    if (!image.contains(source.row, source.column)) return reachable;
    const auto source_index = image.index(source.row, source.column);
    // The retail region marker seeds the source record unconditionally, then applies the verified
    // finite-cost/hard-boundary/(class&blocking) predicate while expanding adjacent records.
    reachable[source_index] = 1U;
    std::queue<V519PathScratchCoordinate> queue;
    queue.push(source);
    while (!queue.empty()) {
        const auto cell = queue.front();
        queue.pop();
        for (const auto& [dr, dc] : kNeighbors) {
            const int32_t nr = cell.row + dr;
            const int32_t nc = cell.column + dc;
            if (!image.contains(nr, nc)) continue;
            const auto ni = image.index(nr, nc);
            if (reachable[ni] != 0U) continue;
            const auto& candidate = image.cells[ni];
            if (candidate.cost < kInfiniteCost || (candidate.state & kHardBoundary) != 0U) continue;
            if ((candidate.state & entity_class_mask) != 0U &&
                (candidate.state & blocking_mask) != 0U) continue;
            reachable[ni] = 1U;
            queue.push({nc, nr});
        }
    }
    (void)high_cost;
    return reachable;
}

std::optional<V519PathScratchCoordinate> unreachable_region_fallback(
    ScratchImage& image, V519PathScratchCoordinate source,
    V519PathScratchCoordinate target, uint32_t entity_class_mask, uint32_t blocking_mask) {
    if (!image.contains(source.row, source.column)) return std::nullopt;
    const auto admissible = [&](V519PathScratchCoordinate cell, bool check_visited) {
        if (!image.contains(cell.row, cell.column)) return false;
        const auto& c = image.cells[image.index(cell.row, cell.column)];
        return c.cost >= kInfiniteCost && (c.state & kHardBoundary) == 0U &&
            (!check_visited || (c.state & 1U) == 0U) &&
            !((c.state & entity_class_mask) != 0U && (c.state & blocking_mask) != 0U);
    };
    std::vector<V519PathScratchCoordinate> pending{source};
    int32_t best_distance = std::numeric_limits<int32_t>::max();
    V519PathScratchCoordinate best = source;
    bool initial = true;
    while (!pending.empty()) {
        const auto seed = pending.back(); pending.pop_back();
        // The original always admits its initial source, then filters recursive entries.
        if (!initial && !admissible(seed, true)) continue;
        initial = false;
        int32_t left = seed.column, right = seed.column;
        image.cells[image.index(seed.row, seed.column)].state |= 1U;
        while (admissible({left - 1, seed.row}, false)) {
            --left; image.cells[image.index(seed.row, left)].state |= 1U;
        }
        while (admissible({right + 1, seed.row}, false)) {
            ++right; image.cells[image.index(seed.row, right)].state |= 1U;
        }
        // 0x00534780 evaluates one target-clamped column for each horizontal span.
        // A row-major scan of every region cell chooses different equal-distance cells.
        const V519PathScratchCoordinate candidate{std::clamp(target.column, left, right), seed.row};
        const int32_t distance = mixed_axial_distance(candidate, target);
        if (distance < best_distance) { best_distance = distance; best = candidate; }
        // Reverse pushes reproduce depth-first upper-row then lower-row, left-to-right order.
        for (int32_t x = right; x >= left - 1; --x) pending.push_back({x, seed.row + 1});
        for (int32_t x = right + 1; x >= left; --x) pending.push_back({x, seed.row - 1});
    }
    return best;
}

std::optional<V519PathScratchCoordinate> lowest_cost_neighbor(const ScratchImage& image,
                                                               V519PathScratchCoordinate cell) noexcept {
    const auto current = static_cast<int64_t>(image.index(cell.row, cell.column));
    uint32_t best = image.cells[current].cost;
    std::optional<V519PathScratchCoordinate> result;
    for (const auto& [dr, dc] : kNeighbors) {
        const int64_t neighbor = current + int64_t{dr} * image.width + dc;
        if (!image.contains_index(neighbor)) continue;
        const uint32_t cost = image.cells[neighbor].cost;
        if (cost < best) {
            best = cost;
            result = V519PathScratchCoordinate{
                static_cast<int32_t>(neighbor % image.width),
                static_cast<int32_t>(neighbor / image.width)};
        }
    }
    return result;
}

std::optional<V519PathScratchCoordinate> compressed_primary(const ScratchImage& image,
                                                             V519PathScratchCoordinate source,
                                                             uint32_t entity_class_mask,
                                                             uint32_t traversal_blocking_mask,
                                                             uint32_t caller_blocking_mask) noexcept {
    V519PathScratchCoordinate scan = source;
    V519PathScratchCoordinate previous = source;
    uint32_t active_caller_mask = caller_blocking_mask;
    for (std::size_t guard = 0; guard < image.cells.size(); ++guard) {
        if ((image.cells[image.index(scan.row, scan.column)].state & caller_blocking_mask) != 0U)
            active_caller_mask = 0U;
        const auto next = lowest_cost_neighbor(image, scan);
        if (!next) {
            // 0x00534740 tests the terminal record's cost, not whether it is the source.
            // A nonzero-cost source is an explicit waypoint, not a direct-route result.
            return image.cells[image.index(scan.row, scan.column)].cost == 0U
                ? std::nullopt : std::optional{scan};
        }
        if (segment_rejected(image, source, *next, entity_class_mask,
                             traversal_blocking_mask, active_caller_mask)) {
            const auto selected = previous == source ? *next : previous;
            // Even after segment rejection, a selected zero-cost record returns carry set.
            return image.cells[image.index(selected.row, selected.column)].cost == 0U
                ? std::nullopt : std::optional{selected};
        }
        previous = *next;
        scan = *next;
        if (image.cells[image.index(scan.row, scan.column)].cost == 0U) return std::nullopt;
    }
    return std::nullopt;
}

} // namespace

void V565PathTerrainCache::reset(const FieldGrid& source) {
    prepare(source);
}

void V565PathTerrainCache::prepare(const FieldGrid& source) {
    field = &source;
    runtime_states.reset();
    states = retail_build_terrain_scratch(source).state;
}

bool v565_path_segment_clear_static_cached(const FieldGrid& field,
                                    V519PathScratchCoordinate start,
                                    V519PathScratchCoordinate end, uint32_t entity_class_mask,
                                    uint32_t caller_blocking_mask,
                                    uint32_t traversal_blocking_mask,
                                    std::span<const V528PathRuntimeBlocker> blockers,
                                    int32_t moving_radius_q12,
                                    V565PathTerrainCache& shared_terrain_cache) noexcept {
    try {
        start = v519_path_clip_to_inner_scratch(field, start);
        end = v519_path_clip_to_inner_scratch(field, end);
        const uint32_t dynamic_block_mask = traversal_blocking_mask & 0x0000FF00U;
        ScratchTerrainStateCache terrain_cache(field, blockers, moving_radius_q12,
                                               &shared_terrain_cache);
        return !segment_rejected_on_demand(field, start, end, entity_class_mask,
            traversal_blocking_mask, caller_blocking_mask, dynamic_block_mask, blockers,
            moving_radius_q12, &terrain_cache);
    } catch (...) {
        return false;
    }
}

V519PathWorldPointQ12 v565_resolve_group_route_segment(const FieldGrid& field,
    V519PathWorldPointQ12 from, V519PathWorldPointQ12 to,
    uint32_t entity_class_mask, uint32_t blocking_mask, V565PathTerrainCache& cache) {
    const auto start=v519_path_clip_to_inner_scratch(field,
        v519_path_world_to_scratch(from.x_q12,from.y_q12));
    const auto end=v519_path_clip_to_inner_scratch(field,
        v519_path_world_to_scratch(to.x_q12,to.y_q12));
    const uint32_t low_mask=blocking_mask&0x0000FF00U;
    ScratchTerrainStateCache terrain(field,{},0,&cache);
    const auto source=nearest_unblocked_source_on_demand(
        field,start,entity_class_mask,blocking_mask,low_mask,{},0,&terrain);
    if(source.relocation_required)
        return v519_path_scratch_center_to_world_q12(source.cell);
    if(segment_rejected_on_demand(field,start,end,entity_class_mask,
                                 blocking_mask,0,low_mask,{},0,&terrain))
        return v519_path_scratch_center_to_world_q12(end);
    return to;
}

bool v528_path_segment_clear_static(const FieldGrid& field, V519PathScratchCoordinate start,
                                    V519PathScratchCoordinate end, uint32_t entity_class_mask,
                                    uint32_t caller_blocking_mask,
                                    uint32_t traversal_blocking_mask,
                                    std::span<const V528PathRuntimeBlocker> blockers,
                                    int32_t moving_radius_q12) noexcept {
    try {
        start = v519_path_clip_to_inner_scratch(field, start);
        end = v519_path_clip_to_inner_scratch(field, end);
        const uint32_t dynamic_block_mask = traversal_blocking_mask & 0x0000FF00U;
        ScratchTerrainStateCache terrain_cache(field, blockers, moving_radius_q12);
        return !segment_rejected_on_demand(field, start, end, entity_class_mask,
            traversal_blocking_mask, caller_blocking_mask, dynamic_block_mask, blockers,
            moving_radius_q12, &terrain_cache);
    } catch (...) {
        return false;
    }
}

V528RouteResult v528_resolve_route(const FieldGrid& field, int32_t source_world_x_q12,
                                   int32_t source_world_y_q12, int32_t target_world_x_q12,
                                   int32_t target_world_y_q12, uint32_t entity_class_mask,
                                   uint32_t traversal_low_band_index,
                                   uint32_t traversal_high_band_index,
                                   std::span<const V528PathRuntimeBlocker> blockers,
                                   int32_t moving_radius_q12,
                                   V565PathTerrainCache* shared_terrain_cache, bool movement_locked) {
    V528RouteResult result;
    result.source_cell = v519_path_clip_to_inner_scratch(
        field, v519_path_world_to_scratch(source_world_x_q12, source_world_y_q12));
    result.routing_source_cell = result.source_cell;
    result.requested_cell = v519_path_clip_to_inner_scratch(
        field, v519_path_world_to_scratch(target_world_x_q12, target_world_y_q12));
    result.resolved_cell = result.requested_cell;
    result.resolved_world_q12 = {target_world_x_q12, target_world_y_q12};

    if (traversal_low_band_index >= 8U || traversal_high_band_index >= 8U ||
        entity_class_mask == 0U) return result;

    const uint32_t high_cost = high_cost_mask(traversal_low_band_index);
    const uint32_t low_block = low_blocking_mask(traversal_low_band_index);
    const uint32_t blocking = low_block | high_blocking_mask(traversal_high_band_index);
    // Terrain classification is immutable for one route query, while GridScratch samples each FLD
    // cell up to sixteen times. Cache the expensive slope/water classification once per owning FLD
    // cell. Routed/fallback paths then retain the retail 4x lattice without recomputing normals for
    // every scratch record.
    ScratchTerrainStateCache terrain_cache(field, blockers, moving_radius_q12, shared_terrain_cache);

    // V565 fast path: the retail source-neighborhood test and integer line walker only touch a
    // small subset of GridScratch for the overwhelmingly common direct-route case. Evaluate those
    // exact cells lazily instead of materializing field.width*4 by field.height*4 scratch state for
    // every moving unit on every 20 Hz tick. Full scratch allocation remains byte-for-byte on the
    // routed/fallback path below.
    const auto source_resolution = nearest_unblocked_source_on_demand(
        field, result.source_cell, entity_class_mask, blocking, low_block, blockers,
        moving_radius_q12, &terrain_cache);
    if (!source_resolution.found) return result;
    if (source_resolution.relocation_required) {
        result.routing_source_cell = source_resolution.cell;
        result.source_cell_relocated = true;
        result.primary_cell = source_resolution.cell;
        result.primary_world_q12 = v519_path_scratch_center_to_world_q12(source_resolution.cell);
        result.disposition = V528RouteDisposition::SourceRelocated;
        return result;
    }
    if (!segment_rejected_on_demand(field, result.source_cell, result.requested_cell,
            entity_class_mask, blocking, high_cost, low_block, blockers, moving_radius_q12,
            &terrain_cache)) {
        result.disposition = V528RouteDisposition::Direct;
        return result;
    }

    auto image = build_scratch(field, entity_class_mask, traversal_low_band_index,
                               traversal_high_band_index, blockers, moving_radius_q12,
                               &terrain_cache);
    result.expanded_nodes = propagate_costs(image, result.source_cell, result.requested_cell,
                                             entity_class_mask, high_cost, blocking, 6U);
    if (!origin_or_neighbor_reached(image, result.source_cell)) {
        // Preserve the original scanline/DFS fallback selection, including equal-distance ties.
        // The explicit stack avoids consuming the native call stack on large regions.
        const auto fallback = unreachable_region_fallback(image, result.source_cell,
            result.requested_cell, entity_class_mask, blocking);
        if (!fallback) return result;
        result.resolved_cell = *fallback;
        result.resolved_world_q12 = v519_path_scratch_center_to_world_q12(*fallback);
        result.requested_cell_retargeted = *fallback != result.requested_cell;
        // The native retarget branch retains the scratch pointer one row
        // above the source for the propagation pass-boundary admission test.
        // Backtracking still starts at the actual source afterwards.
        const V519PathScratchCoordinate retry_origin{
            result.source_cell.column,result.source_cell.row-1};
        result.expanded_nodes += propagate_costs(image, retry_origin, *fallback,
                                                 entity_class_mask, high_cost, blocking, 6U);
        // Native backtracks even when this bounded retry has not reached the
        // source neighborhood; the nearest lower-cost cell is still a waypoint.
        result.disposition = V528RouteDisposition::ReachableFallback;
    } else {
        result.disposition = V528RouteDisposition::Routed;
    }

    // Locked orders still propagate costs normally, then backtrack with
    // only the terrain and low-band blocking masks.
    result.primary_cell = compressed_primary(image, result.source_cell, entity_class_mask,
                                             blocking, movement_locked ? 0U : high_cost);
    if (result.primary_cell)
        result.primary_world_q12 = v519_path_scratch_center_to_world_q12(*result.primary_cell);
    return result;
}


bool simulation_lab_path_world_segment_clear(V519PathWorldPointQ12 from,
    V519PathWorldPointQ12 to,std::span<const V528PathRuntimeBlocker> blockers,
    int32_t moving_radius_q12) noexcept {
    if(moving_radius_q12<=0)return true;
    const double dx=double(to.x_q12)-from.x_q12,dy=double(to.y_q12)-from.y_q12;
    const double step=dx*dx+dy*dy;
    for(const auto& b:blockers){
        if(b.collision_radius_q12<=0)continue;
        const double x=double(from.x_q12)-b.world_x_q12,y=double(from.y_q12)-b.world_y_q12;
        const double radius=double(moving_radius_q12)+b.collision_radius_q12;
        if(std::min(x,x+dx)>radius||std::max(x,x+dx)<-radius||
           std::min(y,y+dy)>radius||std::max(y,y+dy)<-radius)continue;
        if(x*x+y*y<=radius*radius&&x*dx+y*dy>=0.0)continue;
        const double t=step>0?std::clamp(-(x*dx+y*dy)/step,0.0,1.0):0.0;
        if((x+t*dx)*(x+t*dx)+(y+t*dy)*(y+t*dy)<=radius*radius)return false;
    }
    return true;
}

V528RouteResult v565_resolve_route_fast_for_simulation_lab(const FieldGrid& field, int32_t source_world_x_q12,
                                   int32_t source_world_y_q12, int32_t target_world_x_q12,
                                   int32_t target_world_y_q12, uint32_t entity_class_mask,
                                   uint32_t traversal_low_band_index,
                                   uint32_t traversal_high_band_index,
                                   std::span<const V528PathRuntimeBlocker> blockers,
                                   int32_t moving_radius_q12,
                                   V565PathTerrainCache* shared_terrain_cache,
                                   bool direct_segment_already_rejected, bool movement_locked) {
    // Persistent native grids require the native propagation and tie order as well
    // as the original unknown-cell costs. A shortest-cost A* route is not equivalent.
    if (shared_terrain_cache && shared_terrain_cache->field == &field &&
        shared_terrain_cache->runtime_states &&
        shared_terrain_cache->runtime_states->size() == size_t(field.width())*field.height()*16U)
        return v528_resolve_route(field,source_world_x_q12,source_world_y_q12,
            target_world_x_q12,target_world_y_q12,entity_class_mask,
            traversal_low_band_index,traversal_high_band_index,blockers,
            moving_radius_q12,shared_terrain_cache,movement_locked);
    V528RouteResult result;
    result.source_cell = v519_path_clip_to_inner_scratch(
        field, v519_path_world_to_scratch(source_world_x_q12, source_world_y_q12));
    result.routing_source_cell = result.source_cell;
    result.requested_cell = v519_path_clip_to_inner_scratch(
        field, v519_path_world_to_scratch(target_world_x_q12, target_world_y_q12));
    result.resolved_cell = result.requested_cell;
    result.resolved_world_q12 = {target_world_x_q12, target_world_y_q12};

    if (traversal_low_band_index >= 8U || traversal_high_band_index >= 8U ||
        entity_class_mask == 0U) return result;

    const uint32_t high_cost = high_cost_mask(traversal_low_band_index);
    const uint32_t low_block = low_blocking_mask(traversal_low_band_index);
    const uint32_t blocking = low_block | high_blocking_mask(traversal_high_band_index);
    // Terrain classification is immutable for one route query, while GridScratch samples each FLD
    // cell up to sixteen times. Cache the expensive slope/water classification once per owning FLD
    // cell. Routed/fallback paths then retain the retail 4x lattice without recomputing normals for
    // every scratch record.
    ScratchTerrainStateCache terrain_cache(field, blockers, moving_radius_q12, shared_terrain_cache);

    // Simulation Lab fast path: the retail source-neighborhood test and integer line walker only touch a
    // small subset of GridScratch for the overwhelmingly common direct-route case. Evaluate those
    // exact cells lazily instead of materializing field.width*4 by field.height*4 scratch state for
    // every moving unit on every 20 Hz tick. Full scratch allocation remains byte-for-byte on the
    // routed/fallback path below.
    const auto source_resolution = nearest_unblocked_source_on_demand(
        field, result.source_cell, entity_class_mask, blocking, low_block, blockers,
        moving_radius_q12, &terrain_cache,
        V519PathWorldPointQ12{source_world_x_q12, source_world_y_q12});
    if (!source_resolution.found) return result;
    if (source_resolution.relocation_required) {
        result.routing_source_cell = source_resolution.cell;
        result.source_cell_relocated = true;
        result.primary_cell = source_resolution.cell;
        result.primary_world_q12 = v519_path_scratch_center_to_world_q12(source_resolution.cell);
        result.disposition = V528RouteDisposition::SourceRelocated;
        return result;
    }
    const V519PathWorldPointQ12 actual_source{source_world_x_q12,source_world_y_q12};
    const auto requested_state=scratch_state_at(field,result.requested_cell.row,result.requested_cell.column,
        entity_class_mask,low_block,blockers,moving_radius_q12,&terrain_cache);
    const V519PathWorldPointQ12 requested_point{target_world_x_q12,target_world_y_q12};
    const bool requested_world_blocked=std::any_of(blockers.begin(),blockers.end(),[&](const auto& b){
        return within_expanded_radius(requested_point,b,moving_radius_q12);
    });
    if((requested_state&(kHardBoundary|blocking))||requested_world_blocked){
        // Reverse propagation seeds its destination unconditionally. That retail
        // convention cannot make an immutable Lab obstacle a reachable endpoint.
        // Resolve the closest admitted point in the source-connected region first.
        auto image=build_scratch(field,entity_class_mask,traversal_low_band_index,
            traversal_high_band_index,blockers,moving_radius_q12,&terrain_cache);
        const auto fallback=unreachable_region_fallback(image,result.source_cell,
            result.requested_cell,entity_class_mask,blocking);
        if(!fallback)return result;
        const auto state=image.cells[image.index(fallback->row,fallback->column)].state;
        if(state&(kHardBoundary|blocking))return result;
        result.resolved_cell=*fallback;
        result.resolved_world_q12=v519_path_scratch_center_to_world_q12(*fallback);
        result.requested_cell_retargeted=true; // World point may change within the same cell.
        target_world_x_q12=result.resolved_world_q12.x_q12;
        target_world_y_q12=result.resolved_world_q12.y_q12;
    }
    if (!direct_segment_already_rejected &&
        simulation_lab_path_world_segment_clear(actual_source,{target_world_x_q12,target_world_y_q12},blockers,moving_radius_q12) &&
        !world_terrain_segment_rejected(field,actual_source,{target_world_x_q12,target_world_y_q12},
            entity_class_mask,blocking,&terrain_cache) &&
        !segment_rejected_on_demand(field, result.source_cell, result.resolved_cell,
            entity_class_mask, blocking, high_cost, low_block, blockers, moving_radius_q12,
            &terrain_cache)) {
        result.disposition = result.requested_cell_retargeted?V528RouteDisposition::ReachableFallback:V528RouteDisposition::Direct;
        return result;
    }

    // Simulation Lab uses an exact shortest-cost A* execution over the recovered six-neighbor lattice,
    // transition costs, hard boundaries, terrain bands and dynamic blockers. This deliberately does
    // not claim the retail FIFO queue/tie-order of v528_resolve_route; it is the bounded-latency Lab
    // execution path. The public V528 resolver below remains the machine-backed FIFO implementation.
    const auto fast_route = find_routed_path_on_demand(field, result.source_cell,
        result.resolved_cell, entity_class_mask, high_cost, blocking, low_block, blockers,
        moving_radius_q12, &terrain_cache,actual_source);
    result.expanded_nodes = fast_route.expanded_nodes;
    if (!fast_route.cells.empty()) {
        result.disposition = result.requested_cell_retargeted?V528RouteDisposition::ReachableFallback:V528RouteDisposition::Routed;
        result.primary_cell = compressed_primary_on_demand(field, fast_route.cells,
            entity_class_mask, blocking, high_cost, low_block, blockers, moving_radius_q12,
            &terrain_cache,actual_source);
        if (!result.primary_cell && fast_route.cells.size() > 1U)
            result.primary_cell = fast_route.cells[1U];
        if (result.primary_cell)
            result.primary_world_q12 = v519_path_scratch_center_to_world_q12(*result.primary_cell);
        return result;
    }

    if(result.requested_cell_retargeted)return V528RouteResult{};

    // Preserve the recovered reachable-fallback behavior when no path reaches the source region.
    // This path is uncommon in active movement; it remains the exact retail-style flood/region
    // implementation rather than inventing a different retarget policy.
    auto image = build_scratch(field, entity_class_mask, traversal_low_band_index,
                               traversal_high_band_index, blockers, moving_radius_q12,
                               &terrain_cache);
    result.expanded_nodes += propagate_costs(image, result.source_cell, result.requested_cell,
                                              entity_class_mask, high_cost, blocking, 6U);
    if (!origin_or_neighbor_reached(image, result.source_cell)) {
        // Preserve the original scanline/DFS fallback selection, including equal-distance ties.
        // The explicit stack avoids consuming the native call stack on large regions.
        const auto fallback = unreachable_region_fallback(image, result.source_cell,
            result.requested_cell, entity_class_mask, blocking);
        if (!fallback) return result;
        result.resolved_cell = *fallback;
        result.resolved_world_q12 = v519_path_scratch_center_to_world_q12(*fallback);
        result.requested_cell_retargeted = *fallback != result.requested_cell;
        // The native retarget branch retains the scratch pointer one row
        // above the source for the propagation pass-boundary admission test.
        // Backtracking still starts at the actual source afterwards.
        const V519PathScratchCoordinate retry_origin{
            result.source_cell.column,result.source_cell.row-1};
        result.expanded_nodes += propagate_costs(image, retry_origin, *fallback,
                                                 entity_class_mask, high_cost, blocking, 6U);
        // Native backtracks even when this bounded retry has not reached the
        // source neighborhood; the nearest lower-cost cell is still a waypoint.
        result.disposition = V528RouteDisposition::ReachableFallback;
    } else {
        result.disposition = result.requested_cell_retargeted?V528RouteDisposition::ReachableFallback:V528RouteDisposition::Routed;
    }

    result.primary_cell = compressed_primary(image, result.source_cell, entity_class_mask,
                                             blocking, high_cost);
    if (result.primary_cell)
        result.primary_world_q12 = v519_path_scratch_center_to_world_q12(*result.primary_cell);
    return result;
}

const char* v528_route_disposition_name(V528RouteDisposition disposition) noexcept {
    switch (disposition) {
        case V528RouteDisposition::Direct: return "direct";
        case V528RouteDisposition::Routed: return "routed";
        case V528RouteDisposition::SourceRelocated: return "source relocation";
        case V528RouteDisposition::ReachableFallback: return "reachable fallback";
        case V528RouteDisposition::Blocked: return "blocked";
    }
    return "unknown";
}

} // namespace thandor
