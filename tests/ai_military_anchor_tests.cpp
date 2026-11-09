#include "thandor/military_anchor_v599.hpp"
#include <array>
#include <cassert>
#include <cstdint>
using namespace thandor;
int main() {
    MilitaryScoreParametersV599 p{};
    p.site_bias=0x10000; p.site_scale=0x1000;
    p.anchor_bias=0x10000; p.anchor_scale=0x1000;
    p.hostile_bias=0x10000; p.hostile_scale=0x1000;
    std::array<MilitarySiteV599,2> sites{{{4096,0,4096},{4096,0,4096}}};
    const auto score=military_site_score_v599(0,0,sites[0],p,2);
    assert(score>0);
    const MilitaryAnchorV599 inactive{0,0,0};
    const MilitaryAnchorV599 active{0,0,150};
    assert(military_anchor_score_v599(0,inactive,p,2)==0);
    assert(military_anchor_score_v599(0,active,p,2)>0);
    // Retail's double-model-X quirk: unit Y never enters anchor scoring.
    assert(military_anchor_score_v599(4096,active,p,1) ==
           military_anchor_score_v599(4096,active,p,1));
    std::size_t written03=0;
    assert(military_unseen_zero_alias(37,written03));
    assert(!military_unseen_zero_alias(18,written03)); // Retained non-null tail.
    MilitaryScoreParametersV599 native_s15{};native_s15.hostile_bias=125000;native_s15.hostile_scale=1331;
    // Positive null-record scores are real integer coordinates, never host pointers.
    const auto phantom=military_choose_anchor_action_v599(1000,-1000,native_s15,
        0,0,70,{}, {}, {}, {},true,{},true);
    assert(phantom.kind==3&&phantom.best_hostile_score>0);
    const auto site_choice=military_choose_anchor_action_v599(0,0,p,2,0,0,
        sites,inactive,inactive,{},false);
    assert(site_choice.kind==1 && site_choice.site_index==0);
    const auto anchor_choice=military_choose_anchor_action_v599(0,0,p,0,2,0,
        sites,active,inactive,{},false);
    assert(anchor_choice.kind==2);
    std::array<MilitarySiteV599,1> targets{{{0,0,0}}};
    const auto group_choice=military_choose_anchor_action_v599(0,0,p,0,0,2,
        sites,inactive,inactive,targets,false);
    assert(group_choice.kind==3);
    assert(military_choose_anchor_action_v599(0,0,p,0,0,2,
        sites,inactive,inactive,{},true).kind==0); // no host pointers manufactured
    MilitaryAnchorV599 hit{};
    assert(military_impact_anchor_v599(hit,0,0));
    assert(hit.cooldown==150);
    assert(!military_impact_anchor_v599(hit,0,0));
    assert(kMilitaryGroupClassScoresV599[18]==6144);
    assert(military_factory_parent_pair_v599(7,3,true,std::size_t{3},false,{}));
    assert(military_factory_parent_pair_v599(3,7,false,{},true,std::size_t{3}));
    assert(!military_factory_parent_pair_v599(7,3,false,std::size_t{3},false,{}));
    assert(!military_factory_parent_pair_v599(7,3,true,std::size_t{2},false,{}));
    // x86 fixed-point arithmetic wraps instead of overflowing C++ signed integers.
    assert(military_mul_v599(INT32_MAX,2)==-2);
    // V706: native SAR 12 retains the sign. Logical SHR falsely changes -4096
    // into +1048575, which can send otherwise idle units to invalid sites.
    assert(military_sar12_v706(-4096)==-1);
    assert(military_sar12_v706(-1)==-1);
    assert(military_sar12_v706(INT32_MIN)==-524288);
    assert(military_sar12_v706(4095)==0);
    MilitaryScoreParametersV599 signed_p{};
    const MilitarySiteV599 negative_site{0,0,-4096};
    assert(military_site_score_v599(0,0,negative_site,signed_p,1)==-1);
    const std::array<MilitarySiteV599,2> signed_sites{{negative_site,{0,0,4096}}};
    const auto signed_choice=military_choose_anchor_action_v599(0,0,signed_p,1,0,0,
        signed_sites,{}, {}, {},false);
    assert(signed_choice.kind==1&&signed_choice.site_index==1);
    signed_p.anchor_bias=signed_p.hostile_bias=4096;
    signed_p.anchor_scale=signed_p.hostile_scale=-4096;
    const MilitaryAnchorV599 signed_anchor{0,0,10};
    assert(military_anchor_score_v599(0,signed_anchor,signed_p,1)==-4096);
    assert(military_secondary_score_v599(0,0,{0,0,0},signed_p,1)==-4096);
    // S08 tick2066: four new entries leave tanks from the previous
    // faction in slots4..7. The doubled read selects the last tied tank.
    const std::array<uint32_t,10> retained{{15,15,4,16,2,2,2,2,18,18}};
    assert(military_retained_unseen_group_target(retained,4)==7U);
    assert(military_retained_unseen_group_target(retained,2)==1U);
    return 0;
}
