#pragma once
#include <span>
#include <utility>

namespace thandor {
// A native world job captures its current owner-list head and then advances
// through each node's live `next` link.  The compact Lab stores owners in
// vectors, so it first captures the equivalent newest-first order and applies
// this dispatch gate at visit time:
//  * a new head inserted after capture is not visited in the current job;
//  * the current owner is already in the callback and completes even if it
//    unlinks itself;
//  * a future owner unlinked before its turn is skipped.
//
// `captured_order` must not be extended while this helper runs.  `is_linked`
// is intentionally evaluated immediately before every callback.
template<class Owner,class IsLinked,class Visitor>
void simulation_lab_visit_captured_owner_chain(std::span<const Owner> captured_order,
                                                IsLinked&& is_linked,
                                                Visitor&& visitor) {
    for(const auto& owner:captured_order) {
        if(!is_linked(owner))continue;
        visitor(owner);
    }
}
} // namespace thandor
