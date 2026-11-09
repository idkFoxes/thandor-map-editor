#pragma once

#include "thandor/pioneer_site_bucket.hpp"
#include <cstdint>

namespace thandor {
// The three-way caller control-flow at AiUnitBehavior_UpdatePioneerVehicle:
// the query can reject a site, stop the entire Pioneer selector without a move,
// or admit this site to scoring. A false ARM333 chain result means it found a
// near anchor and instructs the caller to stop, not to score the site.
enum class PioneerSiteAdmission : std::uint8_t { SkipSite, StopSelector, ScoreSite };
struct PioneerSiteAdmissionResult {
    PioneerSiteAdmission disposition{};
    std::uint32_t bucket_count{};
};
// Mode3Query returns {succeeded,placement_count}. Mode0Query returns bool.
// Their order is observable: mode0 is not called when mode3 succeeds with
// count zero; mode0 is only queried for failed/nonzero mode3 probes.
template<class Mode3Query, class Mode0Query, class ReserveChain>
PioneerSiteAdmissionResult pioneer_site_admission(
    std::uint32_t separation_quantum_q12,
    Mode3Query&& mode3_query, Mode0Query&& mode0_query,
    ReserveChain&& reserve_chain) {
    const auto [mode3_ok, count] = mode3_query();
    const bool mode0_ok = (!mode3_ok || count != 0U) ? mode0_query() : false;
    const auto bucket = pioneer_site_bucket_count(mode3_ok,count,mode0_ok,separation_quantum_q12);
    if (!bucket.query_succeeded) return {PioneerSiteAdmission::SkipSite,0U};
    if (bucket.bucket_count == 0U) return {PioneerSiteAdmission::StopSelector,0U};
    if (bucket.bucket_count < 5U && !reserve_chain())
        return {PioneerSiteAdmission::StopSelector,bucket.bucket_count};
    return {PioneerSiteAdmission::ScoreSite,bucket.bucket_count};
}
} // namespace thandor
