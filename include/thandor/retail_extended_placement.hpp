#pragma once
#include "thandor/pioneer_class14_placement.hpp"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace thandor {
struct RetailExtendedPrimary {
    MovementPoint point{};
    int32_t command_state{}; // Army +0x100, aliased targetClassShotDamage[0].
};
inline int32_t retail_extended_add(int32_t a, int32_t b) noexcept {
    return std::bit_cast<int32_t>(uint32_t(a)+uint32_t(b));
}
inline int32_t retail_extended_distance(MovementPoint a, MovementPoint b) noexcept {
    const auto absolute=[](uint32_t value) {
        const auto signed_value=std::bit_cast<int32_t>(value);
        return signed_value<0?std::bit_cast<int32_t>(0U-value):signed_value;
    };
    return retail_extended_add(absolute(uint32_t(a.x)-uint32_t(b.x)),
                               absolute(uint32_t(a.y)-uint32_t(b.y)));
}
// Native consumer for declared workspace00/02/03/09 contents. Candidates and callbacks
// retain insertion order; this search consumes no RNG and never runs connectivity flood.
template<class PlacementTest>
std::optional<size_t> retail_extended_placement_select(uint32_t cooldown, MovementPoint anchor,
    std::span<const MovementPoint> candidates, std::span<const RetailExtendedPrimary> primary,
    std::span<const MovementPoint> visible, std::span<const MovementPoint> unseen,
    PlacementTest&& can_place) {
    if(cooldown==0||candidates.empty())return std::nullopt;
    int32_t best_score=INT32_MAX;
    std::optional<size_t> best;
    const auto nearest=[](MovementPoint point,std::span<const MovementPoint> sites) {
        int32_t result=INT32_MAX;
        for(const auto site:sites)result=std::min(result,retail_extended_distance(point,site));
        return result;
    };
    for(size_t i=0;i<candidates.size();++i){
        const auto point=candidates[i];
        const int32_t anchor_distance=retail_extended_distance(anchor,point);
        if(anchor_distance>=best_score)continue;
        int32_t structure_distance=INT32_MAX;
        for(const auto& structure:primary)if(structure.command_state!=0)
            structure_distance=std::min(structure_distance,retail_extended_distance(point,structure.point));
        if(structure_distance<=8191)continue;
        int32_t site_distance=nearest(point,visible);
        if(site_distance<INT32_MAX){
            if(site_distance>20480)continue;
            site_distance=std::bit_cast<int32_t>(uint32_t(site_distance)*4U);
        }else{
            site_distance=nearest(point,unseen);
            if(site_distance>32768)continue;
            site_distance=std::bit_cast<int32_t>(uint32_t(site_distance)*2U);
        }
        const int32_t score=retail_extended_add(anchor_distance,site_distance);
        if(score<best_score&&can_place(i)){best=i;best_score=score;}
    }
    return best;
}
} // namespace thandor
