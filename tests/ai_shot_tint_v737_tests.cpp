#include "thandor/simulation_lab_shot_terrain.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
using namespace thandor;
static void check(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
int main() try {
    constexpr uint32_t tint=0x80CC6633U;
    // For active faction 1, class bits are 0x4 (seen) and 0x8 (current).
    const auto current=simulation_lab_resolve_shot_terrain_state(0x8U,1U,tint);
    check(current.runtime_flags==0x14U,"active current shot must be PRESENT and NOT_REMEMBERED");
    check(current.resolved_mask==0x8U,"present class must retain current bits without inventing seen bits");
    check(current.tint_argb==retail_modulate_argb_mmx(0xFFFFFFFFU,tint),"visible shot tint modulation");
    const auto seen_only=simulation_lab_resolve_shot_terrain_state(0x4U,1U,tint);
    check(seen_only.runtime_flags==0x10U,"shot must not remember explored-only neighborhood");
    check(seen_only.resolved_mask==0U,"seen-only shot must not synthesize history");
    check(seen_only.tint_argb==0U,"unseen shot must be fully transparent");
    const auto different=simulation_lab_resolve_shot_terrain_state(0x8U,2U,tint);
    check(different.runtime_flags==0x10U,"view-faction change must recompute shot flags");
    check(different.tint_argb==0U,"foreign-faction visibility must not tint the shot");
    const auto both=simulation_lab_resolve_shot_terrain_state(0x28U,2U,tint);
    check(both.runtime_flags==0x14U,"second faction's current flag must be recognized");
    check(both.resolved_mask==0x28U,"each current presence must preserve its packed lane");
    const auto zero=simulation_lab_resolve_shot_terrain_state(0U,1U,0xFFFFFFFFU);
    check(zero.runtime_flags==0x10U && zero.tint_argb==0U,"empty terrain must hide shot");
    // The packed mask has eight 2-bit faction pairs; verify that the top lane
    // still resolves without shifting beyond the mask width.
    const auto last=simulation_lab_resolve_shot_terrain_state(0x8000U,7U,0xFFFFFFFFU);
    check(last.runtime_flags==0x14U && last.resolved_mask==0x8000U,
        "last faction mask/shift wrong");
    std::cout<<"V737_SHOT_TINT PASS active/current/explored/foreign/high-lane\n";
} catch(const std::exception& e){std::cerr<<"V737_SHOT_TINT FAIL "<<e.what()<<'\n';return 1;}
