#pragma once

#include "thandor/effect.hpp"
#include "thandor/field.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <limits>
#include <span>
#include <vector>

namespace thandor {

inline constexpr uint32_t kSimulationTicksPerSecond = 20U;
inline constexpr uint32_t kSimulationAimToleranceTurn16 = 0x400U;

enum class SimulationLaunchPolicy : uint8_t {
    AllKind2Attachments,
    AlternateLaunchNodes,
    SequentialKind2Attachments
};

// Weapon callback ownership belongs to the linked MDL runtime class, never the ARM root class.
// Classes 9 and 21 advance one kind-2 attachment selector at a time; other recovered classes
// launch all eligible kind-2 points unless a multi-node assembly later upgrades to Alternate.
SimulationLaunchPolicy simulation_launch_policy_for_runtime_class(uint32_t runtime_class_id) noexcept;

struct SimulationWeaponProfile {
    // The executable treats a weapon assembly as a hierarchy: the highest same-definition node
    // owns yaw, its child owns pitch, and one or more leaf nodes own kind-2 launch attachments and
    // recoil. Keep those roles separate so towers rotate their casing instead of only twisting the
    // muzzle leaf. Multiple launch leaves are one alternating weapon channel, not independent guns.
    uint32_t model_node_index{};       // compatibility alias for launch_model_node_index
    uint32_t yaw_model_node_index{};
    uint32_t pitch_model_node_index{};
    // Class 9 computes the target angles from root->child[0], but launches from the selected
    // kind-2 point after rebuilding the complete live hierarchy. Keep the aim pivot separate from
    // the muzzle node so the turret does not solve its own pose from a moving rocket attachment.
    uint32_t aim_model_node_index{std::numeric_limits<uint32_t>::max()};
    uint32_t launch_model_node_index{}; // compatibility alias for launch_model_node_indices.front()
    // The eight class-9 mesh selector bits live on root->child[0]->child[0]. That rack node is
    // independent from the node whose SPR owns the kind-2 launch points.
    uint32_t rack_mesh_model_node_index{std::numeric_limits<uint32_t>::max()};
    std::vector<uint32_t> launch_model_node_indices;
    SimulationLaunchPolicy launch_policy{SimulationLaunchPolicy::AllKind2Attachments};
    uint32_t model_definition_id{};
    uint32_t runtime_class_id{};
    uint32_t shot_definition_id{};
    uint32_t reload_ticks{};
    // Runtime class 9 uses MDL +0x38 as a shared interval between separate attachment
    // launches while MDL +0x30 remains an independent reload counter for each selector.
    uint32_t inter_shot_ticks{};
    int32_t maximum_range_q12{};
    int32_t yaw_max_velocity_turn16{};
    int32_t yaw_acceleration_turn16{};
    int32_t pitch_max_velocity_turn16{};
    int32_t pitch_acceleration_turn16{};
    int32_t pitch_min_turn16{};
    int32_t pitch_max_turn16{};
    int32_t recoil_rotation_step_turn16{};
    int32_t recoil_duration_ticks{};
    int32_t recoil_translation_q12{};
    uint32_t launch_attachment_count{};
    uint32_t launch_attachment_selector_mask{};

    // Catalog resolution distinguishes valid SHT 0 from an unresolved/default profile.
    bool shot_definition_resolved{};
    bool valid() const noexcept {
        return (shot_definition_id != 0U || shot_definition_resolved) &&
            reload_ticks != 0U && launch_attachment_count != 0U;
    }
};

bool simulation_model_node_is_weapon_aim_axis(
    std::span<const SimulationWeaponProfile> profiles,uint32_t model_node_index) noexcept;

struct SimulationMotionProfile {
    // Ground-movement interpretation of MDL +0x0C. Other runtime classes reuse the same
    // serialized field for class-specific work, including vertical effect/model teardown.
    int32_t speed_q12_per_tick{};
    // Placement-contact callback index from MDL +0x278. Kind 1 also dispatches ground-movement
    // variant B; every other value dispatches variant A. The editor records the exact value even
    // though its direct-drive preview still uses one shared kinematic approximation.
    int32_t placement_contact_kind{};
    int32_t placement_height_offset_q12{};
    int32_t yaw_max_velocity_turn16{};
    int32_t yaw_acceleration_turn16{};
    // V537 ground movement does not use a fixed editor-only yaw tolerance. It interpolates
    // between near/far heading-error limits by remaining route distance. Zero interpolation
    // distance preserves the older fallback behavior for legacy/synthetic profiles.
    int32_t heading_interpolation_distance_q12{};
    uint32_t far_heading_error_limit_turn16{};
    uint32_t near_heading_error_limit_turn16{};
    // Exact retail Army ground-collision radius from MDL +0xDC. A zero radius disables the
    // summed-radius collision helper in the executable and is preserved as zero here.
    int32_t collision_radius_q12{};
    // Retail depth-bin masks clamp MDL +0xDC by the recursively recomputed model-hierarchy
    // subtree radius. Exact contact/impact tests still use collision_radius_q12 above.
    int32_t depth_bin_broad_phase_radius_q12{};

    // Ground callbacks (classes 1, 2 and 18) accelerate once per callback, then
    // multiply the resulting advance by the simulation batch. Synthetic profiles
    // retain constant speed unless this native definition contract is present.
    bool ground_speed_control{};
    uint32_t ground_acceleration_q12{};
    int32_t ground_slope_threshold_turn16{};
    int32_t ground_secondary_slope_threshold_turn16{};

    bool movable() const noexcept { return speed_q12_per_tick != 0; }
};

struct SimulationAxisState {
    uint32_t angle_turn16{};
    int32_t velocity_turn16{};
};

struct SimulationAimAngles {
    uint32_t yaw_turn16{};
    uint32_t pitch_turn16{};
    bool valid{true};
};

struct SimulationWeaponState {
    SimulationAxisState yaw{};
    SimulationAxisState pitch{0x4000U, 0};
    uint32_t cooldown_ticks{};
    uint32_t inter_shot_ticks{};
    std::array<int32_t, 8> attachment_reload_ticks{};
    // Enabled SPR mesh groups for the fixed class-9 rack node. The executable subtracts the
    // simulation step even from zero and restores a bit only after the signed timer becomes
    // negative, so a timer that has just reached zero stays hidden until the following tick.
    uint32_t launch_mesh_group_mask{0xFFFFFFFFU};
    uint32_t last_fired_attachment_selector{0xFFFFFFFFU};
    uint32_t recoil_rotation_ticks{};
    uint32_t recoil_translation_ticks{};
    uint32_t next_launch_attachment{};
    uint32_t recoil_launch_selector{};
    int32_t recoil_angle_offset_turn16{};
    int32_t recoil_translation_offset_q12{};
    std::optional<std::array<int32_t,3>> barrel_translation_q12;
    std::array<std::optional<std::array<int32_t,3>>,2> alternate_barrel_translations_q12;
    std::array<uint32_t,2> alternate_barrel_recoil_ticks{};
    bool target_in_range{};
    bool target_aligned{};
    bool line_of_fire_clear{true};
};

struct SimulationNativeWalkerState {
    bool initialized{};
    uint32_t flags{},start_heading{},end_heading{};
    int32_t speed{},restart_speed{},step_rate{};
    std::array<uint32_t,2> progress{};
    std::array<uint32_t,2> ground_normals{0x40000000U,0x40000000U};
    std::array<uint32_t,2> target_ground_normals{0x40000000U,0x40000000U};
};

struct SimulationActorState {
    SimulationNativeWalkerState articulated_native{};
    Vec3f position{};
    uint32_t body_yaw_turn16{};
    int32_t body_turn_velocity_turn16{};
    std::vector<SimulationWeaponState> weapons;
    std::optional<Vec3f> move_target;
    // Transient active primary route segment reconstructed from V530 GridPath behavior.
    // This is NOT ArmyMovementRuntime::queuedWaypoints[8]: that bounded array is the separately
    // recovered command queue. The active solver segment remains runtime-only and is never serialized.
    std::optional<Vec3f> move_waypoint;
    bool target_in_range{};
    bool target_aligned{};
    uint64_t last_updated_tick{};
    // Runtime class 17 can start with a 24-tick three-child deployment state, but the retail
    // initializer arms it only when rootModelNode->childCount > 2. Banking remains class-wide.
    // When armed, root children 0..2 each advance localRotationAngle1 by 0x2AA per tick.
    uint32_t class17_rotor_deploy_ticks_remaining{};
    uint32_t class17_rotor_tilt_turn16{};
    uint32_t class17_bank_angle_turn16{0x4000U};
    uint32_t class17_bank_heading_turn16{};
    // Runtime class 2 root-node texture offsets use the signed +/-0x100000 period.
    int32_t primary_track_texture_offset_u{};
    int32_t secondary_track_texture_offset_u{};
    // ArmyRuntime_UpdateTimedShotAndEffectEmitters keeps an independent signed countdown for the
    // MDL +0x174/+0x178 movement-ground EFF channel. Runtime classes 2 and 3 only service that
    // countdown from their movement callback, so stationary units pause it instead of advancing a
    // presentation-time loop. Zero is the retail initial state and emits on the first moved tick.
    int32_t movement_ground_effect_countdown_ticks{};
    // Runtime class 3 alternates two terrain contacts. One foot remains anchored while the
    // opposite contact interpolates toward its planned terrain-contact position. Heading changes
    // also advance this state, allowing an in-place 180-degree turn instead of relying on the
    // class-3 root MDL's intentionally zero generic yaw fields.
    // Raw terrain-contact coordinates. Z excludes the terminal SPR placement-height offset;
    // ArmyArticulatedRuntime_UpdateSuspensionHierarchy adds the right terminal-foot resource
    // placement offset to both contacts only after contact interpolation.
    std::array<Vec3f, 2> articulated_foot_anchor_world{};
    // Each active class-3 terrain contact owns a frozen endpoint. Retail does not chase a moving
    // root with the swing foot while progress is active; it computes the endpoint first and then
    // advances the contact interpolation state toward that fixed coordinate.
    std::array<Vec3f, 2> articulated_foot_target_world{};
    std::array<uint32_t, 2> articulated_contact_heading_turn16{};
    std::array<uint32_t, 2> articulated_contact_target_heading_turn16{};
    uint32_t articulated_contact_progress_q12{};
    // Retail initializes the derived contact step to 0x2000 and normally replaces it with
    // (MDL+0xC0 << 13) / (contactDistanceQ12 + 2*(MDL+0xC4)).  The pose pass refreshes this
    // whenever the next planted/swing contact geometry is known.
    int32_t articulated_contact_step_q12{0x2000};
    // Retail processes an already-active class-3 contact transition before it decides that the
    // waypoint has been reached and clears the movement/contact flags. Preserve the most recent
    // contact-progress advance so a partial swing can finish after planar motion has stopped
    // instead of freezing one foot in mid-step.
    uint32_t articulated_last_contact_advance_q12{1U};
    uint64_t articulated_contact_event_serial{};
    uint32_t articulated_last_completed_side{};
    // G391 compatibility view: the low 0x1000 contact domain expanded to turn16.
    uint32_t articulated_gait_phase_turn16{};
    uint32_t articulated_swing_side{};
    uint32_t articulated_pose_swing_side{};
    bool articulated_contacts_initialized{};
    // Pose-state latch used only to freeze a new contact endpoint once. Rendering may read these
    // values but must not advance or reseed them outside the 20 Hz simulation update.
    bool articulated_pose_transition_active{};
    bool articulated_contact_transition_active{};
    // True only while the root has stopped but the last retail-style contact transition is being
    // allowed to reach its planted endpoint. This is distinct from articulated_was_moving so
    // callers can tell planar motion from the short stop-settle animation.
    bool articulated_contact_settling{};
    bool articulated_was_moving{};
    bool articulated_turning{};
    uint32_t ground_advance_q12{};
    int32_t ground_recoil_ticks{};
    int32_t ground_recoil_tilt_per_tick{};
    uint32_t ground_recoil_heading{};
    std::optional<std::array<uint32_t,3>> ground_recoil_angles;
    uint32_t ground_normal_azimuth_turn16{};
    int32_t ground_normal_elevation_turn16{0x4000};
    std::array<int32_t,2> class17_previous_route_point_q12{};
};


enum class SimulationClass13ProductionPhase : uint8_t {
    Idle = 0,
    Building = 1,
    Opening = 2,
    WaitingForProducedUnitClear = 3,
    Closing = 4,
};

struct SimulationClass13ProductionState {
    SimulationClass13ProductionPhase phase{SimulationClass13ProductionPhase::Idle};
    uint32_t produced_army_asset_id{};
    uint32_t build_elapsed_ticks{};
    uint32_t build_duration_ticks{};
    // ArmyRuntime_UpdateAnimatedModelSubnodes is serviced while runtime class 13 is in its
    // production/build state.  Keep a cumulative presentation clock separate from the build
    // countdown so leaving state 1 freezes the authored child pose instead of snapping it back.
    uint32_t animated_subnode_ticks{};
    int32_t active_energy_demand_q4{};
    int32_t primary_texture_offset_v{};
    uint64_t produced_entity_serial{};
};

// Runtime class 13 scans the faction secondary queue using the candidate ARM +0x14 dword against
// the producer root MDL +0xC4 dword. Zero overlap is not compatible.
bool simulation_class13_candidate_compatible(uint32_t producer_mask, uint32_t candidate_flags) noexcept;

void simulation_class13_begin_production(SimulationClass13ProductionState& state,
    uint32_t produced_army_asset_id,uint32_t build_duration_ticks,int32_t energy_demand_q4) noexcept;

// State 1 increments ownerValue64 by the retail simulation step. Completion does not advance the
// state until ArmyRuntime_CreateInstanceFromAssetCf succeeds, so callers may retry a failed spawn.
bool simulation_class13_tick_build(SimulationClass13ProductionState& state,
    uint32_t simulation_step_ticks=1U) noexcept;

void simulation_class13_mark_spawned(SimulationClass13ProductionState& state,
    uint64_t produced_entity_serial) noexcept;

// State 2 advances root primaryTextureOffsetV by MDL +0x0C until the exact 0x80000 clamp, then
// enters state 3. A true result means the produced unit should receive its exit/rally move command.
bool simulation_class13_tick_open(SimulationClass13ProductionState& state,
    int32_t class_step_q12,uint32_t simulation_step_ticks=1U) noexcept;

// State 3 switches to closing only after the child no longer links back to the producer. The true
// edge corresponds to the second positioned completion sound in the executable.
bool simulation_class13_observe_produced_unit_link(SimulationClass13ProductionState& state,
    bool still_linked_to_producer) noexcept;

// State 4 reverses the same texture-V channel. True means the exact zero clamp returned to idle.
bool simulation_class13_tick_close(SimulationClass13ProductionState& state,
    int32_t class_step_q12,uint32_t simulation_step_ticks=1U) noexcept;

// Runtime class 22 (Airport) is a reusable linked-child store/launcher, not a class-13 factory.
// The executable has exactly thirteen persistent asset-id slots and three command lanes whose stock
// ARM ids are fixed globals. Stored ids survive a sortie; the transient class-21 child instance is
// what occupies/releases the per-slot runtime bit.
inline constexpr std::size_t kSimulationClass22StoredSlotCount = 13U;
inline constexpr std::array<uint32_t, 3> kSimulationClass22LinkedArmyAssetIds{240U, 242U, 244U};

enum class SimulationClass22BuildPhase : uint8_t {
    Idle = 0,
    Building = 1,
};

enum class SimulationClass22DispatchPhase : uint8_t {
    Idle = 0,
    Opening = 1,
    RaisingPlatform = 2,
    WaitingForChildRelease = 3,
    LoweringPlatform = 4,
    Closing = 5,
    Reset = 6,
};

struct SimulationClass22LaunchCommand {
    // SelectionPointerArray_ApplyType16MarkerCoordinates stores the command triple as A/B/C.
    // ArmyRuntimeSpawner_CreateLinkedChildInstanceCf then copies A -> child +0x78
    // (movementTarget0 / mission heading), B -> +0x74 (mission X), C -> +0x70 (mission Y).
    int32_t value_a{};
    int32_t value_b{};
    int32_t value_c{};
};
SimulationClass22LaunchCommand simulation_class22_world_launch_command(
    uint32_t heading_turn16,int32_t mission_world_x_q12,int32_t mission_world_y_q12) noexcept;

struct SimulationClass22LaunchRequest {
    uint32_t lane{};
    uint32_t army_asset_id{};
    SimulationClass22LaunchCommand command{};
};

struct SimulationClass22AirportState {
    SimulationClass22BuildPhase build_phase{SimulationClass22BuildPhase::Idle};
    uint32_t building_army_asset_id{};
    uint32_t build_elapsed_ticks{};
    uint32_t build_duration_ticks{};
    uint32_t animated_subnode_ticks{};
    int32_t active_energy_demand_q4{};
    uint32_t storage_capacity{};
    std::array<uint32_t, kSimulationClass22StoredSlotCount> stored_army_asset_ids{};
    uint32_t stored_slot_count{};
    uint32_t available_stored_count{};
    uint16_t occupied_slot_mask{};

    SimulationClass22DispatchPhase dispatch_phase{SimulationClass22DispatchPhase::Idle};
    int32_t primary_texture_offset_v{};
    int32_t platform_local_z_q12{};
    std::array<uint8_t, 3> pending_launch_counts{};
    std::array<SimulationClass22LaunchCommand, 3> launch_commands{};
};

bool simulation_class22_candidate_compatible(uint32_t candidate_flags) noexcept;
void simulation_class22_configure_storage(SimulationClass22AirportState& state,
    uint32_t storage_capacity,int32_t platform_baseline_q12=0) noexcept;
bool simulation_class22_begin_storage_build(SimulationClass22AirportState& state,
    uint32_t army_asset_id,uint32_t build_duration_ticks,int32_t energy_demand_q4) noexcept;
bool simulation_class22_tick_storage_build(SimulationClass22AirportState& state,
    uint32_t simulation_step_ticks=1U) noexcept;
// Completion always leaves build mode and clears active Energy, as the binary does. When
// energy_satisfied is false the completed id is deliberately not inserted into a storage slot.
bool simulation_class22_complete_storage_build(SimulationClass22AirportState& state,
    bool energy_satisfied=true) noexcept;

// SelectionPointerArray_ApplyType16MarkerCoordinates counts all thirteen stored ids, then copies
// those byte counts only into lanes selected by mask bits 1/2/4. It does not filter occupied bits.
void simulation_class22_queue_launch_mask(SimulationClass22AirportState& state,uint32_t lane_mask,
    int32_t value_a,int32_t value_b,int32_t value_c) noexcept;
std::optional<SimulationClass22LaunchRequest>
simulation_class22_take_next_pending_launch(SimulationClass22AirportState& state) noexcept;
std::optional<std::size_t> simulation_class22_find_free_stored_slot(
    const SimulationClass22AirportState& state,uint32_t army_asset_id) noexcept;
bool simulation_class22_mark_linked_child_spawned(SimulationClass22AirportState& state,
    std::size_t storage_slot) noexcept;
void simulation_class22_release_linked_child_slot(SimulationClass22AirportState& state,
    std::size_t storage_slot) noexcept;

bool simulation_class22_tick_open(SimulationClass22AirportState& state,int32_t door_step_q12,
    uint32_t simulation_step_ticks=1U) noexcept;
bool simulation_class22_tick_raise_platform(SimulationClass22AirportState& state,
    int32_t platform_step_q12,int32_t platform_ceiling_q12,
    uint32_t simulation_step_ticks=1U) noexcept;
// The parent has no state-3 timer. Only the class-21 child writes dispatch state 4.
bool simulation_class22_signal_child_release(SimulationClass22AirportState& state) noexcept;
bool simulation_class22_tick_lower_platform(SimulationClass22AirportState& state,
    int32_t platform_step_q12,int32_t platform_baseline_q12,
    uint32_t simulation_step_ticks=1U) noexcept;
bool simulation_class22_tick_close(SimulationClass22AirportState& state,int32_t door_step_q12,
    uint32_t simulation_step_ticks=1U) noexcept;
bool simulation_class22_reset_dispatch_cycle(SimulationClass22AirportState& state) noexcept;

// Minimal linked class-21 handshake used by normal-Simulation parity tests and by any later editor
// runtime integration. Mission travel remains separate from the parent door/platform dispatcher.
enum class SimulationClass21LinkedChildPhase : uint8_t {
    Attached = 1,
    ReturningToPlatform = 2,
    OutboundRelease = 3,
    OffmapOutbound = 4,
    MissionPass = 5,
    OffmapReturn = 6,
};
struct SimulationClass21LinkedChildState {
    SimulationClass21LinkedChildPhase phase{SimulationClass21LinkedChildPhase::Attached};
    int32_t release_countdown{};
    int32_t outbound_countdown{};
    int32_t mission_countdown{};
    // Runtime class 21 overloads ArmyRuntime +0x6C as a signed state-5 emission counter.
    // State 4 seeds it from MDL +0xC8 while ownerValue68 receives twice that value.
    int32_t mission_emission_counter{};
    int32_t return_countdown{};
};

struct SimulationAircraftDefinition {
    int32_t speed_q12{},takeoff_arc_q12{},release_ticks{},pad_ticks{},travel_ticks{},mission_arc_q12{},model_point_step{};
};
struct SimulationAircraftFlight {
    SimulationClass21LinkedChildState runtime;
    bool initialized{},landed{},offmap{};
    uint32_t slot{},mission_heading{};
    int32_t mission_x{},mission_y{},arc_position{},platform_height{},reference_height{},body_height{};
    uint32_t body_pitch{0x4000};
};

struct SimulationClass21MissionTickResult {
    bool completed{};
    uint32_t mapped_terrain_sound_attempts{};
    std::array<uint8_t,7> model_point_ordinals{};
    uint32_t emission_count{};
};
bool simulation_class21_begin_outbound_when_platform_raised(
    SimulationClass21LinkedChildState& child,SimulationClass22DispatchPhase parent_phase,
    int32_t release_countdown,int32_t outbound_countdown) noexcept;
// True exactly on the child edge that writes parent dispatch state 4 and plays the producer sound.
bool simulation_class21_tick_outbound_release(SimulationClass21LinkedChildState& child,
    uint32_t simulation_step_ticks=1U) noexcept;
bool simulation_class21_begin_mission_pass(SimulationClass21LinkedChildState& child,
    int32_t mission_countdown) noexcept;
// True on the exact state-5 edge where ownerValue68 becomes negative and the child goes off-map.
bool simulation_class21_tick_mission_pass(SimulationClass21LinkedChildState& child,
    uint32_t simulation_step_ticks=1U) noexcept;
// Exact state-5 substep owner. The post-decrement +0x6C counter emits ordinals 7..1 at
// {-3,-2,-1,0,+1,+2,+3} * MDL +0x30. The mapped-terrain helper is attempted once per
// state-5 substep, including the substep that transitions ownerValue68 below zero.
SimulationClass21MissionTickResult simulation_class21_tick_mission_pass_events(
    SimulationClass21LinkedChildState& child,int32_t model_point_step30,
    uint32_t simulation_step_ticks=1U) noexcept;

struct SimulationClass21CandidateScoreInput {
    int32_t source_world_x_q12{};
    int32_t source_world_y_q12{};
    uint32_t source_impact_class_index{};
    std::array<int32_t,8> source_target_class_damage_q12{};
    int32_t candidate_world_x_q12{};
    int32_t candidate_world_y_q12{};
    int32_t candidate_collision_radius_q12{};
    uint32_t candidate_runtime_class_id{};
    uint32_t candidate_impact_class_index{};
    std::array<int32_t,8> candidate_target_class_damage_q12{};
};
// Port of AiCombatTarget_EvaluateCandidateScore for the full-integrity normal-Simulation
// surface used by stock class-21 sorties. The binary call site passes sourceClassCount=1 and
// all-ones depth masks; the editor has no normal-Simulation integrity degradation, so the
// hierarchy scale ratio is exactly Q12 unity and the scale-deficit term is zero.
uint32_t simulation_class21_candidate_score_full_integrity(
    const SimulationClass21CandidateScoreInput& input,uint32_t current_best_score=0U) noexcept;
// Exact post-score obstruction branch from AiCombatTarget_EvaluateCandidateScore. Retail only
// evaluates weapon line of fire when raw_score would beat current_best_score. A blocked candidate
// is quartered, then MDL +0x18 decides whether that quartered score survives or becomes zero.
uint32_t simulation_class21_apply_candidate_line_of_fire_result(uint32_t raw_score,
    uint32_t current_best_score,bool line_of_fire_clear,bool source_runtime_field18_nonzero) noexcept;
// Exact post-selection proximity gate from ArmyRuntime_SpawnIndexedModelPointEffectNearCandidate.
// The binary uses FixedMath_Length2 and accepts when distance - candidateRadius < 0x1001.
bool simulation_class21_candidate_within_projectile_lock(
    int32_t source_world_x_q12,int32_t source_world_y_q12,
    int32_t candidate_world_x_q12,int32_t candidate_world_y_q12,
    int32_t candidate_collision_radius_q12) noexcept;

// Pure gate for ArmyRuntime_TryPlayMappedTerrainSoundAtWorldPoint after sound-resource lookup
// and world->cell bounds conversion. A true result means play the global (non-spatial) one-shot
// and reset the active faction relationTransitionTick to zero.
bool simulation_class21_mapped_terrain_sound_gate(uint32_t source_faction_index,
    uint32_t active_faction_index,bool capability_bit_clear,uint8_t active_faction_occupancy_byte,
    uint32_t relation_transition_tick) noexcept;
bool simulation_class21_begin_return_when_parent_idle(SimulationClass21LinkedChildState& child,
    SimulationClass22AirportState& parent,int32_t return_countdown) noexcept;
// True on landing completion; the child returns to Attached and writes parent state 4.
bool simulation_class21_tick_return_to_platform(SimulationClass21LinkedChildState& child,
    SimulationClass22AirportState& parent,uint32_t simulation_step_ticks=1U) noexcept;
// Runtime class 21 writes root child-0 pitch from FixedMath_Atan2Angle16((phase*step*coefficient)>>11, 0x1000).
// This helper preserves that exact integer input domain; the angle conversion itself is binary-oracle tested.
uint32_t simulation_class21_flight_pitch_turn16(int32_t motion_phase_q12,
    int32_t arc_coefficient_q12,uint32_t simulation_step_ticks=1U) noexcept;

enum class SimulationImpactKind : uint8_t {
    None,
    TerrainSurface,
    SecondarySurface,
    TargetModelImpactClass,
};

enum class SimulationProjectileOwnerKind : uint8_t {
    Placement,
    ProducedClass13,
    LinkedClass21,
    // EffectRuntime transition-kind-1 linked shots (for example LAVAM EFF 25 -> SHT 28)
    // are created with ownerArmyRuntime=null in retail. They therefore exclude no army/model
    // from the shared WorldRuntime collision query and report faction zero for impact damage.
    Environmental,
};

struct SimulationProjectileState {
    uint64_t serial{};
    std::size_t native_world_slot{SIZE_MAX};
    uint32_t native_model_roll_turn16{};
    std::size_t owner_placement_index{};
    SimulationProjectileOwnerKind owner_kind{SimulationProjectileOwnerKind::Placement};
    uint64_t owner_runtime_serial{};
    size_t owner_army_pool_slot{SIZE_MAX};
    std::size_t weapon_profile_index{};
    uint32_t shot_definition_id{};
    Vec3f launch_position{};
    Vec3f target_position{};
    // Present only for entity-targeted editor shots. Mode-2 retail projectiles re-read the
    // live target X/Y after their transition instead of freezing the firing-time coordinate.
    std::optional<std::size_t> live_target_placement_index;
    Vec3f launch_velocity{};
    uint32_t spawn_tick{};
    uint32_t travel_ticks{};
    uint32_t lifetime_ticks{};
    uint32_t launch_yaw_turn16{};
    uint32_t launch_pitch_turn16{};
    uint32_t fired_attachment_selector{0xFFFFFFFFU};
    SimulationImpactKind impact_kind{SimulationImpactKind::None};
    Vec3f impact_position{};
    uint32_t impact_material_index{};
    uint32_t impact_effect_id{};
    uint32_t impact_visual_lifetime_ticks{};
    // Identity of the WorldRuntime owner actually selected by the shared model raycast.
    // This is intentionally separate from live_target_placement_index: another model can
    // intercept the shot before the commanded target. Runtime-created class-13/class-21 owners
    // preserve their owner kind + serial so the same SHT damage transition reaches them too.
    std::optional<std::size_t> impact_target_placement_index;
    std::optional<std::size_t> impact_target_model_component_index;
    std::optional<SimulationProjectileOwnerKind> impact_target_runtime_kind;
    uint64_t impact_target_runtime_serial{};
    // Ordinary moving projectiles commit damage once. Direct-line beams are different: retail
    // reapplies targetClassImpactDamage/lifetime on every beam-maintenance tick and only the
    // impact visual is one-shot, so keep transition/effect state separately.
    // Native-catalog flight state. Legacy direct API queries retain their analytic preview.
    // Phase-3/7 diagnostic retained-field shot classification. Raw mask is
    // preserved for auditing; resolved mask, flags and tint reflect the native
    // shot callback for the selected active view faction. Rendering remains
    // separate until the tint handoff can be validated against authored shots.
    uint32_t terrain_neighborhood_mask{},terrain_refresh_tick{};
    uint32_t terrain_runtime_class_mask{},terrain_runtime_flags{},terrain_tint_argb{};
    bool runtime_flight_initialized{},runtime_flight_expired{};
    uint32_t runtime_flight_tick{},runtime_flight_age{};
    Vec3f runtime_flight_position{},runtime_flight_previous{},runtime_flight_ray_end{};
    int32_t runtime_flight_yaw{},runtime_flight_pitch{},runtime_flight_speed_q12{},runtime_elevation_offset{};
    std::array<int32_t,3> runtime_ballistic_direction{};
    bool impact_damage_applied{};
    bool impact_started{};
    bool impact_effect_emitted{};
};

struct SimulationWeaponTickResult {
    bool turned{};
    bool fired{};
    bool in_range{};
    bool aligned{};
    bool line_of_fire_clear{};
    bool ready_to_fire{}; // aim and shared timer gates, before the obstruction callback
    uint32_t fired_attachment_selector{0xFFFFFFFFU};
};

struct SimulationActorTickResult {
    bool moved{};
    bool turned{};
    bool fired{};
    bool in_range{};
    bool aligned{};
    std::vector<std::size_t> fired_weapon_indices;
};

int32_t shortest_turn16_delta(uint32_t current, uint32_t target) noexcept;
uint32_t wrap_turn16(int64_t value) noexcept;
float turn16_to_radians(uint32_t value) noexcept;
uint32_t radians_to_turn16(float radians) noexcept;

// Exact one-tick forms of the recovered yaw and pitch smoothers. The yaw path is modular;
// pitch is signed and clamped to the MDL bounds before acceleration/deceleration.
bool simulation_step_yaw(SimulationAxisState& state, uint32_t target_turn16,
                         int32_t maximum_velocity_turn16,
                         int32_t acceleration_turn16,
                         uint32_t tolerance_turn16 = kSimulationAimToleranceTurn16) noexcept;
bool simulation_step_pitch(SimulationAxisState& state, uint32_t target_turn16,
                           int32_t maximum_velocity_turn16,
                           int32_t acceleration_turn16,
                           int32_t minimum_pitch_turn16,
                           int32_t maximum_pitch_turn16,
                           uint32_t tolerance_turn16 = kSimulationAimToleranceTurn16) noexcept;

// Compatibility wrapper retained for earlier tests/callers; it uses the modular yaw contract.
bool simulation_step_axis(SimulationAxisState& state, uint32_t target_turn16,
                          int32_t maximum_velocity_turn16,
                          int32_t acceleration_turn16,
                          uint32_t tolerance_turn16 = kSimulationAimToleranceTurn16) noexcept;

SimulationAimAngles simulation_target_angles(Vec3f source, Vec3f target,
                                              int32_t elevation_offset_angle16 = 0) noexcept;
// Projectile creation and LOS intentionally have different ballistic failure semantics in retail.
// Creation clamps a negative discriminant to zero; LOS rejects the same trajectory.
SimulationAimAngles simulation_shot_launch_angles(Vec3f source, Vec3f target,
                                                   const ShotDefinitionRecord& shot) noexcept;
SimulationAimAngles simulation_shot_line_of_fire_angles(Vec3f source, Vec3f target,
                                                         const ShotDefinitionRecord& shot) noexcept;

// 0x004BEBC0 transforms the desired world direction through the live node basis and then adds the
// node's current local yaw. This keeps slope/bank orientation in the targeting solution.
uint32_t simulation_relative_yaw_from_world_basis(
    std::span<const float> world_basis, uint32_t current_local_yaw_turn16,
    Vec3f source, Vec3f target) noexcept;

bool simulation_target_within_range(Vec3f source, Vec3f target,
                                    const SimulationWeaponProfile& weapon) noexcept;

// Class-9 launch racks clear the complete low-bit prefix through the fired selector
// (`meshGroupMask &= 0xFFFFFFFE << selector`), not only the selected bit.
uint32_t simulation_class9_mesh_mask_after_fire(uint32_t current_mask,
                                                uint32_t selector) noexcept;
// Return the authoritative class-9 rack mask. Timer expiry mutates the mask in the 20 Hz callback;
// rendering must not synthesize visibility directly from timer==0 because that skips the binary's
// signed-underflow transition and can place restored rockets on the wrong hierarchy node.
uint32_t simulation_class9_render_mesh_mask(const SimulationWeaponState& state,
                                            const SimulationWeaponProfile& weapon) noexcept;

// Mode-zero SHT records are ray/beam models. The binary stores the nearest ray distance in
// ModelRuntimeNode +0xBC and the model renderer adds half that distance to local z==0 vertices
// and the full distance to local z>0 vertices. Negative-z geometry remains at the launch end.
float simulation_mode0_beam_length_world(const SimulationProjectileState& projectile,
                                         const ShotDefinitionRecord& shot) noexcept;
Vec3f simulation_mode0_stretch_local_position(Vec3f local_position,
                                              float beam_length_world) noexcept;

// Raw root-MDL sound slots consumed by the ArmyRuntime method-D audio callback table. The same
// serialized offsets have class-specific state gates; callers must select through
// simulation_runtime_looping_sound_indices() rather than playing every nonzero field.
struct SimulationRuntimeSoundProfile {
    uint32_t runtime_class_id{};
    uint32_t projected_sound_asset_id_d0{};        // MDL +0xD0
    uint32_t projected_sound_asset_id_d8{};        // MDL +0xD8
    uint32_t projected_sound_asset_id_primary{};   // MDL +0x1AC
    uint32_t projected_sound_asset_id_secondary{}; // MDL +0x274
};

struct SimulationRuntimeSoundState {
    bool enabled{};
    bool translated{};
    bool turned{};
    // Classes 13/22 have a second projected channel whose retail gate depends on gameplay state
    // that the map editor does not generally simulate. Keep it explicit instead of inferring it.
    bool secondary_state_active{};
};

// Returns up to two world spatial-sound-table indices requested by the recovered runtime-class
// callback. Zero entries mean no requested slot. Class 23 intentionally returns none even when its
// MDL serializes a nonzero +0x1AC value; its method-D callback is a binary-proven no-op.
std::array<uint32_t, 2> simulation_runtime_looping_sound_indices(
    const SimulationRuntimeSoundProfile& profile,
    const SimulationRuntimeSoundState& state) noexcept;

// Exact retail point-radius predicate extended over a segment for editor anti-tunnelling. Retail
// tests the candidate point against the sum of two MDL +0xDC radii; the segment form conservatively
// applies that same radius contract across the editor preview step.
bool simulation_segment_intersects_expanded_radius(Vec3f from, Vec3f to, Vec3f obstacle_center,
                                                    int32_t mover_radius_q12,
                                                    int32_t obstacle_radius_q12) noexcept;

// Exact 0x00484BA0 FixedMath_Atan2Angle16 compatibility helper. Exposed so terrain
// placement code can reconstruct the low 16-bit azimuth stored in FLD triangle0NormalAngles.
uint32_t fixed_atan2_angle16_compat(int32_t y, int32_t x) noexcept;

uint32_t simulation_depth_bin_mask(int32_t interval_radius_q12, int32_t center_q12) noexcept;
bool simulation_depth_bin_overlap(Vec3f first_center, int32_t first_radius_q12,
                                  Vec3f second_center, int32_t second_radius_q12) noexcept;

// ArmyRuntime_UpdateActivationMetricAndPlayStartSound's speed arithmetic. The
// sound side effect is serviced separately; no batch scaling belongs here.
uint32_t simulation_ground_advance_q12(const SimulationActorState& actor,
                                     const SimulationMotionProfile& motion) noexcept;
bool simulation_tick_actor_motion(SimulationActorState& actor,
                                  const SimulationMotionProfile& motion,uint32_t step_ticks=1U) noexcept;
// Runtime class 17 owns a separate banking movement callback. Unlike the
// generic constant-speed classes, it consumes the current route point when
// this callback's travel reaches half the remaining distance.
bool simulation_tick_class17_actor_motion(SimulationActorState& actor,
                                          const SimulationMotionProfile& motion,
                                          uint32_t step_ticks=1U) noexcept;
// Runtime class 3 does not consume the generic MDL yaw velocity/acceleration pair. Its body
// heading advances through alternating contact transitions. This compatibility controller keeps
// that separate domain and uses the binary-proven 0x0800/0x2000 heading thresholds.
bool simulation_tick_articulated_actor_motion(
    SimulationActorState& actor, const SimulationMotionProfile& motion,
    int32_t contact_interpolation_rate_q12,uint32_t step_ticks=1U) noexcept;

// Recovered runtime-class-2 endpoint/chord update. The primary and secondary track channels use
// opposite lateral points on the root SPR and wrap independently in the signed 0x100000 period.
void simulation_update_tank_track_offsets(SimulationActorState& actor,
                                          Vec3f previous_position,
                                          uint32_t previous_yaw_turn16,
                                          Vec3f current_position,
                                          uint32_t current_yaw_turn16,
                                          float local_bounds_y0,
                                          int32_t texture_travel_scale) noexcept;

// Services the movement-owned MDL +0x174 timed-EFF countdown. The caller must pass true only when
// the runtime callback's root X/Y/yaw-change gate fired. reset_ticks is the already-resolved retail
// interval (stock class-2/3 ground-mark definitions have no random interval component).
bool simulation_tick_movement_ground_effect_countdown(
    SimulationActorState& actor, bool root_moved_or_turned,
    uint32_t reset_ticks, uint32_t simulation_step_ticks = 1U) noexcept;

// Retail class-3 terrain-contact initialization derives the contact step from the complete
// candidate distance and the shared +0xC0/+0xC4 definition fields.
// ArmyArticulatedRuntime_Update{Left,Right}TerrainContact: plan from the
// opposite planted endpoint plus the authored side offset, using MDL +0xC0
// as stride length. The route point caps the reach before terrain sampling.
Vec3f simulation_articulated_step_target(Vec3f root, Vec3f opposite_target,
    Vec3f side_offset, uint32_t heading_turn16, int32_t stride_q12,
    Vec3f route_point) noexcept;

int32_t simulation_articulated_contact_step_q12(
    int32_t contact_interpolation_numerator_q12, int32_t contact_lift_q12,
    int32_t contact_distance_q12) noexcept;
// Advances the class-3 alternating contact transition from movement advance. Yaw requests a pivot.
// The 0x1000 transition domain mirrors runtimeStateA8/terrainContactMode in the original callback.
void simulation_update_articulated_contact_transition(
    SimulationActorState& actor, Vec3f previous_position, uint32_t previous_yaw_turn16,
    Vec3f current_position, uint32_t current_yaw_turn16,
    int32_t contact_interpolation_numerator_q12, int32_t movement_advance_q12 = 0) noexcept;
// Suspension heading follows the two interpolated foot headings, including pivot turns.
uint32_t simulation_articulated_support_heading(const SimulationActorState& actor) noexcept;
// Compatibility overload retained for G391 tests/callers.
void simulation_update_articulated_gait_phase(
    SimulationActorState& actor, Vec3f previous_position, Vec3f current_position,
    int32_t contact_interpolation_rate_q12) noexcept;
void simulation_initialize_class17_deployment(SimulationActorState& actor, bool freshly_placed,
                                              bool deployment_tilt_enabled = true) noexcept;
void simulation_tick_class17_deployment(SimulationActorState& actor) noexcept;
void simulation_tick_class17_banking(SimulationActorState& actor, bool moved) noexcept;
void simulation_tick_class17_pose(SimulationActorState& actor, bool moved) noexcept;
SimulationWeaponTickResult simulation_tick_weapon(
    SimulationActorState& actor,
    SimulationWeaponState& state,
    const SimulationWeaponProfile& weapon,
    const ShotDefinitionRecord& shot,
    Vec3f aim_origin,
    const Vec3f* target,
    bool line_of_fire_clear = true,
    uint32_t available_launch_attachment_selector_mask = 0xFFFFFFFFU,
    std::span<const float> yaw_world_basis = {},
    bool command_aim = false,
    bool native_turret = false,
    std::optional<std::array<uint32_t,3>> native_yaw_angles = std::nullopt) noexcept;

// Compatibility aggregate used by focused tests. Production editor simulation updates every
// recovered weapon profile independently through simulation_tick_weapon.
SimulationActorTickResult simulation_tick_actor(
    SimulationActorState& actor,
    const SimulationMotionProfile& motion,
    const SimulationWeaponProfile* weapon,
    const Vec3f* target,
    uint32_t tick_count = 1U) noexcept;

struct SimulationLinkedEffectShotGeometry {
    Vec3f launch_position{};
    Vec3f aim_position{};
};

// Effect transition kind 1 resolves a class-2 attachment in world space, launches the linked
// SHT from that attachment, and uses the point reflected through the effect root only as the
// projectile aim point. The reflected point is not a terminal projectile position.
SimulationLinkedEffectShotGeometry simulation_linked_effect_shot_geometry(
    Vec3f effect_root_position, Vec3f attachment_world_position) noexcept;

std::vector<SimulationProjectileState> simulation_create_volley(
    std::size_t owner_placement_index,
    std::size_t weapon_profile_index,
    const SimulationWeaponProfile& weapon,
    std::span<const Vec3f> launch_positions,
    Vec3f target_position,
    const ShotDefinitionRecord& shot,
    uint32_t spawn_tick,
    uint64_t& next_serial,
    uint32_t fired_attachment_selector = 0xFFFFFFFFU,
    std::optional<std::size_t> live_target_placement_index = std::nullopt,
    SimulationProjectileOwnerKind owner_kind = SimulationProjectileOwnerKind::Placement,
    uint64_t owner_runtime_serial = 0U,
    std::span<const Vec3f> target_offsets = {}, bool native_launch_angles=false);

// Compatibility overload for the pre-G371 caller shape.
std::vector<SimulationProjectileState> simulation_create_volley(
    std::size_t owner_placement_index,
    const SimulationWeaponProfile& weapon,
    std::span<const Vec3f> launch_positions,
    Vec3f target_position,
    const ShotDefinitionRecord& shot,
    uint32_t spawn_tick,
    uint64_t& next_serial);

Vec3f simulation_projectile_position(const SimulationProjectileState& projectile,
                                     const ShotDefinitionRecord& shot,
                                     uint32_t current_tick,
                                     const Vec3f* live_target_position = nullptr) noexcept;

// Updates the live entity target only for SHT trajectory mode 2 (FixedRange). Direct-line mode 0
// keeps its launch ray frozen and must never be retargeted toward a moved entity after creation.
// For mode 2 this also recomputes the vertical target-reach tick from the retail path.
void simulation_update_projectile_live_target(SimulationProjectileState& projectile,
                                              const ShotDefinitionRecord& shot,
                                              Vec3f target_position) noexcept;
uint32_t simulation_shot_frame_index(const ShotDefinitionRecord& shot,
                                     uint32_t age_ticks) noexcept;
uint32_t simulation_mode3_elevation_offset_turn16(const ShotDefinitionRecord& shot,
                                                  uint32_t age_ticks) noexcept;
uint32_t simulation_mode3_model_pitch_turn16(const SimulationProjectileState& projectile,
                                              const ShotDefinitionRecord& shot,
                                              uint32_t age_ticks) noexcept;
bool simulation_projectile_impacted(const SimulationProjectileState& projectile,
                                    uint32_t current_tick) noexcept;
bool simulation_projectile_expired(const SimulationProjectileState& projectile,
                                   uint32_t current_tick) noexcept;

// Resolves the first terrain or secondary-water surface crossing along the authored
// projectile path. This mirrors the binary's nearest-distance comparison rather than
// classifying only the user-selected endpoint after the shot has already arrived.
void simulation_resolve_projectile_surface_impact(
    SimulationProjectileState& projectile, const ShotDefinitionRecord& shot,
    const FieldGrid& field) noexcept;

// The object/effect image array is sized from the selected Vulkan device. The old fixed 256
// limit was an editor-only truncation and could terminate valid stock effect combinations.
uint32_t simulation_validate_texture_layer_capacity(
    std::size_t requested_layers, uint32_t device_max_layers);

} // namespace thandor
