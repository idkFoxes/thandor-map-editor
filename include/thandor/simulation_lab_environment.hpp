#pragma once
#include "thandor/simulation_lab_runtime.hpp"
namespace thandor {
struct SimulationLabEnvironmentSeed {
    std::vector<SimulationArmyPlacementSeed> initial_army_placements;
    std::vector<SimulationLabScenerySeed> crushable_scenery;
    std::vector<SimulationLabEnvironmentalEmitter> emitters;
    std::vector<SimulationLabStaticPlacementOwner> placement_owners;
    std::vector<SimulationLabStaticShotHull> collision_hulls;
};
SimulationLabEnvironmentSeed simulation_lab_prepare_environment(
    const AssetCatalog& assets,const LevelAsset& level,const FieldGrid& field,
    const SimulationLabRuntimeCatalog& catalog);
// Current SPR bounds, using root contact and authored hierarchy transforms.
// Static models excluded from Lab entities use the same prepared hull representation.
struct SimulationLabLiveShotHull {
    SimulationLabStaticShotHull hull;
    size_t entity_index{};
    std::optional<size_t> component_index;
};
std::vector<SimulationLabEffectPose> simulation_lab_current_model_poses(
    const SimulationLabEntity& entity,const FieldGrid& field,const SimulationLabRuntimeCatalog& catalog);
// Native contact kinds 0/1/2/4 seat the root after a ground callback.
bool simulation_lab_place_root_on_surface(SimulationLabEntity& entity,
    const FieldGrid& field,const SimulationLabRuntimeCatalog& catalog);
// Complete geometry omitted by compact seeds and factory-created entities.
// Authored/captured offsets survive; this does not resample a moving owner's
// cached creation metrics on every tick.
bool simulation_lab_prepare_missing_model_geometry(SimulationLabEntity& entity,
    const FieldGrid& field,const SimulationLabRuntimeCatalog& catalog);
// Recover authored terminal-foot offsets from the instantiated hierarchy, including
// factory products. No catalog I/O; contacts themselves are initialized on live terrain.
bool simulation_lab_prepare_articulated_contacts(SimulationLabEntity& entity,
    const FieldGrid& field,const SimulationLabRuntimeCatalog& catalog);
// One pose walk supplies the selected (live) target node and the separate
// root height used by the native tracked-endpoint branch (notably class 21).
struct SimulationLabTargetAimGeometry {
    Vec3f selected{};
    float root_aim_z{};
};
SimulationLabTargetAimGeometry simulation_lab_target_aim_geometry(
    const SimulationLabEntity& entity,const FieldGrid& field,
    const SimulationLabRuntimeCatalog& catalog);
Vec3f simulation_lab_target_aim_point(const SimulationLabEntity& entity,
    const FieldGrid& field,const SimulationLabRuntimeCatalog& catalog);
// Native fixed-point leg hierarchy; water selection uses the retained rendered
// terminal foot, rather than the ideal contact used to center the root.
std::array<Vec3f,2> simulation_lab_articulated_contact_positions(
    const SimulationLabEntity& entity,const SimulationLabRuntimeCatalog& catalog);
Vec3f simulation_lab_model_world_point(const SimulationLabEffectPose& pose,Vec3f local);
struct SimulationLabWeaponGeometry {
    Vec3f target_resolution_origin{};
    Vec3f aim_origin{};
    std::vector<Vec3f> launch_target_offsets;
    std::vector<Vec3f> launch_points;
    uint32_t available_selector_mask{};
    std::array<float,9> yaw_basis{1,0,0,0,1,0,0,0,1};
    std::optional<std::array<uint32_t,3>> native_yaw_angles;
};
void simulation_lab_step_barrel_recoil(const SimulationLabEntity& entity,
    const SimulationWeaponProfile& profile,SimulationWeaponState& state,
    const SimulationLabRuntimeCatalog& catalog,int32_t multiplier);
SimulationLabWeaponGeometry simulation_lab_weapon_geometry(const SimulationLabEntity& entity,
    const SimulationWeaponProfile& profile,const SimulationWeaponState& state,
    const FieldGrid& field,const SimulationLabRuntimeCatalog& catalog);
std::vector<SimulationLabLiveShotHull> simulation_lab_current_shot_hulls(
    const SimulationLabRun& run,const FieldGrid& field,const SimulationLabRuntimeCatalog& catalog,
    bool ground_only=false,bool animated_only=false);
bool simulation_lab_refresh_environmental_projectile_contact(
    SimulationProjectileState& projectile,const ShotDefinitionRecord& shot,uint32_t current_tick,
    const SimulationLabRuntimeCatalog& catalog,std::span<const SimulationLabLiveShotHull> live_hulls,
    std::span<const SimulationLabStaticShotHull> static_hulls);
}
