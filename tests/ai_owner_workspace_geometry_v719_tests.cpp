#include "thandor/simulation_lab_ai_owner_list_v718.hpp"
#include "thandor/simulation_lab_ai_distances_v719.hpp"
#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>
using namespace thandor;
int main() {
    const MovementPoint root{4096,8192},stored{40960,-2048},query{45056,-2048};
    assert(lab_ai_workspace_distance_v719(1,root,stored,query)==4096);
    assert(lab_ai_workspace_distance_v719(0,root,stored,query)==51200);
    assert(lab_ai_workspace_distance_v719(2,root,stored,query)==51200);
    assert(lab_ai_workspace_distance_v719(1,root,root,query)==51200);
    // Euclidean marker support: (1.5, 1.0) is near although Manhattan
    // would reject it; (1.5, 1.5) lies outside a two-unit radius.
    assert(lab_ai_class13_marker_near_v719({0,0},{6144,4096}));
    assert(!lab_ai_class13_marker_near_v719({0,0},{6144,6144}));
    assert(lab_ai_class13_marker_near_v719({0,0},{0,0}));
    assert(!lab_ai_class13_marker_near_v719({0,0},{8193,0}));
    // Source resource-site candidate scans workspace00, not arbitrary
    // entities beyond its 128-structure limit. Last linked owner wins.
    std::vector<LabAiOwnerKindV718> kinds(132,LabAiOwnerKindV718::Structure00);
    const auto ws=lab_ai_admit_owner_list_v718(kinds.size(),[&](size_t i){return kinds[i];});
    // Native owner list keeps a model until it is unlinked, including during
    // construction and terminal-effect lifetime. Neutral is not an AI owner.
    assert(lab_ai_has_world_model_owner_v722(false,1));
    assert(!lab_ai_has_world_model_owner_v722(true,1));
    assert(!lab_ai_has_world_model_owner_v722(false,0));
    assert(ws.structures00.size()==128U);
    assert(ws.structures00.front()==131 && ws.structures00.back()==4);
    bool saw_overflow=false;
    for(auto i:ws.structures00) if(i==0 || i==1 || i==2 || i==3) saw_overflow=true;
    assert(!saw_overflow);
    const auto negative=lab_ai_manhattan_v719({std::numeric_limits<int32_t>::min(),0},
                                                {std::numeric_limits<int32_t>::max(),0});
    assert(negative==4294967295LL);
}
