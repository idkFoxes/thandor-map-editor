#pragma once
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/pioneer_class14_placement.hpp"
#include "thandor/pioneer_site_admission.hpp"
#include <optional>
#include <span>
#include <vector>

namespace thandor {
struct LabAiOwnerWorkspaceV718;
// Detached placement inputs. No saved PCK layouts are mutated. Simulation callers may explicitly
// supply the native army pool to retain allocation side effects; diagnostic
// queries leave that argument null.
struct SimulationLabSiteModel {
    uint32_t army_asset_id{}, runtime_class{};
    int32_t radius_q12{}, margin_q12{}, support_q12{}, contact_kind{}, resource_selector{};
    std::optional<MovementPoint> factory_exit_local;
};
struct SimulationLabSiteOwner {
    PioneerPlacementOwner support;
    int32_t radius_q12{};
    std::optional<MovementPoint> factory_exit;
    int32_t broad_phase_radius_q12{};
    int32_t contact_kind{};
    std::optional<size_t> army_entity_index{}; // Absent for scenery; transient adapter identity.
};
// Native ScanSiteCell predicates before workspace capacity, separation and base-owner tests.
struct RetailAiSiteAdmission { bool general{},flagged{},resource{},base{},large{}; };
RetailAiSiteAdmission retail_ai_site_admission(const FieldGrid& field,
    const SimulationLabRetailScratchImage& scratch,uint32_t faction,uint32_t x,uint32_t y,
    std::span<const uint32_t,4> active_masks);
std::optional<SimulationLabSiteModel> simulation_lab_first_site_model(
    const SimulationLabRuntimeCatalog& catalog,uint32_t army_asset_id);
std::vector<SimulationLabSiteOwner> simulation_lab_site_owners(
    const SimulationLabRun& run,const FieldGrid& field,const SimulationLabRuntimeCatalog& catalog,
    std::span<const SimulationLabStaticPlacementOwner> static_models = {});
PioneerPlacementResult simulation_lab_query_site_placement(
    const SimulationLabRun& run,const FieldGrid& field,std::span<const SimulationLabSiteOwner> owners,
    SimulationLabSiteModel model,MovementPoint point,uint32_t faction,uint32_t mode,int32_t padding,uint32_t heading=0);
struct SimulationLabSiteReservation {
    bool accepted{};
    bool mode7_accepted{}, mode4_accepted{};
    uint32_t clearance_q12{}, probes_created{};
};
SimulationLabSiteReservation simulation_lab_reserve_special_site(
    const SimulationLabRun& run,const FieldGrid& field,std::span<const SimulationLabSiteOwner> owners,
    SimulationLabSiteModel resource,SimulationLabSiteModel storage,MovementPoint point,
    std::span<const Vec3f> base09,uint32_t faction,SimulationArmyPool* probe_pool=nullptr,
    SimulationLabWorldObjectPool* world_pool=nullptr,const SimulationLabRuntimeCatalog* catalog=nullptr);
struct SimulationLabSpecialSiteCluster {
    bool accepted{},mode3_accepted{},chain_rejected{};
    uint32_t clearance_q12{},bucket_count{},probes_created{};
};
SimulationLabSpecialSiteCluster simulation_lab_special_site_cluster(
    const SimulationLabRun& run,const FieldGrid& field,std::span<const SimulationLabSiteOwner> owners,
    SimulationLabSiteModel resource,std::optional<SimulationLabSiteModel> storage,MovementPoint point,
    std::span<const Vec3f> base09,uint32_t faction,SimulationArmyPool* probe_pool=nullptr,
    SimulationLabWorldObjectPool* world_pool=nullptr,const SimulationLabRuntimeCatalog* catalog=nullptr);
// Exact class-18 caller control around AiPlacement_QueryReachableSiteBucketCount:
// mode 3, conditional mode 0, ceil buckets and at most four temporary ARM333
// probes.  The returned disposition is observable by the Pioneer selector:
// SkipSite continues, StopSelector returns without a move, ScoreSite permits
// the scoring pass.
struct SimulationLabPioneerSiteAdmission {
    PioneerSiteAdmission disposition{PioneerSiteAdmission::SkipSite};
    bool mode3_accepted{},mode0_queried{},mode0_accepted{};
    bool reserve_chain_called{},reserve_chain_rejected{};
    uint32_t clearance_q12{},bucket_count{},probes_created{};
};
SimulationLabPioneerSiteAdmission simulation_lab_pioneer_site_admission(
    const SimulationLabRun& run,const FieldGrid& field,std::span<const SimulationLabSiteOwner> owners,
    SimulationLabSiteModel resource,std::optional<SimulationLabSiteModel> storage,MovementPoint point,
    std::span<const Vec3f> base09,uint32_t faction,SimulationArmyPool* probe_pool=nullptr,
    SimulationLabWorldObjectPool* world_pool=nullptr,const SimulationLabRuntimeCatalog* catalog=nullptr);
// Extended ARM>=340 planner. Scores the owner workspace captured before diplomacy.
std::optional<Vec3f> simulation_lab_extended_placement(SimulationLabRun& run,
    const FieldGrid& field,std::span<const SimulationLabSiteOwner> owners,
    const LabAiOwnerWorkspaceV718& planning_owners,SimulationLabSiteModel model,
    std::span<const Vec3f> workspace09,uint32_t faction);
// x86 wrapping multiplication followed by unsigned assigned-count division; Tritium scaling is signed 64-bit.
uint32_t simulation_lab_extractor_weight(uint32_t base,uint32_t assigned,uint32_t army_asset_id,
    uint32_t tritium_rate,uint32_t supplied_demand,uint32_t unpowered_demand) noexcept;
}
