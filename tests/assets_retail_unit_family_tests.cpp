#include "thandor/ai_unit_producibility_v601.hpp"
#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_catalog.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void check(bool good, const char* msg) { if(!good) throw std::runtime_error(msg); }
SimulationLabLinkedModelGroup tech_group(uint32_t requirement, uint32_t grant) {
    SimulationLabLinkedModelGroup group;
    group.variants.push_back({});
    group.variants.back().required_technology_id=requirement;
    group.variants.back().granted_technology_id=grant;
    group.variants.back().score.resolved=true;
    group.variants.back().score.root_definition_value60=2500;
    group.variants.back().score.root_definition_value0c=256;
    group.variants.back().score.root_definition_value18=1;
    return group;
}
SimulationLabSeedEntity make_factory(uint32_t faction, uint32_t arm,
                                   uint32_t class_mask, uint32_t grant, float x) {
    SimulationLabSeedEntity factory;
    factory.army_asset_id=arm;
    factory.faction_index=faction;
    factory.runtime_model_class_id=13;
    factory.building=true;
    factory.production_compatibility_mask=class_mask;
    factory.root_model_maximum_integrity=20000;
    factory.actor.position={x,-20,0};
    factory.production_spawn_local=Vec3f{2,0,0};
    factory.production_exit_local=Vec3f{4,0,0};
    factory.motion.speed_q12_per_tick=0x80000;
    factory.linked_model_technologies.push_back(tech_group(0,grant));
    return factory;
}
SimulationLabProductionAsset make_unit(uint32_t arm,uint32_t cls,uint32_t flags,uint32_t required) {
    SimulationLabProductionAsset unit;
    unit.army_asset_id=arm;
    unit.army_runtime_flags14_raw=flags;
    unit.ai_faction_score_seeds_prepared=true;
    unit.ai_definition_class_value74=150;
    unit.xenite_cost_q4=80;
    unit.build_duration_ticks=2;
    unit.entity_seed.army_asset_id=arm;
    unit.entity_seed.runtime_model_class_id=cls;
    unit.entity_seed.root_model_maximum_integrity=2500;
    unit.entity_seed.motion.speed_q12_per_tick=1000;
    unit.entity_seed.motion.yaw_max_velocity_turn16=0x7fff;
    unit.entity_seed.motion.yaw_acceleration_turn16=0x7fff;
    unit.entity_seed.linked_model_technologies.push_back(tech_group(required,0));
    return unit;
}
}
int main() try {
    // Retail ARM.PCK +0x14 flags, factory MDL +0xC4 producer-mask:
    // ARM301 (0x40) -> ground, ARM302 (0x02) -> tracked,
    // ARM303 (0x20) -> walker, ARM304 (0x80) -> deploying glider;
    // ARM305 (0x04) -> water class (not permitted on Asgard),
    // class22 aircraft pad takes producer bit 0x08.
    check(ai_v601_workspace11_candidate(1,321,0x40,true),"ARM1 missing from ground factory");
    check(ai_v601_workspace11_candidate(60,259,0x02,true),"ARM60 missing from tracked factory");
    check(ai_v601_workspace11_candidate(112,289,0x20,true),"ARM112 missing from walker factory");
    check(ai_v601_workspace11_candidate(170,385,0x80,true),"ARM170 missing from glider factory");
    check(ai_v601_workspace11_candidate(240,9,0x08,true),"ARM240 missing from aircraft pad");
    check(ai_v601_workspace11_candidate(250,261,0x04,true),"ARM250 missing from water factory");
    check(!ai_v601_workspace11_candidate(250,261,0xA0,true),"naval unit admitted by ground/glider factories");
    check(!ai_v601_workspace11_candidate(112,289,0x20,false),"locked walker admitted");
    check(!ai_v601_workspace11_candidate(303,273,0x10,true),"ARM303 added to workspace11");
    check(ai_v601_workspace11_candidate(370,273,0x10,true),"retail turret absent from workspace11");
    check(ai_v601_producer_accepts(13,0x20,289),"walker factory rejected walker");
    check(ai_v601_producer_accepts(13,0x80,385),"glider factory rejected glider");
    check(!ai_v601_producer_accepts(13,0x40,289),"ground factory accepted walker");
    check(ai_v601_producer_accepts(11,0,273),"constructor rejected turret");
    check(ai_v601_producer_accepts(22,0,9),"aircraft pad rejected aircraft");
    check(!ai_v601_producer_accepts(13,0xEE,0x19),"producer dispatch lost retail precedence");

    // Live Lab integration: the ARM303 / ARM304 creation grants unlock the
    // class3 / class17 products without any artificial technology purchases.
    SimulationLabConfig config;
    config.run_count=1;
    config.duration_ticks=500;
    config.sweep_mode=SimulationLabSweepMode::Fixed;
    config.parameter_index=100;
    std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};
    ki[0x09]=4096;
    std::array<SimulationLabFactionEconomySeed,8> economy{};
    economy[1].xenite_current_q4=10000;
    economy[2].xenite_current_q4=10000;
    std::array<FactionTechnologyMask256,8> masks{};
    set_faction_technology_unlocked(masks[1],0,true);
    set_faction_technology_unlocked(masks[2],0,true);
    // Normal progression already received TEC1 from ARM301 before ARM303/304.
    set_faction_technology_unlocked(masks[1],1,true);
    set_faction_technology_unlocked(masks[2],1,true);
    std::array<SimulationLabTechnologyAsset,46> technologies{};
    for(uint32_t i=0;i<technologies.size();++i){
        technologies[i].technology_id=i;
        technologies[i].dependency_technology_id=i;
    }
    auto factory_walker=make_factory(1,303,0x20,30,15.0F);
    auto factory_glider=make_factory(2,304,0x80,45,40.0F);
    auto walker=make_unit(112,3,289,30);
    auto glider=make_unit(170,17,385,45);
    check(!simulation_lab_current_ai_score_seed(walker,masks[1],1).all_selected_model_technologies_unlocked,
        "walker prematurely unlocked");
    check(!simulation_lab_current_ai_score_seed(glider,masks[2],2).all_selected_model_technologies_unlocked,
        "glider prematurely unlocked");
    SimulationLabExperiment experiment;
    SimulationLabSeedEntity base_walker;
    base_walker.army_asset_id=330; base_walker.faction_index=1;
    base_walker.building=true; base_walker.runtime_model_class_id=14;
    base_walker.actor.position={12,-20,0}; base_walker.root_model_maximum_integrity=1000;
    SimulationLabSeedEntity base_glider=base_walker;
    base_glider.faction_index=2; base_glider.actor.position={43,-20,0};
    std::array factories{factory_walker,factory_glider,base_walker,base_glider};
    std::array products{walker,glider};
    experiment.reset(config,ki,factories,economy,products,technologies,masks,{});
    auto& run=experiment.runs().front();
    run.entities[0].synthetic=true;
    run.entities[1].synthetic=true;
    check(faction_technology_unlocked(run.faction_technology_masks[1],30),"ARM303 creation did not grant TEC30");
    check(faction_technology_unlocked(run.faction_technology_masks[2],45),"ARM304 creation did not grant TEC45");
    check(simulation_lab_current_ai_score_seed(walker,run.faction_technology_masks[1],1).
        all_selected_model_technologies_unlocked,"walker not unlocked after ARM303");
    check(simulation_lab_current_ai_score_seed(glider,run.faction_technology_masks[2],2).
        all_selected_model_technologies_unlocked,"glider not unlocked after ARM304");
    const auto field=FieldGrid::create(59,59,0,0);
    experiment.step(field,250);
    bool seen_walker=false, seen_glider=false;
    for(const auto& entity:run.entities) {
        if(entity.army_asset_id==112 && entity.runtime_model_class_id==3) seen_walker=true;
        if(entity.army_asset_id==170 && entity.runtime_model_class_id==17) seen_glider=true;
    }
    if(!seen_walker || !seen_glider) {
        std::cerr << "DEBUG unit_spawned=" << run.metrics.class13_units_spawned
                  << " cycles=" << run.metrics.planning_cycles
                  << " rebuilds=" << run.metrics.ai_candidate_cache_rebuilds
                  << " purchase=" << run.metrics.ai_purchase_candidates_applied
                  << " secondary=" << run.secondary_purchase_requests.size()
                  << " cooldown=" << run.ai_candidate_cache_reuse_state[1]
                  << " xenite=" << run.faction_economy[1].xenite_current_q4
                  << " unit1mask=" << run.faction_technology_masks[1][0]
                  << " unit2mask1=" << run.faction_technology_masks[2][1]
                  << " entities=" << run.entities.size() << '\n';
        for(auto &e:run.entities) std::cerr << "DENT " << e.faction_index << " " << e.army_asset_id
                  << " class=" << e.runtime_model_class_id << " prod=" << e.production_building
                  << " state=" << e.production_behavior_state << " synthetic=" << e.synthetic << '\n';
    }
    check(run.ai_army_profile_trace[1].profile_invocations[0] != 0U,
          "walker defense profile never evaluated");
    check(run.ai_army_profile_trace[2].profile_invocations[0] != 0U,
          "glider defense profile never evaluated");
    check(run.ai_army_profile_trace[1].best_asset_id[0] == 112U,
          "walker did not win its sole retail defense profile");
    check(run.ai_army_profile_trace[2].best_asset_id[0] == 170U,
          "glider did not win its sole retail defense profile");
    check(seen_walker,"ARM303 did not manufacture walker ARM112");
    check(seen_glider,"ARM304 did not manufacture glider ARM170");
    std::cout << "V601 retail producer masks, turret/aircraft dispatch and live walker/glider builds PASS\n";
} catch(const std::exception& ex){ std::cerr<<ex.what()<<'\n'; return 1; }
