#pragma once
#include <string>

#include "thandor/simulation_lab.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace editor {
// Share immutable decoded MDL/SPR data while the asset root and LEV model
// registry key remain unchanged. Map environment and mutable run state are
// prepared separately. Asset reloads must clear this cache.
class SimulationLabRuntimeCatalogCache {
public:
    using Pointer=std::shared_ptr<const thandor::SimulationLabRuntimeCatalog>;

    template<class Builder>
    Pointer get_or_prepare(Builder&& builder) {
        if(catalog_){++hits_;return catalog_;}
        Pointer prepared=std::forward<Builder>(builder)();
        if(!prepared)throw std::runtime_error("Simulation Lab runtime catalog preparation returned null");
        // Publish only once the complete immutable catalog has been constructed.
        catalog_=std::move(prepared);
        ++misses_;
        return catalog_;
    }
    template<class Builder>
    Pointer get_or_prepare(const std::string& key,Builder&& builder) {
        if(key_!=key){catalog_.reset();key_=key;}
        return get_or_prepare(std::forward<Builder>(builder));
    }
    void clear() noexcept {catalog_.reset();}
    [[nodiscard]] std::uint64_t hits() const noexcept {return hits_;}
    [[nodiscard]] std::uint64_t misses() const noexcept {return misses_;}
    [[nodiscard]] bool populated() const noexcept {return static_cast<bool>(catalog_);}
private:
    Pointer catalog_{};
    std::string key_;
    std::uint64_t hits_{},misses_{};
};
static_assert(std::is_const_v<SimulationLabRuntimeCatalogCache::Pointer::element_type>);
} // namespace editor
