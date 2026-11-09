#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace thandor;
static void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static void docking_case(bool newer_blocker){
    const auto field=FieldGrid::create(20,20,0,0);const auto cell=field.at(10,10);
    const Vec3f center{q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
    SimulationLabSeedEntity moving;moving.army_asset_id=1;moving.faction_index=1;
    moving.source_placement_index=3;moving.runtime_model_class_id=1;moving.root_model_maximum_integrity=100;
    moving.actor.position={center.x-0.5F,center.y,0};moving.actor.move_target=Vec3f{center.x+2,center.y,0};
    moving.motion.speed_q12_per_tick=128;moving.motion.ground_speed_control=true;
    moving.motion.ground_acceleration_q12=64;moving.motion.collision_radius_q12=1000;
    auto platform=moving;platform.army_asset_id=380;platform.runtime_model_class_id=23;
    platform.source_placement_index=0;platform.building=true;platform.actor.position=center;
    platform.actor.move_target.reset();platform.motion={};platform.motion.collision_radius_q12=2000;
    auto blocker=platform;blocker.army_asset_id=2;blocker.runtime_model_class_id=1;
    blocker.building=false;blocker.source_placement_index=2;
    std::vector<SimulationLabSeedEntity> seeds{moving,platform};if(newer_blocker)seeds.push_back(blocker);
    SimulationLabConfig config;config.run_count=1;config.duration_ticks=2;config.parameter_index=100;
    SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=1;
    scenario.ai_planning_enabled=false;std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    SimulationLabExperiment lab;lab.reset(config,parameters,seeds,{},{},{},{},{},scenario);lab.step(field,1);
    const auto& run=lab.runs()[0];
    require(run.entities[0].actor.position.x==moving.actor.position.x,"collision fixture did not stop movement");
    require(bool(run.entities[1].collision_state_bits&1U)==!newer_blocker,
        "docking side effect selected array-first platform instead of newest blocking owner");
}
static void terminal_callback_case(bool newer_scenery){
    const auto field=FieldGrid::create(20,20,0,0);const auto cell=field.at(10,10);
    SimulationLabSeedEntity moving;moving.army_asset_id=1;moving.faction_index=1;
    moving.source_placement_index=1;moving.runtime_model_class_id=1;moving.root_model_maximum_integrity=100;
    moving.actor.position={q12_to_float(cell.world_x_q12)-0.25F,q12_to_float(cell.world_y_q12),0};
    moving.actor.move_target=Vec3f{moving.actor.position.x+2,moving.actor.position.y,0};
    moving.motion.speed_q12_per_tick=128;moving.motion.ground_speed_control=true;
    moving.motion.ground_acceleration_q12=64;moving.motion.collision_radius_q12=1000;
    auto scenery=moving;scenery.army_asset_id=552;scenery.faction_index=0;scenery.runtime_model_class_id=0;
    scenery.source_placement_index=newer_scenery?2:0;scenery.actor.move_target.reset();scenery.motion={};
    scenery.motion.collision_radius_q12=416;scenery.actor.position.x+=0.25F;
    SimulationLabModelComponentSeed component;component.runtime_class_id=0;
    component.maximum_integrity=30;
    component.terminal_attachment_effect_ids[0]=212;
    scenery.model_components={component};
    auto catalog=std::make_shared<SimulationLabRuntimeCatalog>();
    catalog->terminal_effect_completion_ticks[212]=1;
    SimulationLabScenarioRuntimeSeed scene;scene.enabled=true;scene.ai_planning_enabled=false;
    scene.active_faction_count=1;scene.runtime_catalog=catalog;
    SimulationLabConfig config;config.run_count=1;config.duration_ticks=4;
    std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    SimulationLabExperiment lab;const std::array seeds{moving,scenery};
    lab.reset(config,parameters,seeds,{},{},{},{},{},scene);
    lab.runs()[0].entities[1].model_components[0].terminal_attachment_completion_ticks[0]=1;
    lab.step(field,1);
    const auto& c=lab.runs()[0].entities[1].model_components[0];
    require(c.integrity==0,"scenery fixture was not crushed");
    require(c.destruction_complete_tick.has_value()==!newer_scenery,
        "terminal channels ignored whether the scenery callback already ran");
    lab.step(field,1);
    require(lab.runs()[0].entities[1].runtime_removed==!newer_scenery,
        "newer scenery lost its collision owner one callback early");
    lab.step(field,1);require(lab.runs()[0].entities[1].runtime_removed,"terminal effect did not complete");
}
int main()try{docking_case(true);docking_case(false);terminal_callback_case(true);terminal_callback_case(false);std::cout<<"COLLISION_OWNER_ORDER PASS first-blocker docking side effects\n";}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
