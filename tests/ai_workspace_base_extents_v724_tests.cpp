#include "thandor/simulation_lab_ai_base_extents_v724.hpp"
#include "thandor/simulation_lab_ai_owner_list_v718.hpp"
#include <array>
#include <cassert>
#include <optional>
#include <vector>
using namespace thandor;
int main() {
    // Recovered Workspace 00 has a hard 128-owner limit. All 130 owners
    // have extents, but the first linked two are not admitted.
    std::vector<LabAiOwnerKindV718> kinds(130,LabAiOwnerKindV718::Structure00);
    auto admitted=lab_ai_admit_owner_list_v718(kinds.size(),[&](std::size_t i){return kinds[i];});
    assert(admitted.structures00.size()==128U);
    const auto extent=[&](std::size_t index)->std::optional<LabAiPrimaryExtentV724>{
        return LabAiPrimaryExtentV724{{static_cast<int32_t>(index*100000),0},4096};
    };
    assert(!lab_ai_inside_primary_extents_v724({0,0},admitted.structures00,extent));
    assert(!lab_ai_inside_primary_extents_v724({100000,0},admitted.structures00,extent));
    assert(lab_ai_inside_primary_extents_v724({200000,0},admitted.structures00,extent));
    assert(lab_ai_inside_primary_extents_v724({12900000,0},admitted.structures00,extent));
    assert(!lab_ai_inside_primary_extents_v724({200000+4096,0},admitted.structures00,extent));
    assert(lab_ai_inside_primary_extents_v724({200000+4095,4095},admitted.structures00,extent));
    assert(!lab_ai_inside_primary_extents_v724({200000+4095,4096},admitted.structures00,extent));
    // A world-owner record remains eligible while under construction or while
    // its terminal effect is pending: callers must not gate it by either state.
    struct Owner {bool completed, terminal, removed;};
    const std::array<Owner,3> status{{{false,false,false},{true,true,false},{true,false,true}}};
    kinds.assign(status.size(),LabAiOwnerKindV718::Skip);
    for(size_t i=0;i<status.size();++i)
        kinds[i]=lab_ai_has_world_model_owner_v722(status[i].removed,1U)?
            LabAiOwnerKindV718::Structure00:LabAiOwnerKindV718::Skip;
    admitted=lab_ai_admit_owner_list_v718(kinds.size(),[&](std::size_t i){return kinds[i];});
    assert(admitted.structures00.size()==2U);
    const auto pending=[](std::size_t i)->std::optional<LabAiPrimaryExtentV724>{
        return LabAiPrimaryExtentV724{{static_cast<int32_t>(i*100000),0},4000};
    };
    assert(lab_ai_inside_primary_extents_v724({0,0},admitted.structures00,pending));
    assert(lab_ai_inside_primary_extents_v724({100000,0},admitted.structures00,pending));
    assert(!lab_ai_inside_primary_extents_v724({200000,0},admitted.structures00,pending));
    // An unassigned request has no ModelRuntimeSlot and no extent.
    assert(!lab_ai_inside_primary_extents_v724({100000,0},admitted.structures00,
        [](std::size_t)->std::optional<LabAiPrimaryExtentV724>{return std::nullopt;}));
}
