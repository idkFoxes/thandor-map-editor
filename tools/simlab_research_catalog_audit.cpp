#include "thandor/simulation_lab_catalog.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace thandor;
// Read stock metadata only. No experiment is constructed or stepped.
int main(int argc,char** argv) try {
    if(argc!=2) throw std::runtime_error("usage: simlab_research_catalog_audit GAME_ROOT");
    const auto catalog=AssetCatalog::scan_game_root(argv[1]);
    for(const auto& preset:catalog.army_presets()) {
        if(preset.registry_id==0) continue;
        SimulationLabSeedEntity seed;
        simulation_lab_prepare_model_technologies(preset,catalog,seed);
        if(preset.registry_id>=300 && preset.registry_id<=306) {
            std::cout<<"FACTORY arm="<<preset.registry_id<<" class="<<preset.runtime_model_class_id
                <<" producer_mask="<<preset.runtime_class_specific_c4_raw<<" required=";
            for(const auto& g:seed.linked_model_technologies)
                for(const auto& v:g.variants) if(v.variant_index==0) std::cout<<v.required_technology_id<<',';
            std::cout<<" grants=";
            for(const auto& g:seed.linked_model_technologies)
                for(const auto& v:g.variants) if(v.variant_index==0) std::cout<<v.granted_technology_id<<',';
            std::cout<<'\n';
        }
        for(const auto& variant:seed.research_model_variants) {
            const auto emit=[&](const SimulationLabResearchPanel& panel,uint32_t required,bool replacement) {
                if(std::none_of(panel.ids.begin(),panel.ids.end(),[](auto id){return id!=0;})) return;
                std::cout<<"PANEL arm="<<preset.registry_id<<" model="<<panel.model_definition_id
                    <<" variant="<<variant.variant_index<<" replacement="<<replacement
                    <<" required="<<required<<" class="<<panel.runtime_class_id<<" ids=";
                for(auto it=panel.ids.rbegin();it!=panel.ids.rend();++it) if(*it!=0) std::cout<<*it<<',';
                std::cout<<'\n';
            };
            emit(variant.panel,variant.required_technology_id,false);
            for(const auto& r:variant.replacements) emit(r.panel,r.required_technology_id,true);
        }
        if(preset.registry_id<300 && (preset.army_runtime_flags14_raw&1U)!=0) {
            std::cout<<"UNIT arm="<<preset.registry_id<<" class="<<preset.runtime_model_class_id
                <<" flags="<<preset.army_runtime_flags14_raw<<" required=";
            for(const auto& g:seed.linked_model_technologies)
                for(const auto& v:g.variants) if(v.variant_index==0) std::cout<<v.required_technology_id<<',';
            std::cout<<'\n';
        }
    }
    for(const auto& t:catalog.technologies()) {
        std::cout<<"TEC id="<<t.technology_id<<" category="<<static_cast<uint32_t>(t.category)<<" cost="<<t.xenite_cost_q4
            <<" duration="<<t.research_duration_q5<<" score="<<t.ai_base_candidate_score<<" prereq=";
        for(uint32_t id=0;id<256;++id)
            if((t.prerequisite_masks[id>>5]&(1U<<(id&31U)))!=0) std::cout<<id<<',';
        std::cout<<'\n';
    }
    for(const auto& t:simulation_lab_prepare_technologies(catalog))
        std::cout<<"COMPAT tec="<<t.technology_id<<" resolved="<<t.compatible_model_resolved
            <<" model="<<t.compatible_model_definition_id<<" class="<<t.compatible_model_runtime_class_id<<'\n';
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
