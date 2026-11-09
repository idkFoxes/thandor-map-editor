#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace thandor {
// AiPlacement_ReserveMode3SiteCluster returns true on rejection. The small
// cluster branch deliberately uses the chain's REJECTED result as acceptance.
template<class Chain>
bool retail_special_site_cluster_rejected(bool mode3_accepted,uint32_t clearance,
    uint32_t quantum,Chain&& chain_rejected) {
    if(!mode3_accepted||quantum==0U)return true; // Native requires nonzero KI[28].
    const uint32_t buckets=((clearance-1U)+quantum)/quantum;
    if(buckets==0U)return true;
    return buckets<5U ? !chain_rejected() : false;
}
inline uint32_t retail_special_site_weight(uint32_t base,uint32_t assigned,
    uint32_t id,uint32_t tritium,uint32_t supplied,uint32_t unpowered) noexcept {
    const uint32_t divisor=assigned*2U+6U;
    if(!divisor)return 0U;
    uint32_t weight=(base*3U)/divisor;
    const auto scaled=std::bit_cast<int32_t>(tritium<<8U);
    if(id!=330U&&scaled!=0) {
        const auto demand=std::bit_cast<int32_t>(unpowered*2U+supplied);
        weight=uint32_t(int64_t(std::bit_cast<int32_t>(weight))*demand/scaled);
    }
    return weight;
}
struct RetailSpecialSiteCandidate {
    uint32_t id{},kind{},weight{};
    size_t site_index{};
};
// External queries remain injected: original workspace order, short-circuit
// distance gates and first acceptable site are preserved without asset I/O.
template<class Sites,class Cluster,class Distance,class Assigned>
std::optional<RetailSpecialSiteCandidate> retail_special_site_candidate(
    bool unit50_present,bool factory301_present,bool unit50_enabled,bool tech11,
    const Sites& sites,uint32_t base330,uint32_t base_other,uint32_t minimum_distance,
    uint32_t tritium,uint32_t supplied,uint32_t unpowered,
    Cluster&& cluster_rejected,Distance&& distance,Assigned&& assigned) {
    if(unit50_present||(factory301_present&&!unit50_enabled))return {};
    const uint32_t id=factory301_present?(tech11?50U:11U):301U;
    const uint32_t kind=id==11U?2U:0U;
    for(size_t i=0;i<sites.size();++i) {
        if(cluster_rejected(i))continue;
        const auto minimum=std::bit_cast<int32_t>(minimum_distance);
        if(minimum>distance(i,3U)||minimum>distance(i,2U))continue;
        const auto asset=sites[i].army_asset_id;
        return RetailSpecialSiteCandidate{id,kind,
            retail_special_site_weight(asset==330U?base330:base_other,assigned(asset),
                asset,tritium,supplied,unpowered),i};
    }
    return {};
}
}
