#include "thandor/retail_placement.hpp"
#include "thandor/retail_presence.hpp"
#include "thandor/simulation_lab.hpp"

namespace thandor {
SimulationLabRetailScratchImage retail_build_terrain_scratch(const FieldGrid& field) {
    SimulationLabRetailScratchImage result;
    result.width = field.width() * 4U;
    result.height = field.height() * 4U;
    result.state.resize(static_cast<std::size_t>(result.width) * result.height);
    const auto write = [&](std::uint32_t base_x, std::uint32_t base_y,
                           std::int32_t dx, std::int32_t dy, std::uint32_t bits) {
        const auto xx = static_cast<std::int64_t>(base_x) + dx;
        const auto yy = static_cast<std::int64_t>(base_y) + dy;
        if (xx < 0 || yy < 0 || xx >= result.width || yy >= result.height) return;
        result.state[static_cast<std::size_t>(yy)*result.width +
                     static_cast<std::size_t>(xx)] |= bits;
    };
    for (std::uint32_t y=0;y<field.height();++y) {
        for (std::uint32_t x=0;x<field.width();++x) {
            auto bits=field.terrain_compatibility(x,y).classification_bits;
            // Runtime initialization adds the four map-edge flags even when
            // an editable/synthetic FLD has not been normalized for export.
            if (x==0U || y==0U || x+1U==field.width() || y+1U==field.height())
                bits |= 0x80000000U;
            const auto flags=field.at(x,y).flags_and_material;
            const auto base_x=x*4U;
            const auto base_y=y*4U;
            const bool first_row=y==0U || (flags&0x4000U)!=0U,
                       last_row=y+1U==field.height() || (flags&0x80000000U)!=0U;
            const bool first_column=x==0U || (flags&0x2000U)!=0U,
                       last_column=x+1U==field.width() || (flags&0x08000000U)!=0U;
            // Port of the offset writes in scratch_grid.cpp; unlike a block
            // replication, this includes the asymmetric two-sample halo.
            if (!first_row) {
                write(base_x,base_y,2,-2,bits); write(base_x,base_y,3,-2,bits);
                for (int dx=1;dx<=3;++dx) write(base_x,base_y,dx,-1,bits);
                if (!last_column) {
                    write(base_x,base_y,4,-2,bits);
                    write(base_x,base_y,4,-1,bits);
                    write(base_x,base_y,5,-1,bits);
                }
            }
            if (!last_column) {
                write(base_x,base_y,4,0,bits); write(base_x,base_y,5,0,bits);
                write(base_x,base_y,4,1,bits); write(base_x,base_y,5,1,bits);
                write(base_x,base_y,4,2,bits);
            }
            for (int dx=0;dx<4;++dx) write(base_x,base_y,dx,0,bits);
            if (!first_column) {
                write(base_x,base_y,-1,1,bits);
                write(base_x,base_y,-1,2,bits); write(base_x,base_y,-2,2,bits);
                write(base_x,base_y,-1,3,bits); write(base_x,base_y,-2,3,bits);
            }
            for (int dx=0;dx<4;++dx) {
                write(base_x,base_y,dx,1,bits);
                write(base_x,base_y,dx,2,bits);
                write(base_x,base_y,dx,3,bits);
            }
            if (!last_row) {
                for (int dx=0;dx<3;++dx) write(base_x,base_y,dx,4,bits);
                write(base_x,base_y,0,5,bits); write(base_x,base_y,1,5,bits);
                if (!first_column) {
                    write(base_x,base_y,-1,4,bits); write(base_x,base_y,-2,4,bits);
                    write(base_x,base_y,-1,5,bits);
                }
            }
        }
    }
    // Literal one-FLD-cell dilation of terrain class bit24; the original
    // promotes visited markers only in a second pass, never while scanning.
    std::vector<std::uint8_t> promotion(result.state.size());
    constexpr std::uint32_t blocked=0x80000000U, class24=0x01000000U;
    for (std::uint32_t y=4U;y+4U<result.height;++y) {
        for (std::uint32_t x=4U;x+4U<result.width;++x) {
            const auto index=static_cast<std::size_t>(y)*result.width+x;
            if ((result.state[index]&blocked)!=0U) continue;
            const auto has24=[&](std::uint32_t px,std::uint32_t py) {
                return (result.read(px,py)&class24)!=0U;
            };
            if (has24(x,y) || has24(x,y-4U) || has24(x+4U,y-4U) ||
                has24(x-4U,y) || has24(x+4U,y) ||
                has24(x-4U,y+4U) || has24(x,y+4U)) promotion[index]=1U;
        }
    }
    for (std::size_t i=0;i<result.state.size();++i)
        if (promotion[i]!=0U) result.state[i] |= class24;
    return result;
}

// The native scanline fill scans one extra cell at the upper-right and
// lower-left span endpoints. Its connected set is the six-neighbour skewed
// lattice, including those two diagonals. Four-neighbour fills split legal
// corridors and incorrectly promote them to unreachable terrain classes.
void retail_flood_reachable_scratch(SimulationLabRetailScratchImage& scratch,
                                 std::size_t start, std::uint32_t wall_mask,
                                 std::vector<std::size_t>& queue) {
    constexpr std::uint32_t visited=0x00000001U, blocked=0x80000000U;
    if (start>=scratch.state.size() ||
        (scratch.state[start]&(blocked|visited))!=0U) return;
    queue.clear();
    scratch.state[start] |= visited;
    queue.push_back(start);
    for (std::size_t head=0;head<queue.size();++head) {
        const auto index=queue[head];
        const auto x=index%scratch.width;
        const auto y=index/scratch.width;
        const auto visit=[&](std::size_t neighbour) {
            if ((scratch.state[neighbour]&wall_mask)!=0U) return;
            scratch.state[neighbour] |= visited;
            queue.push_back(neighbour);
        };
        if (x>0U) visit(index-1U);
        if (x+1U<scratch.width) visit(index+1U);
        if (y>0U) {
            visit(index-scratch.width);
            if (x+1U<scratch.width) visit(index-scratch.width+1U);
        }
        if (y+1U<scratch.height) {
            visit(index+scratch.width);
            if (x>0U) visit(index+scratch.width-1U);
        }
    }
}

// Native DWORD indexing puts the final lower-row sample at column +1,
// not -2. A single wrong fringe cell changed retained faction admission.
void retail_propagate_field_presence_into_scratch(SimulationLabRetailScratchImage& scratch,
                                               const FieldGrid& field) {
    if (scratch.field_presence_by_faction.size()!=
        static_cast<std::size_t>(field.width())*field.height()) return;
    const auto width=static_cast<std::int64_t>(scratch.width);
    const std::array<std::int64_t,42> offsets{{
        -2*width+2,-2*width+3,-width+1,-width+2,-width+3,-2*width+4,
        -width+4,-width+5,4,5,width+4,width+5,2*width+4,
        0,1,2,3,
        width-1,2*width-1,2*width-2,3*width-1,3*width-2,
        width,width+1,width+2,width+3,
        2*width,2*width+1,2*width+2,2*width+3,
        3*width,3*width+1,3*width+2,3*width+3,
        4*width,4*width+1,4*width+2,5*width,
        4*width-1,4*width-2,5*width-1,5*width+1}};
    for (std::uint32_t y=1;y+1<field.height();++y) {
        for (std::uint32_t x=1;x+1<field.width();++x) {
            if ((field.at(x,y).flags_and_material&0x88006000U)!=0U) continue;
            const auto mask=scratch.field_presence_by_faction[
                static_cast<std::size_t>(y)*field.width()+x];
            if (mask==0U) continue;
            const auto base=static_cast<std::int64_t>(y*4U)*scratch.width+x*4U;
            for (const auto offset:offsets) {
                const auto index=base+offset;
                if (index>=0 && static_cast<std::uint64_t>(index)<scratch.state.size())
                    scratch.state[static_cast<std::size_t>(index)]|=mask;
            }
        }
    }
}

} // namespace thandor
