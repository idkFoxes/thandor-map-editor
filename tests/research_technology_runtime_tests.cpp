#include <algorithm>
#include "thandor/simulation_lab_runtime.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
ModelVisualRecord model(uint32_t id,uint32_t cls,int32_t health,uint32_t tech=0) {
    ModelVisualRecord m;m.definition_id=id;m.runtime_class_id=cls;m.technology_unlock_id=tech;
    m.simulation.maximum_integrity=health;m.simulation.class_step_q12=100;
    m.simulation.fog_uncover_radius_q12=100;m.simulation.occupancy_mark_radius_units=1;
    m.simulation.fog_reference_height_offset_q12=10;m.sprite_path="sprite";return m;
}
ModelVisualNode node(const ModelVisualRecord& m,uint32_t parent=UINT32_MAX,uint32_t slot=0) {
    ModelVisualNode n;n.model_definition_id=m.definition_id;n.runtime_class_id=m.runtime_class_id;
    n.simulation=m.simulation;n.parent_index=parent;n.child_slot=slot;n.sprite_path=m.sprite_path;return n;
}
ArmyAssetPreset army(uint32_t id,std::vector<ModelVisualNode> hierarchy) {
    ArmyAssetPreset p;p.registry_id=id;ArmyModelVariantGroup g;
    ModelTechnologyVariant v;v.model_definition_id=hierarchy[0].model_definition_id;v.model_hierarchy=std::move(hierarchy);
    g.variants.push_back(std::move(v));p.model_variant_groups.push_back(std::move(g));return p;
}
SimulationLabSeedEntity seed(uint32_t id,uint32_t faction,uint32_t cls=1) {
    SimulationLabSeedEntity s;s.army_asset_id=id;s.faction_index=faction;s.runtime_model_class_id=cls;
    s.actor.position={2,2,0};s.building=cls==13;s.root_model_maximum_integrity=100;return s;
}
SimulationLabRuntimeCatalog metadata() {
    SimulationLabRuntimeCatalog c;
    auto base=model(100,1,100),up=model(102,2,250,7),later=model(103,2,500,7);
    base.faction_technology_variant_model_ids={0,999,104,102,103,0};
    auto locked=model(104,2,900,8);up.faction_technology_variant_model_ids[0]=103;
    up.simulation.class_step_q12=200;up.simulation.movement_collision_radius_q12=333;
    up.simulation.fog_uncover_radius_q12=300;up.simulation.occupancy_mark_radius_units=4;
    up.simulation.health_regeneration_per_step=2;
    auto weapon=model(200,7,80),new_weapon=model(201,8,160,7);
    weapon.faction_technology_variant_model_ids[0]=201;
    weapon.simulation.shot_definition_id=1;weapon.simulation.reload_ticks=20;
    new_weapon.simulation.shot_definition_id=2;new_weapon.simulation.reload_ticks=4;
    new_weapon.simulation.yaw_max_velocity_turn16=99;
    new_weapon.simulation.retail_energy_demand_q4=900;
    new_weapon.simulation.terminal_attachment_effect_ids[0]=9;
    new_weapon.simulation.fog_uncover_radius_q12=700;
    new_weapon.simulation.occupancy_mark_radius_units=8;
    new_weapon.simulation.fog_reference_height_offset_q12=80;
    auto linked=model(101,1,300,7);linked.simulation.class_step_q12=500;
    for(const auto& m:{base,up,later,locked,weapon,new_weapon,linked})c.definitions[m.definition_id]=m;
    ShotDefinitionRecord shot;shot.definition_id=1;shot.target_impact_damage_q12[0]=20;c.shots[1]=shot;
    shot.definition_id=2;shot.target_impact_damage_q12[0]=100;c.shots[2]=shot;
    c.sprites["sprite"].launch_geometry={1,1};
    auto axis=node(weapon,0);axis.sprite_path="axis";
    auto p=army(1,{node(base),axis,node(weapon,1)});
    auto v=p.model_variant_groups[0].variants[0];v.variant_index=1;v.technology_unlock_id=7;
    v.model_definition_id=101;axis=node(new_weapon,0);axis.sprite_path="axis";
    v.model_hierarchy={node(linked),axis,node(new_weapon,1)};
    p.model_variant_groups[0].variants.push_back(std::move(v));c.armies[1]=std::move(p);
    return c;
}
SimulationLabConfig config(){SimulationLabConfig c;c.run_count=1;c.duration_ticks=32;c.parameter_index=100;return c;}
}
int main(int argc,char** argv)try {
    auto c=metadata();FactionTechnologyMask256 mask{};
    c.definitions[100].simulation.has_resource_storage_capacity_contribution=true;
    c.definitions[100].simulation.resource_storage_selector=1;
    c.definitions[100].simulation.resource_storage_capacity_contribution_q4=4700;
    c.definitions[100].simulation.energy_generation_capacity_q4=960;
    SimulationLabEntity e;e.faction_index=1;e.actor.position={2,2,0};
    check(simulation_lab_initialize_runtime_entity(e,seed(1,1),mask,c,true),"runtime setup failed");
    check(e.combat_weapons.size()==1&&e.model_components.size()==2,"initial weapon/component hierarchy incorrect");
    e.runtime_model_node_height_offsets_q12={0,100,200};
    e.model_components[0].integrity=33;e.model_components[1].integrity=40;
    e.model_components[1].health_regeneration_delay_ticks=5;e.model_components[1].runtime_flags_ec=0x11;
    e.model_components[1].retail_energy_demand_q4=17;e.model_components[1].terminal_attachment_countdowns[0]=13;
    e.actor.move_target=Vec3f{9,8,0};e.actor.body_turn_velocity_turn16=77;
    e.actor.weapons[0].cooldown_ticks=12;e.actor.weapons[0].yaw.angle_turn16=444;
    const auto published=e.runtime_model_hierarchy;
    set_faction_technology_unlocked(mask,7,true);SimulationLabRunMetrics metrics;
    simulation_lab_apply_runtime_technology(e,mask,c,metrics);
    check(e.model_components[0].model_definition_id==102,"live upgrade chose ARM variant or later replacement");
    check(e.model_components[0].has_resource_storage_capacity_contribution &&
          e.model_components[0].resource_storage_selector==1 &&
          e.model_components[0].resource_storage_capacity_contribution_q4==4700 &&
          e.model_components[0].energy_generation_capacity_q4==960,
          "technology swap re-registered storage or power from replacement metadata");
    check(e.model_components[0].integrity==82&&e.root_model_integrity==82,"signed health scaling/truncation incorrect");
    check(e.model_components[1].model_definition_id==201&&e.model_components[1].integrity==80,"child runtime did not upgrade proportionally");
    check((*e.runtime_model_hierarchy)[2].model_definition_id==201,"same-definition inner node missed replacement");
    check((*published)[0].model_definition_id==100&&(*published)[2].model_definition_id==200,"published snapshot was mutated");
    check(e.model_components[1].runtime_flags_ec==0x11&&e.model_components[1].health_regeneration_delay_ticks==5&&
          e.model_components[1].terminal_attachment_countdowns[0]==13&&e.model_components[1].retail_energy_demand_q4==17,
          "replacement reset runtime flags, timers or active Energy load");
    check(e.model_components[1].terminal_attachment_effect_ids[0]==9,"definition-owned effect field stayed stale");
    check(e.actor.move_target&&e.actor.move_target->x==9&&e.actor.body_turn_velocity_turn16==77,"upgrade reset move command/steering");
    check(e.motion.speed_q12_per_tick==200&&e.motion.collision_radius_q12==333,"root motion stayed baseline");
    // Native aggregation samples one root node per ModelRuntime. Node 2 is an
    // internal visual child of the weapon runtime rooted at node 1, so the eye
    // height is 100 + 80 rather than the old visual-node value 200 + 80.
    check(e.fog_uncover_radius_q12==700&&e.occupancy_mark_radius_units==8&&e.fog_reference_height_offset_q12==180,
          "hierarchy visibility metrics did not rebuild from ModelRuntime roots");
    check(e.combat_weapons[0].profile.shot_definition_id==2&&e.combat_weapons[0].profile.reload_ticks==4&&
          e.combat_weapons[0].shot.target_impact_damage_q12[0]==100,"new weapon definition/SHT was not installed");
    check(e.actor.weapons[0].cooldown_ticks==12&&e.actor.weapons[0].yaw.angle_turn16==444,"reload/aim state was reset");
    check(metrics.runtime_technology_replacements==2,"replacement count incorrect");
    simulation_lab_apply_runtime_technology(e,mask,c,metrics);
    check(e.model_components[0].model_definition_id==103&&e.model_components[0].integrity==164,
          "later pass did not continue replacement chain");
    auto invalid=e;c.definitions[103].simulation.maximum_integrity=0;c.definitions[103].faction_technology_variant_model_ids[0]=102;
    simulation_lab_apply_runtime_technology(invalid,mask,c,metrics);
    check(invalid.model_components[0].model_definition_id==103&&metrics.runtime_technology_invalid_health_scales==1,
          "invalid imported denominator was not contained");
    c=metadata();SimulationLabEntity created;created.building=false;
    check(simulation_lab_initialize_runtime_entity(created,seed(1,1),mask,c,true),"technology-aware creation failed");
    check(created.model_components[0].model_definition_id==101&&created.root_model_integrity==300&&created.motion.speed_q12_per_tick==500,
          "new production reused baseline runtime or applied an MDL replacement at creation");
    check(created.combat_weapons[0].profile.shot_definition_id==2,"new product weapon stayed baseline");

    auto resource=model(350,14,1000);resource.simulation.class_step_q12=0x20000;
    c.definitions[350]=resource;c.armies[350]=army(350,{node(resource)});
    SimulationLabEntity structure;
    check(simulation_lab_initialize_runtime_entity(structure,seed(350,1,14),mask,c,true),"resource structure setup failed");
    check(structure.motion.speed_q12_per_tick==0,"class-specific structure step became horizontal movement");

    // Exercise research completion and factory spawn in the real Lab, without economy progression.
    std::array<uint32_t,AiKnowledgeData::kParameterCount> params{};const std::array seeds{seed(1,1),seed(301,1,13),seed(1,2)};
    SimulationLabProductionAsset product;product.army_asset_id=1;product.entity_seed=seed(1,0);product.build_duration_ticks=1;
    SimulationLabTechnologyAsset technology;technology.technology_id=7;technology.research_duration_ticks=1;technology.dependency_technology_id=256;
    const std::array products{product};const std::array technologies{technology};SimulationLabScenarioRuntimeSeed scenario;
    scenario.runtime_catalog=std::make_shared<const SimulationLabRuntimeCatalog>(metadata());SimulationLabExperiment lab;
    lab.reset(config(),params,seeds,{},products,technologies,{}, {},scenario);
    auto& run=lab.runs()[0];run.entities[0].model_components[0].integrity=40;
    run.entities[1].research_active=true;run.entities[1].research_technology_id=7;run.entities[1].research_duration_ticks=1;
    const auto field=FieldGrid::create(17,17,0,0);lab.step(field,1);
    check(run.metrics.research_jobs_completed==1&&run.entities[0].model_components[0].model_definition_id==102,
          "research completion did not apply runtime upgrade");
    check(run.entities[2].model_components[0].model_definition_id==100,"other faction was upgraded");
    run.entities[1].production_building=true;run.entities[1].production_asset_id=1;run.entities[1].production_duration_ticks=1;
    lab.step(field,1);
    check(run.entities.size()==4&&run.entities.back().model_components[0].model_definition_id==101,
          "factory completion did not select unlocked ARM variant");
    check(run.entities.back().root_model_maximum_integrity==300&&run.entities.back().combat_weapons[0].profile.shot_definition_id==2,
          "factory spawn did not receive technology-aware health/weapon data");
    if(argc==2) {
        const auto catalog=AssetCatalog::scan_game_root(argv[1]);const auto runtime=simulation_lab_prepare_runtime_catalog(catalog);
        check(!runtime->definitions.empty()&&!runtime->armies.empty(),"stock runtime metadata empty");
        const auto knowledge=AiKnowledgeData::parse(catalog.load("engine/ki.dat"));
        std::cout<<"STOCK_STORAGE_KI weight="<<knowledge.parameters()[3]<<" threshold="<<knowledge.parameters()[20]
            <<" gap="<<knowledge.parameters()[22]<<'\n';
        for(const uint32_t id:{330U,331U,332U,333U,390U}) {
            const auto* p=catalog.army_preset(id);if(!p)continue;
            for(const auto& component:simulation_lab_model_components_from_hierarchy(resolve_faction_technology_hierarchy(*p,{})))
                if(component.has_resource_storage_capacity_contribution)
                    std::cout<<"STOCK_STORAGE arm="<<id<<" mdl="<<component.model_definition_id
                        <<" selector="<<component.resource_storage_selector<<" capacity="<<component.resource_storage_capacity_contribution_q4<<'\n';
        }
        uint32_t inspected=0,upgraded=0,replacement_records=0;
        for(const auto& [id,m]:runtime->definitions)
            if(std::any_of(m.faction_technology_variant_model_ids.begin(),m.faction_technology_variant_model_ids.end(),[](auto v){return v!=0U;}))++replacement_records;
        for(const auto& [id,m]:runtime->definitions) {
            if(!std::any_of(m.faction_technology_variant_model_ids.begin(),m.faction_technology_variant_model_ids.end(),[](auto v){return v!=0U;}))continue;
            SimulationLabEntity item;item.runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(std::vector<ModelVisualNode>{node(m)});
            SimulationLabModelComponentState component;component.model_definition_id=id;component.maximum_integrity=m.simulation.maximum_integrity;
            component.integrity=m.simulation.maximum_integrity/2;component.source_model_node_index=0;
            item.model_components.push_back(component);item.root_model_component_index=0;
            FactionTechnologyMask256 full;full.fill(UINT32_MAX);SimulationLabRunMetrics result;
            simulation_lab_apply_runtime_technology(item,full,*runtime,result);
            upgraded+=static_cast<uint32_t>(result.runtime_technology_replacements);
            std::cout<<"STOCK_REPLACEMENT from="<<id<<" to="<<item.model_components[0].model_definition_id
                <<" hp="<<item.model_components[0].integrity<<" replacements="<<result.runtime_technology_replacements<<'\n';
        }
        std::cout<<"STOCK_METADATA definitions="<<runtime->definitions.size()<<" shots="<<runtime->shots.size()
            <<" sprites="<<runtime->sprites.size()<<" replacement_records="<<replacement_records<<'\n';
        for(const uint32_t id:{1U,50U,170U,301U,302U,303U,304U,305U,306U}) {
            const auto* preset=catalog.army_preset(id);if(!preset)continue;
            SimulationLabSeedEntity s=seed(id,1,preset->runtime_model_class_id);s.building=preset->browser_kind==PlacementBrowserKind::Building;
            SimulationLabEntity original;original.building=s.building;SimulationLabEntity all;all.building=s.building;
            FactionTechnologyMask256 full;full.fill(UINT32_MAX);
            check(simulation_lab_initialize_runtime_entity(original,s,{},*runtime,true),"stock baseline setup failed");
            check(simulation_lab_initialize_runtime_entity(all,s,full,*runtime,true),"stock unlocked setup failed");
            auto stock_metrics=SimulationLabRunMetrics{};simulation_lab_apply_runtime_technology(original,full,*runtime,stock_metrics);
            upgraded+=static_cast<uint32_t>(stock_metrics.runtime_technology_replacements);++inspected;
            auto creation_asset=*preset;
            for(auto& group:creation_asset.model_variant_groups)for(auto& variant:group.variants)variant.faction_technology_replacements.clear();
            const auto expected=catalog.simulation_weapon_profiles_for_hierarchy(resolve_faction_technology_hierarchy(creation_asset,full));
            check(all.combat_weapons.size()==expected.size(),"prepared stock weapon metadata differs from catalog I/O path");
            const auto& r=runtime->definitions.at(all.model_components[all.root_model_component_index].model_definition_id);
            check(all.root_model_maximum_integrity==r.simulation.maximum_integrity,"stock product health diverged from selected MDL");
            if(!all.building)check(all.motion.speed_q12_per_tick==r.simulation.class_step_q12,"stock product speed diverged from selected MDL");
            std::cout<<"STOCK_RUNTIME arm="<<id<<" root="<<r.definition_id<<" health="<<all.root_model_maximum_integrity
                <<" speed="<<all.motion.speed_q12_per_tick<<" weapons="<<all.combat_weapons.size()<<'\n';
        }
        check(inspected>=5&&upgraded>0,"stock selected/replacement runtime definitions were not exercised");
        std::cout<<"STOCK_RUNTIME definitions="<<runtime->definitions.size()<<" armies="<<runtime->armies.size()
            <<" inspected="<<inspected<<" replacements="<<upgraded<<'\n';
    }
    std::cout<<"V606_TECHNOLOGY_RUNTIME PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
