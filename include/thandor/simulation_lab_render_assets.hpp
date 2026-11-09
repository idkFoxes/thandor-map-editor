#pragma once
#include "thandor/simulation_lab.hpp"
#include <algorithm>
#include <vector>
namespace thandor {
// Textures are resident per ARM/faction, not per entity instance. Keep the finite
// set resident through stable-index tombstones so deaths do not upload it again.
inline std::vector<uint64_t> simulation_lab_resident_render_assets(const SimulationLabRun& run){
 std::vector<uint64_t> keys;keys.reserve(run.entities.size());
 for(const auto& entity:run.entities)if(entity.synthetic)
  keys.push_back((uint64_t{entity.army_asset_id}<<32U)|entity.faction_index);
 std::sort(keys.begin(),keys.end());keys.erase(std::unique(keys.begin(),keys.end()),keys.end());return keys;
}
}
