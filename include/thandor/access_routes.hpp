#pragma once

#include "thandor/pioneer_movement_update.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace thandor {
class FieldGrid;
class AssetCatalog;
class MapDocument;
struct FogPreviewObserver;

struct AccessRouteBase {
    MovementPoint center;
    int32_t footprint_radius_q12{};
    int32_t sight_radius_q12{};
};

struct AccessRouteObstacle {
    MovementPoint center;
    int32_t radius_q12;
};

struct AccessRouteFactory {
    MovementPoint spawn;
    MovementPoint exit;
    std::optional<MovementPoint> footprint_center;
    uint32_t faction_index{};
};

inline constexpr int access_route_curve_steps = 8;
MovementPoint access_route_curve_point(MovementPoint start, MovementPoint corner, MovementPoint end, double t);

enum class AccessRouteCell : uint8_t { None, Factory, Main };

struct AccessRouteOptions {
    bool bases{true};
    bool resources{};
    bool alternatives{};
    bool defences{};
    bool radar_sites{};
    bool pioneer_sites{};
    bool alternate_bases{};
};

struct AccessRoutePioneerStart {
    MovementPoint position;
    uint32_t faction_index;
};

struct AccessRouteBuildingFootprint {
    int32_t radius_q12{};
    int32_t contact_kind{};
};

struct AccessRouteDefencePlacement {
    int32_t wall_radius_q12{};
    int32_t wall_contact_kind{};
    int32_t turret_radius_q12{};
    int32_t turret_contact_kind{};
    std::span<const AccessRouteObstacle> existing_barriers;
    uint32_t allied_groups{};
    uint32_t truce_groups{};
    AccessRouteBuildingFootprint radar;
    AccessRouteBuildingFootprint headquarters;
    AccessRouteBuildingFootprint factory;
    int32_t radar_visibility_radius_q12{};
    int32_t radar_reference_height_q12{};
    std::span<const AccessRoutePioneerStart> pioneers;
    std::span<const FogPreviewObserver> existing_observers;
    std::span<const MovementPoint> existing_headquarters;
};

struct AccessRoutePlanningSite {
    std::size_t cell;
    uint8_t factions;
};

struct AccessRouteWallSite {
    std::size_t crossing;
    std::vector<std::size_t> cells;
};

struct AccessRouteEdge {
    std::size_t first;
    std::size_t second;
    AccessRouteCell kind;
    uint8_t factions{};
};

struct AccessRouteLead {
    std::size_t cell;
    MovementPoint endpoint;
    AccessRouteCell kind;
};

struct AccessRouteProductionLane {
    MovementPoint spawn;
    MovementPoint exit;
    std::optional<MovementPoint> runout;
};

struct ResourceRouteEdge {
    std::size_t first;
    std::size_t second;
    uint8_t factions{};
    uint8_t alternate_factions{};
};

struct ResourceRouteLead {
    MovementPoint first;
    MovementPoint second;
    uint8_t faction;
    bool alternate{};
};

struct ResourceRouteDeposit {
    std::size_t cell;
    bool tritium;
    uint8_t reachable_factions{};
};

struct AccessRoutes {
    std::vector<AccessRouteCell> cells;
    std::vector<AccessRouteEdge> edges;
    std::vector<AccessRouteLead> leads;
    std::vector<AccessRouteProductionLane> production_lanes;
    std::vector<std::size_t> relocated_starts;
    std::vector<float> base_corner_trim;
    std::vector<AccessRouteEdge> alternate_base_edges;
    std::vector<float> alternate_base_corner_trim;
    std::vector<ResourceRouteEdge> resource_edges;
    std::vector<float> resource_corner_trim;
    std::vector<float> alternate_resource_corner_trim;
    std::vector<ResourceRouteLead> resource_leads;
    std::vector<ResourceRouteDeposit> resource_deposits;
    std::vector<AccessRouteWallSite> wall_sites;
    std::vector<std::size_t> defence_sites;
    std::vector<std::size_t> rally_sites;
    std::vector<AccessRoutePlanningSite> radar_sites;
    std::vector<AccessRoutePlanningSite> pioneer_sites;
    std::vector<AccessRoutePlanningSite> alternate_bases;
    uint8_t alternate_base_factions{};
    std::size_t alternate_base_pairs{};
    std::size_t base_count{};
    std::size_t usable_base_count{};
    std::size_t factory_start_count{};
    std::size_t clear_factory_aprons{};
    std::size_t factory_count{};
    std::size_t connected_pairs{};
    std::size_t connected_bases{};
    std::size_t base_networks{};
    std::size_t connected_factories{};
};

AccessRoutes plan_access_routes(const FieldGrid& field, uint32_t traversal_band,
    std::span<const std::optional<AccessRouteBase>> bases, std::span<const AccessRouteFactory> factories,
    std::span<const AccessRouteObstacle> obstacles, int32_t vehicle_radius_q12 = 1500,
    AccessRouteOptions options = {}, AccessRouteDefencePlacement defence_placement = {});
AccessRoutes plan_map_access_routes(const AssetCatalog& catalog, const MapDocument& document,
    AccessRouteOptions options = {});
}
