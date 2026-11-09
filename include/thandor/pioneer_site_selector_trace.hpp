#pragma once

#include "thandor/pioneer_movement_update.hpp"
#include <cstdint>

namespace thandor {
// Observation of the ACTUAL SimLab class-18 selector invocation, not the
// delayed economy-phase workspace-08 snapshot. Catalog-backed level runs also
// expose the source hostile-distance and mode-3/0/ARM333 admission branches;
// metadata-only compatibility runs leave retail_admission_evaluated false.
struct PioneerSiteSelectorTrace {
    std::uint32_t calls{};
    std::uint32_t last_tick{};
    std::uint32_t last_candidates{};
    std::uint32_t last_scores_evaluated{};
    std::uint32_t last_positive_scores{};
    std::int32_t last_best_score{};
    MovementPoint last_best_point{};
    bool last_best_exists{};
    std::uint32_t empty_workspace_calls{};
    std::uint32_t zero_positive_score_calls{};
    std::uint32_t last_unseen_distance_rejections{};
    std::uint32_t last_visible_distance_rejections{};
    std::uint32_t last_placement_skips{};
    std::uint32_t last_selector_stops{};
    std::uint32_t last_admitted_for_scoring{};
    std::uint32_t last_temporary_arm333_probes{};
    // True only when the selector had the catalog-backed runtime collision,
    // terrain, owner-list, mode-1 and temporary ARM333 inputs available.
    bool retail_admission_evaluated{};

    void begin(std::uint32_t tick, std::uint32_t candidates) noexcept {
        ++calls;
        last_tick = tick;
        last_candidates = candidates;
        last_scores_evaluated = 0;
        last_positive_scores = 0;
        last_best_score = 0;
        last_best_point = {};
        last_best_exists = false;
        last_unseen_distance_rejections = 0;
        last_visible_distance_rejections = 0;
        last_placement_skips = 0;
        last_selector_stops = 0;
        last_admitted_for_scoring = 0;
        last_temporary_arm333_probes = 0;
        retail_admission_evaluated = false;
        if (candidates == 0) ++empty_workspace_calls;
    }
    void observe(std::int32_t score, MovementPoint point) noexcept {
        ++last_scores_evaluated;
        if (score <= 0) return;
        ++last_positive_scores;
        // Retail comparator is strictly greater (ties preserve first seen).
        if (!last_best_exists || score > last_best_score) {
            last_best_score = score;
            last_best_point = point;
            last_best_exists = true;
        }
    }
    void finish() noexcept {
        if (!last_best_exists) ++zero_positive_score_calls;
    }
};
} // namespace thandor
