#include "thandor/simulation_lab_shot_lead.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thandor;
static void require(bool success, const char* reason) {
    if (!success) throw std::runtime_error(reason);
}
static bool near(float a, float b) { return std::abs(a-b) < 0.002F; }
int main() try {
    SimulationLabEntity target;
    target.motion.ground_acceleration_q12 = 400;
    target.motion.speed_q12_per_tick = 2048; // 0.5 world unit per 20-Hz callback
    target.actor.position = {10, 0, 0};
    target.actor.move_target = Vec3f{60,0,0};
    target.last_translated = true;
    ShotDefinitionRecord shot;
    shot.trajectory_mode = 1;
    shot.launch_speed_q12 = 4096;
    const Vec3f source{0,0,0}, target_pose{10,0,2};
    auto aim = simulation_lab_moving_target_aim_point(target, source, target_pose, shot);
    // Q12 range truncation precedes integer lead: target advances half the
    // projectile's flight distance, along its selected movement direction.
    const auto expected = 10.F + float(std::int64_t(std::hypot(10.,2.)*4096.) * 2048 / 4096) / 4096.F;
    require(near(aim.x,expected)&&near(aim.y,0)&&near(aim.z,2),
            "ballistic lead did not retain 3D range and target aim height");
    target.motion.ground_speed_control=false;
    require(near(simulation_lab_moving_target_aim_point(target,source,target_pose,shot).x,expected),
            "source MDL acceleration eligibility must not depend on Lab ground speed controller");
    shot.trajectory_mode=3;
    shot.trajectory_ramp_duration_ticks=12;
    aim=simulation_lab_moving_target_aim_point(target,source,target_pose,shot);
    require(near(aim.x,expected+4.F),"mode-3 171/256 ramp-up lead missing");
    shot.mode3_lead_disabled=1;
    require(near(simulation_lab_moving_target_aim_point(target,source,target_pose,shot).x,10),
            "guided shot should not be led");
    shot.mode3_lead_disabled=0;
    shot.trajectory_mode=0;
    require(near(simulation_lab_moving_target_aim_point(target,source,target_pose,shot).x,10),
            "direct-line beam should not be led");
    shot.trajectory_mode=1;
    target.actor.move_waypoint = Vec3f{10,30,0};
    aim=simulation_lab_moving_target_aim_point(target,source,target_pose,shot);
    require(near(aim.x,10)&&aim.y>0,"active route segment must select turn direction");
    // All native ground wrappers, including class-3 walkers whose foot can
    // still be in motion after a route clears, aim along the retained segment.
    for(uint32_t cls:{1U,2U,3U,17U}){
        auto arriving=target;arriving.runtime_model_class_id=cls;
        arriving.combat_movement_initialized=true;arriving.combat_movement.stored={20*4096,0};
        arriving.actor.move_waypoint.reset();arriving.actor.move_target=Vec3f{10,30,0};
        const auto retained=simulation_lab_moving_target_aim_point(arriving,source,target_pose,shot,0.F);
        require(retained.x>10&&near(retained.y,0),"native ground target lead used a future order instead of the retained segment");
    }
    auto arriving_pioneer=target;
    arriving_pioneer.runtime_model_class_id=18U;
    arriving_pioneer.pioneer_movement_initialized=true;
    arriving_pioneer.pioneer_movement.stored={10*4096,0};
    arriving_pioneer.actor.move_waypoint.reset();
    arriving_pioneer.actor.move_target=Vec3f{10,30,0};
    const auto pioneer_aim=simulation_lab_moving_target_aim_point(arriving_pioneer,source,target_pose,shot,0.F);
    require(pioneer_aim.x>10&&near(pioneer_aim.y,0),
            "arriving Pioneer lead used its future order instead of the retained segment");
    auto ship = target;
    ship.runtime_model_class_id = 19U;
    ship.combat_movement_initialized = true;
    ship.combat_movement.stored = {10 * 4096, 0};
    ship.last_translated = false;
    ship.last_turned = true;
    const auto ship_aim = simulation_lab_moving_target_aim_point(ship, source, target_pose, shot, 0.F);
    require(ship_aim.x > 10 && near(ship_aim.y, 0),
            "turning ship lead ignored the route published before its visible waypoint updated");
    target.actor.move_waypoint.reset();
    target.last_translated=false;
    require(near(simulation_lab_moving_target_aim_point(target,source,target_pose,shot).x,10),
            "stationary actor was incorrectly led from stale orders");
    target.last_translated=true;
    target.actor.move_target.reset();
    require(near(simulation_lab_moving_target_aim_point(target,source,target_pose,shot).x,10),
            "movement with no path manufactured a direction");
    target.actor.move_target=Vec3f{60,0,0};
    target.motion.ground_acceleration_q12=0;
    require(near(simulation_lab_moving_target_aim_point(target,source,target_pose,shot).x,10),
            "native acceleration eligibility gate ignored");
    target.motion.ground_acceleration_q12=400;
    // Native tracked==path stop/end-range branch. With a known, distinct
    // tracked pair it must NOT snap, even if a path coordinate was supplied.
    target.actor.move_target=Vec3f{60,0,0};
    target.ai_path_coordinate_q12=MovementPoint{12*4096,0};
    target.ai_damage_tracked_coordinate_q12=MovementPoint{11*4096,0};
    aim=simulation_lab_moving_target_aim_point(target,source,target_pose,shot);
    require(near(aim.x,expected),"unmatched tracking coordinates incorrectly clamped lead");
    target.ai_damage_tracked_coordinate_q12=target.ai_path_coordinate_q12;
    aim=simulation_lab_moving_target_aim_point(target,source,target_pose,shot);
    require(near(aim.x,12),"native matched-tracking endpoint did not stop lead");
    target.ai_path_coordinate_q12=MovementPoint{40*4096,0};
    target.ai_damage_tracked_coordinate_q12=target.ai_path_coordinate_q12;
    aim=simulation_lab_moving_target_aim_point(target,source,target_pose,shot);
    require(near(aim.x,expected),"distant matched endpoint suppressed valid lead");
    const SimulationLabGroundAimSnapshot earlier{
        {10,0,0},Vec3f{12,0,0},false,MovementPoint{12*4096,0},MovementPoint{12*4096,0}};
    aim=simulation_lab_moving_target_aim_point(target,source,target_pose,shot,0.F,&earlier);
    require(near(aim.x,12)&&near(aim.z,0),
        "older target lead used anchors replaced by its later movement callback");

    target.ai_damage_tracked_coordinate_q12.reset();
    target.ai_path_coordinate_q12=MovementPoint{12*4096,0};
    aim=simulation_lab_moving_target_aim_point(target,source,target_pose,shot);
    require(near(aim.x,expected),"missing native tracking state was guessed");
    target.ai_path_coordinate_q12.reset();
    target.runtime_model_class_id=21;
    require(near(simulation_lab_moving_target_aim_point(target,source,target_pose,shot).x,10),
            "inactive class-21 flight was predicted using stale ground steering");
    target.aircraft.initialized=true;
    target.aircraft.runtime.phase=SimulationClass21LinkedChildPhase::MissionPass;
    target.aircraft_definition.speed_q12=2048;
    target.actor.body_yaw_turn16=0;
    aim=simulation_lab_moving_target_aim_point(target,source,target_pose,shot);
    require(std::hypot(aim.x-target_pose.x,aim.y-target_pose.y)>4.9F,
            "active class-21 shot did not use its mission flight heading/speed");
    // Aircraft are not exempt from the native tracked==path rule. When the
    // flying child sits above the army root, the snapped endpoint must keep
    // root MDL height, not the child's ordinary aim height.
    target.ai_path_coordinate_q12=MovementPoint{11*4096,0};
    target.ai_damage_tracked_coordinate_q12=target.ai_path_coordinate_q12;
    aim=simulation_lab_moving_target_aim_point(target,source,target_pose,shot,0.75F);
    require(near(aim.x,11)&&near(aim.y,0)&&near(aim.z,0.75F),
            "class-21 tracked endpoint must use root Z and not overshoot");
    // A class-21 flying child can be offset in XY from the owner root. The
    // source checks the selected targetNode against the tracked endpoint.
    target.ai_path_coordinate_q12=MovementPoint{20*4096,0};
    target.ai_damage_tracked_coordinate_q12=target.ai_path_coordinate_q12;
    aim=simulation_lab_moving_target_aim_point(target,source,Vec3f{15,0,2},shot,0.75F);
    require(near(aim.x,20)&&near(aim.z,0.75F),
            "aircraft tracked endpoint radius used the root instead of flying child");
    target.ai_path_coordinate_q12=MovementPoint{11*4096,0};
    target.ai_damage_tracked_coordinate_q12=MovementPoint{12*4096,0};
    aim=simulation_lab_moving_target_aim_point(target,source,target_pose,shot,0.75F);
    require(std::hypot(aim.x-target_pose.x,aim.y-target_pose.y)>4.9F && near(aim.z,2),
            "unmatched class-21 tracked pair must retain normal flying-child lead");
    target.ai_path_coordinate_q12.reset();
    target.ai_damage_tracked_coordinate_q12.reset();
    target.aircraft.offmap=true;
    require(near(simulation_lab_moving_target_aim_point(target,source,target_pose,shot).x,10) &&
            near(simulation_lab_moving_target_aim_point(target,source,target_pose,shot).y,0),
            "off-map aircraft was treated as a visible flying lead target");
    target.aircraft.offmap=false;
    target.aircraft.runtime.phase=SimulationClass21LinkedChildPhase::Attached;
    require(near(simulation_lab_moving_target_aim_point(target,source,target_pose,shot).x,10) &&
            near(simulation_lab_moving_target_aim_point(target,source,target_pose,shot).y,0),
            "attached class-21 aircraft was led before release");
    target.runtime_model_class_id=1;
    target.root_model_terminal=true;
    require(near(simulation_lab_moving_target_aim_point(target,source,target_pose,shot).x,10),
            "terminal target was led using stale movement state");
    for(const auto cls:{1U,2U,3U,17U,18U,19U}) {
        target.runtime_model_class_id=cls;
        target.root_model_terminal=true;target.last_translated=true;target.last_turned=false;
        target.combat_movement_initialized=target.pioneer_movement_initialized=true;
        target.combat_movement.stored=target.pioneer_movement.stored={30*4096,0};
        const auto before=simulation_lab_ground_aim_snapshot(target);
        const auto live=simulation_lab_moving_target_aim_point(target,source,target_pose,shot,0.75F);
        const auto retained=simulation_lab_moving_target_aim_point(target,source,target_pose,shot,0.75F,&*before);
        require(live.x>target_pose.x && near(live.x,retained.x) && near(live.y,retained.y),
            "linked terminal owner's live movement lost its native shot lead");
        target.last_translated=false;
        require(near(simulation_lab_moving_target_aim_point(target,source,target_pose,shot,0.75F).x,target_pose.x),
            "stationary terminal owner retained shot lead");
        target.runtime_removed=true;target.last_translated=true;
        require(near(simulation_lab_moving_target_aim_point(target,source,target_pose,shot,0.75F).x,target_pose.x),
            "unlinked owner retained shot lead");
        target.runtime_removed=false;
    }
    for(const auto cls:{18U,19U}) {
        target.runtime_model_class_id=cls;target.root_model_terminal=false;
        target.last_translated=true;target.last_turned=false;
        target.pioneer_movement_initialized=true;
        target.pioneer_movement.stored={30*4096,0};
        target.combat_movement.stored={-30*4096,0}; // Stale factory adapter.
        const auto before=simulation_lab_ground_aim_snapshot(target);
        require(before&&before->route_point&&near(before->route_point->x,30)&&!before->stationary,
            "Pioneer/water unit lost authoritative pre-callback route snapshot");
        target.root_model_terminal=true;target.last_translated=false;
        const auto retained=simulation_lab_moving_target_aim_point(target,source,target_pose,shot,0.75F,&*before);
        require(retained.x>target_pose.x,
            "lethal hit discarded Pioneer lead before its owner callback");
    }
    target.root_model_terminal=false;
    shot.launch_speed_q12=0;
    require(near(simulation_lab_moving_target_aim_point(target,source,target_pose,shot).x,10),
            "invalid shot speed was used in lead division");
    std::cout << "V717_MOVING_TARGET_LEAD PASS\n";
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
