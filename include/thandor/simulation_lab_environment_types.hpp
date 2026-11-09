#pragma once
#include "thandor/field.hpp"
#include "thandor/model.hpp"
#include <array>
#include <cstddef>
#include <vector>
#include <cstdint>
#include <utility>
#include <memory>
#include <optional>
namespace thandor {
struct SimulationLabEffectPose {
    std::array<float,9> basis{1,0,0,0,1,0,0,0,1};
    Vec3f position{};
    std::optional<std::array<uint32_t,3>> native_angles{};
    std::optional<std::array<int32_t,9>> native_basis{};
};
struct SimulationLabScenerySeed {
    std::size_t placement_index{};
    uint32_t army_asset_id{},yaw{};
    Vec3f position{};
};
struct SimulationLabStaticPlacementOwner {
    uint32_t model_definition_id{},faction_index{};
    SimulationLabEffectPose pose{};
    // Original LEV placement identity. Authored MODEL owners are linked while the
    // level is loaded, so head insertion makes larger placement indices newer.
    std::size_t source_placement_index{SIZE_MAX};
    // Complete ArmyRuntime-style aggregate when the prepared ARM hierarchy is
    // available. Attached ModelRuntime roots contribute metrics but do not create
    // independent world owners. Legacy callers may leave this false and retain the
    // root-definition compatibility path.
    bool derived_selection_metrics_valid{};
    uint32_t runtime_class_id{};
    int32_t visibility_radius_q12{};
    uint32_t occupancy_mark_radius_units{};
    int32_t visibility_height_offset_q12{};
};
struct SimulationLabEnvironmentalEmitter {
    uint32_t model_definition_id{};
    SimulationLabEffectPose pose{};
    std::vector<Vec3f> world_effect_points;
    std::vector<std::pair<uint32_t,Vec3f>> world_effect_point_records;
    // Original LEV owner identity; SIZE_MAX denotes caller-authored legacy metadata.
    std::size_t source_placement_index{SIZE_MAX};
};
struct SimulationLabEnvironmentalEmitterState {
    uint32_t countdown{1U};
    uint32_t point_index{};
};
struct SimulationLabTerrainImpact {
    uint32_t completion_age{};
    int32_t radius_q12{}, height_amplitude_q12{}, material{};
};
struct SimulationLabEnvironmentalEffect {
    uint32_t effect_id{};
    uint32_t spawn_tick{};
    SimulationLabEffectPose pose{};
    std::size_t next_emission{};
    // Last native phase-3/7 terrain refresh of this linked effect owner.
    // Do not infer these fields from editor one-shot journals.
    uint32_t terrain_neighborhood_mask{}, terrain_runtime_class_mask{};
    uint32_t terrain_runtime_flags{}, terrain_tint_argb{}, terrain_refresh_tick{};
    // The next shot serial at link time orders same-tick EFF and SHT owners.
    uint64_t next_projectile_serial_at_creation{};
    std::size_t native_world_slot{SIZE_MAX};
    bool world_pool_only{};
    uint32_t terrain_contact_alpha{};
    bool ended_on_terrain{};
    std::optional<SimulationLabTerrainImpact> terrain_impact;
};
// Prepared at catalog load, shared by static and live collider instances.
// The decoded first SPR LOD contains the surfaces actually drawn at full detail;
// broad bounding boxes remain available for sprites without decoded triangles.
struct SimulationLabTriangleSurface {
    std::vector<Vec3f> positions;
    std::vector<uint32_t> indices;
    std::vector<SpriteRaycastTriangle> native_triangles;
};
struct SimulationLabStaticShotHull {
    std::array<Vec3f,8> corners{};
    uint32_t impact_class{};
    std::shared_ptr<const SimulationLabTriangleSurface> collision_surface;
    SimulationLabEffectPose collision_pose{};
    std::optional<int32_t> native_bounding_radius_q12{};
};
}
