#pragma once
#include "thandor/simulation_lab.hpp"
#include <chrono>
#include <algorithm>

namespace thandor {
// Gate BEFORE all-run entity scans, serialization, tellp and flush. Final snapshots bypass
// wall cadence once; immutable same-tick runs are skipped separately by the caller.
inline constexpr auto simulation_lab_log_interval = std::chrono::seconds(1);
inline bool simulation_lab_log_sample_due(bool force, bool initialized, bool final_transition,
    std::chrono::steady_clock::duration elapsed) noexcept {
    return force || !initialized || final_transition || elapsed >= simulation_lab_log_interval;
}
struct SimulationLabPlanSummary {
    std::size_t primary{}, secondary{};
    std::uint32_t oldest_primary_age_ticks{}, oldest_secondary_age_ticks{};
    std::uint64_t primary_placement_attempts{};
};
inline SimulationLabPlanSummary simulation_lab_plan_summary(const SimulationLabRun& run) noexcept {
    SimulationLabPlanSummary result;
    result.primary=run.pending_asset_requests.size();
    result.secondary=run.secondary_purchase_requests.size();
    for (const auto& request:run.pending_asset_requests) {
        result.oldest_primary_age_ticks=std::max(result.oldest_primary_age_ticks,
            run.tick>=request.created_tick ? run.tick-request.created_tick : 0U);
        result.primary_placement_attempts+=request.placement_attempts;
    }
    for (const auto& request:run.secondary_purchase_requests)
        result.oldest_secondary_age_ticks=std::max(result.oldest_secondary_age_ticks,
            run.tick>=request.created_tick ? run.tick-request.created_tick : 0U);
    return result;
}
}
