#include "simulation_lab_runtime_catalog_cache.hpp"
#include <cstdint>
#include "thandor/simulation_lab_runtime.hpp"
#include <memory>
#include <stdexcept>
#include <type_traits>

static void verify(bool condition) {if(!condition)throw std::runtime_error("runtime catalog cache regression");}
int main() {
    editor::SimulationLabRuntimeCatalogCache cache;
    int preparations=0;
    verify(!cache.populated()&&cache.hits()==0&&cache.misses()==0);
    const auto build=[&]() -> editor::SimulationLabRuntimeCatalogCache::Pointer {
        ++preparations;
        auto result=std::make_shared<thandor::SimulationLabRuntimeCatalog>();
        result->conversion_effect_lifetimes.emplace(123U,15U);
        return result;
    };
    const auto first=cache.get_or_prepare(build);
    verify(first&&first->conversion_effect_lifetimes.at(123U)==15U);
    verify(cache.populated()&&preparations==1&&cache.misses()==1&&cache.hits()==0);
    const auto again=cache.get_or_prepare(build);
    verify(again==first&&preparations==1&&cache.hits()==1);
    cache.clear();
    verify(!cache.populated());
    const auto after_clear=cache.get_or_prepare(build);
    verify(after_clear&&after_clear!=first&&preparations==2&&cache.misses()==2);
    // Builder exceptions must not publish an incomplete catalog.
    cache.clear();
    bool caught=false;
    try { (void)cache.get_or_prepare([]() -> editor::SimulationLabRuntimeCatalogCache::Pointer {
        throw std::runtime_error("decode failure");
    }); } catch(const std::runtime_error&) {caught=true;}
    verify(caught&&!cache.populated()&&cache.misses()==2);
    caught=false;
    try { (void)cache.get_or_prepare([]() -> editor::SimulationLabRuntimeCatalogCache::Pointer {return {};}); }
    catch(const std::runtime_error&) {caught=true;}
    verify(caught&&!cache.populated());
    const auto recovered=cache.get_or_prepare(build);
    verify(recovered&&preparations==3&&cache.misses()==3);
    const auto summer=cache.get_or_prepare("summer/model-registry",build);
    verify(summer!=recovered&&preparations==4);
    verify(cache.get_or_prepare("summer/model-registry",build)==summer&&preparations==4);
    const auto winter=cache.get_or_prepare("winter/model-registry",build);
    verify(winter!=summer&&preparations==5);
    // Readers of the previous immutable catalog remain valid across a map change.
    verify(summer->conversion_effect_lifetimes.at(123U)==15U);
}
