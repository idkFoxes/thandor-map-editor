#include "thandor/simulation.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

int main(){
    struct V{std::int32_t phase,coeff;std::uint32_t pitch;};
    constexpr std::array<V,10> vectors{{
        {0,-256,0x4000U},{4096,-256,0x4512U},{-4096,-256,0x3AEFU},
        {8192,-128,0x4512U},{-8192,-128,0x3AEFU},{4096,128,0x3D75U},
        {-4096,128,0x428CU},{4660,-564,0x4C61U},{-9029,291,0x4C5FU},
        {28672,-64,0x48C7U}
    }};
    for(const auto& v:vectors)
        assert((thandor::simulation_class21_flight_pitch_turn16(v.phase,v.coeff)&0xffffU)==v.pitch);
    std::cout<<"V545_CLASS21_PITCH PASS\n";
}
