#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_catalog.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace thandor;
namespace {
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
SimulationLabSeedEntity source(uint32_t arm,uint32_t cls,std::vector<uint32_t> panel={}) {
    SimulationLabSeedEntity s;
    s.army_asset_id=arm;s.runtime_model_class_id=cls;s.faction_index=1U;s.building=true;
    s.actor.position={10.F,10.F,1.F};s.root_model_maximum_integrity=1000;
    s.research_technology_ids=std::move(panel);return s;
}
SimulationLabTechnologyAsset tech(uint32_t id,int score=100) {
    SimulationLabTechnologyAsset t;
    t.technology_id=id;t.dependency_technology_id=id;
    t.ai_base_candidate_score=score;t.research_duration_ticks=2U;return t;
}
void reset(SimulationLabExperiment& e,const std::vector<SimulationLabSeedEntity>& seeds,
           const std::vector<SimulationLabTechnologyAsset>& technologies,
           bool upgraded=false) {
    SimulationLabConfig c;c.run_count=1;c.duration_ticks=256;c.parameter_index=100;
    c.sweep_mode=SimulationLabSweepMode::Fixed;
    std::array<uint32_t,AiKnowledgeData::kParameterCount> p{};p[0x0D]=4096;
    std::array<FactionTechnologyMask256,8> masks{};
    set_faction_technology_unlocked(masks[1],0U,true);
    if(upgraded) set_faction_technology_unlocked(masks[1],8U,true);
    e.reset(c,p,seeds,{}, {},technologies,masks,{},{});
}
}
int main(int argc,char** argv) try {
    const auto field=FieldGrid::create(59,59,0,0);
    {
        SimulationLabExperiment e;
        reset(e,{source(330,11,{6,7})},{tech(6),tech(7)});
        e.step(field,74);
        require(e.runs()[0].entities[0].research_technology_id==7,"research ties must use descending slots");
        e.step(field,1);auto& entity=e.runs()[0].entities[0];
        entity.runtime_flags|=1U;e.step(field,2);
        require(entity.research_active && entity.research_elapsed_ticks==0,"switched-off research must pause");
        entity.runtime_flags&=~1U;e.step(field,2);
        require(e.runs()[0].metrics.research_jobs_completed==1,"research must resume after power-on");
        require(e.runs()[0].ai_research_trace[1].last_completed_technology_id==7,"completion trace lost TEC id");
    }
    {
        std::vector<uint32_t> first,second;std::vector<SimulationLabTechnologyAsset> technologies;
        for(uint32_t id=6;id<34;++id) { first.push_back(id);technologies.push_back(tech(id,1)); }
        for(uint32_t id=100;id<108;++id) { second.push_back(id);technologies.push_back(tech(id,id==100?1000:2)); }
        SimulationLabExperiment e;reset(e,{source(330,11,first),source(331,11,second)},technologies);
        auto& busy=e.runs()[0].entities[0];busy.research_active=true;
        busy.research_technology_id=220;busy.research_duration_ticks=1000;
        e.step(field,74);
        const auto& trace=e.runs()[0].ai_research_trace[1];
        require(trace.admitted_candidates==32 && trace.candidate_cap_reached,"busy source must participate in capped collection");
        require(trace.best_technology_id==107,"candidate cap or descending order admitted the wrong research");
        require(e.runs()[0].entities[1].research_technology_id==107,"executor must skip busy source separately");
    }
    {
        auto radar=tech(183,100);radar.compatible_model_resolved=true;
        radar.compatible_model_runtime_class_id=9; // unsupported kind-3 class scores zero
        SimulationLabExperiment e;reset(e,{source(330,13,{183})},{radar});e.step(field,74);
        require(e.runs()[0].metrics.research_jobs_queued==0,"factory class must precede radar TEC special case");
    }
    {
        auto s=source(330,11,{6});SimulationLabResearchVariant v;
        v.panel.model_definition_id=1;v.panel.runtime_class_id=11;v.panel.ids[27]=6;
        SimulationLabResearchReplacement replacement;
        replacement.required_technology_id=8;replacement.panel.model_definition_id=2;
        replacement.panel.runtime_class_id=11;replacement.panel.ids[27]=7;
        v.replacements.push_back(replacement);s.research_model_variants.push_back(v);
        for(bool upgraded:{false,true}) {
            SimulationLabExperiment e;reset(e,{s},{tech(6),tech(7)},upgraded);e.step(field,74);
            require(e.runs()[0].entities[0].research_technology_id==(upgraded?7U:6U),"selected root research panel did not follow technology mask");
        }
        SimulationLabExperiment e;reset(e,{s},{tech(6),tech(7)});e.step(field,74);
        auto& run=e.runs()[0];set_faction_technology_unlocked(run.faction_technology_masks[1],8U,true);
        run.entities[0].research_pending_payment=false;
        e.step(field,128);
        require(faction_technology_unlocked(run.faction_technology_masks[1],7U),"live panel stayed frozen after a new unlock");
    }
    {
        SimulationLabExperiment e;reset(e,{source(330,11),source(1,11,{6})},{tech(6)});e.step(field,74);
        require(e.runs()[0].metrics.research_jobs_queued==0,"unit source entered the structure research workspace");
        auto off=source(330,11,{6});off.runtime_flags=1;
        reset(e,{off},{tech(6)});e.step(field,74);
        require(e.runs()[0].metrics.research_jobs_queued==0 && e.runs()[0].ai_research_trace[1].best_technology_id==6,
            "inactive executor must be rejected after candidate scoring");
    }
    if(argc==2) {
        const auto catalog=AssetCatalog::scan_game_root(argv[1]);
        const auto technologies=simulation_lab_prepare_technologies(catalog);
        for(const auto factory_id:{302U,303U,306U}) {
            std::vector<SimulationLabSeedEntity> seeds;
            for(const auto id:{330U,factory_id}) {
                const auto* preset=catalog.army_preset(id);
                require(preset!=nullptr,"stock factory fixture asset missing");
                auto seed=source(id,preset->runtime_model_class_id);
                seed.root_model_maximum_integrity=preset->root_maximum_integrity;
                simulation_lab_prepare_model_technologies(*preset,catalog,seed);
                seeds.push_back(std::move(seed));
            }
            SimulationLabConfig c;c.run_count=1;c.duration_ticks=600;c.parameter_index=100;
            c.sweep_mode=SimulationLabSweepMode::Fixed;
            std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};parameters[0x0D]=4096;
            std::array<FactionTechnologyMask256,8> masks{};
            set_faction_technology_unlocked(masks[1],0,true);
            set_faction_technology_unlocked(masks[1],1,true);
            std::array<SimulationLabFactionEconomySeed,8> economy{};
            economy[1].xenite_current_q4=60000;
            economy[1].baseline_xenite_storage_limit_q4=60000;
            economy[1].baseline_energy_supply_q4=60000;
            SimulationLabExperiment e;e.reset(c,parameters,seeds,economy,{},technologies,masks,{},{});
            const auto grant=factory_id==302U?15U:factory_id==303U?30U:75U;
            require(faction_technology_unlocked(e.runs()[0].faction_technology_masks[1],grant),
                "advanced factory failed to grant its baseline family technology");
            e.step(field,74);
            const auto chosen=e.runs()[0].ai_research_trace[1].best_technology_id;
            require(chosen!=0 && e.runs()[0].metrics.research_jobs_queued==1,
                "stock advanced factory could not select and queue research");
            e.step(field,450);
            require(faction_technology_unlocked(e.runs()[0].faction_technology_masks[1],chosen),
                "stock advanced factory research did not complete");
            std::cout<<"STOCK_RESEARCH factory="<<factory_id<<" grant="<<grant
                <<" researched="<<chosen<<" ticks="<<e.runs()[0].tick<<'\n';
        }
    }
    std::cout<<"V603_RESEARCH_PLANNING PASS\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
