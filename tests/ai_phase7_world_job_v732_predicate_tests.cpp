#include "thandor/simulation_lab_phase7_occupancy.hpp"
#include "thandor/simulation_lab_phase7_owner.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
using namespace thandor;
int main() {
    for (std::uint32_t faction=0;faction<9;++faction) {
        for (bool ended:{false,true}) {
            const auto selected=simulation_lab_phase7_ended_faction_mask(faction,ended);
            const auto expected=(ended && faction>=1U && faction<=7U)
                ?(1U<<faction):0U;
            assert(selected==expected);
            auto mask=simulation_lab_phase7_merge_occupancy_mask(0,0,0,selected,0);
            for(std::uint32_t slot=1;slot<8;++slot)
                assert(((mask>>(slot*8U))&1U)==static_cast<std::uint64_t>((expected>>slot)&1U));
        }
    }
    // Native projected marker bits are OR-ed without stripping persistent 0x80.
    for (std::uint32_t marker:{0x88U,0x90U,0xC0U}) {
        const auto projected=static_cast<std::uint64_t>(marker)<<16U;
        const auto merged=simulation_lab_phase7_merge_occupancy_mask(0,projected,0,0,0);
        assert(((merged>>16U)&0xFFU)==marker);
    }
    // Empty native owner lists cannot write new projected-visibility markers,
    // even if the Lab retains an earlier preview buffer of nonzero markers.
    assert(simulation_lab_phase7_projection_available(false,false)); // startup
    assert(simulation_lab_phase7_projection_available(false,true));
    assert(simulation_lab_phase7_projection_available(true,true));
    assert(!simulation_lab_phase7_projection_available(true,false));
    SimulationLabRun run{};
    assert(!simulation_lab_phase7_maybe_has_world_owner(run,0));
    assert(simulation_lab_phase7_maybe_has_world_owner(run,1));
    run.entities.resize(1);
    assert(simulation_lab_phase7_maybe_has_world_owner(run,0));
    run.entities[0].root_model_component_index=0;
    run.entities[0].model_components.resize(1);
    run.entities[0].root_model_terminal=true;
    assert(simulation_lab_phase7_maybe_has_world_owner(run,0));
    run.entities[0].model_components[0].runtime_removed=true;
    assert(!simulation_lab_phase7_maybe_has_world_owner(run,0));
    std::cout<<"V732_PHASE7_PREDICATES PASS\n";
}
