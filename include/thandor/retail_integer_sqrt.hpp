#pragma once
#include <bit>
#include <cstdint>
namespace thandor {
// Original FixedMath_UInt64Sqrt: three Newton updates. This is intentionally
// not a corrected floor root (for example, sqrt(8) returns 3).
constexpr uint32_t retail_integer_sqrt(uint64_t value) noexcept {
    if(!value)return 0;
    const unsigned shift=std::bit_width(value)/2U;
    if(shift>=31U){
        uint64_t root=uint64_t{1}<<shift;
        for(int i=0;i<3;++i)root=(root+value/root)>>1;
        return uint32_t(root);
    }
    uint32_t root=1U<<shift;
    for(int i=0;i<3;++i)root=(root+uint32_t(value/root))>>1;
    return root;
}
}
