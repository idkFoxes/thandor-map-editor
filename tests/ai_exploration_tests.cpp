#include "thandor/simulation_lab.hpp"
#include "thandor/retail_random.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main() try {
    const auto field=FieldGrid::create(32,32,0,0);
    std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};ki[10]=100;
    SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=200;cfg.parameter_index=100;
    SimulationLabSeedEntity hq;hq.army_asset_id=300;hq.runtime_model_class_id=11;
    hq.faction_index=1;hq.building=true;hq.root_model_maximum_integrity=1000;
    hq.actor.position={q12_to_float(field.at(12,12).world_x_q12),q12_to_float(field.at(12,12).world_y_q12),0};
    SimulationLabProductionAsset unit;unit.army_asset_id=2;unit.army_runtime_flags14_raw=0x11;
    unit.xenite_cost_q4=10;unit.build_duration_ticks=1000;
    unit.entity_seed.army_asset_id=2;unit.entity_seed.runtime_model_class_id=2;
    unit.ai_faction_score_seeds_prepared=true;
    unit.ai_baseline_root_definition_value0c=64;unit.ai_baseline_root_definition_value18=1;
    unit.ai_baseline_root_definition_value60=1000;unit.ai_definition_class_value74=50;
    for(auto& s:unit.ai_faction_score_seeds){s.resolved=s.all_selected_model_technologies_unlocked=true;
        s.root_definition_value0c=64;s.root_definition_value18=1;s.root_definition_value60=1000;}
    SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;
    scenario.active_faction_count=1;scenario.human_faction_mask=0;scenario.ai_planning_enabled=false;
    std::array<SimulationLabFactionEconomySeed,8> economy{};economy[1].xenite_current_q4=100000;
    economy[1].baseline_xenite_storage_limit_q4=100000;
    cfg.primary_queue_overrides[1].enabled=true;
    std::array<uint32_t,2> states{};
    for(bool site:{false,true}){
        SimulationLabExperiment lab;lab.reset(cfg,ki,std::span(&hq,1),economy,std::span(&unit,1),{},{},{},scenario);
        lab.step(field,73);auto& r=lab.runs()[0];r.ai_planning_enabled=true;
        auto scratch=std::make_shared<SimulationLabRetailScratchImage>(*r.retail_scratch);
        std::fill(scratch->state.begin(),scratch->state.end(),0U);
        scratch->field_occupancy_masks.assign(32*32,0);
        if(site)scratch->field_occupancy_masks[12*32+12]=uint64_t{0x10}<<8;
        r.retail_scratch=scratch;
        const auto before=r.rng_state;
        lab.step(field,1);
        const auto& diag=r.ai_workspace_diagnostics[1];const auto& profile=r.ai_army_profile_trace[1];
        check(diag.last_planning_tick==74,"fixture missed economy planning");
        check((diag.general05!=0)==site,"fixture general workspace admission differs");
        check(profile.workspace11_count==1,"fixture positive unit not admitted");
        check(profile.profile_invocations[1]==uint32_t(site),"exploration ignored native workspace05 gate");
        const auto& purchase=r.ai_purchase_trace[1];
        bool found=false;for(uint32_t i=0;i<purchase.recorded_count;++i)found|=purchase.candidates[i].id==2;
        check(found==site,"exploration purchased with no admitted site or lost eligible purchase");
        auto expected=before;
        // Only the positive B profile reaches weighted candidate insertion.
        if(site)retail_random_next(expected);
        check(r.rng_state==expected,"empty general workspace consumed candidate RNG");
        states[site]=r.rng_state;
    }
    std::cout<<"V650_AI_EXPLORATION PASS empty_map_workspace=no_purchase,no_rng admitted_site=purchase,one_rng state_empty="<<states[0]<<" state_site="<<states[1]<<'\n';
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
