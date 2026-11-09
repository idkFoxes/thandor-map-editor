#include "simulation_lab_product_cache.hpp"
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {
void require(bool success){if(!success)throw std::runtime_error("product cache regression");}
}
int main(){
    editor::SimulationLabProductCache cache;
    editor::SimulationLabProductCache::Masks masks{};
    std::vector<thandor::SimulationLabProductionAsset> out;
    require(!cache.restore(masks,out));
    require(cache.misses()==1&&cache.hits()==0);
    std::vector<thandor::SimulationLabProductionAsset> original(2);
    original[0].army_asset_id=123;
    original[1].army_asset_id=456;
    cache.store(masks,original);
    require(cache.assets()==2);
    // Neither the caller nor the experiment may mutate the retained version.
    original[0].army_asset_id=789;
    require(cache.restore(masks,out));
    require(out.size()==2&&out[0].army_asset_id==123&&out[1].army_asset_id==456);
    out[0].army_asset_id=5;
    require(cache.restore(masks,out)&&out[0].army_asset_id==123);
    masks[1][2]=0x80000000;
    require(!cache.restore(masks,out));
    require(out[0].army_asset_id==123);
    cache.store(masks,original);
    require(cache.restore(masks,out)&&out[0].army_asset_id==789);
    cache.store(masks,original,"arm/building08");
    require(cache.restore(masks,out,"arm/building08"));
    require(!cache.restore(masks,out,"arm/building24"));
    require(!cache.restore(masks,out));
    cache.clear();
    require(cache.assets()==0&&!cache.restore(masks,out));
    return 0;
}
