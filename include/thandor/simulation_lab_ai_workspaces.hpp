#pragma once
#include "thandor/simulation_lab.hpp"
#include <algorithm>
namespace thandor {
inline constexpr std::uint32_t kSimulationLabAiWorkspace04Capacity = 8U;
inline constexpr std::size_t kSimulationLabAiWorkspace11Capacity = 1024U;
inline constexpr std::size_t kSimulationLabAiWorkspace13Capacity = 128U;

struct SimulationLabAiOwnEntry {
  uint32_t army_asset_id{};
  std::optional<size_t> entity_index;
  size_t component_index{kSimulationLabNoComponent};
  uint32_t model_definition_id{};
};
struct SimulationLabAiOwnWorkspaces {
  std::vector<SimulationLabAiOwnEntry>
      structures; // Native workspace 00, capacity 128.
  std::vector<SimulationLabAiOwnEntry>
      units; // Native workspace 01, capacity 64.
  bool root_admitted(const SimulationLabRun &run, size_t index) const {
    return std::any_of(
        structures.begin(), structures.end(), [&](const auto &e) {
          return e.entity_index == index &&
                 (e.component_index == kSimulationLabNoComponent ||
                  e.component_index ==
                      run.entities[index].root_model_component_index);
        });
  }
  bool unit_root_admitted(const SimulationLabRun &run, size_t index) const {
    return std::any_of(units.begin(), units.end(), [&](const auto &e) {
      return e.entity_index == index &&
             (e.component_index == kSimulationLabNoComponent ||
              e.component_index ==
                  run.entities[index].root_model_component_index);
    });
  }
};
const SimulationLabAiOwnWorkspaces* simulation_lab_retained_planning_owners(
    const SimulationLabRun& run, uint32_t faction) noexcept;
struct SimulationLabPioneerWorkspaceCounts {
  std::uint32_t workspace00{};
  std::uint32_t workspace04{};
};
// AiPlanning_RebuildFactionWorkspaces tests the world-owner LIST head before
// adding pending or in-production records.  A static LEV model is enough to
// make that list nonempty even when run.entities has no live ArmyRuntime root.
inline bool simulation_lab_ai_world_owner_list_nonempty(
    const SimulationLabRun& run, bool additional_world_owners=false) noexcept {
  if (additional_world_owners || run.static_placement_owner_count != 0U)
    return true;
  return std::any_of(run.entities.begin(), run.entities.end(),
      [](const SimulationLabEntity& entity) {
        return simulation_lab_has_live_root_model_owner(entity);
      });
}

// ArmyRuntime_CreateInstance links only the root model node after creating
// its attached hierarchy. Attached runtimes are not separate owner-list
// entries.
inline SimulationLabAiOwnWorkspaces
simulation_lab_ai_own_workspaces(const SimulationLabRun &run, uint32_t faction,
                                 bool additional_world_owners = false) {
  if (run.active_planning_snapshot)
    if (const auto* retained = simulation_lab_retained_planning_owners(run,faction))
      return *retained;
  SimulationLabAiOwnWorkspaces out;
  const auto append = [&](SimulationLabAiOwnEntry e,
                          bool force_structure = false) {
    auto &list =
        force_structure || e.army_asset_id >= 300 ? out.structures : out.units;
    const size_t capacity = &list == &out.structures ? 128 : 64;
    if (list.size() < capacity)
      list.push_back(e);
  };
  bool world_has_owners = simulation_lab_ai_world_owner_list_nonempty(
      run,additional_world_owners);
  for (size_t visit = 0; visit < run.entities.size(); ++visit) {
    const auto index = run.entities.size() - 1 - visit;
    const auto &e = run.entities[index];
    if (!simulation_lab_has_live_root_model_owner(e))
      continue;
    const auto component = e.root_model_component_index;
    if (component < e.model_components.size()) {
      const auto &root = e.model_components[component];
      world_has_owners = true;
      if (e.faction_index == faction)
        append({e.army_asset_id, index, component, root.model_definition_id});
    } else {
      world_has_owners = true;
      if (e.faction_index == faction)
        append({e.army_asset_id, index});
    }
  }
  if (!world_has_owners)
    return out; // Original empty-world quirk.
  for (const auto &p : run.pending_asset_requests)
    if (p.faction_index == faction)
      append({p.army_asset_id, {}}, true);
  for (const auto &p : run.secondary_purchase_requests)
    if (p.faction_index == faction)
      append({p.army_asset_id, {}});
  // Only entries present before this step are visited. Pending/production
  // appends respect the same shared capacities as actual runtime entries.
  const auto before = out.structures.size();
  for (size_t i = 0; i < before; ++i) {
    const auto entry = out.structures[i];
    if (!entry.entity_index)
      continue;
    const auto &e = run.entities[*entry.entity_index];
    if (entry.component_index != kSimulationLabNoComponent &&
        entry.component_index != e.root_model_component_index)
      continue;
    if (e.production_building &&
        (e.runtime_model_class_id == 11 || e.runtime_model_class_id == 13 ||
         e.runtime_model_class_id == 22))
      append({e.production_asset_id, {}}, e.runtime_model_class_id == 11);
  }
  return out;
}

inline std::uint32_t simulation_lab_ai_workspace04_count(
    const SimulationLabRun& run, std::uint32_t faction) {
  const auto requested = simulation_lab_ai_world_owner_list_nonempty(run)
      ? static_cast<std::uint32_t>(std::count_if(
          run.pending_asset_requests.begin(), run.pending_asset_requests.end(),
          [&](const SimulationLabPendingAssetRequest &request) {
            return request.faction_index == faction;
          }))
      : 0U;
  return std::min(requested, kSimulationLabAiWorkspace04Capacity);
}

// The Pioneer compares the same structure and request workspaces used by
// construction planning.
inline SimulationLabPioneerWorkspaceCounts
simulation_lab_pioneer_workspace_counts(const SimulationLabRun &run,
                                        std::uint32_t faction) {
  const auto own = simulation_lab_ai_own_workspaces(run, faction);
  return {static_cast<std::uint32_t>(own.structures.size()),
          simulation_lab_ai_workspace04_count(run, faction)};
}

// AiPrimaryWorkspace_CountAssignedEntriesById counts only workspace-00
// records that still carry a runtime model. Pending requests and products in
// production occupy the workspace but are deliberately not "assigned".
inline std::uint32_t simulation_lab_ai_assigned_structure_count(
    const SimulationLabAiOwnWorkspaces &own, std::uint32_t army_asset_id) {
  return static_cast<std::uint32_t>(std::count_if(
      own.structures.begin(), own.structures.end(), [&](const auto &entry) {
        return entry.entity_index.has_value() &&
               entry.army_asset_id == army_asset_id;
      }));
}

// AiCandidateWorkspace_AddOrAccumulateWeightedEntry searches the bounded
// workspace from the newest record back to the oldest.  A match still
// accumulates when all 128 slots are occupied; only a distinct new record is
// dropped.  Candidate is deliberately a template so the compact runtime can
// retain its local record type while tests exercise this exact workspace
// contract directly.
template <class Candidate>
inline bool simulation_lab_ai_candidate_add_or_accumulate(
    std::vector<Candidate> &workspace, std::uint32_t id, std::uint32_t kind,
    std::uint32_t packed_score_delta) {
  const auto existing = std::find_if(
      workspace.rbegin(), workspace.rend(), [&](const Candidate &candidate) {
        return candidate.id == id && candidate.kind == kind;
      });
  if (existing != workspace.rend()) {
    existing->packed_score = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(existing->packed_score) +
        packed_score_delta);
    return true;
  }
  if (workspace.size() >= kSimulationLabAiWorkspace13Capacity)
    return false;
  workspace.push_back(
      {id, kind, static_cast<std::int32_t>(packed_score_delta | (kind & 0xFU))});
  return true;
}
} // namespace thandor
