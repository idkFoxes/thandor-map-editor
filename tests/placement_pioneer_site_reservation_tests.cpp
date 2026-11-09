#include "thandor/pioneer_site_reservation.hpp"
#include "thandor/pioneer_site_admission.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {
using thandor::MovementPoint;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void nearest_anchor_tests() {
    const MovementPoint site{0,0};
    const std::array<MovementPoint,4> ws{{{5,0},{0,5},{2,0},{1,0}}};
    std::vector<std::int32_t> consulted;
    const auto chosen=thandor::pioneer_nearest_mode1_anchor(site,ws,
        [&](MovementPoint p) {consulted.push_back(p.x);return p.x!=2;});
    check(chosen && chosen->x==1, "nearest placeable must skip rejected mode1 candidates");
    check(consulted==std::vector<std::int32_t>({5,2,1}),
          "equal-distance and non-improving sites must not make a mode1 call");
    const std::array<MovementPoint,3> ties{{{5,0},{0,5},{10,0}}};
    const auto first=thandor::pioneer_nearest_mode1_anchor(site,ties,[](auto){return true;});
    check(first && first->x==5 && first->y==0, "row-major tie must keep first accepted site");
    check(!thandor::pioneer_nearest_mode1_anchor(site,ties,[](auto){return false;}),
          "all rejected mode1 candidates cannot supply an anchor");
    // Signed x86 compare in the original makes a wrapped negative distance
    // 'closer' even though the Manhattan sum is an unsigned dword.
    const std::array<MovementPoint,2> wrap{{{1,0},{INT32_MIN,0}}};
    const auto overflow=thandor::pioneer_nearest_mode1_anchor(site,wrap,[](auto){return true;});
    check(overflow && overflow->x==INT32_MIN, "x86 signed wrapped compare changed");
}
void admission_tests() {
    using thandor::PioneerSiteAdmission;
    auto check_path=[&](bool mode3,std::uint32_t count,bool mode0,bool chain,
                        PioneerSiteAdmission expected,std::uint32_t buckets,
                        const std::vector<int>& expected_calls){
        std::vector<int> calls;
        const auto result=thandor::pioneer_site_admission(0x2000U,
            [&]{calls.push_back(3);return std::pair{mode3,count};},
            [&]{calls.push_back(0);return mode0;},
            [&]{calls.push_back(333);return chain;});
        check(result.disposition==expected && result.bucket_count==buckets,
              "pioneer caller branch/bucket mismatch");
        check(calls==expected_calls,"mode3/mode0/reserve query call order mismatch");
    };
    check_path(false,0,false,false,PioneerSiteAdmission::SkipSite,0,{3,0});
    check_path(false,0,true,false,PioneerSiteAdmission::StopSelector,0,{3,0});
    check_path(true,0,false,false,PioneerSiteAdmission::StopSelector,0,{3});
    check_path(true,1,true,false,PioneerSiteAdmission::StopSelector,0,{3,0});
    check_path(true,1,false,false,PioneerSiteAdmission::StopSelector,1,{3,0,333});
    check_path(true,0x8000,false,false,PioneerSiteAdmission::StopSelector,4,{3,0,333});
    check_path(true,0x8000,false,true,PioneerSiteAdmission::ScoreSite,4,{3,0,333});
    check_path(true,0x8001,false,false,PioneerSiteAdmission::ScoreSite,5,{3,0});
}
void chain_tests() {
    const MovementPoint site{0,0};
    // Closest qualifying anchor on the second probe; first temporary must
    // remain present until the second search, then both destroy LIFO.
    std::vector<int> created,destroyed;
    int next=0;
    const std::array<MovementPoint,3> sequence{{{400,0},{199,0},{1,0}}};
    const auto near=thandor::pioneer_reserve_separated_site_chain<int>(site,200,
        [&](MovementPoint){return next < 3 ? std::optional<MovementPoint>(sequence[next++]) : std::nullopt;},
        [&](MovementPoint){created.push_back(next);return std::optional<int>(next);},
        [&](int token){destroyed.push_back(token);});
    check(!near && created==std::vector<int>({1,2}) && destroyed==std::vector<int>({2,1}),
          "near second probe must succeed and destroy in reverse order");
    created.clear();destroyed.clear();next=0;
    const auto equal=thandor::pioneer_reserve_separated_site_chain<int>(site,199,
        [&](MovementPoint){return next < 3 ? std::optional<MovementPoint>(sequence[next++]) : std::nullopt;},
        [&](MovementPoint){created.push_back(next);return std::optional<int>(next);},
        [&](int token){destroyed.push_back(token);});
    check(!equal && created==std::vector<int>({1,2,3}) &&
          destroyed==std::vector<int>({3,2,1}),
          "distance exactly equal to quantum must continue, not accept");
    created.clear();destroyed.clear();next=0;
    const auto stop_at_four=thandor::pioneer_reserve_separated_site_chain<int>(site,1,
        [&](MovementPoint){++next;return std::optional<MovementPoint>{MovementPoint{next*10,0}};},
        [&](MovementPoint){created.push_back(next);return std::optional<int>(next);},
        [&](int token){destroyed.push_back(token);});
    check(stop_at_four && next==4 && created.size()==4U &&
          destroyed==std::vector<int>({4,3,2,1}),
          "retail probes at most four reservations, then cleans all up");
    created.clear();destroyed.clear();next=0;
    const auto failed_create=thandor::pioneer_reserve_separated_site_chain<int>(site,10,
        [&](MovementPoint){++next;return std::optional<MovementPoint>{MovementPoint{next*100,0}};},
        [&](MovementPoint)->std::optional<int>{if(next==2)return std::nullopt;created.push_back(next);return next;},
        [&](int token){destroyed.push_back(token);});
    check(failed_create && next==2 && created==std::vector<int>({1}) &&
          destroyed==std::vector<int>({1}),"failed creation must clean earlier probes");
    created.clear();destroyed.clear();
    const auto no_anchor=thandor::pioneer_reserve_separated_site_chain<int>(site,10,
        [&](MovementPoint)->std::optional<MovementPoint>{return std::nullopt;},
        [&](MovementPoint)->std::optional<int>{throw std::logic_error("created with no anchor");},
        [&](int){throw std::logic_error("destroyed with no tokens");});
    check(no_anchor,"no eligible anchors must reject without side effects");
}
} // namespace
int main() try {
    nearest_anchor_tests();
    chain_tests();
    admission_tests();
    std::cout << "V591 pioneer reservation source-parity tests PASS\n";
    return 0;
} catch(const std::exception& e) {
    std::cerr << "V591 FAIL: " << e.what() << '\n';
    return 1;
}
