#pragma once

#include <cstddef>
#include <span>
#include <cstdint>
#include <unordered_set>

namespace editor {
// Only the world currently displayed by the editor may contribute synthetic
// assets to the active GPU texture atlas. Snapshot ownership stays with the
// worker: this helper reads immutable runs and does not touch simulation state.
// Deduplicate by asset/faction: every entity in one run shares its faction
// technology mask, so repeated instances require the same exact material set.
// Include terminal actors until runtime_removed: the selected Lab renderer only
// skips runtime_removed, and terminal nodes can still be submitted in the same
// snapshot. Dropping their materials early causes missing atlas layers after
// another entity changes the residency set.
// Returns distinct selected-run synthetic asset/faction pairs.
template <class Run,class Visitor>
std::size_t for_each_selected_lab_synthetic_asset(
    std::span<const Run> runs,std::size_t selected_index,Visitor&& visit){
    if(selected_index>=runs.size())return 0;
    const auto& run=runs[selected_index];
    std::size_t visited=0;
    std::unordered_set<std::uint64_t> visited_assets;
    for(const auto& entity:run.entities){
        if(!entity.synthetic||entity.runtime_removed||
           entity.faction_index==0U)continue;
        const std::size_t faction=entity.faction_index<run.faction_technology_masks.size()
            ?static_cast<std::size_t>(entity.faction_index):0U;
        // A missing faction mask is invalid; avoid reading past the live
        // research table rather than falling back to another faction's skin.
        if(faction==0U)continue;
        const std::uint64_t key=(std::uint64_t{entity.army_asset_id}<<8U)|entity.faction_index;
        if(!visited_assets.insert(key).second)continue;
        visit(run,entity,faction);
        ++visited;
    }
    return visited;
}
// Runtime MDL overrides are per-entity, not per ARM/faction. Both authored and
// synthetic actors can have them. Multiple actors with
// one ARM can have different persistent visual node trees; do not deduplicate them.
// Like catalog residency above, this visits the selected run only.
template <class Run,class Visitor>
std::size_t for_each_selected_lab_runtime_hierarchy(
    std::span<const Run> runs,std::size_t selected_index,Visitor&& visit){
    if(selected_index>=runs.size())return 0;
    const auto& run=runs[selected_index];
    std::size_t count=0;
    for(const auto& entity:run.entities){
        if(entity.runtime_removed||
           entity.faction_index==0U||!entity.runtime_model_hierarchy)continue;
        const auto faction=static_cast<std::size_t>(entity.faction_index);
        if(faction>=run.faction_technology_masks.size())continue;
        visit(run,entity,faction);
        ++count;
    }
    return count;
}
} // namespace editor
