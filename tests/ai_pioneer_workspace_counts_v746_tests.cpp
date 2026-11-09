#include "thandor/simulation_lab_ai_workspaces.hpp"

#include <iostream>
#include <stdexcept>

using namespace thandor;

namespace {
void check(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}
SimulationLabEntity owner(std::uint32_t asset,std::uint32_t faction=1U) {
    SimulationLabEntity entity;
    entity.army_asset_id=asset;
    entity.faction_index=faction;
    return entity;
}
}

int main() try {
    SimulationLabRun run;
    // Oldest entries must not displace newer linked roots from the 128-entry
    // primary workspace. Construction/terminal presentation state is not an
    // owner-list admission predicate; runtime removal is.
    for(std::uint32_t i=0;i<130U;++i) {
        auto entity=owner(300U+i);
        entity.construction_complete=(i&1U)==0U;
        entity.root_model_terminal=(i%3U)==0U;
        run.entities.push_back(entity);
    }
    run.entities[129].runtime_removed=true;
    const auto counts=simulation_lab_pioneer_workspace_counts(run,1U);
    check(counts.workspace00==128U,"Pioneer WS00 did not use the bounded owner image");
    const auto own=simulation_lab_ai_own_workspaces(run,1U);
    check(own.structures.size()==128U && own.structures.front().entity_index==128U &&
          own.structures.back().entity_index==1U,
          "Pioneer WS00 order/capacity differs from newest-first owner traversal");

    for(std::uint32_t i=0;i<12U;++i)
        run.pending_asset_requests.push_back({330U,1U,0U,0U});
    run.pending_asset_requests.push_back({330U,2U,0U,0U});
    check(simulation_lab_pioneer_workspace_counts(run,1U).workspace04==8U,
          "Pioneer WS04 request capacity changed");

    SimulationLabRun assigned;
    assigned.entities.push_back(owner(330U));
    auto producer=owner(300U);
    producer.runtime_model_class_id=11U;
    producer.production_building=true;
    producer.production_asset_id=330U;
    assigned.entities.push_back(producer);
    assigned.pending_asset_requests.push_back({330U,1U,0U,0U});
    assigned.secondary_purchase_requests.push_back({330U,1U,0U,0U});
    const auto assigned_ws=simulation_lab_ai_own_workspaces(assigned,1U);
    check(std::count_if(assigned_ws.structures.begin(),assigned_ws.structures.end(),
        [](const auto& entry){return entry.army_asset_id==330U;})==4,
        "fixture did not expose assigned and unassigned ARM330 records");
    check(simulation_lab_ai_assigned_structure_count(assigned_ws,330U)==1U,
          "assigned structure count included pending/product records");

    SimulationLabRun unit_roots;
    auto removed_root=owner(50U);
    removed_root.model_components.resize(1U);
    removed_root.root_model_component_index=0U;
    removed_root.model_components[0].runtime_removed=true;
    unit_roots.entities.push_back(removed_root);
    auto live_root=owner(51U);
    live_root.model_components.resize(1U);
    live_root.root_model_component_index=0U;
    unit_roots.entities.push_back(live_root);
    const auto unit_ws=simulation_lab_ai_own_workspaces(unit_roots,1U);
    check(!unit_ws.unit_root_admitted(unit_roots,0U) &&
          unit_ws.unit_root_admitted(unit_roots,1U),
          "Pioneer WS01 ignored root-model owner removal");
    SimulationLabRun ownerless;
    ownerless.entities.push_back(removed_root);
    ownerless.pending_asset_requests.push_back({330U,1U,0U,0U});
    check(simulation_lab_ai_own_workspaces(ownerless,1U).structures.empty(),
          "root-removed entity defeated the native empty-world workspace quirk");
    check(simulation_lab_pioneer_workspace_counts(ownerless,1U).workspace04==0U,
          "empty world incorrectly populated requested-asset workspace04");

    SimulationLabRun static_world;
    static_world.static_placement_owner_count=1U;
    static_world.pending_asset_requests.push_back({330U,1U,0U,0U});
    static_world.secondary_purchase_requests.push_back({50U,1U,0U,0U});
    const auto static_ws=simulation_lab_ai_own_workspaces(static_world,1U);
    check(static_ws.structures.size()==1U&&static_ws.structures[0].army_asset_id==330U&&
          static_ws.units.size()==1U&&static_ws.units[0].army_asset_id==50U,
          "static world owner did not enable pending workspace00/01 records");
    check(simulation_lab_pioneer_workspace_counts(static_world,1U).workspace04==1U,
          "static world owner did not enable requested-asset workspace04");

    std::cout<<"V746_PIONEER_WORKSPACE_COUNTS PASS\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<"V746_PIONEER_WORKSPACE_COUNTS FAIL: "<<error.what()<<'\n';
    return 1;
}
