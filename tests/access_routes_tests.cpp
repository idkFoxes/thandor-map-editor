#include "thandor/access_routes.hpp"
#include "thandor/field.hpp"
#include "thandor/fog_preview.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

int main() try {
    using namespace thandor;
    const auto require = [](bool pass, const char* message) {
        if (!pass) throw std::runtime_error(message);
    };
    auto field = FieldGrid::create(33, 33, 0, 0);
    const auto point = [&](unsigned x, unsigned y) -> MovementPoint {
        const auto& cell = field.at(x, y);
        return {cell.world_x_q12, cell.world_y_q12};
    };
    for (unsigned y = 0; y < field.height(); ++y)
        if (y != 16) field.at(16, y).water_delta_q12 = 4096;

    const std::array<std::optional<AccessRouteBase>, 4> bases{
        AccessRouteBase{point(4, 5)}, AccessRouteBase{point(4, 26)},
        AccessRouteBase{point(27, 5)}, AccessRouteBase{point(27, 26)},
    };
    const std::array exits{
        AccessRouteFactory{point(30, 15), point(29, 15), {}},
        AccessRouteFactory{point(15, 2), point(16, 2), {}},
    };
    const auto at = [&](const AccessRoutes& routes, unsigned x, unsigned y) {
        return routes.cells[y * field.width() + x];
    };
    constexpr uint32_t ground_band = 4;
    const auto routes = plan_access_routes(field, ground_band, bases, exits, {});
    require(routes.base_networks == 1 && routes.connected_bases == 4 && routes.connected_pairs == 6,
        "the four bases should share one route network");
    require(at(routes, 16, 16) == AccessRouteCell::Main,
        "the main routes must share the only river crossing");
    for (unsigned y = 0; y < field.height(); ++y)
        if (y != 16) require(at(routes, 16, y) == AccessRouteCell::None,
            "a route crosses deep water");
    require(routes.connected_factories == 1 && routes.production_lanes.size() == 1,
        "only the usable factory exit should connect to the main routes");

    const std::array obstacles{AccessRouteObstacle{point(16, 16), 4096}};
    const auto obstructed = plan_access_routes(field, ground_band, bases, exits, obstacles);
    require(obstructed.base_networks == 2 && obstructed.connected_pairs == 2 &&
        at(obstructed, 16, 16) == AccessRouteCell::None,
        "a placed object blocking the only crossing must split the network");

    field.at(16, 16).water_delta_q12 = 4096;
    const auto separated = plan_access_routes(field, ground_band, bases, exits, obstacles);
    require(separated.base_networks == 2 && separated.connected_bases == 4 && separated.connected_pairs == 2,
        "deep water at the bridge should leave two independent base networks");
    require(at(separated, 16, 16) == AccessRouteCell::None,
        "a route crosses deep water at the bridge");
    const auto stranded = plan_access_routes(field, ground_band, std::span(bases).first<2>(), exits, obstacles);
    require(stranded.connected_factories == 0,
        "a factory on the opposite side of a barrier should remain unconnected");
    require(stranded.production_lanes.size() == 1 && stranded.production_lanes.front().runout.has_value(),
        "an isolated factory should still show its clear forward exit");
    auto open_field = FieldGrid::create(49, 49, 0, 0);
    const auto open_point = [&](unsigned x, unsigned y) -> MovementPoint {
        const auto& cell = open_field.at(x, y);
        return {cell.world_x_q12, cell.world_y_q12};
    };
    const std::array<std::optional<AccessRouteBase>, 3> triangle{
        AccessRouteBase{open_point(4, 4)}, AccessRouteBase{open_point(4, 40)}, AccessRouteBase{open_point(40, 4)},
    };
    const auto direct = plan_access_routes(open_field, ground_band, triangle, {}, {});
    require(direct.connected_pairs == 3 && direct.cells[22 * open_field.width() + 22] == AccessRouteCell::Main,
        "the distant pair should get a direct route instead of detouring through the third base");
    constexpr int32_t headquarters_radius = 5120;
    const auto west = open_point(10, 23);
    const auto east = open_point(38, 23);
    const std::array<std::optional<AccessRouteBase>, 2> headquarters{
        AccessRouteBase{west, headquarters_radius}, AccessRouteBase{east, headquarters_radius},
    };
    const std::array buildings{
        AccessRouteObstacle{west, headquarters_radius}, AccessRouteObstacle{east, headquarters_radius},
    };
    const auto factory_cell = open_point(25, 33);
    const MovementPoint factory_exit{factory_cell.x + 60, factory_cell.y - 100};
    const std::array factory_exits{AccessRouteFactory{open_point(25, 34), factory_exit, {}}};
    const auto building_routes = plan_access_routes(open_field, ground_band, headquarters, factory_exits, buildings);
    const auto lead_for_building = [&](MovementPoint center, bool east_facing) {
        return std::any_of(building_routes.leads.begin(), building_routes.leads.end(), [&](const auto& lead) {
            const auto dx = lead.endpoint.x - center.x;
            const auto dy = lead.endpoint.y - center.y;
            return lead.kind == AccessRouteCell::Main && (east_facing ? dx > 0 : dx < 0) &&
                std::abs(std::hypot(dx, dy) - (headquarters_radius + 1500 + 2)) < 1.0;
        });
    };
    require(building_routes.connected_pairs == 1 && lead_for_building(west, true) && lead_for_building(east, false),
        "base routes should touch the facing edges of both headquarters");
    const std::array detour_obstacles{
        buildings[0], buildings[1], AccessRouteObstacle{open_point(24, 23), headquarters_radius},
    };
    const auto detour = plan_access_routes(open_field, ground_band, headquarters, {}, detour_obstacles);
    require(detour.connected_pairs == 1 &&
        detour.cells[23 * open_field.width() + 24] == AccessRouteCell::None,
        "a route should detour around a solid obstacle");
    auto enclosed_headquarters = headquarters;
    constexpr int32_t sight_radius = 6 * 4096;
    enclosed_headquarters[0]->sight_radius_q12 = sight_radius;
    const std::array enclosure{
        buildings[0], buildings[1], AccessRouteObstacle{west, 4 * 4096},
    };
    const auto relocated = plan_access_routes(open_field, ground_band, enclosed_headquarters, {}, enclosure);
    require(relocated.connected_pairs == 1 && relocated.relocated_starts.size() == 1,
        "an enclosed headquarters should use a nearby clear start");
    const auto& fallback = open_field.cells()[relocated.relocated_starts.front()];
    const auto fallback_distance = std::hypot(fallback.world_x_q12 - west.x, fallback.world_y_q12 - west.y);
    require(fallback_distance > 4 * 4096 + 1500 && fallback_distance <= sight_radius,
        "the relocated start must clear the obstruction and stay inside headquarters sight range");
    enclosed_headquarters[0]->sight_radius_q12 = 3 * 4096;
    const auto out_of_sight = plan_access_routes(open_field, ground_band, enclosed_headquarters, {}, enclosure);
    require(out_of_sight.connected_pairs == 0 && out_of_sight.relocated_starts.empty(),
        "an enclosed headquarters must not start outside its sight range");
    const std::array fallback_exits{
        AccessRouteFactory{open_point(19, 23), open_point(20, 23), {}, 1},
        AccessRouteFactory{open_point(31, 23), open_point(30, 23), {}, 2},
    };
    const auto factory_starts = plan_access_routes(open_field, ground_band, enclosed_headquarters,
        fallback_exits, enclosure);
    require(factory_starts.connected_pairs == 1 && factory_starts.factory_start_count == 2 &&
        factory_starts.connected_factories == 2 && factory_starts.relocated_starts.empty(),
        "if headquarters cannot connect, the route should use both factory exits");
    const std::array<std::optional<AccessRouteBase>, 2> no_headquarters{};
    const auto factories_only = plan_access_routes(open_field, ground_band, no_headquarters, fallback_exits, {});
    require(factories_only.base_count == 2 && factories_only.connected_pairs == 1 &&
        factories_only.factory_start_count == 2,
        "factory exits alone should provide the faction starts");
    const auto headquarters_first = plan_access_routes(open_field, ground_band, headquarters,
        fallback_exits, buildings);
    require(headquarters_first.connected_pairs == 1 && headquarters_first.factory_start_count == 0,
        "a passable headquarters route should remain the main route");
    require(building_routes.production_lanes.size() == 1 &&
        building_routes.production_lanes.front().spawn.y == factory_exits.front().spawn.y,
        "the factory route should include the production lane from its authored spawn");
    const auto& lane = building_routes.production_lanes.front();
    require(lane.runout && lane.runout->y > factory_exit.y && lane.exit.x == factory_exit.x &&
        lane.exit.y == factory_exit.y, "the factory route should continue forward from its authored exit");
    require(std::any_of(building_routes.leads.begin(), building_routes.leads.end(), [&](const auto& lead) {
        return lead.kind == AccessRouteCell::Factory && lead.endpoint.x == lane.runout->x &&
            lead.endpoint.y == lane.runout->y;
    }), "the factory route should join the network after the forward runout");
    const std::array blocked_apron{
        buildings[0], buildings[1], AccessRouteObstacle{open_point(25, 31), 2000},
    };
    const auto no_runout = plan_access_routes(open_field, ground_band, headquarters, factory_exits, blocked_apron);
    require(no_runout.production_lanes.size() == 1 && no_runout.connected_factories == 0,
        "a blocked runout must not connect by turning directly at the factory door");

    auto resource_field = FieldGrid::create(33, 33, 0, 0);
    for (unsigned y = 0; y < resource_field.height(); ++y)
        if (y != 9 && y != 24) resource_field.at(16, y).water_delta_q12 = 4096;
    resource_field.at(26, 15).flags_and_material |= 0x0800U;
    resource_field.at(27, 15).flags_and_material |= 0x0800U;
    resource_field.at(26, 15).flags_and_material |= 0x1000U;
    resource_field.at(16, 29).flags_and_material |= 0x1000U;
    const std::array<std::optional<AccessRouteBase>, 3> resource_bases{bases[0], bases[2], std::nullopt};
    const auto resource_routes = plan_access_routes(resource_field, ground_band, resource_bases, {}, {}, 1500,
        {.bases = false, .resources = true, .alternatives = true});
    require(resource_routes.resource_deposits.size() == 3 && resource_routes.edges.empty(),
        "adjacent cells should form one deposit per resource type and the resource-only mode should omit base routes");
    require(resource_routes.resource_deposits[0].reachable_factions == 3 &&
        resource_routes.resource_deposits[1].reachable_factions == 3 &&
        resource_routes.resource_deposits[2].reachable_factions == 0,
        "both placed factions should reach both usable deposits and the submerged deposit should stay unreachable");
    const auto crosses = [&](const ResourceRouteEdge& edge, unsigned y) {
        const auto crossing = y * resource_field.width() + 16;
        return edge.first == crossing || edge.second == crossing;
    };
    require(std::any_of(resource_routes.resource_edges.begin(), resource_routes.resource_edges.end(),
        [&](const auto& edge) { return (edge.factions & 1U) && crosses(edge, 9); }),
        "the primary resource route should use the nearer crossing");
    require(std::any_of(resource_routes.resource_edges.begin(), resource_routes.resource_edges.end(),
        [&](const auto& edge) { return (edge.alternate_factions & 1U) && crosses(edge, 24); }),
        "the alternate resource route should use the second crossing");
    const auto combined = plan_access_routes(resource_field, ground_band, resource_bases, {}, {}, 1500,
        {.bases = true, .resources = true, .alternatives = true});
    require(combined.connected_pairs == 1 && combined.alternate_base_pairs == 1 &&
        combined.resource_edges.size() == resource_routes.resource_edges.size(),
        "combined mode should keep the resource paths and find both routes between factions");

    auto corridor = FieldGrid::create(49, 49, 0, 0);
    for (unsigned y = 0; y < corridor.height(); ++y)
        for (unsigned x = 0; x < corridor.width(); ++x)
            if (y < 9 || y > 23) corridor.at(x, y).water_delta_q12 = 4096;
    const std::array<std::optional<AccessRouteBase>, 2> corridor_starts{
        AccessRouteBase{open_point(5, 10)}, AccessRouteBase{open_point(43, 10)},
    };
    const auto centered = plan_access_routes(corridor, ground_band, corridor_starts, {}, {});
    const auto middle_row = [](const AccessRoutes& plan) {
        for (unsigned y = 1; y < 48; ++y)
            if (plan.cells[y * 49 + 24] == AccessRouteCell::Main) return y;
        return 0U;
    };
    require(centered.connected_pairs == 1 && middle_row(centered) >= 13 && middle_row(centered) <= 19,
        "a wide corridor should be routed away from the water's edge");
    require(std::find(centered.base_corner_trim.begin(), centered.base_corner_trim.end(), 0.5F) !=
        centered.base_corner_trim.end(), "a clear corridor should allow full-width curve joins");

    auto hillside = FieldGrid::create(49, 49, 0, 0);
    for (unsigned y = 16; y < hillside.height(); ++y)
        for (unsigned x = 0; x < hillside.width(); ++x)
            hillside.at(x, y).terrain_height_q12 = static_cast<int32_t>((y - 15) * 700U);
    const std::array<std::optional<AccessRouteBase>, 2> hillside_starts{
        AccessRouteBase{open_point(5, 21)}, AccessRouteBase{open_point(43, 21)},
    };
    require(!hillside.terrain_forbidden_for_traversal_band(24, 21, ground_band),
        "the hillside must remain drivable for the route preference test");
    const auto flatter = plan_access_routes(hillside, ground_band, hillside_starts, {}, {});
    require(flatter.connected_pairs == 1 && middle_row(flatter) > 0 && middle_row(flatter) < 16,
        "a drivable slope should give way to a nearby flat route");

    auto causeway = FieldGrid::create(49, 49, 0, 0);
    for (unsigned x = 20; x <= 26; ++x)
        for (unsigned y = 1; y < 48; ++y)
            if (y < 14 || y > 18) causeway.at(x, y).water_delta_q12 = 4096;
    const std::array<std::optional<AccessRouteBase>, 2> crossing_starts{
        AccessRouteBase{open_point(6, 16)}, AccessRouteBase{open_point(42, 16)},
    };
    AccessRouteDefencePlacement placement{.wall_radius_q12 = 1100, .turret_radius_q12 = 850,
        .existing_barriers = {}};
    const auto sites = plan_access_routes(causeway, ground_band, crossing_starts, {}, {}, 1500,
        {.defences = true}, placement);
    require(!sites.wall_sites.empty() && !sites.defence_sites.empty() && !sites.rally_sites.empty(),
        "a contested causeway should have wall, turret and rally sites");
    for (const auto& wall : sites.wall_sites) {
        require(sites.cells[wall.crossing] == AccessRouteCell::Main,
            "a wall row must cross a planned route");
        for (const auto cell : wall.cells)
            require(causeway.cells()[cell].water_delta_q12 <= 0, "a wall must stay on dry ground");
    }
    std::vector<AccessRouteObstacle> closed_causeway;
    for (const auto cell : sites.wall_sites.front().cells) {
        const auto& point = causeway.cells()[cell];
        closed_causeway.push_back({{point.world_x_q12, point.world_y_q12}, placement.wall_radius_q12});
    }
    require(plan_access_routes(causeway, ground_band, crossing_starts, {}, closed_causeway).connected_pairs == 0,
        "the proposed wall row should close the causeway to opposing vehicles");
    for (const auto cell : sites.defence_sites)
        require(sites.cells[cell] == AccessRouteCell::None, "a turret must stay off the traffic route");
    placement.allied_groups = (1U << 1U) | (1U << 2U);
    const auto allies = plan_access_routes(causeway, ground_band, crossing_starts, {}, {}, 1500,
        {.defences = true}, placement);
    require(!allies.wall_sites.empty() && allies.defence_sites.empty() && allies.rally_sites.empty(),
        "overlap between allied factions should not produce combat or rally sites");
    placement.allied_groups = 0;
    const auto exposed = plan_access_routes(open_field, ground_band, crossing_starts, {}, {}, 1500,
        {.defences = true}, placement);
    require(exposed.wall_sites.empty() && exposed.defence_sites.empty() && exposed.rally_sites.empty(),
        "a contested route through open ground should not get defensive markers");

    auto mountain_pass = FieldGrid::create(49, 49, 0, 0);
    for (int y = 0; y < 49; ++y)
        for (unsigned x = 0; x < 49; ++x)
            mountain_pass.at(x, y).terrain_height_q12 = std::max({0, 13 - y, y - 19}) * 3800;
    const auto mountain_sites = plan_access_routes(mountain_pass, ground_band, crossing_starts, {}, {}, 1500,
        {.defences = true}, placement);
    require(!mountain_sites.wall_sites.empty() && !mountain_sites.defence_sites.empty(),
        "a drivable pass between steep slopes should allow walls and off-road turrets");

    auto two_crossings = causeway;
    for (unsigned x = 20; x <= 26; ++x)
        for (unsigned y = 28; y <= 32; ++y)
            two_crossings.at(x, y).water_delta_q12 = FieldGrid::kDryWaterDeltaQ12;
    const auto primary_sites = plan_access_routes(two_crossings, ground_band, crossing_starts, {}, {}, 1500,
        {.defences = true}, placement);
    const auto alternate_sites = plan_access_routes(two_crossings, ground_band, crossing_starts, {}, {}, 1500,
        {.alternatives = true, .defences = true}, placement);
    const auto has_second_wall = [](const AccessRoutes& plan) {
        return std::any_of(plan.wall_sites.begin(), plan.wall_sites.end(),
            [](const auto& wall) { return wall.crossing / 49 >= 28; });
    };
    require(!has_second_wall(primary_sites) && has_second_wall(alternate_sites),
        "enabling alternate base routes should find a wall row at the second causeway");
    const std::array<std::optional<AccessRouteBase>, 2> short_crossing_starts{
        AccessRouteBase{open_point(16, 16)}, AccessRouteBase{open_point(31, 16)},
    };
    const auto long_backup = plan_access_routes(two_crossings, ground_band, short_crossing_starts, {}, {}, 1500,
        {.alternatives = true});
    require(long_backup.alternate_base_pairs == 1 &&
        std::any_of(long_backup.alternate_base_edges.begin(), long_backup.alternate_base_edges.end(), [](const auto& edge) {
            return edge.first / 49 >= 28 && edge.first % 49 >= 20 && edge.first % 49 <= 26;
        }), "a blocked crossing should allow a longer backup by the far causeway");
    require(std::all_of(alternate_sites.alternate_base_edges.begin(), alternate_sites.alternate_base_edges.end(),
        [](const auto& edge) { return edge.factions == 3U; }),
        "alternate base routes must carry both factions for contested site analysis");
    const auto by_second_approach = [](auto cell) { return cell / 49 >= 24; };
    require(std::none_of(primary_sites.defence_sites.begin(), primary_sites.defence_sites.end(), by_second_approach) &&
        std::any_of(alternate_sites.defence_sites.begin(), alternate_sites.defence_sites.end(), by_second_approach),
        "the second causeway should also support a turret site beside an alternate enemy approach");

    auto divided = FieldGrid::create(49, 49, 0, 0);
    for (unsigned y = 0; y < 49; ++y) divided.at(24, y).water_delta_q12 = 4096;
    const std::array pioneer_starts{AccessRoutePioneerStart{open_point(36, 23), 1}};
    placement.pioneers = pioneer_starts;
    placement.radar = {850, 0};
    placement.headquarters = {5120, 0};
    placement.factory = {3520, 0};
    placement.radar_visibility_radius_q12 = 35000;
    placement.radar_reference_height_q12 = 3072;
    const auto planning = plan_access_routes(divided, ground_band, std::span(crossing_starts).first(1), {}, {}, 1500,
        {.bases = false, .radar_sites = true, .alternate_bases = true}, placement);
    require(!planning.radar_sites.empty() && !planning.alternate_bases.empty(),
        "a large clear area reachable by the Pioneer should have radar and alternate base sites");
    const auto on_pioneer_side = [](const auto& site) { return site.cell % 49 > 24 && site.factions == 1U; };
    require(std::all_of(planning.radar_sites.begin(), planning.radar_sites.end(), on_pioneer_side) &&
        std::all_of(planning.alternate_bases.begin(), planning.alternate_bases.end(), on_pioneer_side),
        "site recommendations must follow the placed Pioneer instead of a start across impassable water");

    const auto observer = open_point(24, 24);
    const std::array complete_sight{FogPreviewObserver{observer.x, observer.y, 16384, 0x240U * 255U,
        retail_self_faction_visibility_mask(1, 0x88U)}};
    placement.existing_observers = complete_sight;
    const auto already_visible = plan_access_routes(open_field, ground_band, std::span(crossing_starts).first(1),
        {}, {}, 1500, {.bases = false, .radar_sites = true}, placement);
    require(already_visible.radar_sites.empty(), "radars should add sight instead of covering only visible cells");
    placement.existing_observers = {};
    placement.pioneers = {};
    const auto wall_radars = plan_access_routes(two_crossings, ground_band, crossing_starts, {}, {}, 1500,
        {.alternatives = true, .radar_sites = true}, placement);
    require(!wall_radars.wall_sites.empty(), "the radar layer should analyze wall rows on its own");
    bool radar_near_wall = false;
    bool radar_near_turret = false;
    for (const auto& radar : wall_radars.radar_sites) {
        const auto& site = two_crossings.cells()[radar.cell];
        for (const auto cell : wall_radars.defence_sites) {
            const auto& turret = two_crossings.cells()[cell];
            radar_near_turret |= std::hypot(double(site.world_x_q12) - turret.world_x_q12,
                double(site.world_y_q12) - turret.world_y_q12) < 2.25 * 4096;
        }
        for (const auto& wall : wall_radars.wall_sites)
            for (const auto cell : wall.cells) {
                const auto& barrier = two_crossings.cells()[cell];
                const auto distance = std::hypot(double(site.world_x_q12) - barrier.world_x_q12,
                    double(site.world_y_q12) - barrier.world_y_q12);
                require(distance > placement.wall_radius_q12 + placement.radar.radius_q12,
                    "radar and wall footprints must fit together");
                radar_near_wall |= distance < 4.0 * 4096;
            }
    }
    require(radar_near_wall, "a useful radar position should be suggested beside a defended crossing");
    require(radar_near_turret, "the radar layer should consider usable ground at or beside turret sites");

    const auto staging = plan_access_routes(two_crossings, ground_band, crossing_starts, {}, {}, 1500,
        {.alternatives = true, .pioneer_sites = true}, placement);
    require(!staging.pioneer_sites.empty() && !staging.defence_sites.empty(),
        "the Pioneer layer should find staging positions near turrets even without the turret overlay");
    for (const auto& site : staging.pioneer_sites) {
        require(site.factions == 3U && staging.cells[site.cell] == AccessRouteCell::None,
            "Pioneer staging should be reachable and off the main traffic lane");
        require(std::find(staging.defence_sites.begin(), staging.defence_sites.end(), site.cell) ==
            staging.defence_sites.end(), "Pioneers must stage beside the turret footprint");
    }
    auto expansion_field = FieldGrid::create(73, 73, 0, 0);
    const auto expansion_point = [&](unsigned x, unsigned y) {
        const auto& cell = expansion_field.at(x, y);
        return MovementPoint{cell.world_x_q12, cell.world_y_q12};
    };
    const std::array<std::optional<AccessRouteBase>, 2> expansion_starts{
        AccessRouteBase{expansion_point(8, 36)}, AccessRouteBase{expansion_point(64, 36)},
    };
    const std::array extra_headquarters{expansion_point(25, 36)};
    placement.existing_headquarters = extra_headquarters;
    const auto balanced = plan_access_routes(expansion_field, ground_band, expansion_starts, {}, {}, 1500,
        {.bases = false, .alternate_bases = true}, placement);
    require(balanced.alternate_base_factions == 3U && !balanced.alternate_bases.empty(),
        "a wide open map should have a comparable expansion set");
    std::array<unsigned, 2> counts{};
    for (const auto& site : balanced.alternate_bases) {
        require(site.factions == 1U || site.factions == 2U, "each expansion site belongs to one faction");
        ++counts[site.factions - 1];
        const auto& cell = expansion_field.cells()[site.cell];
        const auto distant = [&](MovementPoint point) {
            return std::hypot(double(cell.world_x_q12) - point.x, double(cell.world_y_q12) - point.y) >= 10.0 * 4096;
        };
        require(distant(expansion_starts[0]->center) && distant(expansion_starts[1]->center) &&
            distant(extra_headquarters[0]), "expansions should stay clear of every existing headquarters and start");
    }
    require(counts[0] == counts[1], "both factions should get equal numbers of expansion sites");
    for (std::size_t i = 0; i < balanced.alternate_bases.size(); i += 2) {
        const auto travel = [](const auto& site) {
            const int dx = static_cast<int>(site.cell % 73) - (site.factions == 1U ? 8 : 64);
            const int dy = static_cast<int>(site.cell / 73) - 36;
            return (std::abs(dx) + std::abs(dy) + std::abs(dx + dy)) / 2;
        };
        const auto low = std::min(travel(balanced.alternate_bases[i]), travel(balanced.alternate_bases[i + 1]));
        const auto high = std::max(travel(balanced.alternate_bases[i]), travel(balanced.alternate_bases[i + 1]));
        require(high <= low + std::max(6, low / 3), "each expansion round should have comparable Pioneer travel");
    }
    for (unsigned y = 0; y < 73; ++y)
        for (unsigned x = 51; x < 73; ++x)
            if (x < 58 || x > 68 || y < 30 || y > 42) expansion_field.at(x, y).water_delta_q12 = 4096;
    const auto roomy_side = plan_access_routes(expansion_field, ground_band, std::span(expansion_starts).first(1),
        {}, {}, 1500, {.bases = false, .alternate_bases = true}, placement);
    const auto unequal = plan_access_routes(expansion_field, ground_band, expansion_starts, {}, {}, 1500,
        {.bases = false, .alternate_bases = true}, placement);
    require(!roomy_side.alternate_bases.empty() && unequal.alternate_base_factions == 3U && unequal.alternate_bases.empty(),
        "expansions should be withheld if only one faction has enough room");
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
