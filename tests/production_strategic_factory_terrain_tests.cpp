#include "thandor/strategic_factory_terrain_v600.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    using thandor::select_strategic_factory_terrain_v600;
    std::vector<std::uint32_t> all_free(256U,0U);
    std::array<bool,5> absent{};
    std::array<bool,5> enabled{};
    enabled.fill(true);
    const auto one=select_strategic_factory_terrain_v600(all_free,0U,absent,enabled);
    assert((one.free_percent==std::array<std::uint32_t,3>{100,100,100}));
    assert(one.selected_army_id==302U && one.existing_count==0U);
    // ARM302 and ARM303 tie at 25600 without jitter, earlier ARM302 wins.
    absent[0]=true;
    const auto two=select_strategic_factory_terrain_v600(all_free,0U,absent,enabled);
    assert(two.selected_army_id==303U && two.existing_count==1U);
    std::vector<std::uint32_t> high_blocked(256U,0x70000000U);
    const auto three=select_strategic_factory_terrain_v600(high_blocked,0U,{},enabled);
    assert(three.free_percent[0]==0U && three.free_percent[1]==100U);
    assert(three.selected_army_id==304U);
    std::vector<std::uint32_t> all_blocked(256U,0x7F000000U);
    const auto four=select_strategic_factory_terrain_v600(all_blocked,0U,{},enabled);
    assert((four.free_percent==std::array<std::uint32_t,3>{0,0,0}));
    assert(four.selected_army_id==302U);
    enabled.fill(false); enabled[4]=true;
    const auto five=select_strategic_factory_terrain_v600(all_free,0U,{},enabled);
    assert(five.selected_army_id==0U); // ARM306 recovered quirk
    // Jitter advances only when an enabled absent record is evaluated.
    enabled.fill(false); enabled[1]=true; enabled[2]=true;
    const auto six=select_strategic_factory_terrain_v600(all_free,0x7FFFU,{},enabled);
    assert(six.selected_army_id==303U);
    assert(six.selected_score==24000+1600+0x3FFF);
    const auto empty=select_strategic_factory_terrain_v600({},123U,{},enabled);
    assert(empty.selected_army_id==0U);
}
