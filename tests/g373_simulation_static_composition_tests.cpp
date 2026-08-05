#include "thandor/simulation_render_policy.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
}

int main() try {
    using namespace thandor;

    // A stock unit or tower is not replaced merely because it is movable/armed. Those properties
    // are intentionally absent from the policy input; live transient state is the replacement gate.
    const SimulationRenderPolicy ordinary_unit{false,true,false,true,false};
    require(!ordinary_unit.simulation_replacement(),"ordinary unit became a simulation replacement");
    require(ordinary_unit.packed_static_control_bits(false)==0U,
            "ordinary unit received a static-discard category");

    const SimulationRenderPolicy ordinary_tower{true,false,false,true,false};
    require(!ordinary_tower.simulation_replacement(),"ordinary tower became a simulation replacement");
    require(ordinary_tower.packed_static_control_bits(false)==0U,
            "ordinary tower received a static-discard category");

    const SimulationRenderPolicy authored_building{true,false,true,false,false};
    require(authored_building.authored_animated_building(),"authored building animation was lost");
    require(authored_building.packed_static_control_bits(false)==kObjectVertexAuthoredAnimatedBuildingBit,
            "authored building category changed");

    const SimulationRenderPolicy simulated_building{true,false,false,true,true};
    require(simulated_building.simulation_replacement(),"live simulated building was not replaced");
    require(simulated_building.packed_static_control_bits(true)==
                (kObjectVertexDirectTintBit|kObjectVertexSimulatedBuildingBit),
            "simulated building packed bits are wrong");
    require(simulated_building.dynamic_replacement_enabled(true,false),
            "building toggle did not enable selected simulation replacement");
    require(!simulated_building.dynamic_replacement_enabled(false,true),
            "unit toggle incorrectly enabled a simulated building");

    const SimulationRenderPolicy simulated_unit{false,true,false,true,true};
    require(simulated_unit.packed_static_control_bits(false)==kObjectVertexSimulatedUnitBit,
            "simulated unit packed bits are wrong");
    require(simulated_unit.dynamic_replacement_enabled(false,true),
            "unit toggle did not enable selected simulation replacement");
    require(!simulated_unit.dynamic_replacement_enabled(true,false),
            "building toggle incorrectly enabled a simulated unit");

    std::cout << "G373 simulation/static composition policy PASS\n";
    return 0;
} catch(const std::exception& error){
    std::cerr << "G373 simulation/static composition policy FAIL: " << error.what() << '\n';
    return 1;
}
