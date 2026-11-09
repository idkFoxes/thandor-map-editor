#include "thandor/retail_placement.hpp"
#include "thandor/simulation_lab.hpp"
#include <bit>
namespace thandor {
std::optional<size_t> retail_placement_scratch_index(const SimulationLabRetailScratchImage& scratch,MovementPoint p) noexcept {
    const auto row_term=uint32_t((int64_t(p.y)*-0x20C8CC)>>21);
    const auto col_term=uint32_t((int64_t(p.x)*0x1C6E9C)>>20);
    const auto col=std::bit_cast<int32_t>(col_term-row_term+0x800U)>>10;
    const auto row=std::bit_cast<int32_t>(row_term*2U+0x800U)>>10;
    if(col<0||row<0||uint32_t(col)>=scratch.width||uint32_t(row)>=scratch.height)return {};
    return size_t(row)*scratch.width+size_t(col);
}
std::uint32_t lab_grid_influence_ring_mask(
    std::uint32_t squared_distance,
    const std::array<std::uint32_t,8>& squared_thresholds) noexcept {
    std::uint32_t mask=0U;
    for (std::size_t index=0U;index<squared_thresholds.size();++index)
        if (squared_distance<=squared_thresholds[index]) mask|=1U<<index;
    return mask;
}

// In the x86 implementation all squared distances, threshold values and the
// sum are dwords; preserve modulo-2^32 arithmetic in diagnostic maps too.
std::uint32_t lab_grid_influence_sqdist(std::int64_t dx,std::int64_t dy) noexcept {
    const auto x=static_cast<std::uint32_t>(dx);
    const auto y=static_cast<std::uint32_t>(dy);
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(x)*x+
                                      static_cast<std::uint64_t>(y)*y);
}

void retail_visit_grid_influence(uint32_t width, std::span<const uint32_t> terrain,
    MovementPoint position, int32_t radius, uint32_t runtime_class,
    const std::function<void(size_t,uint32_t)>& write) {
    if (width == 0 || terrain.size() % width != 0) return;
    const auto coordinate = v519_path_world_to_scratch(position.x, position.y);
    const auto height = terrain.size() / width;
    if (coordinate.column < 0 || coordinate.row < 0 ||
        uint32_t(coordinate.column) >= width || uint32_t(coordinate.row) >= height) return;
    const size_t projected = size_t(coordinate.row)*width+uint32_t(coordinate.column);
    // Recovered 11-column army callback table, column gridInfluenceAdd.
    // Classes 5..9 and 21 never stamp the primary scratch grid.
    const bool low=runtime_class==4U || (runtime_class>=10U && runtime_class<=16U) ||
                   runtime_class==20U || runtime_class==22U;
    const bool high=runtime_class<=3U || (runtime_class>=17U && runtime_class<=19U) ||
                    runtime_class==23U;
    if (!low && !high) return;
    // The recovered ModelDefinition +0xDC footprintRadius is populated from
    // footprintRadiusClass and shared with the grid, collision and placement
    // callbacks. The Lab already carries MDL +0xDC in this motion field.
    if (radius<=0) return;
    if ((terrain[projected]&0x80000000U)!=0U) return;
    constexpr std::array<std::uint32_t,8> offsets{
        1000U,1250U,1500U,1600U,1920U,2240U,2600U,4100U};
    std::array<std::uint32_t,8> thresholds{};
    for (std::size_t index=0U;index<thresholds.size();++index) {
        const auto extent=static_cast<std::uint32_t>(radius)+499U+offsets[index];
        thresholds[index]=static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(extent)*extent);
    }
    const auto center_world_x=static_cast<std::int64_t>(position.x);
    const auto center_world_y=static_cast<std::int64_t>(position.y);
    const auto grid_y=static_cast<std::int64_t>(projected/width);
    const auto grid_x=static_cast<std::int64_t>(projected%width);
    const auto row_q12=grid_y*0x400-0x600;
    const auto cell_world_x=((row_q12+(grid_x*0x400-0x600)*2)*0x901) >> 13;
    const auto cell_world_y=(row_q12*static_cast<std::int64_t>(-1999)) >> 12;
    // Reference walkers each write their current cell, step +/-2 scratch rows
    // and +/-1 linear cell, then check the NEXT cell's blocked bit before
    // declaring that the current cell counted towards the walk result.
    const auto walk=[&](std::int64_t start_index,std::int64_t world_x,
                        std::int64_t world_y,bool upwards)->std::uint32_t {
        auto index=start_index;
        const auto dx=world_x-center_world_x;
        auto dy=world_y-center_world_y;
        std::uint32_t touched=0U;
        for (;;) {
            const auto mask=lab_grid_influence_ring_mask(
                lab_grid_influence_sqdist(dx,dy),thresholds);
            if (mask==0U || index<0 ||
                static_cast<std::uint64_t>(index)>=terrain.size()) return touched;
            write(static_cast<std::size_t>(index), mask<<(low?8U:16U));
            world_y += upwards?999:-999;
            dy=world_y-center_world_y;
            index += upwards?(-2*static_cast<std::int64_t>(width)+1):
                             (2*static_cast<std::int64_t>(width)-1);
            if (index<0 || static_cast<std::uint64_t>(index)>=terrain.size() ||
                (terrain[static_cast<std::size_t>(index)]&0x80000000U)!=0U)
                return touched;
            ++touched;
        }
    };
    const auto origin=static_cast<std::int64_t>(projected);
    // Centre row: left, then right, calling the down walker only if the up
    // walker reached at least one unblocked successor (retail short circuit).
    for (std::int64_t step=0;;++step) {
        const auto point=origin-step;
        if (point<0 || static_cast<std::uint64_t>(point)>=terrain.size() ||
            !walk(point,cell_world_x-step*0x240,cell_world_y,true) ||
            !walk(point,cell_world_x-step*0x240,cell_world_y,false)) break;
    }
    for (std::int64_t step=0;;++step) {
        const auto point=origin+step;
        if (point<0 || static_cast<std::uint64_t>(point)>=terrain.size() ||
            !walk(point,cell_world_x+step*0x240,cell_world_y,true) ||
            !walk(point,cell_world_x+step*0x240,cell_world_y,false)) break;
    }
    // One row above: two upward-only walks, one below: two downward-only.
    const auto stride=static_cast<std::int64_t>(width);
    for (std::int64_t step=0;;++step) {
        const auto index=origin-stride-step;
        if (index<0 || static_cast<std::uint64_t>(index)>=terrain.size() ||
            !walk(index,cell_world_x-0x120-step*0x240,cell_world_y+499,true)) break;
    }
    for (std::int64_t step=0;;++step) {
        const auto index=origin-stride+1+step;
        if (index<0 || static_cast<std::uint64_t>(index)>=terrain.size() ||
            !walk(index,cell_world_x+0x120+step*0x240,cell_world_y+499,true)) break;
    }
    for (std::int64_t step=0;;++step) {
        const auto index=origin+stride+step;
        if (index<0 || static_cast<std::uint64_t>(index)>=terrain.size() ||
            !walk(index,cell_world_x+0x120+step*0x240,cell_world_y-500,false)) break;
    }
    for (std::int64_t step=0;;++step) {
        const auto index=origin+stride-1-step;
        if (index<0 || static_cast<std::uint64_t>(index)>=terrain.size() ||
            !walk(index,cell_world_x-0x120-step*0x240,cell_world_y-500,false)) break;
    }
}

void retail_placement_stamp_influence(SimulationLabRetailScratchImage& scratch,
    MovementPoint position, int32_t radius, uint32_t runtime_class) {
    retail_visit_grid_influence(scratch.width,scratch.state,position,radius,runtime_class,
        [&](size_t index,uint32_t bits){scratch.state[index] |= bits;});
}

}
