#pragma once
#include "thandor/field.hpp"
#include "thandor/pioneer_class14_placement.hpp"
#include <array>
#include <optional>
#include <functional>
#include <span>
namespace thandor {
struct SimulationLabRetailScratchImage;
// Shared staggered terrain expansion and class-24 dilation from scratch_grid.cpp.
SimulationLabRetailScratchImage retail_build_terrain_scratch(const FieldGrid& field);
// Connected-set equivalent of native GridScratch_FloodFillConnectedCells.
// The start cell ignores class-wall bits, but still respects blocked/visited.
void retail_flood_reachable_scratch(SimulationLabRetailScratchImage& scratch,
    size_t start,uint32_t wall_mask,std::vector<size_t>& queue);
// Visits the original influence walker without copying a full scratch image per route.
void retail_visit_grid_influence(uint32_t width, std::span<const uint32_t> terrain,
    MovementPoint position, int32_t radius, uint32_t runtime_class,
    const std::function<void(size_t,uint32_t)>& write);
// Original precomputed sine table, reconstructed with the original decimal pi.
std::optional<size_t> retail_placement_scratch_index(const SimulationLabRetailScratchImage& scratch,MovementPoint p) noexcept;
void retail_placement_stamp_influence(SimulationLabRetailScratchImage& scratch,MovementPoint position,int32_t radius,uint32_t runtime_class);
int32_t retail_placement_sine_table_entry(uint32_t table_index);
MovementPoint retail_placement_anchor(MovementPoint position,uint32_t heading,MovementPoint local);
uint32_t retail_placement_normal_angles(const FieldGrid& field,uint32_t x,uint32_t y,bool secondary);
int32_t retail_placement_height_q12(const FieldGrid& field,MovementPoint point,int32_t contact_kind);
// Native primary-scratch side effects are retained; the caller owns a detached image.
// Invalid editable grids are bounded rather than reproducing native out-of-bounds access.
bool retail_placement_region_split(SimulationLabRetailScratchImage& scratch,uint32_t radius,MovementPoint point);
}
