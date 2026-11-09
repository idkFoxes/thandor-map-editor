#pragma once

#include <cstdint>

namespace thandor {
// Retail AiPlanningRebuild_CollectProducibleAssets and
// AiPurchaseCandidate_HasEligibleProducer (Open Thandor
// gameplay/ai/perception.cpp and gameplay/ai/purchasing.cpp).
// This models the *purchase workspace* only. Terrain suitability and
// factory/pad occupancy must still be evaluated independently.
// Stock class-13 producers: Light weapons factory (ARM301), Armour factory
// (302), Walker factory (303), Glider factory (304), Harbour (305).
// Airport (306) is class 22; Headquarters (300) is class 11. These runtime
// classes and producer masks are separate from technology categories A/B/C/-.
enum class AiV601ProducerKind : std::uint8_t { None, Class11, Class22, Class13 };

constexpr AiV601ProducerKind ai_v601_producer_kind(std::uint32_t flags) noexcept {
    // Ordering is significant: retail uses if / else if / else if.
    if ((flags & 0x10U) != 0U) return AiV601ProducerKind::Class11;
    if ((flags & 0x08U) != 0U) return AiV601ProducerKind::Class22;
    if ((flags & 0xEEU) != 0U) return AiV601ProducerKind::Class13;
    return AiV601ProducerKind::None;
}

constexpr bool ai_v601_workspace11_candidate(
    std::uint32_t army_asset_id, std::uint32_t army_flags,
    std::uint32_t live_producer_mask, bool baseline_technology_unlocked) noexcept {
    return (army_flags & 1U) != 0U && baseline_technology_unlocked &&
           (army_flags & live_producer_mask) != 0U &&
           (army_asset_id < 300U || army_asset_id >= 340U);
}

constexpr bool ai_v601_producer_accepts(
    std::uint32_t producer_runtime_class, std::uint32_t class13_factory_mask,
    std::uint32_t asset_flags) noexcept {
    switch (ai_v601_producer_kind(asset_flags)) {
        case AiV601ProducerKind::Class11: return producer_runtime_class == 11U;
        case AiV601ProducerKind::Class22: return producer_runtime_class == 22U;
        case AiV601ProducerKind::Class13:
            return producer_runtime_class == 13U &&
                   (class13_factory_mask & asset_flags & 0xEEU) != 0U;
        default: return false;
    }
}
} // namespace thandor
