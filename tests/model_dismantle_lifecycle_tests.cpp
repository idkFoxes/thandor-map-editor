#include "thandor/simulation_lab_dismantle.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace thandor;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static SimulationLabEntity armed(uint32_t arm=111){
    SimulationLabEntity e;e.army_asset_id=arm;e.faction_index=1;e.root_model_component_index=0;
    e.model_components.resize(2);e.model_components[0].integrity=900;
    e.model_components[0].maximum_integrity=1000;e.model_components[1].parent_component_index=0;
    return e;
}
int main()try{
    SimulationLabModelComponentState c;c.integrity=900;c.maximum_integrity=1000;
    for(unsigned tick=1;tick<=3;++tick)check(!simulation_lab_advance_component_dismantling(c,3200).advanced,"healthy model dismantled");
    c.runtime_flags_ec=0x418;
    for(unsigned tick=4;tick<13;++tick)check(!simulation_lab_advance_component_dismantling(c,3200).advanced,"dismantle restarted its shared twelve-tick clock");
    const auto pulse=simulation_lab_advance_component_dismantling(c,3200);
    check(pulse.advanced&&pulse.refund_q4==100&&!pulse.completed&&c.integrity==838,"native refund or health fraction differs");
    c.dismantle_countdown=0;c.integrity=20;
    const auto end=simulation_lab_advance_component_dismantling(c,3200);
    check(end.completed&&c.integrity==0&&c.runtime_flags_ec==0x628,"last dismantle pulse did not switch flags 10/200 and start removal");
    c.runtime_flags_ec=0x418;c.dismantle_countdown=0;c.integrity=20;
    check(!simulation_lab_advance_component_dismantling(c,0).advanced&&c.integrity==20,"zero-value model was dismantled");
    SimulationLabRun run;run.entities={armed(),armed(),armed(),armed()};
    const std::array<size_t,3> three{0,1,2};const std::array<size_t,2> two{0,1};
    simulation_lab_update_active_entity_pressure_flag(run,1,{},two);
    check(!run.ai_active_entity_pressure_latched[1]&&!run.entities[0].root_model_terminal,"small initial force was incorrectly dismantled");
    simulation_lab_update_active_entity_pressure_flag(run,1,{},three);
    check(run.ai_active_entity_pressure_latched[1],"three armed runtimes did not establish the faction");
    simulation_lab_update_active_entity_pressure_flag(run,1,{},two);
    check(run.entities[0].model_components[0].runtime_flags_ec==0x418&&run.entities[1].root_model_terminal,"established under-strength force was not marked");
    check(!run.entities[2].root_model_terminal&&!run.entities[3].root_model_terminal,"AI dismantled owners outside its admitted workspaces");
    SimulationLabRun mixed;mixed.entities={armed(350),armed(),armed()};
    const std::array<size_t,1> structure{0};const std::array<size_t,2> units{1,2};
    simulation_lab_update_active_entity_pressure_flag(mixed,1,structure,units);
    check(mixed.ai_active_entity_pressure_latched[1],"one defensive structure and two armed units did not qualify");
    SimulationLabRun held;held.ai_active_entity_pressure_latched[1]=true;held.entities={armed()};
    held.entities[0].runtime_flags=8U; // Pioneer conversion hold is already native bit 8.
    simulation_lab_update_active_entity_pressure_flag(held,1,{},structure);
    check(!held.entities[0].root_model_terminal&&!(held.entities[0].model_components[0].runtime_flags_ec&0x10U),"conversion hold was incorrectly changed into dismantling");
    SimulationLabRun dead;dead.ai_active_entity_pressure_latched[1]=true;dead.entities={armed()};
    dead.entities[0].model_components[0].terminal_runtime_flag8=true;
    simulation_lab_update_active_entity_pressure_flag(dead,1,{},structure);
    check(!(dead.entities[0].model_components[0].runtime_flags_ec&0x10U),"already destroyed model was marked for recycling");
    auto stopped=armed();stopped.model_components[0].runtime_flags_ec=0x418;
    stopped.model_components[1].runtime_flags_ec=0x418;stopped.runtime_flags=0x418;
    stopped.root_model_terminal=true;
    simulation_lab_cancel_dismantling_on_stop(stopped);
    check(!stopped.root_model_terminal&&stopped.runtime_flags==0x400&&
        stopped.model_components[0].runtime_flags_ec==0x400&&stopped.model_components[1].runtime_flags_ec==0x400,
        "stop did not cancel live hierarchy dismantling while retaining no-regeneration");
    stopped.model_components[0].runtime_flags_ec=0x418;stopped.model_components[0].integrity=0;
    simulation_lab_cancel_dismantling_on_stop(stopped);
    check(stopped.model_components[0].runtime_flags_ec==0x418,"stop revived a zero-health dismantled root");
    std::cout<<"MODEL_DISMANTLE PASS shared clock/refund/final flags and AI threshold/admission\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
