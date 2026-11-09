#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/binary.hpp"
#include "thandor/simulation_command_markers.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
SimulationLabSeedEntity seed(){
    SimulationLabSeedEntity s;s.army_asset_id=1;s.faction_index=1;s.actor.position={3,-1,0};
    s.runtime_model_class_id=2;s.water_damage_multiplier=1;
    SimulationLabModelComponentSeed c;c.maximum_integrity=1;c.runtime_class_id=2;
    c.terminal_attachment_effect_ids[0]=53;c.source_model_node_index=0;
    c.has_resource_storage_capacity_contribution=true;c.resource_storage_capacity_contribution_q4=100;
    s.model_components.push_back(c);return s;
}
}
int main(int argc,char** argv)try{
    std::vector<std::byte> bytes(0x2c0);write_u32(bytes,0,fourcc("eff"));write_u32(bytes,4,bytes.size());
    write_u32(bytes,0xc,0x40007);write_u32(bytes,0xb0,1);
    write_u32(bytes,0x200,1);write_u32(bytes,0x208,53);write_u32(bytes,0x20c,24);
    write_u32(bytes,0x224,10);write_u32(bytes,0x240,30);
    write_u16(bytes,0x27c,uint16_t('x'));
    auto eff=EffectAsset::parse(bytes).definitions()[0];
    check(eff.completion_countdown_ticks==10,"EFF +0x24 decode");eff.frame_advance_threshold_q4=30;
    check(effect_model_completion_tick(eff)==19,"EFF53 owner completion must take 19 ticks");
    eff.frame_advance_threshold_q4=20;check(effect_model_completion_tick(eff)==13,"EFF50 completion must take 13 ticks");
    eff.completion_countdown_ticks=0;check(!effect_model_completion_tick(eff),"zero counter completed immediately");
    eff.completion_countdown_ticks=24;check(!effect_model_completion_tick(eff),"expiry must precede callback");
    eff.completion_countdown_ticks=10;eff.transition_kind=4;check(!effect_model_completion_tick(eff),"kind4 requires contact state");
    auto field=FieldGrid::create(16,16,0,0);
    for(uint32_t y=0;y<field.height();++y)for(uint32_t x=0;x<field.width();++x)field.at(x,y).water_delta_q12=128;
    // Terrain blocking is a model-band property, independent from timed lava shots.
    const auto a=v519_path_world_to_scratch(field.at(6,6).world_x_q12,field.at(6,6).world_y_q12);
    const auto b=v519_path_world_to_scratch(field.at(9,6).world_x_q12,field.at(9,6).world_y_q12);
    const auto masks=v519_path_masks(1,0,4);V565PathTerrainCache cache;
    for(uint32_t y=0;y<field.height();++y)for(uint32_t x=0;x<field.width();++x)field.at(x,y).water_delta_q12=600;
    cache.prepare(field);
    check(!v565_path_segment_clear_static_cached(field,a,b,masks.faction_presence_mask,
        masks.high_cost_mask,masks.blocking_mask,{},0,cache),"ground band4 crossed deep water");
    for(uint32_t y=0;y<field.height();++y)for(uint32_t x=0;x<field.width();++x)field.at(x,y).water_delta_q12=128;
    cache.prepare(field);
    check(v565_path_segment_clear_static_cached(field,a,b,masks.faction_presence_mask,
        masks.high_cost_mask,masks.blocking_mask,{},0,cache),"ground band4 rejected shallow water");
    auto s=seed();std::array<uint32_t,AiKnowledgeData::kParameterCount> params{};
    SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=64;
    std::array<SimulationLabFactionEconomySeed,8> economy{};
    economy[1].derive_storage_limits_from_components=true;economy[1].baseline_xenite_storage_limit_q4=100;
    economy[1].xenite_current_q4=100;
    SimulationLabExperiment lab;lab.reset(cfg,params,std::span(&s,1),economy);
    auto& run=lab.runs()[0];auto& root=run.entities[0].model_components[0];
    root.terminal_attachment_metadata_prepared=true;root.terminal_attachment_point_mask=1;
    root.terminal_attachment_completion_ticks[0]=19;
    lab.step(field);
    check(root.terminal_self_reference&&run.entities[0].root_model_terminal,"water impact must start root destruction");
    check(root.destruction_complete_tick==20,"root channel skipped or completion timing drifted");
    check(simulation_lab_one_shot_events_after(run,0).size()==1,"root channel must emit once");
    check(run.faction_economy[1].xenite_storage_limit_q4==200,"capacity released before hierarchy");
    lab.step(field,18);check(!run.entities[0].runtime_removed,"model released early");
    lab.step(field);check(run.entities[0].runtime_removed&&root.runtime_removed,"completed model remained visible");
    check(!simulation_lab_model_node_present(run.entities[0],0),"renderer presence gate failed");
    check(run.entities.size()==1&&run.entities[0].source_placement_index==0,"runtime indices shifted");
    check(run.metrics.entities_removed==1&&run.metrics.model_hierarchies_removed==1,"release counters");
    check(run.faction_economy[1].xenite_storage_limit_q4==100&&run.faction_economy[1].xenite_current_q4==50,
        "capacity/proportional stock release failed");
    lab.step(field,20);check(run.metrics.entities_removed==1&&run.faction_economy[1].xenite_current_q4==50,"release repeated");
    lab.reset(cfg,params,std::span(&s,1),economy);
    auto& absent=lab.runs()[0].entities[0].model_components[0];
    absent.terminal_attachment_metadata_prepared=true;absent.terminal_attachment_completion_ticks[0]=19;
    lab.step(field,25);
    check(!lab.runs()[0].entities[0].runtime_removed&&simulation_lab_one_shot_events_after(lab.runs()[0],0).empty(),
        "missing class3 attachment invented a callback");
    SimulationLabEntity factory;factory.runtime_model_class_id=13;
    check(!simulation_lab_production_effect_active(factory),"idle smoke");
    factory.production_building=true;factory.production_behavior_state=1;
    check(simulation_lab_production_effect_active(factory),"working factory smoke missing");
    factory.production_behavior_state=2;check(!simulation_lab_production_effect_active(factory),"opening smoke");
    factory.production_building=false;factory.research_active=true;
    check(!simulation_lab_production_effect_active(factory),"opening factory emitted research smoke");
    factory.production_behavior_state=0;
    check(simulation_lab_production_effect_active(factory),"research smoke missing");
    factory.model_components.emplace_back();factory.root_model_component_index=0;
    factory.model_components[0].runtime_flags_ec=1;
    check(!simulation_lab_production_effect_active(factory),"powered-down smoke");
    factory.model_components[0].runtime_flags_ec=0;factory.runtime_removed=true;
    check(!simulation_lab_production_effect_active(factory),"removed smoke");
    auto child_seed=seed();child_seed.water_damage_multiplier=0;
    child_seed.model_components[0].has_resource_storage_capacity_contribution=false;
    for(size_t i=1;i<=2;++i){auto c=child_seed.model_components[0];c.source_model_node_index=i;
        c.parent_component_index=i-1;child_seed.model_components.push_back(c);}
    lab.reset(cfg,params,std::span(&child_seed,1));
    auto& ce=lab.runs()[0].entities[0];auto nodes=std::make_shared<std::vector<ModelVisualNode>>(3);
    (*nodes)[0].parent_index=UINT32_MAX;(*nodes)[1].parent_index=0;(*nodes)[2].parent_index=1;
    ce.runtime_model_hierarchy=nodes;ce.model_components[1].destruction_complete_tick=1;
    lab.step(field);
    check(!ce.runtime_removed&&simulation_lab_model_node_present(ce,0)&&
        !simulation_lab_model_node_present(ce,1)&&!simulation_lab_model_node_present(ce,2),
        "child completion must remove only its subtree");
    SimulationLabRun marker_run;marker_run.entities.resize(2);
    auto& mobile=marker_run.entities[0];mobile.faction_index=1;mobile.motion.speed_q12_per_tick=409;
    mobile.actor.position={3,-1,0};mobile.actor.move_target=Vec3f{10,-1,0};
    check(simulation_lab_command_markers(marker_run).size()==1,"moving unit target marker");
    mobile.actor.position={9,-1,0};
    check(simulation_lab_command_markers(marker_run).empty(),"arrived final target marker retained");
    mobile.actor.move_target.reset();mobile.actor.move_waypoint=Vec3f{20,-1,0};
    check(simulation_lab_command_markers(marker_run).empty(),"orphan waypoint/own-position marker retained");
    marker_run.entities[1].runtime_model_class_id=13;
    marker_run.entities[1].production_exit_world=Vec3f{9,-1,0};
    check(simulation_lab_command_markers(marker_run).size()==1&&simulation_lab_command_markers(marker_run)[0].effect_id==143,
        "arrival removed persistent factory rally point");
    if(argc>1){
        auto catalog=AssetCatalog::scan_game_root(argv[1]);auto prepared=simulation_lab_prepare_runtime_catalog(catalog);
        FactionTechnologyMask256 mask{};mask[0]=3;mask[6]=278003968;
        for(auto id:{1U,111U,171U,333U}){
            SimulationLabScenarioRuntimeSeed scenario;scenario.runtime_catalog=prepared;
            const auto* preset=catalog.army_preset(id);check(preset!=nullptr,"stock ARM absent");
            auto h=resolve_faction_technology_hierarchy(*preset,mask);
            SimulationLabSeedEntity stock;stock.army_asset_id=id;stock.faction_index=1;stock.actor.position={3,-1,0};
            stock.model_components=simulation_lab_model_components_from_hierarchy(h);
            std::array<FactionTechnologyMask256,8> masks{};masks[1]=mask;
            lab.reset(cfg,params,std::span(&stock,1),{},{},{},masks,{},scenario);
            auto& sr=lab.runs()[0];auto& ent=sr.entities[0];auto& c=ent.model_components[ent.root_model_component_index];
            check(c.terminal_attachment_metadata_prepared&&c.terminal_attachment_point_mask,"stock attachment metadata absent");
            std::cout<<"stock ARM="<<id<<" traversal="<<ent.has_traversal_profile<<":"<<ent.traversal_low_band_index
                <<","<<ent.traversal_high_band_index<<" waterDamage="<<ent.water_damage_multiplier<<" threshold="<<ent.water_damage_threshold_q12<<'\n';
            c.integrity=0;c.terminal_runtime_flag8=true;c.terminal_self_reference=true;
            ent.root_model_terminal=true;ent.root_model_integrity=0;
            lab.step(field);const uint32_t due=id==111||id==333?14:20;
            check(c.destruction_complete_tick==due,"stock completion delay mismatch");
            lab.step(field,due-2);check(!ent.runtime_removed,"stock model released early");
            lab.step(field);check(ent.runtime_removed,"stock model stuck");
            std::cout<<"stock ARM="<<id<<" removed_tick="<<sr.tick<<'\n';
        }
    }
    std::cout<<"V614_DESTRUCTION_PRODUCTION PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
