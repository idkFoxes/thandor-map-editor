#include "thandor/simulation_lab.hpp"
#include "thandor/pioneer_site_score.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

thandor::SimulationLabConfig config() {
    thandor::SimulationLabConfig value;
    value.run_count = 1U;
    value.duration_ticks = 128U;
    value.parameter_index = 100U;
    value.sweep_mode = thandor::SimulationLabSweepMode::Fixed;
    return value;
}

thandor::SimulationLabSeedEntity seed_at(const thandor::FieldCell& cell,
                                         std::uint32_t asset,
                                         std::uint32_t faction,
                                         std::uint32_t runtime_class,
                                         std::int32_t speed_q12) {
    thandor::SimulationLabSeedEntity seed;
    seed.source_placement_index = asset;
    seed.army_asset_id = asset;
    seed.faction_index = faction;
    seed.runtime_model_class_id = runtime_class;
    seed.building = false;
    seed.motion.speed_q12_per_tick = speed_q12;
    seed.actor.position = {thandor::q12_to_float(cell.world_x_q12),
                           thandor::q12_to_float(cell.world_y_q12),
                           thandor::q12_to_float(cell.terrain_height_q12)};
    return seed;
}

bool near(float lhs, float rhs) {
    return std::abs(lhs - rhs) < 0.001F;
}
} // namespace

int main() try {
    auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    const auto start_cell = field.at(5U, 5U);
    const auto enemy_cell = field.at(7U, 5U);
    auto& special_cell = field.at(20U, 20U);
    special_cell.flags_and_material |= 0x00000800U; // retail Xenite support bit, occupancy class 0

    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> parameters{};
    // The dedicated class-18 handler uses (bias - Manhattan distance) * KI[0x57]. Choose a
    // comfortably positive bias so the special-site branch has a non-zero retail score.
    parameters[83U] = 0x10000000U;
    parameters[87U] = 1U;

    // Retail first compares workspace00 and workspace04. A true Pioneer-only start has the same
    // primary null records in both, so equality takes the 0x2D05 straight-ahead roaming branch;
    // it does not immediately target the globally best resource cell.
    std::array<thandor::SimulationLabSeedEntity, 2> bootstrap_seeds{
        seed_at(start_cell, 50U, 1U, 18U, 0x1000),
        seed_at(enemy_cell, 51U, 2U, 1U, 0),
    };
    bootstrap_seeds[0].actor.body_yaw_turn16 = 0U;

    thandor::SimulationLabExperiment bootstrap;
    bootstrap.reset(config(), parameters, bootstrap_seeds);
    bootstrap.step(field, 9U);
    require(bootstrap.runs().front().entities.front().pioneer_ai_command_count == 0U,
            "Pioneer command ran before faction 1's scheduled tick 10");
    bootstrap.step(field, 1U);
    const auto& roaming = bootstrap.runs().front().entities.front();
    require(roaming.actor.move_target.has_value(),
            "runtime class 18 did not acquire its bootstrap straight-roam command");
    require(roaming.actor.move_target->x > thandor::q12_to_float(start_cell.world_x_q12) &&
                near(roaming.actor.move_target->y, thandor::q12_to_float(start_cell.world_y_q12)),
            "runtime class 18 bootstrap command was not 0x2D05 straight along heading");
    require(!near(roaming.actor.move_target->x, thandor::q12_to_float(special_cell.world_x_q12)) ||
                !near(roaming.actor.move_target->y, thandor::q12_to_float(special_cell.world_y_q12)),
            "Pioneer-only startup incorrectly skipped roaming and targeted workspace08 immediately");
    require(!roaming.target_entity_index.has_value(),
            "runtime class 18 incorrectly entered ordinary nearest-enemy target acquisition");
    require(!near(roaming.actor.position.x, thandor::q12_to_float(start_cell.world_x_q12)) ||
                !near(roaming.actor.position.y, thandor::q12_to_float(start_cell.world_y_q12)),
            "runtime class 18 received bootstrap movement but did not drive");

    // V590: distinguish a conversion-held Pioneer from one whose movement
    // callback is actively executing but cannot choose/finish a site.
    require(roaming.pioneer_runtime_step_count == 10U &&
                roaming.pioneer_runtime_last_step_tick == 10U &&
                roaming.pioneer_hold_skipped_ticks == 0U,
            "moving Pioneer did not record class-18 movement callbacks");
    auto held_seeds = bootstrap_seeds;
    held_seeds[0].runtime_flags = 0x08U;
    thandor::SimulationLabExperiment held;
    held.reset(config(), parameters, held_seeds);
    held.step(field, 10U);
    const auto& held_pioneer=held.runs().front().entities.front();
    require(held_pioneer.pioneer_runtime_step_count == 0U &&
                held_pioneer.pioneer_hold_skipped_ticks == 10U &&
                held_pioneer.pioneer_ai_command_count == 0U,
            "conversion hold must be visible as skipped movement, not route failure");
    auto immobile_seeds = bootstrap_seeds;
    immobile_seeds[0].motion.speed_q12_per_tick = 0;
    thandor::SimulationLabExperiment immobile;
    immobile.reset(config(), parameters, immobile_seeds);
    immobile.step(field, 10U);
    const auto& immobile_pioneer=immobile.runs().front().entities.front();
    require(immobile_pioneer.pioneer_runtime_step_count == 0U &&
                immobile_pioneer.pioneer_immobile_skipped_ticks == 10U &&
                immobile_pioneer.pioneer_hold_skipped_ticks == 0U,
            "immobile Pioneer skip was conflated with conversion hold");

    // Once workspace00 differs from workspace04 (here: one live same-faction ARM>=300 and no
    // primary queue), retail switches to the bounded workspace08 Xenite/Tritium scorer.
    auto assigned = seed_at(field.at(6U, 5U), 300U, 1U, 11U, 0);
    assigned.building = true;
    std::array<thandor::SimulationLabSeedEntity, 3> assigned_seeds{
        seed_at(start_cell, 50U, 1U, 18U, 0x1000), assigned,
        seed_at(enemy_cell, 51U, 2U, 1U, 0),
    };
    thandor::SimulationLabExperiment special;
    special.reset(config(), parameters, assigned_seeds);
    special.step(field, 10U);
    const auto& pioneer = special.runs().front().entities.front();
    require(pioneer.actor.move_target.has_value(),
            "runtime class 18 did not acquire a workspace08 special-site movement target");
    require(pioneer.pioneer_site_selector.calls == 1U &&
                pioneer.pioneer_site_selector.last_tick == 10U &&
                pioneer.pioneer_site_selector.last_candidates >= 1U &&
                pioneer.pioneer_site_selector.last_scores_evaluated >= 1U &&
                pioneer.pioneer_site_selector.last_positive_scores >= 1U &&
                pioneer.pioneer_site_selector.last_best_exists &&
                !pioneer.pioneer_site_selector.retail_admission_evaluated,
            "V593 selector-time score telemetry did not observe the actual runtime dispatch");
    require(near(pioneer.actor.move_target->x, thandor::q12_to_float(special_cell.world_x_q12)) &&
                near(pioneer.actor.move_target->y, thandor::q12_to_float(special_cell.world_y_q12)),
            "runtime class 18 targeted something other than the FLD resource-support special site");
    require(!pioneer.target_entity_index.has_value(),
            "runtime class 18 special-site branch entered ordinary nearest-enemy acquisition");

    // Hard-blocked resource support is not a workspace08 candidate. With workspace counts unequal,
    // this branch must stay idle rather than falling back to bootstrap roaming.
    auto blocked_field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    blocked_field.at(20U, 20U).flags_and_material |= 0x00000800U | 0x00002000U;
    thandor::SimulationLabExperiment blocked;
    blocked.reset(config(), parameters, assigned_seeds);
    blocked.step(blocked_field, 10U);
    require(!blocked.runs().front().entities.front().actor.move_target.has_value(),
            "hard-blocked resource support leaked into class-18 special-site targeting");
    require(blocked.runs().front().entities.front().pioneer_site_selector.calls == 1U &&
                !blocked.runs().front().entities.front().pioneer_site_selector.retail_admission_evaluated,
            "direct-API legacy field scan should still enter the selector, not claim retail admission");
    auto empty_field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    thandor::SimulationLabExperiment empty_selector;
    empty_selector.reset(config(), parameters, assigned_seeds);
    empty_selector.step(empty_field, 10U);
    const auto& empty_trace = empty_selector.runs().front().entities.front().pioneer_site_selector;
    require(empty_trace.calls == 1U && empty_trace.last_candidates == 0U &&
                empty_trace.empty_workspace_calls == 1U &&
                empty_trace.zero_positive_score_calls == 1U,
            "V593 must distinguish empty score selector from absent selector entry");

    // Recovered class-18 score: a zero-priority resource representative is
    // still scoreable from a live workspace01 Pioneer if its secondary distance
    // is shorter than KI[82]. Previously that entire term was absent.
    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> scoring{};
    scoring[82U] = 0x100000U;
    scoring[86U] = 1U;
    require(thandor::simulation_lab_pioneer_site_weighted_score(
                0, 330U, 0x10000, INT32_MAX, 0x1000000, 0U, scoring) ==
                2 * (0x100000 - 0x10000),
            "workspace01 secondary shortfall missing from Pioneer site score");
    require(thandor::simulation_lab_pioneer_site_weighted_score(
                0, 330U, INT32_MAX, INT32_MAX, 0x1000000, 0U, scoring) == 0,
            "missing workspace01 runtime manufactured a Pioneer score");
    scoring[82U] = 0U;
    scoring[86U] = 0U;
    scoring[84U] = 0x100000U;
    scoring[88U] = 2U;
    require(thandor::simulation_lab_pioneer_site_weighted_score(
                0, 332U, INT32_MAX, 0x4000U, 0x1000000, 0U, scoring) == 0x8000,
            "workspace02 visible-hostile Pioneer term missing");

    std::array<std::uint32_t, thandor::AiKnowledgeData::kParameterCount> shortfall_parameters{};
    shortfall_parameters[82U] = 0x100000U;
    shortfall_parameters[86U] = 1U;
    thandor::SimulationLabExperiment secondary_shortfall;
    secondary_shortfall.reset(config(), shortfall_parameters, assigned_seeds);
    secondary_shortfall.step(field, 10U);
    require(secondary_shortfall.runs().front().entities.front().pioneer_ai_command_count != 0U,
            "zero-priority workspace08 site did not earn a command from workspace01 shortfall");
    require(secondary_shortfall.runs().front().entities.front().actor.move_target.has_value(),
            "positive workspace01 shortfall never became a Pioneer route");
    require(near(secondary_shortfall.runs().front().entities.front().actor.move_target->x,
                 thandor::q12_to_float(special_cell.world_x_q12)) &&
                near(secondary_shortfall.runs().front().entities.front().actor.move_target->y,
                     thandor::q12_to_float(special_cell.world_y_q12)),
            "workspace01 scoring produced a route to a non-admitted resource cell");

    // A completely blocked path must never be counted as a successful Pioneer
    // move order to its current location. This is a Lab safety/integrity test;
    // it does not claim to reproduce retail's entire route fallback policy.
    auto isolated = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    for (std::uint32_t y=0; y<isolated.height(); ++y)
        for (std::uint32_t x=0; x<isolated.width(); ++x)
            isolated.at(x,y).flags_and_material |= 0x2000U;
    isolated.at(5U,5U).flags_and_material &= ~0x2000U;
    auto isolated_seeds=bootstrap_seeds;
    isolated_seeds[0].has_traversal_profile=true;
    isolated_seeds[0].traversal_low_band_index=0U;
    isolated_seeds[0].traversal_high_band_index=0U;
    thandor::SimulationLabExperiment rejected_route;
    rejected_route.reset(config(), parameters, isolated_seeds);
    rejected_route.step(isolated, 10U);
    const auto& isolated_pioneer = rejected_route.runs().front().entities.front();
    require(isolated_pioneer.pioneer_route_rejection_count == 1U,
            "blocked bootstrap did not surface a route rejection");
    require(isolated_pioneer.pioneer_ai_command_count == 0U,
            "Pioneer counted a blocked navigation result as a successful move order");
    require(!isolated_pioneer.actor.move_target.has_value(),
            "blocked route manufactured a movement target");

    std::cout << "V567_PIONEER_SPECIAL_SITE_MOVEMENT PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V567_PIONEER_SPECIAL_SITE_MOVEMENT FAIL: " << error.what() << '\n';
    return 1;
}
