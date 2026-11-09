#pragma once
#include <tuple>
#include "thandor/simulation_lab.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
namespace thandor {

// Native EntityPathing_RebuildOverlappingGroupRoutes uses one fixed 32-pair
// workspace. MODEL owners are admitted newest-first, then heap-sorted by the
// exact recovered priority before their route segments are stamped.
inline constexpr std::size_t kSimulationLabRoutePriorityPairCapacity = 32U;

struct SimulationLabRoutePriorityPair {
    std::size_t entity_index{SIZE_MAX};
    std::int32_t priority{};
};

struct SimulationLabRoutePriorityImage {
    std::array<SimulationLabRoutePriorityPair,
               kSimulationLabRoutePriorityPairCapacity> pairs{};
    std::size_t count{};
    bool capacity_reached{};
};

SimulationLabRoutePriorityImage simulation_lab_overlapping_route_priority_image(
    const SimulationLabRun& run, std::size_t source_index, MovementPoint target,bool within_owner_callback=false) noexcept;
std::optional<size_t> simulation_lab_visible_command_target(SimulationLabRun& run,const FieldGrid& field,size_t source);
// The native common coordinate pairs alias movement fallback, primary and stored.
void simulation_lab_sync_movement_coordinates(SimulationLabEntity& entity,
    const PioneerMovementState& state) noexcept;
void simulation_lab_sync_follow(SimulationLabEntity& entity);
void simulation_lab_stop_follow(SimulationLabEntity& entity);
// Only for a queued player/selection Stop Movement command. Not a route arrival or weapon-follow stop.
bool simulation_lab_explicit_stop_movement(SimulationLabEntity& entity);
// Native AiUnitCommand_AssignWorkspacePoint/AssignFactionAnchorPoint route order.
// The command target is explicit so a movement-locked unit can append it without
// replacing the active actor destination. The route is resolved at command time,
// matching ArmyRuntime_StartRoutedMoveCommand.
MovementRouteResult simulation_lab_resolve_native_route(SimulationLabRun& run,const FieldGrid& field,
    size_t source_index,PioneerCommandState& state,MovementPoint target,
    std::span<const V528PathRuntimeBlocker> static_blockers,V565PathTerrainCache& cache,
    bool within_owner_callback,bool& usable);
void simulation_lab_set_site_route(SimulationLabRun& run,const FieldGrid& field,
    size_t source_index,MovementPoint target,
    std::span<const V528PathRuntimeBlocker> static_blockers,
    V565PathTerrainCache& terrain_cache);
// SelectionPointerArray_ApplyPositionCommand uses
// ArmyRuntime_AppendWaypointOrStartMove: append to an active non-direct queue,
// otherwise start the same issue-tick routed command as a plain move.
void simulation_lab_append_waypoint_or_start_route(SimulationLabRun& run,const FieldGrid& field,
    size_t source_index,MovementPoint target,
    std::span<const V528PathRuntimeBlocker> static_blockers,
    V565PathTerrainCache& terrain_cache);
// AiUnitGroup_AssignCollectedEntitiesToBestTarget calls
// ArmyRuntime_ResolveCommandTargetAndRoute before stamping the group command.
// Resolve the target's current position immediately through the native fallback-
// waypoint branch, then retain the assigned target even when route guards reject
// the issue-tick solve (locked/routed/retry state).
void simulation_lab_set_group_target_route(SimulationLabRun& run,const FieldGrid& field,
    size_t source_index,size_t target_index,
    std::span<const V528PathRuntimeBlocker> static_blockers,
    V565PathTerrainCache& terrain_cache);
// Runtime class 13 dispatches a newly built unit with
// ArmyRuntime_StartMoveCommandWithAuxiliaryValues: the factory exit is routed
// immediately while the stored drive/rally point is queued as the one final
// waypoint. The child remains movement-locked until the factory releases it.
void simulation_lab_set_factory_exit_route(SimulationLabRun& run,const FieldGrid& field,
    size_t source_index,size_t parent_index,MovementPoint exit_target,
    MovementPoint queued_rally,
    std::span<const V528PathRuntimeBlocker> static_blockers,
    V565PathTerrainCache& terrain_cache);
void simulation_lab_flush_factory_exit_routes(SimulationLabRun& run,const FieldGrid& field,
    size_t parent_index,std::span<const V528PathRuntimeBlocker> static_blockers,
    V565PathTerrainCache& terrain_cache);
bool simulation_lab_primary_follow_weapon(const SimulationLabEntity& entity,size_t weapon_index);
void simulation_lab_weapon_follow(SimulationLabRun& run,const FieldGrid& field,size_t source_index,
    size_t weapon_index,Vec3f target,bool line_clear,std::span<const V528PathRuntimeBlocker> static_blockers,
    V565PathTerrainCache& terrain_cache);
inline auto simulation_lab_owner_order(const SimulationLabEntity& entity,std::size_t index) {
    return std::tuple{entity.synthetic,entity.synthetic?entity.construction_start_tick:0U,
                      entity.source_placement_index,index};
}
bool simulation_lab_prepare_follow_motion(SimulationLabRun& run,const FieldGrid& field,size_t source_index,
    std::span<const V528PathRuntimeBlocker> static_blockers,V565PathTerrainCache& terrain_cache,bool within_movement_callback=false);
void simulation_lab_finish_follow_motion(SimulationLabEntity& entity,bool point_reached,bool blocked);
}
