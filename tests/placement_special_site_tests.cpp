#include "thandor/retail_special_site.hpp"
#include "thandor/simulation_lab_site_placement.hpp"
#include "thandor/fog_preview.hpp"
#include "thandor/retail_integer_sqrt.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
int main() try {
    static_assert(retail_integer_sqrt(8)==3);
    const std::array<PioneerPlacementOwner,1> tiny{{{{2,2},1,11,0,1,true}}};
    check(pioneer_class14_support(3,2,{0,0},{0,0,0},1,tiny).placement_value==2,
          "support clearance corrected native approximate square root to floor");
    int calls=0;
    const auto chain=[&]{++calls;return true;};
    check(retail_special_site_cluster_rejected(false,100,10,chain)&&calls==0,"failed mode3 reached chain");
    check(retail_special_site_cluster_rejected(true,0,10,chain)&&calls==0,"zero bucket accepted");
    check(!retail_special_site_cluster_rejected(true,40,10,chain)&&calls==1,"small rejected chain must accept cluster");
    check(retail_special_site_cluster_rejected(true,40,10,[]{return false;}),"near chain must reject small cluster");
    check(!retail_special_site_cluster_rejected(true,41,10,chain)&&calls==1,"five buckets must bypass chain");
    check(retail_special_site_cluster_rejected(true,40,0,chain),"invalid quantum divided by zero");
    struct Site{uint32_t army_asset_id;};std::array sites{Site{330},Site{332}};
    for(int mode=0;mode<7;++mode){
        int clusters=0,distances=0;
        auto c=retail_special_site_candidate(mode==1,mode>=2,mode!=3,mode==4,sites,
            1000,1000,100,1,256,0,[&](size_t i){++clusters;return mode==5&&i==0;},
            [&](size_t i,uint32_t ws){++distances;return mode==6&&i==0&&ws==3U?99:100;},
            [](uint32_t){return 0U;});
        if(mode==1||mode==3)check(!c&&clusters==0&&distances==0,"workspace gate still queried sites");
        else {check(c.has_value(),"qualifying site absent");
            check(c->id==(mode>=2?(mode==4?50U:11U):301U)&&c->kind==(mode>=2&&mode!=4?2U:0U),"301/TEC11/ARM50 branch or kind differs");
            check(c->site_index==size_t(mode>=5),"first passing site order differs");}
    }
    check(retail_special_site_weight(1000,0,330,0,0,0)==500,"special weight denominator differs");
    check(retail_special_site_weight(1000,0,332,1,256,0)==500,"special weight used extraction rate instead of current Tritium");
    check(retail_special_site_weight(1000,0,332,0,0,0)==500,"zero current Tritium did not retain weight");
    // Same represented geometry, opposite native decisions for additional-site
    // mode7 and special-candidate mode3. Probes must remain detached.
    auto field=FieldGrid::create(80,80,0,0);
    const auto point=[&](uint32_t x){const auto& c=field.at(x,40);return MovementPoint{c.world_x_q12,c.world_y_q12};};
    const auto xyz=[&](uint32_t x){auto p=point(x);return Vec3f{q12_to_float(p.x),q12_to_float(p.y),0};};
    auto p=point(40);const auto cell=retail_nearest_world_to_cell(field,p.x,p.y);field.at(cell->x,cell->y).flags_and_material|=0x800;
    SimulationLabRun run;run.rng_state=123;
    auto scratch=std::make_shared<SimulationLabRetailScratchImage>();scratch->field_occupancy_masks.assign(field.cells().size(),uint64_t{1}<<8);run.retail_scratch=scratch;
    run.ai_parameters[81]=60000;run.ai_parameters[28]=6144;
    SimulationLabSiteModel resource{330,14,256,0,8192,0,0},storage{333,15,256,0,8192,0,0};
    std::vector<SimulationLabSiteOwner> owners{{{point(30),1,11,0,8192,true},256,{}}};
    const std::array anchors{xyz(33),xyz(36),xyz(38)};
    const auto additional=simulation_lab_reserve_special_site(run,field,owners,resource,storage,p,anchors,1);
    const auto special=simulation_lab_special_site_cluster(run,field,owners,resource,storage,p,anchors,1);
    check(additional.accepted&&!special.accepted&&special.probes_created==3&&!special.chain_rejected,"mode3 reused the mode7 acceptance polarity");
    const auto none=simulation_lab_special_site_cluster(run,field,owners,resource,storage,p,{},1);
    check(none.accepted&&none.chain_rejected&&none.probes_created==0,"failed nearest anchor has wrong cluster polarity");
    run.ai_parameters[28]=std::max(1U,special.clearance_q12/5U);
    const auto large=simulation_lab_special_site_cluster(run,field,owners,resource,storage,p,anchors,1);
    check(large.accepted&&large.probes_created==0,"large cluster unnecessarily reserved chain");
    check(owners.size()==1&&run.rng_state==123&&run.entities.empty(),"cluster probes leaked detached state");

    // Execute a real planning pass: a deposit outside admitted workspace08
    // cannot bootstrap ARM301, even when KI's global resource weight is high.
    auto small=FieldGrid::create(32,32,0,0);small.at(20,20).flags_and_material|=0x800;
    SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=100;cfg.parameter_index=100;cfg.primary_queue_overrides[1].enabled=true;
    std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};ki[1]=1000;
    SimulationLabSeedEntity hq;hq.army_asset_id=300;hq.faction_index=1;hq.runtime_model_class_id=11;hq.root_model_maximum_integrity=1000;
    SimulationLabProductionAsset factory;factory.army_asset_id=301;factory.army_runtime_flags14_raw=0x11;factory.xenite_cost_q4=100;
    SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;scenario.active_faction_count=1;scenario.human_faction_mask=0;scenario.ai_planning_enabled=false;
    scenario.runtime_catalog=std::make_shared<SimulationLabRuntimeCatalog>();
    std::array<SimulationLabFactionEconomySeed,8> economy{};economy[1].xenite_current_q4=10000;economy[1].baseline_xenite_storage_limit_q4=10000;
    SimulationLabExperiment lab;lab.reset(cfg,ki,std::span(&hq,1),economy,std::span(&factory,1),{},{},{},scenario);
    lab.step(small,73);auto& r=lab.runs()[0];r.ai_planning_enabled=true;const auto rng=r.rng_state;lab.step(small,1);
    check(r.ai_special_site_trace[1].tick==74&&r.ai_special_site_trace[1].workspace08_count==0,"planning regression did not evaluate empty workspace08");
    check(!r.ai_special_site_trace[1].selected&&r.secondary_purchase_requests.empty()&&r.rng_state==rng,"global deposit still invented ARM301 candidate/RNG");
    // The special-site kind 0 and strategic kind 1 are separate native
    // entries even when both name ARM301 in the same planning pass.
    auto runtime=std::make_shared<SimulationLabRuntimeCatalog>();
    auto& support=runtime->definitions[1];support.runtime_class_id=11;
    support.simulation.class_specific_19c_raw=1000;support.simulation.movement_collision_radius_q12=256;
    auto& deposit=runtime->definitions[2];deposit.runtime_class_id=14;
    deposit.simulation.movement_collision_radius_q12=256;
    auto& group=runtime->armies[330].model_variant_groups.emplace_back();
    group.parent_group_index=UINT32_MAX;group.variants.resize(1);group.variants[0].model_definition_id=2;
    scenario.runtime_catalog=runtime;
    hq.actor.position={q12_to_float(small.at(5,12).world_x_q12),q12_to_float(small.at(5,12).world_y_q12),0};
    SimulationLabModelComponentSeed component;component.model_definition_id=1;component.runtime_class_id=11;component.maximum_integrity=1000;
    hq.model_components={component};ki[7]=1000;ki[28]=1000;ki[81]=60000;
    small.at(12,12).flags_and_material|=0x800;
    SimulationLabExperiment both;both.reset(cfg,ki,std::span(&hq,1),economy,std::span(&factory,1),{},{},{},scenario);
    both.step(small,73);auto& br=both.runs()[0];br.ai_planning_enabled=true;
    br.pending_asset_requests.push_back({330,1,73,0});
    auto terrain=std::make_shared<SimulationLabRetailScratchImage>(*br.retail_scratch);
    std::fill(terrain->state.begin(),terrain->state.end(),0U);terrain->field_occupancy_masks.assign(small.cells().size(),0);
    terrain->field_occupancy_masks[12*32+12]=uint64_t{0x10}<<8;br.retail_scratch=terrain;
    both.step(small,1);bool kind0=false,kind1=false;
    const auto& pass=br.ai_purchase_trace[1];
    for(uint32_t i=0;i<pass.recorded_count;++i)if(pass.candidates[i].id==301){kind0|=pass.candidates[i].kind==0;kind1|=pass.candidates[i].kind==1;}
    check(br.ai_special_site_trace[1].selected&&kind0&&kind1,"structural candidate transport merged/lost native kind 0");
    std::cout<<"V651_SPECIAL_SITE PASS policy, first-site order, 301/TEC11/ARM50, inverse cluster polarity, detached geometry, no global-resource purchase\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
