#pragma once
#include <cstdint>

namespace thandor {
// Isolated decision kernel of AiPlacement_QueryReachableSiteBucketCount
// (recovered placement.cpp). Callers must obtain BOTH placement-mode query
// booleans and mode-3 count from a real placement implementation; this function
// does not substitute a reach-only test for mode 0/3 or reserve ARM333 models.
struct PioneerSiteBucketDecision {
    bool query_succeeded{};
    std::uint32_t bucket_count{};
};
constexpr PioneerSiteBucketDecision pioneer_site_bucket_count(
    bool mode3_succeeded, std::uint32_t mode3_count,
    bool mode0_succeeded, std::uint32_t separation_quantum_q12) noexcept {
    if (!mode3_succeeded && !mode0_succeeded) return {false, 0U};
    if (!mode3_succeeded || mode3_count == 0U || mode0_succeeded)
        return {true, 0U};
    // Stock KI[28] is positive (0x2000). Explicitly reject malformed external
    // data rather than divide by zero or claim a meaningful bucket count.
    if (separation_quantum_q12 == 0U) return {false, 0U};
    // Ceil without overflow in (count - 1 + quantum), for uint32 fields.
    return {true, 1U + (mode3_count - 1U) / separation_quantum_q12};
}
} // namespace thandor
