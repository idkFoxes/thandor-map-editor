#include THANDOR_NATIVE_AI_TECHNOLOGY
#include THANDOR_NATIVE_WORKSPACES
#include THANDOR_NATIVE_RANDOM
#include THANDOR_NATIVE_PERCEPTION
#include "thandor/simulation_lab_ai_workspaces.hpp"
#include <iostream>
#include <random>
#include <bit>
GameFactionRuntimeImage g_GameFactionRuntimeImage{};
TechnologyAsset* g_TechnologyAsset{};
TechnologyCategoryMasks g_TechnologyCategoryMasks{};
void Thandor_Ptr32Overflow(uintptr_t){std::abort();}
void Thandor_Log(const char*,...){}
static ModelDefinition lookup_definitions[256]{};
ModelDefinitionRecordPrefix* ModelDefinitionRegistry_FindByRuntimeClassId(ModelRuntimeClassId id) {
    return reinterpret_cast<ModelDefinitionRecordPrefix*>(&lookup_definitions[id]);
}

static ModelRuntimeSlot models[256]{};
static ModelDefinition definitions[256]{};
static AiStructureWorkspaceEntry structures[128]{};
static AiRuntimeWorkspaceEntry units[64]{};
static AiTechnologyPlanningCandidate candidates[32]{};
static struct {TechnologyAssetHeader header;TechnologyRecord records[256];} tech_image{};
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main()try {
    using namespace thandor;
    g_TechnologyAsset=reinterpret_cast<::TechnologyAsset*>(&tech_image);
    g_AiWorkspace00Structures=structures;g_AiWorkspace01Units=units;
    g_AiWorkspace12TechnologyCandidates=candidates;
    const auto field=thandor::FieldGrid::create(59,59,0,0);
    std::mt19937 random(0x652);uint32_t score_checks=0,blocked_checks=0;
    for(uint32_t n=0;n<2400;++n) {
        const auto mode=n%8;const uint32_t count=mode==4||mode==5?132:3;
        std::vector<SimulationLabSeedEntity> seeds;
        for(uint32_t i=0;i<count;++i) {
            SimulationLabSeedEntity s;s.army_asset_id=i+1==count?330:390;
            s.faction_index=1;s.building=true;s.runtime_model_class_id=13;
            s.root_model_maximum_integrity=1000;s.actor.position={10,10,0};
            if(i+1==count || mode==6)for(uint32_t id=6;id<=33;++id)s.research_technology_ids.push_back(id);
            seeds.push_back(s);
        }
        const uint32_t unit_count=random()%71;
        for(uint32_t i=0;i<unit_count;++i) {
            SimulationLabSeedEntity unit;unit.army_asset_id=1;unit.faction_index=1;
            unit.runtime_model_class_id=1;unit.root_model_maximum_integrity=1000;
            unit.actor.position={20,20,0};SimulationLabModelComponentSeed component;
            component.model_definition_id=1006+random()%28+(random()%3)*1000;
            unit.model_components.push_back(component);seeds.push_back(unit);
        }
        std::vector<SimulationLabTechnologyAsset> technologies;
        std::array<FactionTechnologyMask256,8> masks{};set_faction_technology_unlocked(masks[1],0,true);
        auto& faction=g_GameFactionRuntimeImage.records[1];faction={};faction.technologyMasks256Bits[0]=1;
        for(uint32_t id=6;id<=33;++id) {
            SimulationLabTechnologyAsset t;t.technology_id=t.dependency_technology_id=id;
            t.ai_base_candidate_score=std::bit_cast<int32_t>(uint32_t(random()));
            t.research_duration_ticks=100000;t.xenite_cost_q4=400;
            t.compatible_model_resolved=t.compatible_model_lookup_complete=true;
            t.compatible_model_definition_id=1000+id;
            const uint32_t classes[]{1,2,3,17,19,21};t.compatible_model_runtime_class_id=classes[random()%6];
            if((random()%8)==0) {set_faction_technology_unlocked(masks[1],id,true);faction.technologyMasks256Bits[id>>5]|=1U<<(id&31);}
            if((random()%8)==0)t.prerequisite_masks[7]=1;
            auto& record=tech_image.records[id];record={};record.baseCandidateScore=t.ai_base_candidate_score;
            for(size_t j=0;j<8;++j)record.prerequisiteMasks[j]=t.prerequisite_masks[j];
            lookup_definitions[id]={};lookup_definitions[id].definitionId=t.compatible_model_definition_id;
            lookup_definitions[id].runtimeClassId=static_cast<ModelRuntimeClassId>(t.compatible_model_runtime_class_id);
            technologies.push_back(t);
        }
        SimulationLabConfig config;config.run_count=1;config.duration_ticks=200;config.parameter_index=100;
        std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};ki[13]=4096;
        std::array<SimulationLabFactionEconomySeed,8> economy{};
        economy[1].xenite_current_q4=economy[1].baseline_xenite_storage_limit_q4=100000;
        SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=1;
        SimulationLabExperiment lab;lab.reset(config,ki,seeds,economy,{},technologies,masks,{},scenario);
        auto& run=lab.runs()[0];run.tick=73;
        if(mode<=2)run.secondary_purchase_requests.push_back({302+mode*2,1,0,0});
        if(mode==3 || mode==4 || mode==5) {
            auto& job=run.entities[mode==4?0:count-2];job.research_technology_id=6;
            job.runtime_flags=0x80;
        }
        if(mode==7)run.entities[count-1].construction_complete=false;
        const auto workspace=simulation_lab_ai_own_workspaces(run,1);
        g_AiWorkspace00Count=workspace.structures.size();g_AiWorkspace01Count=workspace.units.size();
        for(size_t i=0;i<run.entities.size();++i) {
            const auto& e=run.entities[i];models[i]={};definitions[i]={};
            definitions[i].runtimeClassId=static_cast<ModelRuntimeClassId>(e.runtime_model_class_id);
            if(!e.model_components.empty())definitions[i].definitionId=e.model_components[e.root_model_component_index].model_definition_id;
            for(size_t j=0;j<e.research_technology_ids.size();++j)definitions[i].researchTechnologyIds[j+1]=e.research_technology_ids[j];
            models[i].definitionOrSavedId.runtimeDefinition=&definitions[i];
            models[i].classState.stateFlags=static_cast<ArmyRuntimeFlags>(e.runtime_flags);
            models[i].researchTechnologyId=e.research_technology_id;
        }
        for(size_t i=0;i<workspace.structures.size();++i) {
            const auto& entry=workspace.structures[i];structures[i].armyAssetId=entry.army_asset_id;
            structures[i].runtimeSlotAddressOrZero=entry.entity_index?&models[*entry.entity_index]:nullptr;
        }
        for(size_t i=0;i<workspace.units.size();++i) {
            const auto& entry=workspace.units[i];units[i].armyAssetId=entry.army_asset_id;
            units[i].modelRuntime=entry.entity_index?&models[*entry.entity_index]:nullptr;
        }
        // Native collection/availability/score bodies, with controlled resolved MDL lookup.
        g_AiWorkspace12Count=0;AiPlanningRebuild_CollectResearchCandidates(1);
        std::vector<std::pair<uint32_t,uint32_t>> expected;
        uint32_t best_id=0;int32_t best_score=0;
        for(size_t i=0;i<g_AiWorkspace12Count;++i) {
            const auto id=candidates[i].technologyId00;
            const int32_t score=AiTechnologyScore_ComputeRuntimeClassCompatibleCandidateValue(1,id,nullptr);
            expected.emplace_back(id,uint32_t(score));
            if(score>best_score){best_score=score;best_id=id;}
        }
        lab.step(field,1);const auto& trace=run.ai_research_trace[1];
        if(trace.candidate_count!=expected.size() || trace.best_technology_id!=best_id || trace.best_score!=uint32_t(best_score)) {
            std::cerr<<"case="<<n<<" mode="<<mode<<" native_count="<<expected.size()<<" lab_count="<<trace.candidate_count<<" native_best="<<best_id<<" lab_best="<<trace.best_technology_id<<'\n';
            throw std::runtime_error("original research admission/best-score mismatch");
        }
        for(size_t i=0;i<expected.size();++i) {
            check(trace.candidates[i].technology_id==expected[i].first,"original research collection order mismatch");
            if(trace.candidates[i].score!=expected[i].second) {
                std::cerr<<"case="<<n<<" mode="<<mode<<" TEC="<<expected[i].first<<" native_score="<<expected[i].second<<" lab_score="<<trace.candidates[i].score<<'\n';
                throw std::runtime_error("original research per-candidate score mismatch");
            }
            ++score_checks;
        }
        blocked_checks+=mode==3||mode==4||mode==5;
    }
    std::cout<<"V652_NATIVE_RESEARCH PASS passes=2400 candidate_scores="<<score_checks<<" raw_assignment_workspace_cases="<<blocked_checks<<"; original collection, availability and runtime-class scoring bodies; controlled model lookup, no full native game loop\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
