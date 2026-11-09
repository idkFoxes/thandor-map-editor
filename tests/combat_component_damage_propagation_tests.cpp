#include "thandor/simulation_lab.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

thandor::SimulationLabSeedEntity mover_seed() {
    thandor::SimulationLabSeedEntity seed;
    seed.source_placement_index = 0U;
    seed.army_asset_id = 0x100U;
    seed.faction_index = 1U;
    seed.runtime_model_class_id = 3U;
    seed.motion.speed_q12_per_tick = 0x0100;
    seed.motion.yaw_max_velocity_turn16 = 0x1000U;
    seed.motion.yaw_acceleration_turn16 = 0x0400U;
    seed.motion.collision_radius_q12 = 0x0800;
    seed.actor.position = {8.0F, 8.0F, 0.0F};
    return seed;
}

thandor::SimulationLabConfig one_run_config() {
    thandor::SimulationLabConfig config;
    config.run_count = 1U;
    config.duration_ticks = 8U;
    config.parameter_index = 1U;
    return config;
}
}

int main() try {
    // Native averages only attached MODEL runtimes. A released zero-health
    // weapon has a null native attachment and must not weaken its living parent.
    thandor::SimulationLabEntity condition;
    condition.model_components.resize(3);condition.root_model_component_index=0;
    for(auto& c:condition.model_components)c.maximum_integrity=1000;
    condition.model_components[0].integrity=500;
    condition.model_components[1].parent_component_index=0;
    condition.model_components[2].parent_component_index=1;
    condition.model_components[2].integrity=1000;
    require(thandor::simulation_lab_hierarchy_condition_ratio_q12(condition)==1024,
            "linked dead weapon must still weaken hierarchy condition");
    condition.model_components[1].runtime_removed=true;
    require(thandor::simulation_lab_hierarchy_condition_ratio_q12(condition)==2048,
            "released weapon or its retained descendants weakened the live hierarchy");
    condition.model_components[0].runtime_removed=true;
    require(thandor::simulation_lab_hierarchy_condition_ratio_q12(condition)==0,
            "released root retained hierarchy condition");
    // Runtime-component extraction uses the same boundary the editor already uses for linked MDLs:
    // a live node is a new ModelRuntime component when it is the hierarchy root or its MDL id
    // differs from its parent node's MDL id.
    std::array<thandor::ModelVisualNode, 5> hierarchy{};
    hierarchy[0].model_definition_id = 100U;
    hierarchy[0].runtime_class_id = 4U;
    hierarchy[0].parent_index = std::numeric_limits<std::uint32_t>::max();
    hierarchy[0].simulation.maximum_integrity = 0x70000;
    hierarchy[0].local_rotation_angle0 = 0x1111U;
    hierarchy[1].model_definition_id = 100U;
    hierarchy[1].runtime_class_id = 4U;
    hierarchy[1].parent_index = 0U;
    hierarchy[1].simulation.maximum_integrity = 0x70000;
    hierarchy[2].model_definition_id = 200U;
    hierarchy[2].runtime_class_id = 0U;
    hierarchy[2].parent_index = 1U;
    hierarchy[2].simulation.maximum_integrity = 0x30000;
    hierarchy[2].local_rotation_angle0 = 0x2222U;
    hierarchy[3].model_definition_id = 200U;
    hierarchy[3].runtime_class_id = 0U;
    hierarchy[3].parent_index = 2U;
    hierarchy[4].model_definition_id = 300U;
    hierarchy[4].runtime_class_id = 0U;
    hierarchy[4].parent_index = 3U;
    hierarchy[4].simulation.maximum_integrity = 0x10000;
    hierarchy[4].local_rotation_angle0 = 0x3333U;

    const auto components = thandor::simulation_lab_model_components_from_hierarchy(hierarchy);
    require(components.size() == 3U, "linked-MDL hierarchy did not collapse to three runtime components");
    require(!components[0].parent_component_index &&
            components[1].parent_component_index == 0U &&
            components[2].parent_component_index == 1U,
            "runtime component parent chain did not follow the linked model-node ancestry");
    require(components[0].maximum_integrity == 0x70000 &&
            components[1].maximum_integrity == 0x30000 &&
            components[2].maximum_integrity == 0x10000,
            "component integrity did not come from each owning MDL +0x60");

    // Editor sanitation only: malformed imported hierarchies must not create recursive damage
    // cycles. Retail-created trees are acyclic, so this is deliberately not a retail claim.
    std::array<thandor::ModelVisualNode, 2> cyclic_hierarchy{};
    cyclic_hierarchy[0].model_definition_id = 500U;
    cyclic_hierarchy[0].parent_index = 1U;
    cyclic_hierarchy[1].model_definition_id = 501U;
    cyclic_hierarchy[1].parent_index = 0U;
    const auto sanitized_cycle = thandor::simulation_lab_model_components_from_hierarchy(cyclic_hierarchy);
    require(sanitized_cycle.size() == 2U,
            "malformed cyclic hierarchy unexpectedly lost runtime components");
    require(!sanitized_cycle[0].parent_component_index || !sanitized_cycle[1].parent_component_index,
            "malformed cyclic hierarchy was not broken before damage propagation");

    constexpr std::array<std::uint32_t, 10> relation_d_classes{
        4U, 10U, 11U, 13U, 14U, 15U, 16U, 20U, 22U, 23U};
    for (std::uint32_t runtime_class = 0; runtime_class < 24U; ++runtime_class) {
        const bool expected = std::find(relation_d_classes.begin(), relation_d_classes.end(), runtime_class) !=
                              relation_d_classes.end();
        require(thandor::simulation_lab_runtime_class_routes_terminal_to_relation_d(runtime_class) == expected,
                "24-entry retail class-command counter routing drifted");
    }

    const auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> base{};

    // Strike the deepest class-0 component. 0x100000 damage consumes 0x10000, then 0x30000,
    // then 0x70000 in the Group-A root, leaving 0x50000 unabsorbed at the root.
    std::array<thandor::SimulationLabSeedEntity, 2> chain_seeds{};
    chain_seeds[0] = mover_seed();
    chain_seeds[1].source_placement_index = 1U;
    chain_seeds[1].army_asset_id = 0x200U;
    chain_seeds[1].faction_index = 2U;
    chain_seeds[1].runtime_model_class_id = 4U;
    chain_seeds[1].motion.collision_radius_q12 = 0x0800;
    chain_seeds[1].actor.position = {8.25F, 8.0F, 0.0F};
    chain_seeds[1].model_components = components;
    chain_seeds[1].collision_model_component_index = 2U;

    thandor::SimulationLabExperiment chain_experiment;
    chain_experiment.reset(one_run_config(), base, chain_seeds);
    chain_experiment.step(field, 1U);
    const auto& chain_run = chain_experiment.runs().front();
    const auto& target = chain_run.entities[1];
    require(target.model_components.size() == 3U,
            "component graph was not copied independently into the run");
    require(target.model_components[2].integrity == 0 && target.model_components[2].terminal_runtime_flag8 &&
            target.model_components[1].integrity == 0 && target.model_components[1].terminal_runtime_flag8 &&
            target.model_components[0].integrity == 0 && target.model_components[0].terminal_runtime_flag8,
            "fixed collision damage did not exhaust the linked child-to-parent chain");
    // The root's terminal callback clears direct child links after damage propagation.
    // The grandchild retains its own link because the cleared child no longer services channels.
    require(target.model_components[2].terminal_self_reference &&
            !target.model_components[1].terminal_self_reference &&
            target.model_components[0].terminal_self_reference &&
            target.model_components[2].raw_impact_state_f8_ff == 0x200U &&
            target.model_components[1].raw_impact_state_f8_ff == 0x200U &&
            target.model_components[0].raw_impact_state_f8_ff == 0x200U,
            "terminal link lifecycle or raw +0xF8 impact state drifted");
    require(target.model_components[2].last_impact_overshoot == 0xF0000 &&
            target.model_components[1].last_impact_overshoot == 0xC0000 &&
            target.model_components[0].last_impact_overshoot == 0x50000,
            "component overshoot values do not match retail recursive subtraction");
    require(chain_run.metrics.component_terminal_events == 3U &&
            chain_run.metrics.component_parent_propagations == 2U &&
            chain_run.metrics.class0_terminal_impacts == 1U &&
            chain_run.metrics.class0_impact_damage_applied == 0xB0000U &&
            chain_run.metrics.class0_impact_overshoot == 0x50000U,
            "component terminal/propagation metrics do not match the recovered damage graph");
    // The class-0 physical collision path calls ArmyRuntime_ApplyDamageAndPropagateToParent;
    // its damaged relation-counter branch is unreachable in retail. Only the separate
    // projectile shot-impact caller may award counter C/D (with an identified source).
    require(chain_run.faction_relation_counter_c[2] == 0U &&
            chain_run.faction_relation_counter_d[2] == 0U &&
            chain_run.metrics.root_relation_counter_d_events == 0U,
            "Group-A collision-only root terminal incorrectly credited relation counter D");
    require(target.model_components[0].model_rotation_angle0 == 0x1111U &&
            chain_run.metrics.root_direct_impact_angle_updates == 0U,
            "propagated parent damage incorrectly inherited the child impact angle");
    require(target.root_model_integrity == 0 && target.root_model_terminal &&
            target.root_model_last_impact_overshoot == 0x50000,
            "legacy root summary no longer mirrors the primary root component");

    // Direct root class-0 terminal damage has an additional side effect: when MDL +0x278 is zero,
    // 0x0052A2E0 stores the impact angle into root model-node rotation angle0 before counter C/D.
    std::array<thandor::SimulationLabSeedEntity, 2> root_seeds{};
    root_seeds[0] = mover_seed();
    root_seeds[1].source_placement_index = 2U;
    root_seeds[1].army_asset_id = 0x201U;
    root_seeds[1].faction_index = 3U;
    root_seeds[1].runtime_model_class_id = 0U;
    root_seeds[1].motion.collision_radius_q12 = 0x0800;
    root_seeds[1].actor.position = {8.25F, 8.0F, 0.0F};
    thandor::SimulationLabModelComponentSeed direct_root;
    direct_root.model_definition_id = 400U;
    direct_root.runtime_class_id = 0U;
    direct_root.placement_contact_kind = 0;
    direct_root.maximum_integrity = 0x80000;
    direct_root.model_rotation_angle0 = 0x4444U;
    root_seeds[1].model_components.push_back(direct_root);

    thandor::SimulationLabExperiment root_experiment;
    root_experiment.reset(one_run_config(), base, root_seeds);
    root_experiment.step(field, 1U);
    const auto& root_run = root_experiment.runs().front();
    const auto& direct = root_run.entities[1].model_components.front();
    require(direct.integrity == 0 && direct.last_impact_overshoot == 0x80000,
            "direct class-0 root terminal damage/overshoot drifted");
    require(direct.model_rotation_angle0 == 0U && root_run.metrics.root_direct_impact_angle_updates == 1U,
            "eastward direct terminal impact did not store the recovered zero turn16 impact angle");
    require(root_run.faction_relation_counter_c[3] == 0U &&
            root_run.faction_relation_counter_d[3] == 0U &&
            root_run.metrics.root_relation_counter_c_events == 0U,
            "non-Group-A collision-only root terminal incorrectly credited relation counter C");

    std::cout << "V537_COMPONENT_DAMAGE_PROPAGATION PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V537_COMPONENT_DAMAGE_PROPAGATION FAIL: " << error.what() << '\n';
    return 1;
}
