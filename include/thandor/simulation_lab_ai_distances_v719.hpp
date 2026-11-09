#pragma once
// Reference: AiSecondaryWorkspace_GetMinimumManhattanDistanceToPoint,
// AiPrimaryWorkspace_GetMinimumManhattanDistanceToPoint,
// AiHostileWorkspace_GetNearestVisibleHostileDistance (ai/workspaces.cpp).
// Workspace 01 reads GameEntityRuntime::common.pathCoordinate{0,1}Q12;
// workspace 00 and 02 read model-root world translation instead.
#include "thandor/simulation.hpp"
#include "thandor/pioneer_movement_update.hpp"
#include "thandor/retail_contact.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace thandor {
inline MovementPoint lab_ai_model_point_v719(Vec3f point) noexcept {
    return {float_to_q12(point.x),float_to_q12(point.y)};
}
inline std::int64_t lab_ai_manhattan_v719(MovementPoint a,MovementPoint b) noexcept {
    return std::llabs(std::int64_t(a.x)-b.x)+std::llabs(std::int64_t(a.y)-b.y);
}
// Native AiSiteCandidate_IsNearClass13StructureMarker tests a radial
// FixedMath_Length2 distance below 0x2001, not Manhattan distance.
inline bool lab_ai_class13_marker_near_v719(MovementPoint site,MovementPoint marker) noexcept {
    const std::int64_t dx=std::int64_t(site.x)-marker.x;
    const std::int64_t dy=std::int64_t(site.y)-marker.y;
    const auto ax=std::uint64_t(dx<0?-dx:dx),ay=std::uint64_t(dy<0?-dy:dy);
    if(ax>std::numeric_limits<std::uint32_t>::max()||ay>std::numeric_limits<std::uint32_t>::max() ||
       ax*ax>std::numeric_limits<std::uint64_t>::max()-ay*ay)return false;
    return retail_uint64_sqrt(ax*ax+ay*ay) < 0x2001U;
}
inline std::int64_t lab_ai_workspace_distance_v719(
        unsigned workspace, MovementPoint model, MovementPoint path, MovementPoint point) noexcept {
    return lab_ai_manhattan_v719(workspace==1U?path:model,point);
}
} // namespace thandor
