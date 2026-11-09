#include "thandor/simulation_lab_workspace05_score.hpp"
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace thandor;
int main(){
    std::array<std::uint32_t,128> p{};
    p[0x21]=1000;p[0x22]=200;p[0x24]=300;
    p[0x25]=4;p[0x26]=2;p[0x28]=3;
    assert(simulation_lab_workspace05_native_score(100,50,80,p)==3940);
    assert(simulation_lab_workspace05_native_score(5000,1000,1000,p)==1300);
    // A valid large coefficient wraps 32-bit and may become negative.
    p[0x25]=0x70000000U;
    const auto expected=std::bit_cast<std::int32_t>(
        std::uint32_t(900U*0x70000000U+80U*3U+50U*2U));
    assert(simulation_lab_workspace05_native_score(100,50,80,p)==expected);
    // Different from a 64-bit clamped or saturated score.
    assert(expected!=INT32_MAX);
    // Native comparison with signed caps (not uint64 min on high-bit cap).
    p[0x25]=0;p[0x22]=0xFFFFFFFFU;
    assert(simulation_lab_workspace05_native_score(100,200,80,p)==
        std::bit_cast<std::int32_t>(std::uint32_t(80U*3U+0xFFFFFFFEU)));
    std::cout<<"V741 workspace05 native score PASS\n";
}
