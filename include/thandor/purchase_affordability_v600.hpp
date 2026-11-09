#pragma once
#include <cstdint>

namespace thandor {
enum class AiPurchaseBlockReasonV600 : std::uint32_t {
    None, TemporaryBalance, StorageCeiling, UnknownStorage
};
// Diagnostic only. AiPurchasePlanner_ExecuteAffordableCandidates *stops* at
// the first candidate cost exceeding remaining planning Xenite, regardless of
// whether a cheaper candidate is behind it. Never bypass that stop here.
inline AiPurchaseBlockReasonV600 ai_purchase_block_reason_v600(
    std::uint32_t price_q4, std::uint32_t remaining_q4,
    bool storage_cap_known, std::uint32_t storage_cap_q4) noexcept {
    if (price_q4 <= remaining_q4) return AiPurchaseBlockReasonV600::None;
    if (!storage_cap_known) return AiPurchaseBlockReasonV600::UnknownStorage;
    return price_q4 > storage_cap_q4
        ? AiPurchaseBlockReasonV600::StorageCeiling
        : AiPurchaseBlockReasonV600::TemporaryBalance;
}
struct AiPurchaseBlockDiagnosticV600 {
    std::uint32_t calls{};
    std::uint32_t ceiling_blocks{};
    std::uint32_t balance_blocks{};
    std::uint32_t unknown_storage_blocks{};
    std::uint32_t last_tick{};
    std::uint32_t last_candidate_id{};
    std::uint32_t last_cost_q4{};
    std::uint32_t last_balance_q4{};
    std::uint32_t last_storage_cap_q4{};
    std::uint32_t last_cached{};
    AiPurchaseBlockReasonV600 last_reason{};
    void record(std::uint32_t tick, std::uint32_t id, std::uint32_t cost,
                std::uint32_t balance, bool known, std::uint32_t storage,
                bool cached) noexcept {
        last_reason=ai_purchase_block_reason_v600(cost,balance,known,storage);
        if (last_reason==AiPurchaseBlockReasonV600::None) return;
        ++calls;
        ceiling_blocks += last_reason==AiPurchaseBlockReasonV600::StorageCeiling;
        balance_blocks += last_reason==AiPurchaseBlockReasonV600::TemporaryBalance;
        unknown_storage_blocks += last_reason==AiPurchaseBlockReasonV600::UnknownStorage;
        last_tick=tick; last_candidate_id=id; last_cost_q4=cost;
        last_balance_q4=balance; last_storage_cap_q4=storage;
        last_cached=cached?1U:0U;
    }
};
} // namespace thandor
