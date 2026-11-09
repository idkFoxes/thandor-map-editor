#pragma once
#include "thandor/field.hpp"
#include <bit>
namespace thandor {
// Native world-job phases 1/5. Every source observes preceding in-place updates.
inline bool retail_relax_water_surface(FieldGrid& field,bool reverse) noexcept {
    const auto width=field.width(),height=field.height();
    if(width<3U||height<3U)return false;
    auto cells=field.cells();bool changed=false;
    const auto pull=[&](size_t index,int32_t surface){auto& c=cells[index];
        if(c.flags_and_material&0x20000000U)return;
        const auto difference=std::bit_cast<int32_t>(uint32_t(c.water_delta_q12)+uint32_t(c.terrain_height_q12)-uint32_t(surface));
        const auto next=std::bit_cast<int32_t>(uint32_t(c.water_delta_q12)-uint32_t(difference>>3));
        changed|=next!=c.water_delta_q12;c.water_delta_q12=next;
    };
    for(uint32_t row=1;row+1U<height;++row)for(uint32_t column=1;column+1U<width;++column){
        const auto y=reverse?height-1U-row:row,x=reverse?width-1U-column:column;
        const size_t i=size_t(y)*width+x;const auto& source=cells[i];
        if(source.water_delta_q12<0||(source.flags_and_material&0x40000000U))continue;
        const auto surface=std::bit_cast<int32_t>(uint32_t(source.water_delta_q12)+uint32_t(source.terrain_height_q12));
        pull(i-width,surface);pull(i+1U-width,surface);pull(i+width,surface);
        pull(i+width-1U,surface);pull(i-1U,surface);pull(i+1U,surface);
    }
    return changed;
}
} // namespace thandor
