#include "thandor/simulation_lab_targeting.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/fog_preview.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
static void require(bool check,const char* message) {if(!check)throw std::runtime_error(message);}
int main()try {
    auto field=FieldGrid::create(90,55,0,0);
    SimulationLabRun run;
    run.entities.resize(4);
    // Source healer (index 0), damaged friendly (1), nearby enemy (2), full-health friendly (3).
    for(size_t i=0;i<run.entities.size();++i){
        auto& e=run.entities[i];
        e.faction_index=i==2?2:1;
        e.construction_complete=true;
        e.runtime_model_class_id=2;
        e.shot_impact_class_index=i==0?1:2;
        e.root_model_maximum_integrity=4096;
        e.root_model_integrity=i==1?2048:4096;
        const auto& c=field.at(20+static_cast<uint32_t>(i),21);
        e.actor.position={q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),0};
    }
    SimulationWeaponProfile p; p.shot_definition_id=80;p.reload_ticks=1;p.maximum_range_q12=30*4096;
    p.launch_attachment_count=1;p.pitch_min_turn16=-0x4000;p.pitch_max_turn16=0x4000;
    ShotDefinitionRecord repair;repair.definition_id=80;repair.trajectory_mode=2;
    repair.mode2_selection_range_q12=30*4096;repair.projectile_lifetime_ticks=20;
    repair.target_impact_damage_q12[0]=-4096;
    repair.target_impact_damage_q12[2]=-4096;
    run.entities[0].combat_weapons={{p,repair}};
    run.current_visibility_occupancy.assign(field.cells().size(),1ULL<<8U);
    run.faction_packed_relation_states[1]=0x11111111U | (15U<<4U);
    run.faction_capability_flags[1]|=1U<<1U;
    const auto target=simulation_lab_select_combat_target(run,field,0);
    require(target.entity_index==1,"damaged friend must be selected, not enemy/full-health ally");
    require(target.score.value>0,"repair selection must have valid numeric score");
    run.entities[1].runtime_flags=0x400U;
    require(!simulation_lab_select_combat_target(run,field,0).entity_index,"no-regeneration target must be rejected");
    run.entities[1].runtime_flags=0;
    run.entities[1].root_model_integrity=4096;
    require(!simulation_lab_select_combat_target(run,field,0).entity_index,"full-health ally must be rejected");
    run.entities[1].root_model_integrity=2048;
    run.entities[1].faction_index=2;
    require(!simulation_lab_select_combat_target(run,field,0).entity_index,"repair must not select wounded enemy");
    run.entities[1].faction_index=1;
    run.entities[0].combat_weapons[0].shot.target_impact_damage_q12[2]=4096;
    require(!simulation_lab_select_combat_target(run,field,0).entity_index,"positive class counter must not repair friendly target");
    // Run the real combat callback, not just the selection helper. This checks
    // that negative-damage weapons reach the projectile/firing pipeline.
    std::array<SimulationLabSeedEntity,2> seeds{};
    for(size_t i=0;i<2;++i){
        const auto& e=run.entities[i];auto& seed=seeds[i];
        seed.army_asset_id=static_cast<uint32_t>(100+i);
        seed.faction_index=1;
        seed.runtime_model_class_id=e.runtime_model_class_id;
        seed.shot_impact_class_index=e.shot_impact_class_index;
        seed.actor=e.actor;
        seed.root_model_maximum_integrity=4096;
        seed.fog_uncover_radius_q12=30*4096;
        if(i==0)seed.combat_weapons={{p,repair}};
    }
    SimulationLabScenarioRuntimeSeed scenario;
    scenario.enabled=true;
    scenario.active_faction_count=scenario.assignable_faction_count=2;
    scenario.simulate_all_active_factions_as_ai=true;
    scenario.runtime_catalog=std::make_shared<SimulationLabRuntimeCatalog>();
    SimulationLabConfig config;config.run_count=1;config.duration_ticks=70;
    std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
    SimulationLabExperiment lab;
    lab.reset(config,ki,seeds,{},{},{},{},{},scenario);
    auto& live=lab.runs()[0];
    live.entities[1].root_model_integrity=2048;
    if(!live.entities[1].model_components.empty())live.entities[1].model_components[0].integrity=2048;
    lab.step(field,4);
    require(live.metrics.weapon_shots_fired>0,"repair weapon must be allowed to fire at friendly target");
    // A synthetic empty catalog has no SPR collision hulls, so this fixture only
    // verifies firing. Healing needs a separate asset-backed projectile contact test.
    std::cout<<"V706_REPAIR_TARGETING PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
