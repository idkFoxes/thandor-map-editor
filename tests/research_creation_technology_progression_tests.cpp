#include "thandor/simulation_lab_catalog.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thandor;
void require(bool ok,const char* text) { if(!ok) throw std::runtime_error(text); }
SimulationLabLinkedModelGroup group(uint32_t required,uint32_t grant) {
    SimulationLabLinkedModelGroup g;
    SimulationLabLinkedModelTechnology v;
    v.required_technology_id=required; v.granted_technology_id=grant;
    v.score.resolved=true; v.score.root_definition_value0c=64;
    v.score.root_definition_value18=1; v.score.root_definition_value60=1000;
    g.variants.push_back(v); return g;
}
int main() try {
    SimulationLabConfig config; config.run_count=1; config.duration_ticks=1000;
    config.sweep_mode=SimulationLabSweepMode::Fixed; config.parameter_index=100;
    std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    parameters[0x09]=4096;
    std::array<SimulationLabFactionEconomySeed,8> economy{};
    economy[1].xenite_current_q4=10000;
    std::array<FactionTechnologyMask256,8> masks{};
    std::array<SimulationLabTechnologyAsset,3> technologies{};
    for(uint32_t i=0;i<3;++i) { technologies[i].technology_id=i; technologies[i].dependency_technology_id=i; }
    technologies[2].dependency_technology_id=1;
    SimulationLabSeedEntity factory;
    factory.army_asset_id=301; factory.faction_index=1; factory.runtime_model_class_id=13;
    factory.building=true; factory.production_compatibility_mask=0x20;
    factory.production_spawn_local=Vec3f{2,0,0};
    factory.production_exit_local=Vec3f{4,0,0};
    factory.actor.body_yaw_turn16=0x4000;
    factory.motion.speed_q12_per_tick=0x80000; // factory door step
    factory.root_model_maximum_integrity=1000; factory.actor.position={20,-20,0};
    // Required bit 7 differs from granted bit 2. Granting a prerequisite/required
    // bit instead of +0x1C4 would leave TEC1 locked and fail this live planner test.
    factory.linked_model_technologies.push_back(group(7,2));
    auto root=SimulationLabSeedEntity{};
    root.army_asset_id=330; root.faction_index=1; root.root_model_maximum_integrity=1000;
    root.actor.position={16,-20,0};
    SimulationLabProductionAsset unit;
    unit.army_asset_id=100; unit.army_runtime_flags14_raw=0x21;
    unit.ai_faction_score_seeds_prepared=true; unit.ai_definition_class_value74=50;
    unit.xenite_cost_q4=100; unit.build_duration_ticks=2;
    unit.entity_seed.army_asset_id=100; unit.entity_seed.runtime_model_class_id=2;
    unit.entity_seed.motion.speed_q12_per_tick=4096;
    unit.entity_seed.motion.yaw_max_velocity_turn16=0x7FFF;
    unit.entity_seed.motion.yaw_acceleration_turn16=0x7FFF;
    unit.entity_seed.root_model_maximum_integrity=1000;
    unit.entity_seed.linked_model_technologies.push_back(group(1,0));
    // Deliberately stale startup seeds say locked. Current masks must override them.
    require(!simulation_lab_current_ai_score_seed(unit,masks[1],1).all_selected_model_technologies_unlocked,
            "unit admitted before creation grant");
    SimulationLabExperiment experiment;
    std::array seeds{factory,root}; std::array products{unit};
    experiment.reset(config,parameters,seeds,economy,products,technologies,masks,{});
    auto& run=experiment.runs().front();
    require(faction_technology_unlocked(run.faction_technology_masks[1],2)&&
            faction_technology_unlocked(run.faction_technology_masks[1],1)&&
            !faction_technology_unlocked(run.faction_technology_masks[1],7),
            "creation grant/dependency confused granted and required technologies");
    require(run.metrics.research_jobs_completed==0,"creation grant reported as research");
    run.entities[0].synthetic=true; // same pose/metadata as a committed AI factory
    auto field=FieldGrid::create(59,59,0,0);
    experiment.step(field,74);
    require(run.metrics.production_jobs_started==0,"world job started a producer that already ran this tick");
    experiment.step(field,1);
    require(run.metrics.production_jobs_started==1,"next owner callback did not accept the unlocked product");
    experiment.step(field,1);
    require(run.metrics.class13_units_spawned==0,"two-tick product completed before both progress callbacks");
    experiment.step(field,1);
    require(run.metrics.class13_units_spawned==1,"startup-locked unit never became producible");
    require(run.entities[0].production_exit_world&&
            std::abs(run.entities[0].production_exit_world->x-20)<0.01F&&
            std::abs(run.entities[0].production_exit_world->y+16)<0.01F,
            "synthetic factory lost rotated authored exit lookup");
    experiment.step(field,10);
    require(!run.entities.back().production_linked_to_parent&&
            run.entities.back().actor.position.y>-18.0F,
            "produced unit never drove out and released its factory");
    // Scoring must reselect upgraded roots and the first two child weapons.
    auto upgrade=unit.entity_seed.linked_model_technologies[0].variants[0];
    upgrade.variant_index=1; upgrade.required_technology_id=2;
    upgrade.score.root_definition_value60=4000;
    unit.entity_seed.linked_model_technologies[0].variants.push_back(upgrade);
    require(simulation_lab_current_ai_score_seed(unit,run.faction_technology_masks[1],1).
        root_definition_value60==4000,"research did not refresh AI scoring variant");
    auto child=group(1,0); child.parent_index=0; child.child_slot=0;
    child.variants[0].weapon.valid=true; child.variants[0].weapon.reload_divisor30=4;
    unit.entity_seed.linked_model_technologies.push_back(child);
    auto score=simulation_lab_current_ai_score_seed(unit,run.faction_technology_masks[1],1);
    require(score.child_list_count==1&&score.child_weapons[0].reload_divisor30==4,
            "current child weapon score was lost");
    std::cout << "V598 creation grants and live technology eligibility passed\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
