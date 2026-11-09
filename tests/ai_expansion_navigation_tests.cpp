#include "thandor/path_diagnostics.hpp"
#include "thandor/simulation_lab.hpp"
#include "thandor/technology.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

thandor::SimulationLabConfig config(std::uint32_t duration = 512U) {
    thandor::SimulationLabConfig value;
    value.run_count = 1U;
    value.duration_ticks = duration;
    value.parameter_index = 100U;
    value.sweep_mode = thandor::SimulationLabSweepMode::Fixed;
    value.fixed_value = 0;
    return value;
}

thandor::SimulationLabSeedEntity seed(std::uint32_t asset_id, std::uint32_t runtime_class,
                                      std::uint32_t faction, thandor::Vec3f position,
                                      bool building, std::int32_t extent_q12 = 0) {
    thandor::SimulationLabSeedEntity value;
    value.source_placement_index = static_cast<std::size_t>(asset_id + faction * 0x1000U);
    value.army_asset_id = asset_id;
    value.faction_index = faction;
    value.runtime_model_class_id = runtime_class;
    value.building = building;
    value.actor.position = position;
    value.root_model_maximum_integrity = 1000;
    value.runtime_class_specific_19c_raw = extent_q12;
    return value;
}

thandor::SimulationLabProductionAsset product(std::uint32_t asset_id,
                                               std::uint32_t runtime_class,
                                               std::int32_t class_c0,
                                               std::int32_t extent_q12,
                                               std::uint32_t flags = 0x11U) {
    thandor::SimulationLabProductionAsset value;
    value.army_asset_id = asset_id;
    value.army_runtime_flags14_raw = flags;
    value.runtime_class_specific_c0_raw = class_c0;
    value.xenite_cost_q4 = 1U;
    value.build_duration_ticks = 2U;
    value.entity_seed = seed(asset_id, runtime_class, 0U, {}, true, extent_q12);
    value.entity_seed.runtime_class_specific_c0_raw = class_c0;
    return value;
}

std::vector<thandor::SimulationLabProductionAsset> startup_catalog(bool include_factory301) {
    std::vector<thandor::SimulationLabProductionAsset> products{
        product(300U, 11U, 0, 0x9000),
        product(330U, 14U, 0, 0x9000),
        product(332U, 14U, 7, 0x9000),
        product(333U, 16U, 0, 0x9000),
    };
    if (include_factory301) products.push_back(product(301U, 13U, 0, 0x9000));
    return products;
}

std::array<thandor::SimulationLabFactionEconomySeed, 8> rich_economy() {
    std::array<thandor::SimulationLabFactionEconomySeed, 8> economy{};
    economy[1].xenite_current_q4 = 100000U;
    economy[1].tritium_current_q4 = 100000U;
    return economy;
}

void paint_xenite(thandor::FieldGrid& field, float x, float y) {
    thandor::Brush brush;
    brush.radius_cells = 2.0F;
    brush.shape = thandor::BrushShape::Square;
    brush.falloff = thandor::BrushFalloff::Constant;
    field.paint_placement_occupancy_class(x, y, brush, 0U, true);
}
}

int main() try {
    // Regression 1: the raw 0x2D05 Pioneer bootstrap command is allowed to point beyond the map,
    // but retail immediately resolves/clips it before storing the movement target. The Lab must do
    // the same instead of routing to the edge while retaining an off-map actor.move_target.
    {
        auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
        const auto edge_scratch = thandor::v519_path_clip_to_inner_scratch(field, {100000, 100000});
        const auto edge_world = thandor::v519_path_scratch_center_to_world_q12(edge_scratch);
        auto pioneer = seed(50U, 18U, 1U,
            {thandor::q12_to_float(edge_world.x_q12) - 0.25F,
             thandor::q12_to_float(edge_world.y_q12), 0.0F}, false, 0x5000);
        pioneer.actor.body_yaw_turn16 = 0U; // +X, deliberately out through the nearest boundary.
        pioneer.motion.speed_q12_per_tick = 0x300;
        pioneer.motion.yaw_max_velocity_turn16 = 0x800;
        pioneer.motion.yaw_acceleration_turn16 = 0x100;
        pioneer.has_traversal_profile = true;
        pioneer.traversal_low_band_index = 0U;
        pioneer.traversal_high_band_index = 0U;

        std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
        thandor::SimulationLabExperiment experiment;
        const auto products = startup_catalog(false);
        experiment.reset(config(64U), parameters,
            std::span<const thandor::SimulationLabSeedEntity>(&pioneer, 1U),
            rich_economy(), products);
        experiment.step(field, 10U);
        const auto& run = experiment.runs().front();
        const auto& live = run.entities.front();
        require(run.metrics.navigation_destination_retargets != 0U,
                "off-map Pioneer command did not exercise the V574 destination retarget");
        if (live.actor.move_target) {
            const auto raw = thandor::v519_path_world_to_scratch(
                thandor::float_to_q12(live.actor.move_target->x),
                thandor::float_to_q12(live.actor.move_target->y));
            require(raw == thandor::v519_path_clip_to_inner_scratch(field, raw),
                    "Pioneer retained an off-map raw movement target after path resolution");
        }
        const auto immediate_actor_cell = thandor::v519_path_world_to_scratch(
            thandor::float_to_q12(live.actor.position.x),
            thandor::float_to_q12(live.actor.position.y));
        require(immediate_actor_cell == thandor::v519_path_clip_to_inner_scratch(field, immediate_actor_cell),
                "Pioneer actor escaped the retail inner GridScratch boundary on the retarget tick");
        experiment.step(field, 63U);
        const auto actor_cell = thandor::v519_path_world_to_scratch(
            thandor::float_to_q12(experiment.runs().front().entities.front().actor.position.x),
            thandor::float_to_q12(experiment.runs().front().entities.front().actor.position.y));
        require(actor_cell == thandor::v519_path_clip_to_inner_scratch(field, actor_cell),
                "Pioneer actor escaped the retail inner GridScratch boundary");
    }

    // Regression 2: a prebuilt HQ suppresses the special four-entry startup primary queue, but it
    // does not suppress ordinary AI expansion. Retail emits ARM301 from the strategic 0x12D family
    // once ARM330 exists and TEC1 is still locked. V573 omitted ARM301 from the construction candidate
    // set entirely, so a valid HQ could sit forever without creating its first factory.
    {
        auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
        paint_xenite(field, 20.0F, 20.0F);
        const auto& hq_cell = field.at(20U, 20U);
        auto hq = seed(300U, 11U, 1U,
            {thandor::q12_to_float(hq_cell.world_x_q12),
             thandor::q12_to_float(hq_cell.world_y_q12), 0.0F}, true, 0x9000);
        auto mine = seed(330U, 14U, 1U,
            {thandor::q12_to_float(hq_cell.world_x_q12) + 1.0F,
             thandor::q12_to_float(hq_cell.world_y_q12), 0.0F}, true, 0x9000);
        std::array<thandor::SimulationLabSeedEntity, 2> seeds{hq, mine};

        auto products = startup_catalog(true);
        std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
        parameters[0x07U] = 200000U; // exact KI source for strategic ARM301/0x12D.
        std::array<thandor::FactionTechnologyMask256, 8> technology_masks{};
        // Retail selects ARM301 before TEC1; after TEC1 it scores ARM302..ARM306.
        // Keep TEC1 locked when asserting the ARM301 branch.

        thandor::SimulationLabExperiment experiment;
        experiment.reset(config(), parameters, seeds, rich_economy(), products, {},
                         technology_masks, {});
        auto& run = experiment.runs().front();
        require(run.pending_asset_requests.empty() && run.secondary_purchase_requests.empty(),
                "prebuilt HQ fixture unexpectedly received startup primary stock");
        for (unsigned batch = 0; batch < 12U; ++batch) {
            experiment.step(field, 32U);
            if (std::any_of(run.entities.begin(), run.entities.end(), [](const auto& entity) {
                    return entity.faction_index == 1U && entity.army_asset_id == 301U;
                })) break;
        }
        require(run.metrics.factory301_candidate_events != 0U,
                "retail strategic ARM301 candidate family never fired");
        require(std::any_of(run.entities.begin(), run.entities.end(), [](const auto& entity) {
                    return entity.faction_index == 1U && entity.army_asset_id == 301U;
                }),
                "prebuilt HQ never produced/placed ARM301 factory");
        require(run.metrics.production_jobs_started != 0U && run.metrics.pending_requests_consumed != 0U,
                "ARM301 did not traverse producer -> pending placement -> committed runtime");
    }


    // Regression 3: exercise the user's reported Pioneer-only failure as one continuous runtime
    // chain. A mobile class-18 Pioneer must reach a resource island, place ARM330, grow ARM300,
    // autonomously buy/place ARM301, let that class-13 factory produce a scored combat unit, and
    // finally let the unit drive to and destroy a hostile runtime. This deliberately uses the same
    // planner/movement/combat entry points as ordinary Lab ticks instead of manually injecting the
    // intermediate primary/secondary requests.
    {
        auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
        paint_xenite(field, 30.0F, 20.0F);
        const auto& start_cell = field.at(25U, 20U);
        const auto& hostile_cell = field.at(50U, 20U);

        auto pioneer = seed(50U, 18U, 1U,
            {thandor::q12_to_float(start_cell.world_x_q12),
             thandor::q12_to_float(start_cell.world_y_q12), 0.0F}, false, 0x5000);
        pioneer.actor.body_yaw_turn16 = std::getenv("PIONEER_HEADING") ? static_cast<unsigned>(std::strtoul(std::getenv("PIONEER_HEADING"),nullptr,0)) : 0U;
        pioneer.has_traversal_profile = true;
        pioneer.traversal_low_band_index = 0U;
        pioneer.traversal_high_band_index = 4U;
        pioneer.motion.speed_q12_per_tick = 0x300;
        pioneer.motion.yaw_max_velocity_turn16 = 0x800;
        pioneer.motion.yaw_acceleration_turn16 = 0x100;

        auto hostile = seed(0x700U, 11U, 2U,
            {thandor::q12_to_float(hostile_cell.world_x_q12),
             thandor::q12_to_float(hostile_cell.world_y_q12), 1.0F}, true, 0x7000);
        hostile.shot_impact_class_index = 2U;
        hostile.root_model_maximum_integrity = 1200;

        auto products = startup_catalog(true);
        auto factory301 = std::find_if(products.begin(), products.end(), [](const auto& asset) {
            return asset.army_asset_id == 301U;
        });
        require(factory301 != products.end(), "ARM301 product fixture missing");
        factory301->entity_seed.production_compatibility_mask = 0x20U;

        thandor::SimulationLabProductionAsset combat_unit;
        combat_unit.army_asset_id = 0x120U;
        combat_unit.army_runtime_flags14_raw = 0x21U; // class-13 factory only; not class-11 HQ.
        combat_unit.xenite_cost_q4 = 1U;
        combat_unit.build_duration_ticks = 2U;
        combat_unit.ai_definition_class_value74 = 100U;
        combat_unit.ai_faction_score_seeds_prepared = true;
        combat_unit.ai_baseline_root_definition_value0c = 64;
        combat_unit.ai_baseline_root_definition_value18 = 1;
        combat_unit.ai_baseline_root_definition_value60 = 1000;
        combat_unit.ai_baseline_root_category_maximum_index5c = 2U;
        auto& score = combat_unit.ai_faction_score_seeds[1U];
        score.resolved = true;
        score.all_selected_model_technologies_unlocked = true;
        score.root_definition_value0c = 64;
        score.root_definition_value18 = 1;
        score.root_definition_value60 = 1000;
        score.root_category_maximum_index5c = 2U;
        combat_unit.entity_seed = seed(combat_unit.army_asset_id, 2U, 0U, {}, false);
        combat_unit.entity_seed.motion.speed_q12_per_tick = thandor::float_to_q12(1.0F);
        combat_unit.entity_seed.motion.yaw_max_velocity_turn16 = 0x7FFF;
        combat_unit.entity_seed.motion.yaw_acceleration_turn16 = 0x7FFF;
        combat_unit.entity_seed.root_model_maximum_integrity = 1000;

        thandor::SimulationWeaponProfile weapon;
        weapon.runtime_class_id = 2U;
        weapon.shot_definition_id = 0x574U;
        weapon.reload_ticks = 4U;
        weapon.maximum_range_q12 = thandor::float_to_q12(7.0F);
        weapon.yaw_max_velocity_turn16 = 0x7FFF;
        weapon.yaw_acceleration_turn16 = 0x7FFF;
        weapon.pitch_max_velocity_turn16 = 0x7FFF;
        weapon.pitch_acceleration_turn16 = 0x7FFF;
        weapon.pitch_min_turn16 = -0x4000;
        weapon.pitch_max_turn16 = 0x4000;
        weapon.launch_attachment_count = 1U;
        thandor::ShotDefinitionRecord shot;
        shot.definition_id = weapon.shot_definition_id;
        shot.trajectory_mode = static_cast<std::int32_t>(thandor::ShotTrajectoryMode::DirectLine);
        shot.launch_speed_q12 = thandor::float_to_q12(4.0F);
        shot.projectile_lifetime_ticks = 16U;
        shot.target_impact_damage_q12[2U] = 2000;
        combat_unit.entity_seed.combat_weapons.push_back({weapon, shot});
        combat_unit.entity_seed.actor.weapons.resize(1U);
        products.push_back(combat_unit);

        std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
        parameters[0x07U] = 200000U; // strategic ARM301 candidate.
        parameters[0x09U] = 200000U; // autonomous army candidate variant A.
        std::array<thandor::FactionTechnologyMask256, 8> technology_masks{};
        // ARM301 is selected while TEC1 is LOCKED. This synthetic catalog has no TEC
        // research record, so enable TEC1 only after ARM301 exists, to exercise the
        // subsequent autonomous defense-army purchase without reversing the factory gate.
        std::array<thandor::SimulationLabSeedEntity, 2> seeds{pioneer, hostile};

        thandor::SimulationLabExperiment experiment;
        experiment.reset(config(1400U), parameters, seeds, rich_economy(), products, {},
                         technology_masks, {});
        auto& run = experiment.runs().front();
        bool staged_tec1_unlock = false;
        for (unsigned batch = 0U; batch < 44U && !run.entities[1U].root_model_terminal; ++batch) {
            experiment.step(field, 32U);
            if (!staged_tec1_unlock && std::any_of(run.entities.begin(), run.entities.end(),
                    [](const auto& entity) {
                        return entity.faction_index == 1U && entity.army_asset_id == 301U &&
                               entity.construction_complete && !entity.root_model_terminal;
                    })) {
                thandor::set_faction_technology_unlocked(run.faction_technology_masks[1U], 1U, true);
                staged_tec1_unlock = true;
            }
        }

        const auto has_asset = [&](std::uint32_t id) {
            return std::any_of(run.entities.begin(), run.entities.end(), [&](const auto& entity) {
                return entity.faction_index == 1U && entity.army_asset_id == id;
            });
        };
        require(has_asset(330U), "Pioneer-only chain never placed ARM330 resource structure");
        require(has_asset(300U), "Pioneer-only chain never placed ARM300 headquarters");
        require(has_asset(301U), "Pioneer-only chain never placed ARM301 factory");
        require(run.metrics.class13_units_spawned != 0U,
                "ARM301 factory never produced an autonomous combat unit");
        require(run.metrics.weapon_shots_fired != 0U && run.metrics.projectile_impacts != 0U,
                "produced combat unit never drove into weapon range and attacked");
        require(run.entities[1U].root_model_terminal,
                "end-to-end Pioneer expansion force did not destroy the hostile target");
    }

    std::cout << "V574_AI_EXPANSION_NAVIGATION PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V574_AI_EXPANSION_NAVIGATION FAIL: " << error.what() << '\n';
    return 1;
}
