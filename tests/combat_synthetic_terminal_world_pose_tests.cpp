#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/simulation_lab_environment.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

int main() try {
    using namespace thandor;
    const auto field=FieldGrid::create(59U,59U,0,0U);
    auto catalog=std::make_shared<SimulationLabRuntimeCatalog>();
    constexpr uint32_t army_id=0x234U, model_id=0x345U, effect_id=0x456U;
    ModelVisualNode node{};
    node.parent_index=UINT32_MAX;
    node.model_definition_id=model_id;
    node.runtime_class_id=4U;
    node.sprite_path="spr/test/synthetic_terminal.spr";
    node.simulation.maximum_integrity=4096;
    node.simulation.terminal_attachment_effect_ids[2]=effect_id;
    node.simulation.terminal_attachment_delay_ticks[2]=0;
    ArmyAssetPreset army{};
    army.model_hierarchy.push_back(node);
    army.resolved_model_hierarchy.push_back(node);
    ArmyModelVariantGroup group{};
    ModelTechnologyVariant variant{};
    variant.variant_index=0U;
    variant.model_definition_id=model_id;
    variant.model_hierarchy.push_back(node);
    group.variants.push_back(variant);
    army.model_variant_groups.push_back(group);
    catalog->armies.emplace(army_id,army);
    ModelVisualRecord model{};
    model.definition_id=model_id;
    model.runtime_class_id=4U;
    model.simulation=node.simulation;
    model.hierarchy_nodes.push_back(node);
    catalog->definitions.emplace(model_id,model);
    SimulationLabRuntimeSprite sprite{};
    sprite.damage_effect_points.emplace_back(2U,Vec3f{0.25F,-0.5F,0.75F});
    sprite.terminal_attachment_point_mask=1U<<2U;
    catalog->sprites.emplace(node.sprite_path,sprite);
    SimulationLabSeedEntity seed{};
    seed.army_asset_id=army_id;
    seed.source_placement_index=SIZE_MAX; // Newly constructed, not in original LEV.
    seed.faction_index=1U;
    seed.runtime_model_class_id=4U;
    seed.building=true;
    seed.actor.position={10.0F,-10.0F,0.0F};
    seed.model_components=simulation_lab_model_components_from_hierarchy(army.model_hierarchy);
    SimulationLabScenarioRuntimeSeed scenario{};
    scenario.runtime_catalog=catalog;
    std::array<uint32_t,AiKnowledgeData::kParameterCount> knowledge{};
    SimulationLabConfig config{};
    config.run_count=1U;
    config.duration_ticks=5U;
    config.parameter_index=1U;
    SimulationLabExperiment experiment;
    std::array seeds{seed};
    experiment.reset(config,knowledge,seeds,
        std::span<const SimulationLabFactionEconomySeed>{},
        std::span<const SimulationLabProductionAsset>{},
        std::span<const SimulationLabTechnologyAsset>{},
        std::span<const FactionTechnologyMask256>{},
        std::span<const SimulationLabFactionScoreSeed>{},scenario);
    auto& entity=experiment.runs()[0].entities.at(0);
    if (!entity.runtime_model_hierarchy || entity.model_components.size()!=1U)
        throw std::runtime_error("runtime hierarchy not initialized for synthetic fixture");
    auto& component=entity.model_components.at(0);
    component.integrity=0;
    component.terminal_runtime_flag8=true;
    component.terminal_self_reference=true;
    entity.root_model_terminal=true;
    const auto transforms=simulation_lab_current_model_poses(entity,field,*catalog);
    const auto& p=transforms.at(0);
    const auto local=std::get<1>(sprite.damage_effect_points[0]);
    const Vec3f expected{p.position.x+p.basis[0]*local.x+p.basis[1]*local.y+p.basis[2]*local.z,
        p.position.y+p.basis[3]*local.x+p.basis[4]*local.y+p.basis[5]*local.z,
        p.position.z+p.basis[6]*local.x+p.basis[7]*local.y+p.basis[8]*local.z};
    experiment.step(field,1U);
    const auto events=simulation_lab_one_shot_events_after(experiment.runs()[0],0U);
    if(events.size()!=1U || events[0].kind!=SimulationLabOneShotEventKind::TerminalAttachmentEffect ||
       events[0].source_placement_index!=SIZE_MAX || events[0].effect_id!=effect_id ||
       events[0].terminal_attachment_world_points.size()!=1U)
        throw std::runtime_error("synthetic terminal event lost its historical attachment");
    const auto actual=events[0].terminal_attachment_world_points.at(0).position;
    const auto close=[](float a,float b){return std::abs(a-b)<0.001F;};
    if(!close(actual.x,expected.x)||!close(actual.y,expected.y)||!close(actual.z,expected.z))
        throw std::runtime_error("terminal effect moved away from historical sprite lookup position");
    std::cout<<"V703_SYNTHETIC_TERMINAL_WORLD_POSE PASS\n";
    return 0;
} catch(const std::exception& e){
    std::cerr<<"V703_SYNTHETIC_TERMINAL_WORLD_POSE FAIL: "<<e.what()<<'\n';
    return 1;
}
