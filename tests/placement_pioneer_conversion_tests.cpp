#include "thandor/simulation_lab.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

thandor::SimulationLabConfig config() {
    thandor::SimulationLabConfig value;
    value.run_count = 1U;
    value.duration_ticks = 256U;
    value.parameter_index = 100U;
    value.sweep_mode = thandor::SimulationLabSweepMode::Fixed;
    return value;
}

thandor::SimulationLabSeedEntity pioneer_seed(std::uint32_t flags = 0U) {
    thandor::SimulationLabSeedEntity seed;
    seed.source_placement_index = 7U;
    seed.army_asset_id = 50U;
    seed.faction_index = 1U;
    seed.runtime_model_class_id = 18U;
    seed.root_model_maximum_integrity = 4000;
    seed.runtime_class_specific_c0_raw = 900;
    seed.runtime_class_specific_c4_raw = 153U;
    // Keep the lifecycle test independent of the generic placement candidate chosen by the compact
    // Lab. The recovered handler still performs the same exact +0x19C/+0x1A8 circle test.
    seed.runtime_class_specific_19c_raw = 0x01000000;
    seed.class18_conversion_effect_lifetime_ticks = 3U;
    seed.runtime_flags = flags;
    seed.motion.speed_q12_per_tick = 0x1000;
    seed.actor.position = {10.0F, 10.0F, 2.0F};
    seed.actor.body_yaw_turn16 = 0x2345U;
    seed.actor.move_target = thandor::Vec3f{25.0F, 25.0F, 2.0F};
    return seed;
}

thandor::SimulationLabSeedEntity group_a_seed() {
    thandor::SimulationLabSeedEntity seed;
    seed.source_placement_index = 8U;
    seed.army_asset_id = 330U;
    seed.faction_index = 1U;
    // Class 14 is a proven Group-A class and ARM330 is the Pioneer-placeable Xenite special asset.
    // Using this recovered startup path avoids an impossible ordinary building placement from a
    // Pioneer-only faction.
    seed.runtime_model_class_id = 14U;
    seed.building = true;
    seed.root_model_maximum_integrity = 10000;
    seed.runtime_class_specific_1a8_raw = 0x01000000;
    seed.actor.position = {11.0F, 10.0F, 2.0F};
    return seed;
}

thandor::SimulationLabProductionAsset group_a_asset() {
    thandor::SimulationLabProductionAsset asset;
    asset.army_asset_id = 330U;
    asset.army_runtime_flags14_raw = 1U;
    asset.entity_seed = group_a_seed();
    asset.entity_seed.source_placement_index = std::numeric_limits<std::size_t>::max();
    asset.entity_seed.faction_index = 0U;
    return asset;
}

thandor::SimulationLabProductionAsset replacement_asset() {
    thandor::SimulationLabProductionAsset asset;
    asset.army_asset_id = 900U;
    asset.entity_seed.source_placement_index = std::numeric_limits<std::size_t>::max();
    asset.entity_seed.army_asset_id = 900U;
    asset.entity_seed.runtime_model_class_id = 4U;
    asset.entity_seed.building = true;
    asset.entity_seed.root_model_maximum_integrity = 8000;
    return asset;
}

thandor::SimulationLabExperiment make_experiment(
    std::vector<thandor::SimulationLabSeedEntity> seeds,
    std::span<const thandor::SimulationLabProductionAsset> products = {}) {
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
    std::array<thandor::SimulationLabFactionEconomySeed, 8> economy{};
    thandor::SimulationLabExperiment experiment;
    experiment.reset(config(), parameters, seeds, economy, products);
    return experiment;
}

void queue_group_a_placement(thandor::SimulationLabRun& run) {
    run.pending_asset_requests.push_back({330U, 1U, run.tick, 0U});
}
} // namespace

int main() try {
    auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    // The newly committed Group-A object is the retail Pioneer-placeable Xenite extractor path.
    // Give the fixture a real class-0 support island near the Pioneer rather than bypassing V569's
    // recovered build-area/range gates with an arbitrary generic structure.
    thandor::Brush resource_brush;
    resource_brush.radius_cells = 1.0F;
    resource_brush.shape = thandor::BrushShape::Square;
    resource_brush.falloff = thandor::BrushFalloff::Constant;
    field.paint_placement_occupancy_class(11.0F, 10.0F, resource_brush, 0U, true);
    const auto replacement = replacement_asset();
    const auto group_a_product = group_a_asset();
    std::array<thandor::SimulationLabProductionAsset, 2> products{replacement, group_a_product};

    // Existing/default Group-A runtimes are class-identity state only. Retail does not replay their
    // class command every update, so merely starting the Lab next to one must not convert Pioneer.
    auto preexisting = make_experiment({pioneer_seed(), group_a_seed()}, products);
    preexisting.step(field, 80U);
    const auto& preexisting_run = preexisting.runs().front();
    require(preexisting_run.metrics.pioneer_conversion_effects_started == 0U,
            "pre-existing Group-A building incorrectly replayed its class command");
    require((preexisting_run.entities.front().runtime_flags & 0x08U) == 0U,
            "pre-existing Group-A building incorrectly held the Pioneer");

    // A newly committed Group-A placement does dispatch ArmyRuntime_DispatchClassCommand. Conversion
    // begins on that placement tick and preserves the previously recovered effect/replacement lifecycle.
    auto experiment = make_experiment({pioneer_seed()}, products);
    auto& run = experiment.runs().front();
    run.entities.front().root_model_integrity = 2000;
    run.entities.front().model_components[run.entities.front().root_model_component_index].integrity = 2000;
    queue_group_a_placement(run);

    experiment.step(field, 73U);
    require(run.metrics.pioneer_conversion_effects_started == 0U,
            "Group-A conversion fired before the construction placement commit");
    experiment.step(field, 1U);
    const auto pioneer_it = std::find_if(run.entities.begin(), run.entities.end(),
        [](const auto& entity) { return entity.army_asset_id == 50U; });
    require(pioneer_it != run.entities.end(),
            "Pioneer disappeared before the conversion effect completed");
    require((pioneer_it->runtime_flags & 0x08U) != 0U,
            "new Group-A placement did not set the Pioneer runtime hold bit");
    require(!pioneer_it->actor.move_target.has_value(),
            "conversion-held Pioneer retained its previous movement target");
    require(run.metrics.pioneer_conversion_effects_started == 1U,
            "conversion effect did not start exactly once");
    require(run.metrics.pioneer_conversions_completed == 0U,
            "Pioneer converted on the effect-start tick");
    const auto conversion_position = pioneer_it->actor.position;
    const auto conversion_yaw = pioneer_it->actor.body_yaw_turn16;
    const auto events = thandor::simulation_lab_one_shot_events_after(run, 0U);
    require(events.size() == 1U &&
            events.front().kind == thandor::SimulationLabOneShotEventKind::PioneerConversionEffect &&
            events.front().effect_id == 153U,
            "conversion effect event did not preserve the Pioneer MDL +0xC4 EFF id");

    experiment.step(field, 2U);
    require(std::any_of(run.entities.begin(), run.entities.end(),
                        [](const auto& entity) { return entity.army_asset_id == 50U; }),
            "Pioneer converted before the recovered effect lifetime elapsed");

    experiment.step(field, 1U);
    require(run.metrics.pioneer_conversions_completed == 1U,
            "completion action 3 did not replace the Pioneer");
    require(std::none_of(run.entities.begin(), run.entities.end(),
                         [](const auto& entity) { return entity.army_asset_id == 50U; }),
            "old Pioneer survived a successful replacement creation");
    const auto replacement_it = std::find_if(run.entities.begin(), run.entities.end(),
        [](const auto& entity) { return entity.army_asset_id == 900U; });
    require(replacement_it != run.entities.end(),
            "MDL +0xC0 replacement ARM was not created");
    require(replacement_it->synthetic && replacement_it->source_placement_index == 7U,
            "replacement did not preserve authored PlayerStart lineage");
    require(replacement_it->faction_index == 1U &&
            replacement_it->actor.position.x == conversion_position.x &&
            replacement_it->actor.position.y == conversion_position.y &&
            replacement_it->actor.position.z == conversion_position.z &&
            replacement_it->actor.body_yaw_turn16 == conversion_yaw,
            "replacement did not preserve Pioneer conversion-time faction/pose/heading");
    require(replacement_it->root_model_maximum_integrity == 8000 &&
            replacement_it->root_model_integrity == 4000,
            "replacement integrity was not scaled oldCurrent*newMax/oldMax");

    // An AI world-job placement happens after owner maintenance, so its EFF
    // begins counting on the next tick. Batch changes affect the pending effect.
    auto long_effect=pioneer_seed();long_effect.class18_conversion_effect_lifetime_ticks=23U;
    auto batched=make_experiment({long_effect},products);
    batched.set_runtime_callback_step_ticks(5U);
    queue_group_a_placement(batched.runs().front());batched.step(field,74U);
    require(batched.runs().front().entities.front().class18_conversion_effect_remaining_ticks==23U,
            "AI placement effect advanced before the next owner-maintenance phase");
    batched.step(field,4U);
    require(batched.runs().front().entities.front().class18_conversion_effect_remaining_ticks==3U,
            "effect did not consume five maintenance updates per callback");
    batched.set_runtime_callback_step_ticks(1U);batched.step(field,2U);
    require(batched.runs().front().metrics.pioneer_conversions_completed==0U,
            "pending effect ignored the callback batch change");
    batched.step(field);
    require(batched.runs().front().metrics.pioneer_conversions_completed==1U,
            "effect did not complete on its twenty-third maintenance update");

    auto blocker = pioneer_seed();
    blocker.source_placement_index = 9U;
    blocker.army_asset_id = 77U;
    blocker.runtime_model_class_id = 1U;
    blocker.runtime_class_specific_c0_raw = 0;
    blocker.runtime_class_specific_c4_raw = 0U;
    blocker.class18_conversion_effect_lifetime_ticks = 0U;
    blocker.actor.position = {10.5F, 10.0F, 2.0F};
    auto blocked = make_experiment({pioneer_seed(), blocker}, products);
    queue_group_a_placement(blocked.runs().front());
    blocked.step(field, 74U);
    require(blocked.runs().front().metrics.pioneer_conversion_effects_started == 0U,
            "nearby same-faction non-Pioneer did not block Group-A conversion");

    auto ineligible = make_experiment({pioneer_seed(0x10U)}, products);
    queue_group_a_placement(ineligible.runs().front());
    ineligible.step(field, 74U);
    require(ineligible.runs().front().metrics.pioneer_conversion_effects_started == 0U,
            "Pioneer with runtime flag mask 0x18 set was incorrectly eligible for conversion");

    auto oldest = pioneer_seed();
    auto newest = oldest;
    newest.source_placement_index = oldest.source_placement_index + 1U;
    auto overlapping = make_experiment({oldest, newest}, products);
    queue_group_a_placement(overlapping.runs().front());
    overlapping.step(field, 74U);
    const auto& contenders = overlapping.runs().front().entities;
    require((contenders[0].runtime_flags & 0x08U) != 0U &&
            (contenders[1].runtime_flags & 0x08U) == 0U,
            "Group-A conversion did not select the oldest eligible Pioneer");

    std::array<thandor::SimulationLabProductionAsset, 1> group_only{group_a_product};
    auto missing_product = make_experiment({pioneer_seed()}, group_only);
    queue_group_a_placement(missing_product.runs().front());
    missing_product.step(field, 78U);
    const auto& failed_run = missing_product.runs().front();
    require(std::any_of(failed_run.entities.begin(), failed_run.entities.end(),
                        [](const auto& entity) { return entity.army_asset_id == 50U; }),
            "Pioneer was destroyed when replacement creation failed");
    require(failed_run.metrics.pioneer_conversions_completed == 0U,
            "failed replacement creation was counted as a completed conversion");

    std::cout << "V568_PIONEER_PLACEMENT_DISPATCH PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V568_PIONEER_PLACEMENT_DISPATCH FAIL: " << error.what() << '\n';
    return 1;
}
