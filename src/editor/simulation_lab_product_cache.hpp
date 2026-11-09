#pragma once

#include "thandor/simulation_lab.hpp"

#include <array>
#include <cstdint>
#include <vector>
#include <string>
#include <string_view>

namespace editor {
// Prepared ARM production templates depend on the scanned, immutable AssetCatalog
// and the faction technology masks, but NOT on the simulation seed, tick, run
// count, stock balances or mutable entity state. Keep an owning copy across
// restarts so the costly retail hierarchy resolution is not repeated.
// Level-scoped MDL/ARM registries participate in the key as well as masks.
class SimulationLabProductCache {
public:
    using Masks = std::array<thandor::FactionTechnologyMask256,8>;

    bool restore(const Masks& masks,std::vector<thandor::SimulationLabProductionAsset>& out,std::string_view registry_key={}) {
        if(!valid_ || masks_!=masks || registry_key_!=registry_key){++misses_;return false;}
        // The experiment is allowed to mutate its copy of the templates.
        // Never hand ownership of our cache to the worker.
        out=products_;
        ++hits_;
        return true;
    }
    void store(const Masks& masks,const std::vector<thandor::SimulationLabProductionAsset>& products,std::string_view registry_key={}) {
        // Populate only after successful preparation. An exception leaves the
        // last complete cached snapshot intact and cannot produce partial data.
        auto prepared=products;
        std::string prepared_key(registry_key);
        products_.swap(prepared);
        masks_=masks;
        registry_key_.swap(prepared_key);
        valid_=true;
    }
    void clear() noexcept {products_.clear();valid_=false;}
    [[nodiscard]] std::uint64_t hits() const noexcept {return hits_;}
    [[nodiscard]] std::uint64_t misses() const noexcept {return misses_;}
    [[nodiscard]] std::size_t assets() const noexcept {return products_.size();}
private:
    Masks masks_{};
    std::string registry_key_;
    std::vector<thandor::SimulationLabProductionAsset> products_;
    bool valid_{};
    std::uint64_t hits_{},misses_{};
};
} // namespace editor
