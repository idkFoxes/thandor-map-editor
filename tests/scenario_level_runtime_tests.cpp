#include "thandor/field.hpp"
#include "thandor/simulation_lab.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

thandor::SimulationLabConfig config_for(std::uint32_t duration = 80U) {
    thandor::SimulationLabConfig config;
    config.run_count = 1U;
    config.duration_ticks = duration;
    config.vary_seed_per_run = false;
    return config;
}

thandor::SimulationLabModelComponentSeed component(std::uint32_t runtime_class,
                                                   std::uint32_t definition_id = 0U) {
    thandor::SimulationLabModelComponentSeed result;
    result.runtime_class_id = runtime_class;
    result.model_definition_id = definition_id;
    result.maximum_integrity = 1000;
    return result;
}

void test_retail_bootstrap_capacity_and_primary_energy_demand() {
    thandor::SimulationLabSeedEntity seed;
    seed.army_asset_id = 10U;
    seed.faction_index = 1U;
    seed.runtime_model_class_id = 11U;
    auto storage_x = component(14U, 100U);
    storage_x.has_resource_storage_capacity_contribution = true;
    storage_x.resource_storage_selector = 0U;
    storage_x.resource_storage_capacity_contribution_q4 = 160U;
    storage_x.retail_energy_demand_q4 = 100U;
    auto storage_t = component(15U, 101U);
    storage_t.has_resource_storage_capacity_contribution = true;
    storage_t.resource_storage_selector = 1U;
    storage_t.resource_storage_capacity_contribution_q4 = 320U;
    storage_t.parent_component_index = 0U;
    auto generator = component(16U, 102U);
    generator.energy_generation_capacity_q4 = 150U;
    generator.parent_component_index = 0U;
    seed.model_components = {storage_x, storage_t, generator};

    std::array<thandor::SimulationLabFactionEconomySeed, 8> economy{};
    economy[1].baseline_xenite_storage_limit_q4 = 4000U;
    economy[1].baseline_tritium_storage_limit_q4 = 4000U;
    economy[1].derive_storage_limits_from_components = true;
    economy[1].derive_energy_allocation_from_components = true;
    economy[1].baseline_energy_supply_q4 = 0U;
    economy[1].baseline_energy_generation_capacity_q4 = 50U;
    economy[1].tritium_current_q4 = 100U;

    thandor::SimulationLabProductionAsset low_cost_primary;
    low_cost_primary.army_asset_id = 500U;
    low_cost_primary.ai_definition_class_value74 = 0U;
    thandor::SimulationLabProductionAsset high_cost_primary;
    high_cost_primary.army_asset_id = 501U;
    high_cost_primary.ai_definition_class_value74 = 1U;
    const std::array production_assets{low_cost_primary, high_cost_primary};

    auto config = config_for();
    config.primary_queue_overrides[1].enabled = true;
    config.primary_queue_overrides[1].count = 2U;
    config.primary_queue_overrides[1].army_asset_ids[0] = 500U;
    config.primary_queue_overrides[1].army_asset_ids[1] = 501U;

    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> ai{};
    thandor::SimulationLabExperiment experiment;
    experiment.reset(config, ai, std::span<const thandor::SimulationLabSeedEntity>(&seed, 1U),
                     economy, production_assets);
    auto& initial = experiment.runs().front();
    require(initial.faction_economy[1].xenite_storage_limit_q4 == 4160U,
            "retail 4000-Q4 Xenite baseline was not combined with class-14 capacity");
    require(initial.faction_economy[1].tritium_storage_limit_q4 == 4320U,
            "retail 4000-Q4 Tritium baseline was not combined with class-15 capacity");
    require(initial.pending_asset_requests.size() == 2U,
            "primary inventory override was not populated for the energy baseline test");

    thandor::FieldGrid field = thandor::FieldGrid::create(4U, 4U, 0, 0U);
    experiment.step(field, 8U);
    const auto& run = experiment.runs().front();
    require(run.faction_energy_generation_capacity_q4[1] == 200U,
            "retail base generation plus class-16 contribution was not preserved");
    require(run.faction_supplied_energy_demand_q4[1] == 196U,
            "primary-inventory 0x10/0x50 demand was not included ahead of component demand");
    require(run.faction_unpowered_energy_demand_q4[1] == 0U,
            "component unexpectedly browned out with 200 Q4 supply and 196 Q4 total demand");
    require(run.faction_last_tritium_energy_burn_q4[1] == 12U &&
            run.faction_economy[1].tritium_current_q4 == 88U,
            "primary-inventory demand was not included in retail Tritium burn");
}

void test_relation_bootstrap_conditions_and_trigger_cadence() {
    thandor::SimulationLabSeedEntity faction1;
    faction1.army_asset_id = 456U;
    faction1.faction_index = 1U;
    faction1.runtime_model_class_id = 2U;
    faction1.model_components = {component(2U, 123U)};
    faction1.motion.speed_q12_per_tick = 0x100;
    faction1.actor.position = {0.0F, 0.0F, 0.0F};

    thandor::SimulationLabSeedEntity faction2 = faction1;
    faction2.army_asset_id = 457U;
    faction2.faction_index = 2U;
    faction2.actor.position = {1.0F, 0.0F, 0.0F};
    const std::array seeds{faction1, faction2};

    thandor::SimulationLabScenarioRuntimeSeed scenario;
    scenario.enabled = true;
    scenario.assignable_faction_count = 1U;
    scenario.active_faction_count = 2U;
    // One low-byte group containing factions 1 and 2: retail level init transitions the pair to 8/8.
    scenario.relation_state8_faction_group_masks = (1U << 1U) | (1U << 2U);

    auto& relation = scenario.scheduled_conditions[0];
    relation.status_and_kind = static_cast<std::uint8_t>(
        thandor::LevelScheduledConditionKind::FactionInactiveOrRelationAtLeast8);
    relation.set_operand(0, 1U);
    relation.set_operand(1, 2U);

    auto& countdown = scenario.scheduled_conditions[1];
    countdown.status_and_kind = static_cast<std::uint8_t>(
        thandor::LevelScheduledConditionKind::CountdownElapsed);
    countdown.set_operand(1, 2U);

    auto& expression = scenario.scheduled_conditions[2];
    expression.status_and_kind = static_cast<std::uint8_t>(
        thandor::LevelScheduledConditionKind::BooleanPostfixExpression);
    expression.payload_bytes.fill(0U);
    expression.payload_bytes[0] = 0U;
    expression.payload_bytes[1] = 1U;
    expression.payload_bytes[2] = 0xFEU; // AND
    expression.payload_bytes[3] = 0xFCU; // END

    auto& inactive_trigger = scenario.end_condition_triggers[0];
    inactive_trigger.state_flags = 1U;
    inactive_trigger.faction_runtime_index = 3U;
    inactive_trigger.condition_index = 2U;

    auto& trigger = scenario.end_condition_triggers[1];
    trigger.state_flags = 1U;
    trigger.faction_runtime_index = 2U;
    trigger.condition_index = 2U;
    trigger.end_movie_selection_index = 7U;

    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> ai{};
    std::array<thandor::SimulationLabFactionEconomySeed, 8> economy{};
    thandor::SimulationLabExperiment experiment;
    experiment.reset(config_for(), ai, seeds, economy, {}, {}, {}, {}, scenario);
    thandor::FieldGrid field = thandor::FieldGrid::create(8U, 8U, 0, 0U);

    const auto& initial = experiment.runs().front();
    require(((initial.faction_packed_relation_states[1] >> 8U) & 0xFU) == 8U &&
            ((initial.faction_packed_relation_states[2] >> 4U) & 0xFU) == 8U,
            "LEV state-8 group was not applied symmetrically to the bootstrap relation image");
    require(((initial.faction_packed_relation_states[1] >> 4U) & 0xFU) == 0xFU,
            "retail self relation nibble F was not preserved");
    require((initial.ai_controlled_faction_mask & (1U << 2U)) != 0U,
            "faction after assignable prefix was not marked computer-only");
    require((initial.ai_controlled_faction_mask & (1U << 1U)) == 0U,
            "retail LEV assignable human prefix unexpectedly became computer-only");
    {
        auto lab_scenario=scenario;
        lab_scenario.simulate_all_active_factions_as_ai=true;
        thandor::SimulationLabExperiment lab;
        lab.reset(config_for(),ai,seeds,economy,{}, {}, {}, {},lab_scenario);
        const auto& autonomous=lab.runs().front();
        require((autonomous.ai_controlled_faction_mask & (1U << 1U)) != 0U &&
                (autonomous.ai_controlled_faction_mask & (1U << 2U)) != 0U,
                "Simulation Lab must mark factions 1 and 2 AI even in an assignable LEV");
        require(autonomous.assignable_faction_count==scenario.assignable_faction_count,
                "Lab autonomy incorrectly rewrote retail LEV player-assignment metadata");
    }

    experiment.step(field, 19U);
    require(!experiment.runs().front().scheduled_conditions[0].satisfied(),
            "condition runtime executed before the retail 20-tick cadence");

    experiment.step(field, 1U);
    auto& at20 = experiment.runs().front();
    require(at20.scheduled_conditions[0].satisfied(),
            "relation>=8 condition did not evaluate from the live packed relation row");
    require(!at20.scheduled_conditions[1].satisfied() &&
            at20.scheduled_conditions[1].operand(1) == 1U,
            "countdown did not decrement exactly once on the 20-tick condition pass");
    require(!at20.scheduled_conditions[2].satisfied() && !at20.last_fired_end_trigger_index,
            "postfix AND or trigger fired before the countdown became satisfied");
    // Allied state >=8 must also remove the opposing faction from the compact Lab target search.
    require(!at20.entities[0].target_entity_index.has_value(),
            "relation-state 8 faction was still treated as a hostile Simulation Lab target");

    experiment.step(field, 20U);
    auto& at40 = experiment.runs().front();
    require(at40.scheduled_conditions[1].satisfied() &&
            at40.scheduled_conditions[1].operand(1) == 0U,
            "countdown did not clamp to zero on its second scheduled evaluation");
    require(at40.scheduled_conditions[2].satisfied(),
            "retail postfix condition bit-stack did not evaluate C0 C1 AND");
    require(at40.last_fired_end_trigger_index == 1U &&
            at40.end_condition_triggers[0].processed() &&
            at40.end_condition_triggers[1].processed() &&
            at40.faction_lifecycle_states[2] == 2U,
            "trigger for inactive faction blocked an active faction's end trigger");

    experiment.step(field, 20U);
    const auto& at60 = experiment.runs().front();
    require(at60.faction_lifecycle_states[2] == 3U,
            "ENDING_PENDING faction did not advance to ENDED_OR_TRANSITIONED on the next condition pass");
}
// V586: the recovered 0x0050E??? level-script scan uses faction IDs for
// conditions 1/2/3/8. These cases used to compare operand 0 to a model MDL
// definition ID, silently ending otherwise live factions on Asgard.
void test_retail_script_faction_owner_node_conditions() {
    thandor::SimulationLabSeedEntity pioneer;
    pioneer.faction_index = 1U;
    pioneer.army_asset_id = 50U;
    pioneer.runtime_model_class_id = 18U;
    pioneer.model_components = {component(18U, 0x1234U)};
    pioneer.actor.position = {0.0F, 0.0F, 0.0F};

    thandor::SimulationLabSeedEntity factory;
    factory.faction_index = 2U;
    factory.army_asset_id = 300U;
    factory.runtime_model_class_id = 11U;
    auto linked = component(14U, 0x5678U);
    linked.parent_component_index = 0U;
    factory.model_components = {component(11U, 0x4321U), linked};
    factory.actor.position = {3.0F, -2.0F, 0.0F};
    const std::array seeds{pioneer, factory};

    thandor::SimulationLabScenarioRuntimeSeed scenario;
    scenario.enabled = true;
    scenario.active_faction_count = 2U;
    scenario.assignable_faction_count = 2U;
    const auto setup = [&](std::size_t index, thandor::LevelScheduledConditionKind kind,
                           std::uint32_t faction, std::uint32_t third_operand = 0U) {
        auto& c = scenario.scheduled_conditions[index];
        c.status_and_kind = static_cast<std::uint8_t>(kind);
        c.set_operand(0, faction);
        c.set_operand(2, third_operand);
    };
    using Kind = thandor::LevelScheduledConditionKind;
    setup(0U, Kind::NoActiveEntityWithDefinition, 1U); // live Pioneer
    setup(1U, Kind::NoActiveEntityWithDefinitionAndClassCommandGroupA, 2U); // live class 11
    setup(2U, Kind::NoActiveEntityWithDefinitionAndRuntimeId, 2U, 300U);
    setup(3U, Kind::MatchingDefinitionAndRuntimeIdActiveEntityCountAtLeast, 2U, 300U);
    scenario.scheduled_conditions[3].set_operand(1U, 2U); // one root plus an unlinked child
    setup(4U, Kind::NoActiveEntityWithDefinitionAndClassCommandGroupA, 1U);
    setup(5U, Kind::NoActiveEntityWithDefinitionAndRuntimeId, 1U, 999U);
    setup(6U, Kind::NoActiveEntityWithDefinition, 3U); // absent faction
    for (std::uint32_t i = 0U; i != 2U; ++i) {
        auto& trigger = scenario.end_condition_triggers[i];
        trigger.state_flags = 1U;
        trigger.faction_runtime_index = static_cast<std::uint8_t>(i + 1U);
        trigger.condition_index = static_cast<std::uint8_t>(i);
    }
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> ai{};
    std::array<thandor::SimulationLabFactionEconomySeed, 8> economy{};
    thandor::SimulationLabExperiment experiment;
    experiment.reset(config_for(), ai, seeds, economy, {}, {}, {}, {}, scenario);
    auto field = thandor::FieldGrid::create(16U, 16U, 0, 0U);
    experiment.step(field, 20U);
    const auto& run = experiment.runs().front();
    const auto& c = run.scheduled_conditions;
    require(!c[0].satisfied() && !c[1].satisfied() && !c[2].satisfied(),
            "a live faction's MODEL owner nodes must prevent the corresponding end conditions");
    require(!c[3].satisfied(), "asset-count condition included an unlinked attached model");
    require(c[4].satisfied() && c[5].satisfied() && c[6].satisfied(),
            "absent faction, asset, or Group-A model must satisfy its negative condition");
    require(!run.last_fired_end_trigger_index &&
            !run.end_condition_triggers[0].processed() &&
            !run.end_condition_triggers[1].processed() &&
            run.faction_lifecycle_states[1U] == 1U && run.faction_lifecycle_states[2U] == 1U,
            "live P1/P2 armies must not be disabled by a false scenario end condition");
    for (const auto& entity : run.entities)
        require(entity.model_components.empty() ||
                (entity.model_components[0].runtime_flags_ec & 0x10U) == 0U,
                "a false level end trigger disabled a live Pioneer or factory");
}

void test_local_end_trigger_reveals_view_faction_on_phase7() {
    auto field=thandor::FieldGrid::create(16U,16U,0,0U);
    std::array<std::uint32_t,thandor::AiKnowledgeData::kParameterCount> ai{};
    thandor::SimulationLabSeedEntity owner;
    owner.army_asset_id=300;owner.faction_index=1;owner.runtime_model_class_id=14;
    owner.model_components={component(14U,123U)};
    owner.actor.position={thandor::q12_to_float(field.at(4,4).world_x_q12),
                          thandor::q12_to_float(field.at(4,4).world_y_q12),0};
    for(const auto view:{1U,2U})for(const auto skip:{0U,1U}){
        thandor::SimulationLabScenarioRuntimeSeed scenario;
        scenario.enabled=true;scenario.active_faction_count=2;
        scenario.active_view_faction_index=view;
        auto& condition=scenario.scheduled_conditions[0];
        condition.status_and_kind=static_cast<uint8_t>(thandor::LevelScheduledConditionKind::CountdownElapsed);
        condition.set_operand(1,1);
        auto& trigger=scenario.end_condition_triggers[0];
        trigger.state_flags=1;trigger.faction_runtime_index=2;trigger.condition_index=0;
        trigger.skip_army_disable_when_one=skip;
        thandor::SimulationLabExperiment lab;
        lab.reset(config_for(),ai,std::span<const thandor::SimulationLabSeedEntity>(&owner,1),{},{},{},{},{},scenario);
        lab.step(field,19);
        require(!lab.runs()[0].local_faction_ended,"view ended before its scheduled end trigger");
        lab.step(field,1);
        const bool reveal=view==2&&skip!=1;
        require(lab.runs()[0].local_faction_ended==reveal,
                "local end visibility must follow current view and the skip-disable flag");
        lab.step(field,3); // next occupancy rebuild, tick23
        const auto& run=lab.runs()[0];
        require(run.retail_scratch&&run.retail_scratch->field_occupancy_masks.size()==field.cells().size(),
                "end-trigger fixture has no retained field occupancy");
        for(const auto mask:run.retail_scratch->field_occupancy_masks)
            require(((mask>>(2U*8U))&1U)==unsigned(reveal),
                    "phase7 did not apply local faction end reveal across the whole map");
    }
}

} // namespace

int main() {
    try {
        test_retail_bootstrap_capacity_and_primary_energy_demand();
        test_relation_bootstrap_conditions_and_trigger_cadence();
        test_retail_script_faction_owner_node_conditions();
        test_local_end_trigger_reveals_view_faction_on_phase7();
        std::cout << "V572 V537 level-runtime tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V572 level-runtime failure: " << error.what() << '\n';
        return 1;
    }
}
