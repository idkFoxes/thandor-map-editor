#include "thandor/pioneer_class14_placement.hpp"
#include "thandor/pioneer_site_admission.hpp"
#include "thandor/field.hpp"
#include "thandor/fog_preview.hpp"
#include "thandor/pioneer_resource_grid.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using namespace thandor;
void check(bool yes, const char* text) { if (!yes) throw std::runtime_error(text); }
PioneerPlacementOwner owner(int x, int y, int radius, unsigned faction = 1,
                             unsigned cls = 18, unsigned flags = 0) {
    return {{x,y}, faction, cls, flags, radius, true};
}
void sqrt_tests() {
    constexpr std::array<std::uint64_t, 15> cases{{0,1,2,3,4,15,16,17,24,25,26,
        0xFFFFFFFEULL,0xFFFFFFFFULL,0x100000000ULL,0x7FFFFFFFFFFFFFFFULL}};
    for (const auto n : cases) {
        const auto root = pioneer_uint64_sqrt(n);
        check(static_cast<std::uint64_t>(root)*root <= n, "sqrt rounded up");
        check(static_cast<std::uint64_t>(root+1U)*(root+1U)>n, "sqrt rounded down");
    }
}
void owner_support_tests() {
    constexpr PioneerPlacementModel def{0x500,0x100,0};
    constexpr MovementPoint point{0,0};
    const std::array owners{owner(0,0,0x1000),owner(0,0,0x1000,2),
                            owner(0,0,0x3000,1,18,0x08),owner(0,0,0x1000,1,18),
                            owner(0,0,0x2000,1,14)};
    const auto mode0=pioneer_class14_support(0,0,point,def,1,owners);
    check(mode0.accepted && mode0.placement_value==0U,"mode0 accepts first eligible pioneer");
    const auto mode3=pioneer_class14_support(3,0,MovementPoint{0x1200,0},def,1,owners);
    check(mode3.accepted && mode3.placement_value==0U,"mode3 should use class14 supporter");
    const auto mode3_clearance=pioneer_class14_support(3,0x300,MovementPoint{0x2200,0},def,1,owners);
    check(mode3_clearance.accepted && mode3_clearance.placement_value==0x100U,
          "mode3 must calculate sqrt-distance minus support radius and margin");
    const std::array only_pioneer{owner(0,0,0x5000)};
    check(pioneer_class14_support(0,0,point,def,1,only_pioneer).accepted,
          "mode0 must allow class18 when alive");
    check(!pioneer_class14_support(3,0,point,def,1,only_pioneer).accepted,
          "mode3 must exclude class18");
    check(!pioneer_class14_support(1,0,point,def,1,only_pioneer).accepted,
          "mode1 must exclude class18");
    const std::array held_pioneer{owner(0,0,0x5000,1,18,0x10)};
    check(!pioneer_class14_support(0,0,point,def,1,held_pioneer).accepted,
          "mode0 skips destroyed/dismantling class18");
    const std::array different_faction{owner(0,0,0x5000,2,14)};
    check(!pioneer_class14_support(0,0,point,def,1,different_faction).accepted,
          "support cannot cross faction owner list");
    const std::array no_support{owner(0,0,0,1,14)};
    check(!pioneer_class14_support(0,0,point,def,1,no_support).accepted,
          "zero-radius supporters must be excluded");
    const std::array outside{owner(0x1001,0,0x1000,1,14)};
    check(!pioneer_class14_support(0,0,point,PioneerPlacementModel{0x500,0,0},1,outside).accepted,
          "reach boundary off by one");
    check(pioneer_class14_support(0,0,point,PioneerPlacementModel{0x500,1,0},1,outside).accepted,
          "candidate margin must extend reach");
    check(pioneer_class14_support(0,1,point,PioneerPlacementModel{0x500,0,0},1,outside).accepted,
          "mode3 KI clearance must extend reach");
    const std::array off_axis{owner(0x100,0x300,0x300,1,14)};
    check(!pioneer_class14_support(0,0,point,PioneerPlacementModel{0,0,0},1,off_axis).accepted,
          "support distance must be Euclidean, not axis-aligned");
    const std::array quirk{owner(3,4,5,1,14)};
    check(pioneer_class14_support(0,0,point,PioneerPlacementModel{0,0,0},1,quirk)
              .placement_value==16,"mode0 value must be original squared y delta");
    const std::array nearer{owner(20,0,100,1,14),owner(6,8,100,1,14)};
    check(pioneer_class14_support(3,0,point,PioneerPlacementModel{0,0,0},1,nearer)
              .placement_value==0,"mode3 nearest value clamp under radius");
    // A zero-radius or wrong-kind owner must not change the first-match path.
    auto mixed=owners;
    mixed[0].is_runtime_model=false;
    check(pioneer_class14_support(0,0,point,def,1,mixed).accepted,
          "owner type filter changed");
}
void full_query_tests() {
    auto field=FieldGrid::create(12U,12U,0,0U);
    auto& support=field.at(6U,6U);
    support.flags_and_material |= 0x800U;
    const MovementPoint candidate{support.world_x_q12,support.world_y_q12};
    const std::array owners{owner(candidate.x,candidate.y,0x2000U,1U,14U)};
    const PioneerPlacementModel definition{0x300,0x200,0U};
    int collision_calls=0, terrain_calls=0, blocker_calls=0;
    bool collision=false, terrain_block=false, field_block=false;
    const auto collision_fn=[&](std::uint32_t, std::int32_t radius, MovementPoint p){
        ++collision_calls; check(radius==definition.footprint_radius_q12 &&
                                 p.x==candidate.x,"collision signature mismatch");
        return collision;};
    const auto terrain_fn=[&](std::int32_t,MovementPoint){
        ++terrain_calls;return PioneerTerrainPlacementResult{terrain_block,0xC000U};};
    const auto field_fn=[&](std::uint32_t,MovementPoint){++blocker_calls;return field_block;};
    const auto query=[&](std::uint32_t mode, std::uint32_t flags, bool interaction=false,
                        std::optional<PioneerPlacementModel> model=std::nullopt){
        return pioneer_class14_placement(mode,0,candidate,model.value_or(definition),1,owners,flags,interaction,
                                         collision_fn,terrain_fn,field_fn);};
    auto r=query(3,support.flags_and_material);
    check(r.accepted && r.reason==PioneerPlacementReason::Accepted && r.placement_value==0U,
          "class14 full query should pass with support and resource bit");
    collision=true; collision_calls=terrain_calls=blocker_calls=0;
    r=query(3,support.flags_and_material);
    check(r.reason==PioneerPlacementReason::RuntimeCollision && collision_calls==1 &&
          terrain_calls==0 && blocker_calls==0,"collision must short circuit");
    collision=false; terrain_block=true;collision_calls=terrain_calls=blocker_calls=0;
    r=query(3,support.flags_and_material);
    check(r.reason==PioneerPlacementReason::TerrainRejected && terrain_calls==1 &&
          blocker_calls==0,"terrain must short circuit field blocker");
    terrain_block=false;field_block=true;collision_calls=terrain_calls=blocker_calls=0;
    r=query(3,support.flags_and_material);
    check(r.reason==PioneerPlacementReason::FieldBlocked && blocker_calls==1,
          "field blocker must run before support list");
    field_block=false;
    r=query(0,support.flags_and_material & ~0x800U);
    check(r.reason==PioneerPlacementReason::MissingResourceDeposit,
          "class14 must require deposit even after reachable common test");
    r=query(3,support.flags_and_material,true);
    check(r.accepted && r.placement_value==0xC000U,
          "active editor interaction must propagate terrain value");
    const auto wrong_selector=PioneerPlacementModel{definition.footprint_radius_q12,
                                                   definition.placement_margin_q12,1};
    r=query(0,support.flags_and_material,false,wrong_selector);
    check(r.reason==PioneerPlacementReason::MissingResourceDeposit,
          "classParameterC0=1 selects Tritium bit, not Xenite");
    support.flags_and_material |= 0x1000U;
    r=query(0,support.flags_and_material,false,wrong_selector);
    check(r.accepted,"classParameterC0=1 must read Tritium field bit");
    // Source uses (uint8_t)selector & 31 and 32-bit dword shifts.
    const auto masked_selector=PioneerPlacementModel{0x300,0x200,33};
    check(query(0,support.flags_and_material,false,masked_selector).accepted,
          "classParameterC0 selector must wrap modulo 32");
}
void resource_cell_mapping_tests() {
    auto field=FieldGrid::create(19U,19U,0,0U);
    std::uint32_t diverging=0;
    for (std::uint32_t row=3;row<16;++row) for (std::uint32_t col=3;col<16;++col) {
        const auto& center=field.at(col,row);
        const auto mapped=retail_nearest_world_to_cell(field,center.world_x_q12,
                                                        center.world_y_q12);
        check(mapped && mapped->x==col && mapped->y==row,
              "class14 rounded world coordinate must map authored FLD center to self");
        for (int dy=-0x600;dy<=0x600;dy+=0x200)
            for (int dx=-0x600;dx<=0x600;dx+=0x200) {
                const auto x=center.world_x_q12+dx, y=center.world_y_q12+dy;
                const auto rounded=retail_nearest_world_to_cell(field,x,y);
                const auto triangular=retail_fog_world_to_cell(field,x,y);
                if (rounded && triangular &&
                    (rounded->x!=triangular->x || rounded->y!=triangular->y))
                    ++diverging;
            }
    }
    check(diverging>0,"resource extraction and fog triangle mappings should diverge at edge offsets");
    // Exercise the actual SimLab resource selector on an off-center world
    // coordinate for which the two lattice mappings disagree. Only the
    // rounded cell carries the deposit, so a triangular lookup must fail.
    bool witnessed=false;
    for (std::uint32_t row=4;row<15 && !witnessed;++row)
        for (std::uint32_t col=4;col<15 && !witnessed;++col) {
            const auto& center=field.at(col,row);
            for (int dy=-0x600;dy<=0x600 && !witnessed;dy+=0x200)
                for (int dx=-0x600;dx<=0x600 && !witnessed;dx+=0x200) {
                    const auto x=center.world_x_q12+dx, y=center.world_y_q12+dy;
                    const auto rounded=retail_nearest_world_to_cell(field,x,y);
                    const auto triangular=retail_fog_world_to_cell(field,x,y);
                    if (!rounded || !triangular ||
                        (rounded->x==triangular->x && rounded->y==triangular->y)) continue;
                    auto& deposit=field.at(rounded->x,rounded->y);
                    auto& other=field.at(triangular->x,triangular->y);
                    const auto saved_deposit=deposit.flags_and_material;
                    const auto saved_other=other.flags_and_material;
                    deposit.flags_and_material |= 0x800U;
                    other.flags_and_material &= ~0x800U;
                    if ((deposit.flags_and_material & 0x88006000U)==0U &&
                        (deposit.flags_and_material & 0x800U)!=0U &&
                        (other.flags_and_material & 0x800U)==0U) {
                        check(pioneer_class14_resource_cell_eligible(field,x,y,0),
                              "class14 lookup lost the deposit in the rounded cell");
                        deposit.flags_and_material &= ~0x800U;
                        check(!pioneer_class14_resource_cell_eligible(field,x,y,0),
                              "class14 lookup incorrectly accepted a deposit-free nearest cell");
                        witnessed=true;
                    }
                    deposit.flags_and_material=saved_deposit;
                    other.flags_and_material=saved_other;
                }
        }
    check(witnessed,"no valid interior example for rounded-vs-triangle deposit test");
    std::cout << "rounded-vs-triangle divergent off-center probes=" << diverging << '\n';
}
void admission_integrated_tests() {
    const PioneerPlacementModel def{0x500,0,0};
    const MovementPoint site{0,0};
    const std::array pioneers{owner(0,0,0x10000,1,18)};
    int chains=0;
    const auto mode=[&](unsigned m){
        const auto result=pioneer_class14_placement(m,0,site,def,1,pioneers,0x800U,false,
            [](auto,auto,auto){return false;},
            [](auto,auto){return PioneerTerrainPlacementResult{false,0};},
            [](auto,auto){return false;});
        return result;
    };
    const auto admission=pioneer_site_admission(0x2000,
        [&]{ const auto q=mode(3);return std::pair{q.accepted,q.placement_value};},
        [&]{return mode(0).accepted;}, [&]{++chains;return true;});
    check(admission.disposition==PioneerSiteAdmission::StopSelector && chains==0,
          "mode3 failure + real mode0 Pioneer support stops selector without chain");
    const std::array structures{owner(0x4000,0,0x20000,1,14)};
    auto query=[&](unsigned m){return pioneer_class14_placement(m,0,site,def,1,structures,
        0x800U,false,[](auto,auto,auto){return false;},
        [](auto,auto){return PioneerTerrainPlacementResult{false,0};},
        [](auto,auto){return false;});};
    const auto both=pioneer_site_admission(0x2000,
        [&]{const auto q=query(3);return std::pair{q.accepted,q.placement_value};},
        [&]{return query(0).accepted;}, [&]{++chains;return true;});
    check(both.disposition==PioneerSiteAdmission::StopSelector && chains==0,
          "mode3 accepted + mode0 accepted still stops selector");
}
} // namespace
int main() try {
    sqrt_tests();owner_support_tests();full_query_tests();resource_cell_mapping_tests();admission_integrated_tests();
    std::cout<<"V592 class14 source-contract/support/admission tests PASS\n";
    return 0;
} catch(const std::exception& e) {
    std::cerr<<"V592 FAIL "<<e.what()<<'\n';return 1;
}
