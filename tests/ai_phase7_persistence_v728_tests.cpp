#include "thandor/simulation_lab_phase7_occupancy.hpp"
#include <cassert>
#include <cstdint>
#include <initializer_list>
using namespace thandor;

int main() {
    constexpr auto slot=[](std::uint64_t mask,std::uint32_t faction) {
        return static_cast<std::uint8_t>(mask>>(faction*8U));
    };
    // Native clear retains persistent fog in every slot, even without a
    // corresponding Lab-explored-history bit this phase.
    const std::uint64_t previous=(0xC5ULL<<8U)|(0xBFULL<<16U)|0xFFULL;
    auto mask=simulation_lab_phase7_merge_occupancy_mask(previous,0,0,0,0);
    assert(slot(mask,0)==0x80U && slot(mask,1)==0x80U && slot(mask,2)==0x80U);
    assert(simulation_lab_phase7_present_factions(mask)==((1U<<1U)|(1U<<2U)));
    // Historic bit 7 is part of retail 0xF9, not just visible presence.
    // Projected fog reconstructs current bits; *old* transient occupancy
    // must not survive phase 7 once those callbacks stop emitting it.
    mask=simulation_lab_phase7_merge_occupancy_mask(previous,0x08ULL<<16U,0,0,0);
    assert(slot(mask,1)==0x80U && slot(mask,2)==0x88U);
    assert(simulation_lab_phase7_present_factions(mask)==((1U<<1U)|(1U<<2U)));
    // A sector bit-1 writer cannot claim an occupied AI-site presence.
    mask=simulation_lab_phase7_merge_occupancy_mask(0,0,1U<<3U,0,0);
    assert(slot(mask,3)==0x02U && simulation_lab_phase7_present_factions(mask)==0U);
    // A new history bit augments retained bit-7, and synthetic build-radius
    // bit 0 is an independent compatibility-only current-presence mark.
    mask=simulation_lab_phase7_merge_occupancy_mask(0,0,0,1U<<4U,1U<<5U);
    assert(slot(mask,4)==0x01U && slot(mask,5)==0x80U);
    assert(simulation_lab_phase7_present_factions(mask)==((1U<<4U)|(1U<<5U)));
    // Stock projected marker high bits 0x88, 0x90 and 0xC0 are written
    // directly by TerrainProjectedOcclusion, including bit 7.
    for (const auto marker : {0x88U,0x90U,0xC0U}) {
        const auto projected=static_cast<std::uint64_t>(marker)<<24U;
        mask=simulation_lab_phase7_merge_occupancy_mask(0,projected,0,0,0);
        assert(slot(mask,3)==marker);
        assert(simulation_lab_phase7_present_factions(mask)==(1U<<3U));
    }
    // The merger must not erase other faction slots or fabricate presence.
    mask=simulation_lab_phase7_merge_occupancy_mask(0x80ULL<<56U,0x10ULL<<48U,0,0,0);
    assert(slot(mask,7)==0x80U && slot(mask,6)==0x10U);
    assert(simulation_lab_phase7_present_factions(mask)==((1U<<6U)|(1U<<7U)));
    // Differential check against a literal per-byte rendition of retail
    // clear + callbacks. Use reproducible input masks across all eight slots.
    std::uint64_t state=0x4d595df4d0f33173ULL;
    const auto random=[&]() {
        state ^= state<<13U;
        state ^= state>>7U;
        state ^= state<<17U;
        return state;
    };
    for (int trial=0;trial<20000;++trial) {
        const auto old=random(), projection=random();
        const auto sectors=static_cast<std::uint8_t>(random());
        const auto fallback=static_cast<std::uint8_t>(random());
        const auto history=static_cast<std::uint8_t>(random());
        std::uint64_t expected=0U;
        std::uint8_t expected_presence=0U;
        for (std::uint32_t faction=0U;faction<8U;++faction) {
            const auto shift=faction*8U;
            std::uint8_t byte=static_cast<std::uint8_t>((old>>shift)&0x80U);
            byte|=static_cast<std::uint8_t>(projection>>shift);
            if (faction>0U) {
                if ((sectors&(1U<<faction))!=0U) byte|=0x02U;
                if ((fallback&(1U<<faction))!=0U) byte|=0x01U;
                if ((history&(1U<<faction))!=0U) byte|=0x80U;
                if ((byte&0xF9U)!=0U) expected_presence|=static_cast<std::uint8_t>(1U<<faction);
            }
            expected|=static_cast<std::uint64_t>(byte)<<shift;
        }
        const auto actual=simulation_lab_phase7_merge_occupancy_mask(
            old,projection,sectors,fallback,history);
        assert(actual==expected);
        assert(simulation_lab_phase7_present_factions(actual)==expected_presence);
    }
}
