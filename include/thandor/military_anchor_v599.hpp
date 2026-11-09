#pragma once

// V599: source-recovered AiUnitBehavior_Compute*DistanceScore contracts.
// All dword arithmetic is x86 low-32 wrapping; the anchor scorer deliberately
// compares BOTH anchor coordinates with model X (retail 0x0053B... quirk).
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace thandor {

struct MilitarySiteV599 {
    std::int32_t x_q12{};
    std::int32_t y_q12{};
    std::int32_t score{};
};
struct MilitaryScoreParametersV599 {
    std::int32_t site_bias{};     // KI 0x23
    std::int32_t site_scale{};    // KI 0x27
    std::int32_t anchor_bias{};   // KI 0x33
    std::int32_t anchor_scale{};  // KI 0x37
    std::int32_t hostile_bias{};  // KI 0x43
    std::int32_t hostile_scale{}; // KI 0x47
};
// Original ArmyRuntime creation copies ARM +0x70/+0x74/+0x78 to
// runtime +0x80/+0x84/+0x88. Scorers consume those fields in that order.
struct MilitaryArmWeightsV602 {
    std::int32_t site{}, anchor{}, secondary{};
};
inline MilitaryArmWeightsV602 military_arm_weights_v602(
        std::uint32_t arm70, std::uint32_t arm74, std::uint32_t arm78) noexcept {
    return {std::bit_cast<std::int32_t>(arm70),std::bit_cast<std::int32_t>(arm74),
            std::bit_cast<std::int32_t>(arm78)};
}

struct MilitaryAnchorV599 {
    std::int32_t model_x_q12{}; // anchorYQ12, stored as model X by impact callback
    std::int32_t model_y_q12{}; // anchorXQ12, stored as model Y by impact callback
    std::uint32_t cooldown{};
};
struct MilitaryPositionQ12 { std::int32_t x{},y{}; };
struct MilitaryChoiceV599 {
    std::int32_t score{};
    std::uint32_t kind{}; // 0 collect, 1 general site, 2 alert anchor, 3 collect/group target
    std::size_t site_index{};
    std::int32_t best_site_score{};
    std::int32_t best_anchor_score{};
    std::int32_t best_hostile_score{};
};

inline std::int32_t military_wrap_v599(std::uint32_t value) noexcept {
    return std::bit_cast<std::int32_t>(value);
}
inline std::int32_t military_add_v599(std::int32_t a,std::int32_t b) noexcept {
    return military_wrap_v599(static_cast<std::uint32_t>(a)+static_cast<std::uint32_t>(b));
}
inline std::int32_t military_sub_v599(std::int32_t a,std::int32_t b) noexcept {
    return military_wrap_v599(static_cast<std::uint32_t>(a)-static_cast<std::uint32_t>(b));
}
inline std::int32_t military_mul_v599(std::int32_t a,std::int32_t b) noexcept {
    return military_wrap_v599(static_cast<std::uint32_t>(a)*static_cast<std::uint32_t>(b));
}
// The original x86 score helpers use signed SAR 12, *not* a logical SHR.
// Converting to uint32_t before shifting turns a negative wrapped site score
// into a very large positive score, causing incorrect military site orders.
inline std::int32_t military_sar12_v706(std::int32_t value) noexcept {
    return value >= 0 ? value / 4096 :
        static_cast<std::int32_t>(-((-static_cast<std::int64_t>(value) + 4095) / 4096));
}
inline std::int32_t military_negative_abs_v599(std::int32_t delta) noexcept {
    return delta>=0? military_sub_v599(0,delta):delta;
}
inline std::int32_t military_distance_term_v599(std::int32_t dx,std::int32_t dy,
                                                 std::int32_t bias) noexcept {
    const auto term=military_add_v599(
        military_add_v599(military_negative_abs_v599(dx),military_negative_abs_v599(dy)),bias);
    return term<0?0:term;
}
inline std::int32_t military_site_score_v599(
        std::int32_t x,std::int32_t y,const MilitarySiteV599& site,
        const MilitaryScoreParametersV599& params,std::int32_t weight) noexcept {
    const auto term=military_distance_term_v599(military_sub_v599(x,site.x_q12),
                                               military_sub_v599(y,site.y_q12),params.site_bias);
    const auto numerator=military_add_v599(military_mul_v599(term,params.site_scale),site.score);
    return military_mul_v599(military_sar12_v706(numerator),weight);
}
inline std::int32_t military_anchor_score_v599(
        std::int32_t model_x,const MilitaryAnchorV599& anchor,
        const MilitaryScoreParametersV599& params,std::int32_t weight) noexcept {
    if (anchor.cooldown==0) return 0;
    const auto term=military_distance_term_v599(military_sub_v599(model_x,anchor.model_x_q12),
                    military_sub_v599(model_x,anchor.model_y_q12),params.anchor_bias);
    return military_mul_v599(military_sar12_v706(military_mul_v599(term,params.anchor_scale)),weight);
}
inline std::int32_t military_secondary_score_v599(
        std::int32_t x,std::int32_t y,const MilitarySiteV599& target,
        const MilitaryScoreParametersV599& params,std::int32_t weight) noexcept {
    const auto term=military_distance_term_v599(military_sub_v599(x,target.x_q12),
                                               military_sub_v599(y,target.y_q12),params.hostile_bias);
    return military_mul_v599(military_sar12_v706(military_mul_v599(term,params.hostile_scale)),weight);
}
// Workspace03 is allocated zeroed once, then only its live prefix is
// overwritten. The secondary scorer reads ws03[2*i] at a 16-byte stride.
// Non-null model pointers are outside the stock map/distance-bias range and
// score zero; an untouched record supplies the real integer point (0,0).
inline bool military_unseen_zero_alias(std::size_t count,std::size_t& written_prefix) noexcept {
    written_prefix=std::max(written_prefix,count);
    const auto first_even_unwritten=(written_prefix+1U)&~std::size_t{1};
    return first_even_unwritten<512U&&first_even_unwritten/2U<count;
}
inline MilitaryChoiceV599 military_choose_anchor_action_v599(
        std::int32_t x,std::int32_t y,const MilitaryScoreParametersV599& params,
        std::int32_t site_weight,std::int32_t anchor_weight,std::int32_t hostile_weight,
        std::span<const MilitarySiteV599> sites,
        MilitaryAnchorV599 primary,MilitaryAnchorV599 secondary,
        std::span<const MilitarySiteV599> visible_targets,
        bool has_unseen_workspace03,
        std::optional<MilitaryPositionQ12> site_origin = {},
        bool unseen_zero_alias = false) noexcept {
    MilitaryChoiceV599 selection{};
    std::int32_t best_site=0,best_anchor=0,best_hostile=0;
    // Original 0x53B1F0/1F6 read ArmyRuntime +B8/+BC (stored path
    // destination). Anchor and hostile scorers still read the live model.
    const auto site_position=site_origin.value_or(MilitaryPositionQ12{x,y});
    for (std::size_t i=0;i<sites.size();++i) {
        const auto score=military_site_score_v599(site_position.x,site_position.y,sites[i],params,site_weight);
        if (score>best_site) best_site=score;
        if (score>selection.score) selection={score,1U,i}; // strict >, earlier site wins ties
    }
    for (const auto anchor:{primary,secondary}) {
        const auto score=military_anchor_score_v599(x,anchor,params,anchor_weight);
        if (score>best_anchor) best_anchor=score;
        if (score>selection.score) selection={score,2U,0U};
    }
    // In retail ws07 excludes hidden ws03. With no ws07, ws03 is misread as
    // pointer/dword coordinates at 16-byte stride; host pointers cannot be
    // substituted by fake world points. The caller must account for the skip.

    for (const auto& target:visible_targets) {
        const auto score=military_secondary_score_v599(x,y,target,params,hostile_weight);
        if (score>best_hostile) best_hostile=score;
        if (score>selection.score) selection={score,3U,0U};
    }
    if(visible_targets.empty()&&has_unseen_workspace03&&unseen_zero_alias) {
        auto score=military_secondary_score_v599(x,y,{0,0,0},params,hostile_weight);
        // Native IMUL 3 then signed SAR 2, including dword overflow.
        score=military_mul_v599(score,3)>>2;
        best_hostile=std::max(best_hostile,score);
        if(score>selection.score)selection={score,3U,0U};
    }
    selection.best_site_score=best_site;
    selection.best_anchor_score=best_anchor;
    selection.best_hostile_score=best_hostile;
    return selection;
}

inline bool military_impact_anchor_v599(MilitaryAnchorV599& anchor,
                                       std::int32_t hit_x,std::int32_t hit_y) noexcept {
    // The notification gate is separate from updating the anchor. Lab does not
    // render retail notification movies but retains its boolean result.
    const auto old=anchor.cooldown;
    const auto dx=military_negative_abs_v599(military_sub_v599(hit_x,anchor.model_x_q12));
    const auto dy=military_negative_abs_v599(military_sub_v599(hit_y,anchor.model_y_q12));
    const bool notify=old<50U && (old==0U ||
        military_sub_v599(0,military_add_v599(dx,dy))>0xC000);
    anchor={hit_x,hit_y,150U};
    return notify;
}

// A spawned class-13 child is still collision-linked to its producing parent
// until the factory exit move finishes. This is distinct from the generic
// class-23/22 linked-entity field and must exclude BOTH partner orientations.
inline bool military_factory_parent_pair_v599(
        std::size_t first, std::size_t second,
        bool first_linked,std::optional<std::size_t> first_parent,
        bool second_linked,std::optional<std::size_t> second_parent) noexcept {
    return (first_linked && first_parent==second) ||
           (second_linked && second_parent==first);
}

inline constexpr std::array<std::uint32_t,24> kMilitaryGroupClassScoresV599{{
    0,2048,4096,4096,512,0,0,0,0,0,384,256,0,128,1024,896,
    768,4096,6144,4096,256,4096,192,64}};

// The backing image retains entries beyond the current rebuilt prefix. Bound
// reads by both the native candidate count and the allocated workspace image.
inline std::optional<std::size_t> military_retained_unseen_group_target(
        std::span<const uint32_t> retained_classes,std::size_t current_count) noexcept {
    std::optional<std::size_t> best;uint32_t score=0;
    for(size_t i=0;i<current_count&&2U*i+1U<retained_classes.size();++i){
        const auto slot=2U*i+1U;const auto cls=retained_classes[slot];
        if(cls>=kMilitaryGroupClassScoresV599.size())continue;
        const auto candidate=kMilitaryGroupClassScoresV599[cls];
        if(candidate&&candidate>=score){score=candidate;best=slot;}
    }
    return best;
}

// Native ws03 contains 8-byte entries but the ws07 fallback reads at 16-byte
// strides. Consequently candidate i aliases ws03[2*i+1]. Only the initialized
// half can be safely reconstructed in the Lab: never read stale/uninitialized
// pointers or invent additional targets to fill the original candidate count.
// Returns the index into the original unseen-owner array, not its entity index.
inline std::optional<std::size_t> military_bounded_unseen_group_target_v705(
        std::span<const std::uint32_t> unseen_runtime_classes) noexcept {
    std::optional<std::size_t> best;
    std::uint32_t best_score=0;
    for (std::size_t slot=1;slot<unseen_runtime_classes.size();slot+=2) {
        const auto cls=unseen_runtime_classes[slot];
        if(cls>=kMilitaryGroupClassScoresV599.size())continue;
        const auto score=kMilitaryGroupClassScoresV599[cls];
        if(score==0)continue;
        // Original group-attack selector updates on >= (later candidate wins).
        if(score>=best_score) {best_score=score;best=slot;}
    }
    return best;
}
} // namespace thandor
