#include "thandor/simulation_lab_effect_terrain.hpp"
#include <stdexcept>
#include <iostream>
using namespace thandor;
int main() {
    constexpr auto tint=0xAAC06030U;
    const auto present=simulation_lab_resolve_effect_terrain_state(0x8U,1U,tint,128U);
    if(present.runtime_flags!=0x14U || present.resolved_mask!=0x8U ||
       present.tint_argb!=retail_modulate_argb_mmx(0x80FFFFFFU,tint))
        throw std::runtime_error("visible effect modulation or flags");
    const auto historic=simulation_lab_resolve_effect_terrain_state(0x4U,1U,tint,255U);
    if(historic.runtime_flags!=0x10U || historic.tint_argb!=0x00000000U)
        throw std::runtime_error("effect should not appear solely from history");
    const auto hidden=simulation_lab_resolve_effect_terrain_state(0x8U,2U,tint,255U);
    if(hidden.tint_argb!=retail_ui_state_tint_argb(hidden.runtime_flags))
        throw std::runtime_error("foreign faction hidden tint");
    const auto zero=simulation_lab_resolve_effect_terrain_state(0x8U,1U,tint,0U);
    if((zero.tint_argb&0xFF000000U)!=0U)throw std::runtime_error("alpha fade ignored");
    std::cout<<"V738 EFFECT TERRAIN PASS\n";
}
