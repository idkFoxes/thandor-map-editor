#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/simulation_lab_environment.hpp"
#include "thandor/simulation_lab_live_aim_nodes.hpp"
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace thandor;
static void check(bool passed, const char* what) { if (!passed) throw std::runtime_error(what); }
int main() try {
    constexpr uint32_t parent_model_id=0x123U, turret_model_id=0x124U, terminal_effect_id=0x125U;
    auto catalog=std::make_shared<SimulationLabRuntimeCatalog>();
    ModelVisualNode root;
    root.parent_index=UINT32_MAX;
    root.model_definition_id=parent_model_id;
    root.runtime_class_id=4U;
    root.sprite_path="spr/test/parent.spr";
    root.simulation.maximum_integrity=1000;
    ModelVisualNode turret;
    turret.parent_index=0U;
    turret.child_slot=1U;
    turret.model_definition_id=turret_model_id;
    turret.runtime_class_id=9U;
    turret.sprite_path="spr/test/turret.spr";
    turret.simulation.maximum_integrity=200;
    turret.simulation.terminal_attachment_effect_ids[0]=terminal_effect_id;
    const std::vector<ModelVisualNode> hierarchy{root,turret};
    ModelVisualRecord parent_record;
    parent_record.definition_id=parent_model_id;
    parent_record.runtime_class_id=4U;
    parent_record.simulation=root.simulation;
    parent_record.hierarchy_nodes=hierarchy;
    catalog->definitions.emplace(parent_model_id,parent_record);
    ModelVisualRecord turret_record;
    turret_record.definition_id=turret_model_id;
    turret_record.runtime_class_id=9U;
    turret_record.simulation=turret.simulation;
    turret_record.hierarchy_nodes.push_back(turret);
    catalog->definitions.emplace(turret_model_id,turret_record);
    ArmyAssetPreset army;
    army.registry_id=0x126;
    army.model_hierarchy=hierarchy;
    army.resolved_model_hierarchy=hierarchy;
    ArmyModelVariantGroup group;
    ModelTechnologyVariant variant;
    variant.model_definition_id=parent_model_id;
    variant.model_hierarchy=hierarchy;
    group.variants.push_back(variant);
    army.model_variant_groups.push_back(group);
    catalog->armies.emplace(army.registry_id,army);
    catalog->sprites[root.sprite_path]=SimulationLabRuntimeSprite{};
    auto& spr=catalog->sprites[turret.sprite_path];
    spr.terminal_attachment_point_mask=1U;
    spr.effect_point_records.emplace_back(0U,Vec3f{0,0,0});
    catalog->effects.emplace(terminal_effect_id,SimulationLabRuntimeEffect{});
    catalog->terminal_effect_completion_ticks[terminal_effect_id]=3U;

    SimulationLabSeedEntity seed;
    seed.source_placement_index=0;
    seed.army_asset_id=0x126;
    seed.faction_index=1;
    seed.runtime_model_class_id=4;
    seed.actor.position={10,10,0};
    seed.model_components=simulation_lab_model_components_from_hierarchy(hierarchy);
    SimulationLabScenarioRuntimeSeed scenario;
    scenario.runtime_catalog=catalog;
    SimulationLabConfig config;
    config.run_count=1;
    config.duration_ticks=12;
    std::array<uint32_t,AiKnowledgeData::kParameterCount> knowledge{};
    std::array seeds{seed};
    SimulationLabExperiment lab;
    lab.reset(config,knowledge,seeds, {},{},{},{},{},scenario);
    auto& initial=lab.runs()[0].entities.at(0);
    check(initial.model_components.size()==2 &&
        initial.model_components[1].parent_component_index==0U,
        "independent turret component was not mounted on parent");
    check(simulation_lab_model_node_present(initial,1),"mounted gun must initially be present");
    // A child may be destroyed without killing the carrying building.
    initial.model_components[1].integrity=0;
    initial.model_components[1].terminal_self_reference=true;
    initial.model_components[1].terminal_runtime_flag8=true;
    const auto field=FieldGrid::create(59,59,0,0U);
    lab.step(field,1);
    const auto& pending=lab.runs()[0].entities.at(0);
    check(!pending.runtime_removed && !pending.root_model_terminal &&
          pending.model_components[0].integrity==1000 &&
          !pending.model_components[1].runtime_removed,
          "gun removed early or parent destroyed by child effect scheduling");
    check(lab.runs()[0].metrics.terminal_attachment_effect_events==1,
          "child terminal effect not emitted exactly once");
    check(pending.model_components[1].destruction_complete_tick &&
          *pending.model_components[1].destruction_complete_tick==4,
          "authored child effect completion lifetime was ignored");
    lab.step(field,2);
    check(simulation_lab_model_node_present(lab.runs()[0].entities.at(0),1),
          "gun vanished before effect callback elapsed");
    lab.step(field,1);
    const auto& released=lab.runs()[0].entities.at(0);
    check(released.model_components[1].runtime_removed &&
          !simulation_lab_model_node_present(released,1) &&
          simulation_lab_model_node_present(released,0) &&
          !released.runtime_removed && released.root_model_integrity==1000,
          "child detachment did not preserve the live parent hierarchy");
    lab.step(field,2);
    check(lab.runs()[0].metrics.terminal_attachment_effect_events==1 &&
          lab.runs()[0].metrics.model_hierarchies_removed==1,
          "released gun emitted duplicate terminal effect or release");
    std::cout << "V717_CHILD_TERMINAL_DETACHMENT PASS\n";
} catch(const std::exception& ex) {std::cerr<<ex.what()<<'\n';return 1;}
