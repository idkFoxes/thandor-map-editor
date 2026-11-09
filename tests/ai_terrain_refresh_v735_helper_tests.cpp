#include "thandor/simulation_lab_terrain_refresh.hpp"
#include <cassert>
#include <cstdint>
#include <memory>
#include <iostream>
using namespace thandor;
int main() {
    SimulationLabRun run{};
    constexpr std::size_t cells=64;
    run.current_visibility_occupancy.assign(cells,0x88ULL<<16U);
    auto retained=std::make_shared<SimulationLabRetailScratchImage>();
    retained->field_occupancy_masks.assign(cells,0x80ULL<<16U);
    run.retail_scratch=retained;
    // Startup uses an initial projection, but both world jobs use FLD.
    assert(simulation_lab_terrain_refresh_source(run,cells,false)[0]==0x88ULL<<16U);
    assert(simulation_lab_terrain_refresh_source(run,cells,true)[0]==0x80ULL<<16U);
    assert(simulation_lab_terrain_refresh_source(run,cells+1,true).empty());
    run.retail_scratch.reset();
    assert(simulation_lab_terrain_refresh_source(run,cells,true).empty());
    assert(!simulation_lab_terrain_refresh_source(run,cells,false).empty());
    SimulationLabEntity entity{};
    entity.root_model_component_index=0;
    entity.model_components.resize(1);
    assert(simulation_lab_terrain_refresh_root_linked(entity));
    entity.root_model_terminal=true;
    assert(simulation_lab_terrain_refresh_root_linked(entity));
    entity.model_components[0].runtime_removed=true;
    assert(!simulation_lab_terrain_refresh_root_linked(entity));
    entity.model_components[0].runtime_removed=false;
    entity.runtime_removed=true;
    assert(!simulation_lab_terrain_refresh_root_linked(entity));
    std::cout<<"V735_TERRAIN_REFRESH_HELPERS PASS\n";
}
