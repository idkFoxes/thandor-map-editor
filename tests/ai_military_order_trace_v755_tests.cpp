#include "thandor/simulation_lab.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

using namespace thandor;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}

SimulationLabConfig config(std::uint32_t ticks=128U){
    SimulationLabConfig value;
    value.run_count=1U;
    value.duration_ticks=ticks;
    value.sweep_mode=SimulationLabSweepMode::Fixed;
    value.parameter_index=100U;
    return value;
}

SimulationLabScenarioRuntimeSeed scenario(){
    SimulationLabScenarioRuntimeSeed value;
    value.enabled=true;
    value.active_faction_count=2U;
    value.assignable_faction_count=2U;
    value.simulate_all_active_factions_as_ai=true;
    return value;
}

SimulationLabSeedEntity unit(const FieldGrid& field,std::uint32_t x,std::uint32_t y,
                             std::uint32_t faction,std::uint32_t arm,
                             std::uint32_t runtime_class=2U){
    SimulationLabSeedEntity value;
    value.army_asset_id=arm;
    value.faction_index=faction;
    value.runtime_model_class_id=runtime_class;
    const auto& cell=field.at(x,y);
    value.actor.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0.0F};
    value.root_model_maximum_integrity=1000;
    value.motion.collision_radius_q12=512;
    value.motion.speed_q12_per_tick=runtime_class==15U?0:512;
    value.motion.yaw_max_velocity_turn16=0x7fff;
    value.motion.yaw_acceleration_turn16=0x7fff;
    value.has_traversal_profile=runtime_class!=15U;
    value.traversal_low_band_index=0U;
    value.traversal_high_band_index=4U;
    value.fog_uncover_radius_q12=20*4096;
    value.fog_reference_height_offset_q12=4*4096;
    value.building=runtime_class==15U;
    return value;
}

const SimulationLabMilitaryOrderTrace& newest_trace(const SimulationLabRun& run){
    require(run.military_order_trace_count!=0U,"military order trace stayed empty");
    const auto newest=(run.military_order_trace_write_index+
        kSimulationLabMilitaryOrderTraceCapacity-1U)%kSimulationLabMilitaryOrderTraceCapacity;
    return run.military_order_trace[newest];
}

void bounded_ring(){
    SimulationLabRun run;
    for(std::uint32_t index=0;index<70U;++index){
        SimulationLabMilitaryOrderTrace trace;
        trace.tick=index;
        trace.source_entity_index=index;
        simulation_lab_append_military_order_trace(run,trace);
    }
    require(run.military_order_trace_count==kSimulationLabMilitaryOrderTraceCapacity,
            "military trace exceeded its snapshot-safe bound");
    require(run.military_order_trace_overwrites==6U,
            "military trace overwrite counter is wrong");
    require(run.next_military_order_trace_sequence==71U,
            "military trace sequence did not remain monotonic");
    require(run.military_order_trace[run.military_order_trace_write_index].sequence==7U,
            "military trace ring lost chronological oldest record");
}

void site_order_trace(){
    auto field=FieldGrid::create(59,59,0,0);
    auto source=unit(field,20,20,1,100);
    SimulationLabProductionAsset product;
    product.army_asset_id=100;
    product.entity_seed=source;
    product.ai_definition_class_value70=1U;
    std::array<std::uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    parameters[0x23U]=250000U;
    parameters[0x27U]=4096U;
    std::array seeds{source};
    std::array products{product};
    SimulationLabExperiment lab;
    lab.reset(config(32),parameters,seeds,{},products,{},{},{},scenario());
    lab.step(field,14U);
    const auto& run=lab.runs()[0];
    const auto& trace=newest_trace(run);
    require(trace.workspace==SimulationLabMilitaryOrderWorkspace::General05,
            "WS05 command was not identified in the military trace");
    require(trace.source_entity_index==0U&&trace.target_entity_index==UINT32_MAX,
            "WS05 trace gained an entity target");
    require(trace.selected_score>0&&trace.best_site_score==trace.selected_score,
            "WS05 score provenance is incomplete");
    require(trace.cooldown==8U&&trace.route_started,
            "WS05 command flags were sampled before command completion");
    require(trace.direct_route&&trace.primary.x==trace.fallback.x&&
            trace.primary.y==trace.fallback.y&&trace.stored.x==trace.fallback.x&&
            trace.stored.y==trace.fallback.y,
            "clear WS05 route did not publish one resolved direct destination");
}

void anchor_order_trace(){
    auto field=FieldGrid::create(59,59,0,0);
    auto source=unit(field,20,20,1,101);
    SimulationLabProductionAsset product;
    product.army_asset_id=101;
    product.entity_seed=source;
    product.ai_definition_class_value74=1U;
    std::array<std::uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    parameters[0x33U]=250000U;
    parameters[0x37U]=4096U;
    std::array seeds{source};
    std::array products{product};
    SimulationLabExperiment lab;
    lab.reset(config(32),parameters,seeds,{},products,{},{},{},scenario());
    auto& run=lab.runs()[0];
    const auto& anchor_cell=field.at(24,24);
    run.ai_primary_impact_anchor[1]={anchor_cell.world_x_q12,anchor_cell.world_y_q12,150U};
    lab.step(field,14U);
    const auto& trace=newest_trace(run);
    require(trace.workspace==SimulationLabMilitaryOrderWorkspace::FactionAnchor,
            "faction-anchor command was not identified in the military trace");
    require(trace.workspace_entry_index==0U,
            "primary faction anchor lost its recovered writer selection");
    require(trace.best_anchor_score==trace.selected_score&&trace.selected_score>0,
            "anchor score provenance is incomplete");
    require(trace.requested.x==anchor_cell.world_x_q12&&
            trace.requested.y==anchor_cell.world_y_q12,
            "anchor trace did not retain the issued point");
}

void visible_group_order_trace(){
    auto field=FieldGrid::create(59,59,0,0);
    auto first=unit(field,20,20,1,102);
    auto second=unit(field,20,21,1,102);
    auto hostile=unit(field,23,20,2,333,15U);
    SimulationLabProductionAsset product;
    product.army_asset_id=102;
    product.entity_seed=first;
    product.ai_definition_class_value78=1U;
    std::array<std::uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    parameters[0x43U]=250000U;
    parameters[0x47U]=4096U;
    std::array seeds{first,second,hostile};
    std::array products{product};
    SimulationLabExperiment lab;
    lab.reset(config(128),parameters,seeds,{},products,{},{},{},scenario());
    lab.step(field,64U);
    const auto& run=lab.runs()[0];
    require(run.metrics.military_ai_group_orders>=2U,
            "fixture did not issue the expected visible WS07 group order");
    std::uint32_t matching=0U;
    for(const auto& trace:run.military_order_trace){
        if(trace.sequence==0U||trace.workspace!=SimulationLabMilitaryOrderWorkspace::Visible07)continue;
        ++matching;
        require(trace.target_entity_index==2U&&trace.target_owner_live,
                "WS07 trace lost the live group target");
        require(trace.collected_count==2U&&trace.accumulated_strength>=0x200U,
                "WS14 strength/admission provenance is incomplete");
        require(trace.command_mode==5U&&trace.command_generation==32,
                "group trace was sampled before native command stamps");
        require(trace.workspace_entry_index==0U&&!trace.target_from_unseen,
                "visible WS07 trace was mislabeled as the bounded WS03 fallback");
    }
    require(matching>=2U,"one or more collected WS14 members were not traced");
}
}

int main()try{
    bounded_ring();
    site_order_trace();
    anchor_order_trace();
    visible_group_order_trace();
    std::cout<<"V755_MILITARY_ORDER_TRACE PASS capacity=64 site=1 anchor=1 group=2\n";
}catch(const std::exception& error){
    std::cerr<<error.what()<<'\n';
    return 1;
}
