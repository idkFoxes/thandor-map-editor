#include "thandor/ai_knowledge.hpp"
#include "thandor/field.hpp"
#include "thandor/effect.hpp"
#include "thandor/path_diagnostics.hpp"
#include "thandor/simulation.hpp"
#include "thandor/v528_pathing.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() try {
    auto field = thandor::FieldGrid::create(59U, 59U, 0, 0U);
    const auto source_cell = thandor::V519PathScratchCoordinate{24, 24};
    const auto target_cell = thandor::V519PathScratchCoordinate{52, 24};
    const auto source = thandor::v519_path_scratch_center_to_world_q12(source_cell);
    const auto target = thandor::v519_path_scratch_center_to_world_q12(target_cell);

    // V530 0x00524EB0 / 0x00513160: resource support is the actual FLD bit at the
    // sampled cell (0x0800 << occupancy class). A hard 0x88006000 cell still rejects it.
    {
        auto audit = field.audit_resource_support_cell(10U, 10U, 0U);
        require(audit.inside && audit.required_support_mask == 0x0800U && !audit.support_present,
                "resource-support audit did not expose the missing Xenite bit");
        field.at(10U, 10U).flags_and_material |= 0x0800U;
        audit = field.audit_resource_support_cell(10U, 10U, 0U);
        require(audit.valid(), "resource-support audit rejected the machine-required Xenite bit");
        field.at(10U, 10U).flags_and_material |= 0x2000U;
        audit = field.audit_resource_support_cell(10U, 10U, 0U);
        require(audit.hard_blocked && !audit.valid(),
                "resource-support audit ignored the retail hard-blocker mask");
        field.at(10U, 10U).flags_and_material = 0x1000U;
        require(field.audit_resource_support_cell(10U, 10U, 1U).valid(),
                "resource-support audit rejected the machine-required Tritium bit");
        require(!field.audit_resource_support_cell(999U, 999U, 0U).inside,
                "resource-support audit accepted an out-of-bounds cell");
    }

    constexpr uint32_t kClassMask = 1U;
    constexpr uint32_t kLowBand = 0U;
    constexpr uint32_t kHighBand = 1U;
    constexpr int32_t kMoverRadius = 0x0800;

    const auto direct = thandor::v528_resolve_route(
        field, source.x_q12, source.y_q12, target.x_q12, target.y_q12,
        kClassMask, kLowBand, kHighBand, {}, kMoverRadius);
    require(direct.usable(), "V528 route rejected an unobstructed flat-field path");
    require(direct.disposition == thandor::V528RouteDisposition::Direct,
            "V528 unobstructed path no longer resolves directly");
    require(!direct.primary_world_q12.has_value(),
            "V528 direct path unexpectedly emitted an intermediate primary segment");
    require(!direct.live_influence_bands_complete,
            "editor falsely claims the runtime-only GridScratch influence workspace is complete");

    const auto blocker_cell = thandor::V519PathScratchCoordinate{38, 24};
    const auto blocker_world = thandor::v519_path_scratch_center_to_world_q12(blocker_cell);
    const std::vector<thandor::V528PathRuntimeBlocker> blockers{{
        blocker_world.x_q12, blocker_world.y_q12, 0x0800}};
    const auto routed = thandor::v528_resolve_route(
        field, source.x_q12, source.y_q12, target.x_q12, target.y_q12,
        kClassMask, kLowBand, kHighBand, blockers, kMoverRadius);
    require(routed.usable(), "V528 route could not detour around an isolated summed-radius blocker");
    require(routed.disposition == thandor::V528RouteDisposition::Routed,
            "V530 isolated blocker did not enter the retail routed disposition");
    require(routed.primary_world_q12.has_value(),
            "V528 isolated blocker did not produce an active primary route segment");

    const auto primary_cell = thandor::v519_path_clip_to_inner_scratch(
        field, thandor::v519_path_world_to_scratch(
            routed.primary_world_q12->x_q12, routed.primary_world_q12->y_q12));
    const auto masks = thandor::v519_path_masks(0U, kLowBand, kHighBand);
    require(masks.traversal_bands_valid, "test traversal bands unexpectedly invalid");
    require(thandor::v528_path_segment_clear_static(
                field, source_cell, primary_cell, kClassMask,
                masks.high_cost_mask, masks.blocking_mask, blockers, kMoverRadius),
            "V528 selected primary segment is not clear under the same blocker model");


    // V530 proves nearest-unblocked recovery is source-side. A completely blocked seven-cell
    // source neighborhood must produce a transient source escape without rewriting the target.
    const std::vector<thandor::V528PathRuntimeBlocker> source_blockers{{
        source.x_q12, source.y_q12, 0x0800}};
    const auto escaped = thandor::v528_resolve_route(
        field, source.x_q12, source.y_q12, target.x_q12, target.y_q12,
        kClassMask, kLowBand, kHighBand, source_blockers, kMoverRadius);
    require(escaped.usable(), "V530 source-side blocked-cell recovery failed");
    require(escaped.disposition == thandor::V528RouteDisposition::SourceRelocated &&
                escaped.source_cell_relocated && escaped.primary_cell.has_value(),
            "V530 blocked source was not represented as a source relocation");
    require(escaped.requested_cell == target_cell && escaped.resolved_cell == target_cell &&
                !escaped.requested_cell_retargeted,
            "V530 source recovery incorrectly rewrote the requested destination");

    const auto direct_policy = thandor::shot_line_of_fire_policy(
        thandor::ShotTrajectoryMode::DirectLine);
    const auto ballistic_policy = thandor::shot_line_of_fire_policy(
        thandor::ShotTrajectoryMode::Ballistic);
    const auto fixed_policy = thandor::shot_line_of_fire_policy(
        thandor::ShotTrajectoryMode::FixedRange);
    const auto lead_policy = thandor::shot_line_of_fire_policy(
        thandor::ShotTrajectoryMode::LeadAdjusted);
    require(direct_policy.test_terrain && direct_policy.test_models &&
                !ballistic_policy.test_terrain && ballistic_policy.test_models &&
                !fixed_policy.test_terrain && !fixed_policy.test_models &&
                lead_policy.test_terrain && lead_policy.test_models,
            "V530 line-of-fire trajectory-mode surface policy regressed");

    thandor::ShotDefinitionRecord direct_range;
    direct_range.trajectory_mode = static_cast<int32_t>(thandor::ShotTrajectoryMode::DirectLine);
    direct_range.launch_speed_q12 = 0x1000;
    direct_range.projectile_lifetime_ticks = 10U;
    direct_range.trajectory_ramp_duration_ticks = 0U;
    require(thandor::shot_selection_range_q12(direct_range) == 0xA000,
            "selection-range fixture drifted");
    require(thandor::shot_line_of_fire_range_q12(direct_range).value_or(0) == 0x9000,
            "V530 direct line-of-fire range did not preserve the final -1 tick");
    direct_range.trajectory_mode = static_cast<int32_t>(thandor::ShotTrajectoryMode::Ballistic);
    require(!thandor::shot_line_of_fire_range_q12(direct_range).has_value(),
            "ballistic mode acquired a fabricated direct/lead line-of-fire range");

    // V530 ArmyRuntime_UpdateMovementAndWaypoints uses an inclusive component-wise +/-0x1000 Q12
    // final-arrival window and does not snap the model to the command point. A transient primary
    // route waypoint is intentionally not consumed by this wider final-target rule.
    thandor::SimulationMotionProfile arrival_motion;
    arrival_motion.speed_q12_per_tick = 0x100;
    arrival_motion.yaw_max_velocity_turn16 = 0x1000;
    arrival_motion.yaw_acceleration_turn16 = 0x1000;
    thandor::SimulationActorState final_arrival;
    final_arrival.position = {10.0F, 20.0F, 0.0F};
    final_arrival.move_target = thandor::Vec3f{10.75F, 19.0F, 0.0F};
    require(thandor::simulation_tick_actor_motion(final_arrival, arrival_motion),
            "V530 final-arrival window did not complete the move");
    require(!final_arrival.move_target.has_value(),
            "V530 final-arrival window did not clear the final command");
    require(final_arrival.position.x == 10.0F && final_arrival.position.y == 20.0F,
            "V530 final-arrival window incorrectly snapped the model to the command point");

    thandor::SimulationActorState waypoint_arrival;
    waypoint_arrival.position = {0.0F, 0.0F, 0.0F};
    waypoint_arrival.move_target = thandor::Vec3f{8.0F, 0.0F, 0.0F};
    waypoint_arrival.move_waypoint = thandor::Vec3f{0.75F, 0.0F, 0.0F};
    require(thandor::simulation_tick_actor_motion(waypoint_arrival, arrival_motion),
            "transient waypoint did not advance");
    require(waypoint_arrival.move_waypoint.has_value(),
            "final-arrival tolerance was incorrectly applied to a transient route waypoint");

    thandor::SimulationActorState pitch_actor;
    pitch_actor.position = {0.0F, 0.0F, 0.0F};
    pitch_actor.body_yaw_turn16 = 0U;
    pitch_actor.weapons.resize(1U);
    thandor::SimulationWeaponProfile pitch_weapon;
    pitch_weapon.shot_definition_id = 1U;
    pitch_weapon.reload_ticks = 1U;
    pitch_weapon.launch_attachment_count = 1U;
    pitch_weapon.maximum_range_q12 = 0x100000;
    pitch_weapon.yaw_max_velocity_turn16 = 0x4000;
    pitch_weapon.yaw_acceleration_turn16 = 0x4000;
    pitch_weapon.pitch_max_velocity_turn16 = 0x4000;
    pitch_weapon.pitch_acceleration_turn16 = 0x4000;
    pitch_weapon.pitch_min_turn16 = -0x1000;
    pitch_weapon.pitch_max_turn16 = 0x1000;
    thandor::ShotDefinitionRecord pitch_shot;
    pitch_shot.trajectory_mode = static_cast<int32_t>(thandor::ShotTrajectoryMode::DirectLine);
    pitch_shot.launch_speed_q12 = 0x1000;
    pitch_shot.projectile_lifetime_ticks = 100U;
    const thandor::Vec3f steep_target{1.0F, 0.0F, 8.0F};
    const auto pitch_result = thandor::simulation_tick_weapon(
        pitch_actor, pitch_actor.weapons[0], pitch_weapon, pitch_shot,
        pitch_actor.position, &steep_target, true);
    require(!pitch_result.aligned && !pitch_result.fired,
            "V530 elevation-window rejection was replaced by clamp-and-fire behavior");

    // 0x0052B8C0 projectile creation clamps a negative ballistic discriminant to zero, while
    // 0x0052B9D0 LOS rejects the same impossible arc. Near equal height, creation selects the
    // low root by negating the sqrt term.
    thandor::ShotDefinitionRecord ballistic;
    ballistic.trajectory_mode = static_cast<int32_t>(thandor::ShotTrajectoryMode::Ballistic);
    ballistic.launch_speed_q12 = thandor::float_to_q12(1.0F);
    ballistic.ballistic_divisor_q12 = thandor::float_to_q12(1.0F);
    const thandor::Vec3f impossible_target{8.0F, 0.0F, 8.0F};
    require(thandor::simulation_shot_launch_angles({}, impossible_target, ballistic).valid,
            "retail projectile creation no longer clamps a negative ballistic discriminant");
    require(!thandor::simulation_shot_line_of_fire_angles({}, impossible_target, ballistic).valid,
            "retail ballistic LOS no longer rejects a negative discriminant");
    ballistic.launch_speed_q12 = thandor::float_to_q12(8.0F);
    ballistic.ballistic_divisor_q12 = thandor::float_to_q12(1.0F);
    const auto low_arc = thandor::simulation_shot_launch_angles({}, {8.0F,0.0F,0.0F}, ballistic);
    require(low_arc.valid && static_cast<int16_t>(low_arc.pitch_turn16) < 0x2000,
            "near-level ballistic launch no longer selects the recovered low root");

    // ModelNodeRuntime_ComputeRelativeDirectionAngle works in the live 3D model basis. A flat
    // basis matches world yaw; a banked/slope basis changes the requested local yaw.
    const std::array<float,9> flat_basis{1,0,0, 0,1,0, 0,0,1};
    const auto flat_relative = thandor::simulation_relative_yaw_from_world_basis(
        flat_basis,0U,{0,0,0},{1,1,0});
    require(std::abs(thandor::shortest_turn16_delta(flat_relative,0x2000U)) <= 1,
            "flat root-basis relative yaw drifted from 45 degrees");
    const std::array<float,9> tilted_basis{
        0.70710678F,0.0F,0.70710678F,
        0.0F,1.0F,0.0F,
        -0.70710678F,0.0F,0.70710678F};
    const auto tilted_relative = thandor::simulation_relative_yaw_from_world_basis(
        tilted_basis,0U,{0,0,0},{1,1,0});
    require(tilted_relative != flat_relative,
            "tilted live root basis was ignored by relative target orientation");

    const auto semantics = thandor::known_ai_knowledge_semantics();
    require(semantics.size() == 62U, "expanded KI dictionary is not exactly 62 entries");
    const auto* capacity_a = thandor::find_ai_knowledge_semantic(0x10U);
    const auto* capacity_b = thandor::find_ai_knowledge_semantic(0x11U);
    require(capacity_a != nullptr &&
                capacity_a->identifier ==
                    "AI_KNOWLEDGE_STRATEGIC_12D_AND_141_TO_143_ADDITIONAL_PLANNING_CAPACITY",
            "V528 KI[0x10] additional-planning-capacity semantic missing");
    require(capacity_b != nullptr &&
                capacity_b->identifier ==
                    "AI_KNOWLEDGE_STRATEGIC_12F_TO_132_ADDITIONAL_PLANNING_CAPACITY",
            "V528 KI[0x11] additional-planning-capacity semantic missing");
    require(thandor::find_ai_knowledge_semantic(0x0EU) == nullptr &&
                thandor::find_ai_knowledge_semantic(0x0FU) == nullptr &&
                thandor::find_ai_knowledge_semantic(0x12U) == nullptr,
            "V528 assigned a semantic to still-unresolved KI[0x0E/0x0F/0x12]");

    std::cout << "V530_PATH_COLLISION_LOS_AI PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V530_PATH_COLLISION_LOS_AI FAIL: " << error.what() << '\n';
    return 1;
}
