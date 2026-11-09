#include "thandor/fog_preview.hpp"
#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_derived_selection_metrics.hpp"
#include "thandor/simulation_lab_owner_order.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/simulation_lab_site_placement.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace thandor;

namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}

std::uint8_t faction_byte(std::uint64_t mask,std::uint32_t faction){
    return static_cast<std::uint8_t>(mask>>(faction*8U));
}

SimulationLabExperiment create_lab(const FieldGrid& field,bool aggregate){
    auto catalog=std::make_shared<SimulationLabRuntimeCatalog>();
    ModelVisualRecord root{};root.runtime_class_id=1U;
    root.simulation.fog_uncover_radius_q12=0;
    root.simulation.occupancy_mark_radius_units=0U;
    catalog->definitions.emplace(100U,root);

    SimulationLabScenarioRuntimeSeed scenario{};
    scenario.enabled=true;scenario.assignable_faction_count=2U;
    scenario.active_faction_count=2U;scenario.active_view_faction_index=2U;
    scenario.ai_planning_enabled=false;scenario.runtime_catalog=catalog;
    SimulationLabStaticPlacementOwner owner{};
    owner.model_definition_id=100U;owner.faction_index=2U;
    owner.source_placement_index=7U;
    const auto& cell=field.at(30U,30U);
    owner.pose.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0.0F};
    if(aggregate){
        owner.derived_selection_metrics_valid=true;
        owner.runtime_class_id=18U;
        owner.visibility_radius_q12=0x240*9;
        owner.occupancy_mark_radius_units=0x240*9;
        owner.visibility_height_offset_q12=0x400;
    }
    scenario.static_placement_owners.push_back(owner);
    SimulationLabConfig config{};config.run_count=1U;config.duration_ticks=32U;
    std::array<std::uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    SimulationLabExperiment lab;lab.reset(config,parameters,{},{},{},{},{},{},scenario);
    return lab;
}

std::size_t count_marker(const SimulationLabRun& run,std::uint32_t faction,std::uint8_t marker){
    std::size_t count=0;
    for(const auto mask:run.retail_scratch->field_occupancy_masks)
        if((faction_byte(mask,faction)&0xF8U)==(marker&0xF8U))++count;
    return count;
}
std::size_t count_sector(const SimulationLabRun& run,std::uint32_t faction){
    std::size_t count=0;
    for(const auto mask:run.retail_scratch->field_occupancy_masks)
        if((faction_byte(mask,faction)&0x02U)!=0U)++count;
    return count;
}
}

int main()try{
    std::vector<ModelVisualNode> hierarchy(4);
    hierarchy[0].parent_index=UINT32_MAX;hierarchy[0].model_definition_id=100U;
    hierarchy[0].runtime_class_id=11U;hierarchy[0].simulation.fog_uncover_radius_q12=100;
    hierarchy[0].simulation.occupancy_mark_radius_units=2U;
    hierarchy[0].simulation.fog_reference_height_offset_q12=10;
    // Same-MDL visual child: even a large height/metadata value is not another ModelRuntime.
    hierarchy[1]=hierarchy[0];hierarchy[1].parent_index=0U;
    hierarchy[1].simulation.fog_uncover_radius_q12=9000;
    hierarchy[1].simulation.occupancy_mark_radius_units=90U;
    hierarchy[1].simulation.fog_reference_height_offset_q12=900;
    // Linked MDL root: contributes its own metrics and relative root height.
    hierarchy[2].parent_index=1U;hierarchy[2].model_definition_id=200U;
    hierarchy[2].runtime_class_id=9U;hierarchy[2].simulation.fog_uncover_radius_q12=300;
    hierarchy[2].simulation.occupancy_mark_radius_units=8U;
    hierarchy[2].simulation.fog_reference_height_offset_q12=30;
    hierarchy[3]=hierarchy[2];hierarchy[3].parent_index=2U;
    hierarchy[3].simulation.fog_reference_height_offset_q12=1200;
    std::array<std::int32_t,4> heights{0,700,400,1100};
    {
        ModelVisualNode root;root.model_definition_id=1;root.parent_index=UINT32_MAX;
        root.simulation.fog_reference_height_offset_q12=2000;
        ModelVisualNode child;child.model_definition_id=2;child.parent_index=0;
        child.simulation.fog_reference_height_offset_q12=0;
        const std::array nodes{root,child};const std::array<int32_t,2> z{0,-239};
        const auto wrapped=simulation_lab_derived_selection_metrics(nodes,z);
        require(wrapped.fog_reference_height_offset_q12==-239,
              "negative child eye height lost native unsigned maximum");
    }
    const auto metrics=simulation_lab_derived_selection_metrics(hierarchy,heights);
    require(metrics.runtime_root_count==2U,"visual child counted as ModelRuntime root");
    require(metrics.fog_uncover_radius_q12==300,"visibility radius did not aggregate runtime roots");
    require(metrics.occupancy_mark_radius_units==8U,"occupancy radius did not aggregate runtime roots");
    require(metrics.fog_reference_height_offset_q12==430,"eye height used visual child instead of runtime root");

    const SimulationLabOwnerOrderKey authored_old{false,0U,3U,0U};
    const SimulationLabOwnerOrderKey authored_new{false,0U,9U,1U};
    const SimulationLabOwnerOrderKey synthetic_old{true,4U,SIZE_MAX,2U};
    const SimulationLabOwnerOrderKey synthetic_new{true,8U,SIZE_MAX,3U};
    require(simulation_lab_owner_newer_first(authored_new,authored_old),"authored owner order not newest-first");
    require(simulation_lab_owner_newer_first(synthetic_old,authored_new),"synthetic owner not linked ahead of LEV owner");
    require(simulation_lab_owner_newer_first(synthetic_new,synthetic_old),"synthetic creation order lost");

    // Static and live MODEL owners share one intrusive list. Verify the
    // represented site/placement adapter does not append all static roots after
    // every dynamic actor.
    const auto order_field=FieldGrid::create(32U,32U,0,0);
    SimulationLabRuntimeCatalog order_catalog;
    ModelVisualRecord order_definition{};order_definition.runtime_class_id=11U;
    order_catalog.definitions.emplace(1U,order_definition);
    SimulationLabRun order_run;order_run.level_runtime_enabled=true;
    for(std::size_t index=0;index<3U;++index){
        SimulationLabEntity entity;entity.faction_index=1U;entity.army_asset_id=300U;
        entity.source_placement_index=index==0U?2U:(index==1U?8U:SIZE_MAX);
        entity.synthetic=index==2U;entity.construction_start_tick=index==2U?4U:0U;
        entity.actor.position={float(10U+index*5U),-10.0F,0.0F};
        entity.root_model_component_index=0U;
        SimulationLabModelComponentState component;component.model_definition_id=1U;
        entity.model_components.push_back(component);order_run.entities.push_back(entity);
    }
    SimulationLabStaticPlacementOwner ordered_static{};ordered_static.model_definition_id=1U;
    ordered_static.source_placement_index=5U;ordered_static.pose.position={25.0F,-10.0F,0.0F};
    const auto site_owners=simulation_lab_site_owners(order_run,order_field,order_catalog,
        std::span<const SimulationLabStaticPlacementOwner>(&ordered_static,1U));
    require(site_owners.size()==4U,"combined static/live owner list lost an owner");
    require(site_owners[0].army_entity_index==2U,"synthetic head owner was not first");
    require(site_owners[1].army_entity_index==1U,"newer authored live owner order lost");
    require(!site_owners[2].army_entity_index.has_value(),"authored static owner was not interleaved");
    require(site_owners[3].army_entity_index==0U,"older authored live owner order lost");

    const auto field=FieldGrid::create(64U,64U,0,0);
    auto fallback=create_lab(field,false);fallback.step(field,7U);
    auto aggregate=create_lab(field,true);aggregate.step(field,7U);
    const auto& fallback_run=fallback.runs().front();const auto& aggregate_run=aggregate.runs().front();
    require(fallback_run.retail_scratch&&aggregate_run.retail_scratch,"phase-7 scratch missing");
    const auto marker18=retail_visibility_marker_for_model_class(18U);
    const auto marker1=retail_visibility_marker_for_model_class(1U);
    require(count_marker(aggregate_run,2U,marker18)>count_marker(fallback_run,2U,marker18),
        "static hierarchy aggregate visibility radius/class was ignored");
    require(count_marker(aggregate_run,2U,marker1)==0U,
        "aggregate static owner used root catalog class instead of ArmyRuntime root class");
    require(count_sector(aggregate_run,2U)>count_sector(fallback_run,2U),
        "static hierarchy aggregate occupancy radius was ignored");

    std::cout<<"V756_STATIC_OWNER_AGGREGATE PASS roots=2 order=1 projected=1 sector=1\n";
}catch(const std::exception& error){
    std::cerr<<"V756_STATIC_OWNER_AGGREGATE FAIL "<<error.what()<<'\n';return 1;
}
