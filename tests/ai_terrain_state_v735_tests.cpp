#include "thandor/simulation_lab.hpp"
#include "thandor/fog_preview.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace thandor;
static void check(bool condition,const char* msg) {
    if(!condition)throw std::runtime_error(msg);
}
int main() try {
    auto field=FieldGrid::create(64,64,0,0);
    SimulationLabSeedEntity seed{};
    seed.army_asset_id=50;
    seed.faction_index=2;
    seed.runtime_model_class_id=18;
    seed.root_model_maximum_integrity=1000;
    seed.actor.position={q12_to_float(field.at(30,30).world_x_q12),
                         q12_to_float(field.at(30,30).world_y_q12),0};
    SimulationLabScenarioRuntimeSeed scenario{};
    scenario.enabled=true;
    scenario.active_faction_count=2;
    scenario.assignable_faction_count=2;
    scenario.active_view_faction_index=2;
    scenario.ai_planning_enabled=false;
    SimulationLabConfig config{};
    config.run_count=1;
    config.duration_ticks=32;
    std::array<std::uint32_t,AiKnowledgeData::kParameterCount> params{};
    SimulationLabExperiment lab;
    lab.reset(config,params,std::span(&seed,1),{},{},{},{},{},scenario);
    lab.step(field,2);
    auto& run=lab.runs()[0];
    check(run.retail_scratch!=nullptr,"no initial scratch");
    // A current-visibility projection from tick 1 is not a new world-job
    // terrain field. On phase 3, native callbacks read the retained FLD.
    auto previous=std::make_shared<SimulationLabRetailScratchImage>(*run.retail_scratch);
    previous->field_occupancy_masks.assign(field.cells().size(),0ULL);
    run.retail_scratch=previous;
    run.current_visibility_occupancy.assign(field.cells().size(),0x88ULL<<16U);
    run.current_visibility_occupancy_tick=1;
    run.entities[0].model_primary_occupancy_mask=0xFFFFFFFFU;
    lab.step(field,1);
    check(run.tick==3,"missed phase 3");
    check(run.entities[0].model_primary_occupancy_mask==0,
          "phase 3 used stale projected markers instead of retained field");
    // An unlinked root does not receive the army terrain-state callback,
    // even if the owning entity is still in the Lab's entity vector.
    run.entities[0].root_model_component_index=0;
    if(run.entities[0].model_components.empty())run.entities[0].model_components.resize(1);
    run.entities[0].model_components[0].runtime_removed=true;
    run.entities[0].model_primary_occupancy_mask=0x12345678U;
    lab.step(field,4);
    check(run.tick==7,"missed phase 7");
    check(run.entities[0].model_primary_occupancy_mask==0x12345678U,
          "released root was reclassified during phase 7");
    std::cout<<"V735_TERRAIN_STATE PASS phase3 retained FLD, phase7 released root\n";
} catch (const std::exception& e) {
    std::cerr<<"V735_TERRAIN_STATE FAIL "<<e.what()<<'\n';
    return 1;
}
