#include "thandor/access_routes.hpp"

#include "thandor/field.hpp"
#include "thandor/catalog.hpp"
#include "thandor/map_document.hpp"
#include "thandor/simulation_lab_geometry.hpp"
#include "thandor/simulation_lab_catalog.hpp"
#include "thandor/simulation_placement.hpp"
#include "thandor/fog_preview.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <functional>
#include <cmath>
#include <tuple>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace thandor {
namespace {

constexpr auto no_cell = std::numeric_limits<std::size_t>::max();
constexpr auto unreachable = std::numeric_limits<uint64_t>::max();
constexpr uint8_t closed_route_cell = 255;
constexpr std::array<std::pair<int, int>, 6> neighbors{{
    {1, 0}, {1, -1}, {0, -1}, {-1, 0}, {-1, 1}, {0, 1}
}};

struct RoutesGrid {
    explicit RoutesGrid(const FieldGrid& field, uint32_t traversal_band)
        : width(field.width()), height(field.height()), passable(field.cells().size()), blocked_edges(field.cells().size()),
          terrain_cost(field.cells().size()) {
        if (traversal_band >= 7U) return;
        const uint32_t forbidden = 0x80000000U | (0x01000000U << traversal_band);
        for (int y = 1; y + 1 < height; ++y)
            for (int x = 1; x + 1 < width; ++x) {
                const auto cell = index(x, y);
                const auto terrain = field.terrain_compatibility(x, y);
                passable[cell] = (terrain.classification_bits & forbidden) == 0;
                const auto tilt = std::max(0, 16384 - int(terrain.terrain_normal_elevation_turn16));
                terrain_cost[cell] = static_cast<uint8_t>(std::min(32, tilt / 180) + (terrain.water_covered ? 4 : 0));
            }
    }

    std::size_t index(int x, int y) const { return static_cast<std::size_t>(y) * width + x; }

    std::size_t neighbor_at(std::size_t cell, std::size_t direction, int distance = 1) const {
        const auto [dx, dy] = neighbors[direction];
        const int x = static_cast<int>(cell % width) + dx * distance;
        const int y = static_cast<int>(cell / width) + dy * distance;
        return x >= 0 && y >= 0 && x < width && y < height ? index(x, y) : no_cell;
    }

    template<class Visitor>
    void visit_neighbors(std::size_t cell, Visitor visit, bool check_obstacles = true) const {
        const int x = static_cast<int>(cell % width);
        const int y = static_cast<int>(cell / width);
        for (std::size_t direction = 0; direction < neighbors.size(); ++direction) {
            if (check_obstacles && (blocked_edges[cell] & (1U << direction))) continue;
            const auto [dx, dy] = neighbors[direction];
            const int nx = x + dx;
            const int ny = y + dy;
            if (nx >= 0 && ny >= 0 && nx < width && ny < height)
                visit(index(nx, ny));
        }
    }

    int width;
    int height;
    std::vector<bool> passable;
    std::vector<uint8_t> blocked_edges;
    std::vector<uint8_t> terrain_cost;
};

std::vector<uint32_t> route_step_costs(const RoutesGrid& grid) {
    constexpr std::array<uint8_t, 8> clearance_cost{28, 20, 14, 9, 5, 3, 1, 0};
    const auto size = grid.passable.size();
    std::vector<uint8_t> distance(size, clearance_cost.size() - 1);
    std::vector<std::size_t> frontier;
    for (std::size_t cell = 0; cell < size; ++cell) {
        if (!grid.passable[cell]) continue;
        unsigned exits = 0;
        grid.visit_neighbors(cell, [&](auto neighbor) { exits += grid.passable[neighbor]; });
        if (exits == neighbors.size()) continue;
        distance[cell] = 0;
        frontier.push_back(cell);
    }
    for (std::size_t i = 0; i < frontier.size(); ++i) {
        const auto cell = frontier[i];
        const auto next = static_cast<uint8_t>(distance[cell] + 1);
        grid.visit_neighbors(cell, [&](auto neighbor) {
            if (!grid.passable[neighbor] || distance[neighbor] <= next) return;
            distance[neighbor] = next;
            frontier.push_back(neighbor);
        });
    }
    std::vector<uint32_t> cost(size);
    for (std::size_t cell = 0; cell < size; ++cell)
        cost[cell] = 12U + clearance_cost[distance[cell]] + grid.terrain_cost[cell];
    return cost;
}

struct FactoryApron {
    std::size_t cell;
    std::size_t lane;
    MovementPoint exit;
    MovementPoint runout;
    double cost;
};

double factory_alignment(const FieldGrid& field, const FactoryApron& apron, std::size_t neighbor) {
    const auto& cell = field.cells()[apron.cell];
    const auto& step = field.cells()[neighbor];
    const double dx = double(cell.world_x_q12) - apron.exit.x;
    const double dy = double(cell.world_y_q12) - apron.exit.y;
    const double step_x = double(step.world_x_q12) - cell.world_x_q12;
    const double step_y = double(step.world_y_q12) - cell.world_y_q12;
    return (step_x * dx + step_y * dy) / (std::hypot(step_x, step_y) * std::hypot(dx, dy));
}

struct Entrance {
    std::size_t cell;
    uint64_t cost{};
    const FactoryApron* apron{};
};

struct Distances {
    std::vector<uint64_t> cost;
    std::vector<std::size_t> parent;
    std::size_t reached{no_cell};
    std::size_t arrival_from{no_cell};
};

std::size_t route_edge_key(std::size_t a, std::size_t b, std::size_t cell_count) {
    return std::min(a, b) * cell_count + std::max(a, b);
}

std::vector<uint8_t> alternate_route_cost(const RoutesGrid& grid,
                                           const std::unordered_set<std::size_t>& primary_edges) {
    const auto cell_count = grid.passable.size();
    std::vector<uint8_t> cost(cell_count);
    for (const auto edge : primary_edges) {
        cost[edge / cell_count] = 36;
        cost[edge % cell_count] = 36;
    }
    // Discourage tracing one cell beside the primary path, while allowing a
    // detour to use a shared bridge or doorway when it has to.
    for (const auto [from, to] : {std::pair{36U, 16U}, std::pair{16U, 6U}})
        for (std::size_t cell = 0; cell < cell_count; ++cell)
            if (cost[cell] == from)
                grid.visit_neighbors(cell, [&](auto neighbor) {
                    if (cost[neighbor] == 0) cost[neighbor] = static_cast<uint8_t>(to);
                });
    return cost;
}

std::vector<std::vector<std::size_t>> primary_route_crossings(const FieldGrid& field,
    const RoutesGrid& grid, std::span<const std::size_t> path) {
    struct Crossing { std::size_t position; std::vector<std::size_t> cells; };
    std::vector<Crossing> candidates;
    const auto cells = field.cells();
    const auto clearance = std::max<std::size_t>(3, path.size() / 7);
    for (std::size_t i = clearance; i + clearance < path.size(); ++i) {
        const auto center = path[i];
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto step = grid.neighbor_at(center, axis);
            if (step == no_cell) continue;
            const auto& a = cells[center];
            const double dx = double(cells[step].world_x_q12) - a.world_x_q12;
            const double dy = double(cells[step].world_y_q12) - a.world_y_q12;
            const auto side = [&](std::size_t cell) {
                return dx * (double(cells[cell].world_y_q12) - a.world_y_q12) -
                    dy * (double(cells[cell].world_x_q12) - a.world_x_q12);
            };
            if (side(path[i - 2]) * side(path[i + 2]) >= 0) continue;
            std::vector<std::size_t> crossing{center};
            const auto reaches_flank = [&](std::size_t direction) {
                for (int distance = 1; distance <= 12; ++distance) {
                    const auto cell = grid.neighbor_at(center, direction, distance);
                    if (cell == no_cell) return false;
                    if (!grid.passable[cell]) return true;
                    crossing.push_back(cell);
                }
                return false;
            };
            if (reaches_flank(axis) && reaches_flank(axis + 3) && crossing.size() <= 16)
                candidates.push_back({i, std::move(crossing)});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [&](const auto& a, const auto& b) {
        const auto midpoint = [&](std::size_t i) { return i > path.size()/2 ? i - path.size()/2 : path.size()/2 - i; };
        return std::pair{a.cells.size(), midpoint(a.position)} < std::pair{b.cells.size(), midpoint(b.position)};
    });
    std::vector<std::vector<std::size_t>> result;
    std::vector<std::size_t> selected;
    for (auto& candidate : candidates) {
        if (std::any_of(selected.begin(), selected.end(), [&](auto i) {
            return std::max(i, candidate.position) - std::min(i, candidate.position) < 5;
        })) continue;
        selected.push_back(candidate.position);
        result.push_back(std::move(candidate.cells));
        if (result.size() == 4) break;
    }
    return result;
}

Distances find_routes(const FieldGrid& field, const RoutesGrid& grid, std::span<const AccessRouteCell> routes,
                      std::span<const Entrance> starts, std::span<const Entrance> targets,
                      std::span<const uint32_t> step_cost,
                      std::span<const uint8_t> avoid = {}) {
    using Entry = std::pair<uint64_t, std::size_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    Distances result{std::vector<uint64_t>(routes.size(), unreachable),
                     std::vector<std::size_t>(routes.size(), no_cell)};
    std::vector<const FactoryApron*> departure, arrival;
    if (!starts.empty()) departure.resize(routes.size());
    if (!targets.empty()) arrival.resize(routes.size());
    const auto seed = [&](Entrance entrance) {
        if (entrance.cost >= result.cost[entrance.cell]) return;
        if (!departure.empty()) departure[entrance.cell] = entrance.apron;
        result.cost[entrance.cell] = entrance.cost;
        open.emplace(entrance.cost, entrance.cell);
    };
    if (starts.empty()) {
        for (std::size_t cell = 0; cell < routes.size(); ++cell)
            if (routes[cell] == AccessRouteCell::Main) seed({cell});
    } else {
        for (const auto entrance : starts) seed(entrance);
    }
    std::vector<uint64_t> target_cost(routes.size(), unreachable);
    for (const auto entrance : targets) {
        if (entrance.cost >= target_cost[entrance.cell]) continue;
        target_cost[entrance.cell] = entrance.cost;
        arrival[entrance.cell] = entrance.apron;
    }
    uint64_t best = unreachable;
    const auto reach_target = [&](std::size_t cell, std::size_t from, uint64_t cost) {
        if (target_cost[cell] == unreachable || cost + target_cost[cell] >= best) return;
        best = cost + target_cost[cell];
        result.reached = cell;
        result.arrival_from = from;
    };
    while (!open.empty()) {
        const auto [cost, cell] = open.top();
        open.pop();
        if (cost >= best) break;
        if (cost != result.cost[cell]) continue;
        if (!arrival.empty() && !arrival[cell]) reach_target(cell, no_cell, cost);
        grid.visit_neighbors(cell, [&](std::size_t neighbor) {
            if (!grid.passable[neighbor] || (!avoid.empty() && avoid[neighbor] == closed_route_cell)) return;
            if (!departure.empty() && result.parent[cell] == no_cell && departure[cell] &&
                factory_alignment(field, *departure[cell], neighbor) < -0.05) return;
            const auto shared_discount = routes[cell] == AccessRouteCell::Main && routes[neighbor] == AccessRouteCell::Main ? 3U : 0U;
            const auto movement_cost = (step_cost[cell] + step_cost[neighbor]) / 2U - shared_discount;
            const auto extra_cost = avoid.empty() ? 0U : avoid[neighbor];
            const auto next_cost = cost + movement_cost + extra_cost;
            if (!arrival.empty() && arrival[neighbor] &&
                factory_alignment(field, *arrival[neighbor], cell) >= -0.05)
                reach_target(neighbor, cell, next_cost);
            if (next_cost >= result.cost[neighbor]) return;
            result.cost[neighbor] = next_cost;
            result.parent[neighbor] = cell;
            open.emplace(next_cost, neighbor);
        });
    }
    return result;
}

double squared_distance(const FieldCell& cell, MovementPoint point) {
    const double dx = double(cell.world_x_q12) - point.x;
    const double dy = double(cell.world_y_q12) - point.y;
    return dx * dx + dy * dy;
}

MovementPoint cell_point(const FieldCell& cell) {
    return {cell.world_x_q12, cell.world_y_q12};
}

std::vector<std::size_t> route_path(const Distances& distances, std::size_t end,
                                    std::size_t arrival_from = no_cell) {
    std::vector<std::size_t> path{end};
    auto cell = arrival_from == no_cell ? distances.parent[end] : arrival_from;
    while (cell != no_cell) {
        path.push_back(cell);
        cell = distances.parent[cell];
    }
    return path;
}

bool useful_alternate(std::span<const std::size_t> path, std::size_t primary_size, std::span<const uint8_t> avoid,
                      bool require_short_detour = true) {
    if (primary_size < 2 || (require_short_detour && path.size() > 2 * primary_size + 6)) return false;
    const auto distinct = static_cast<std::size_t>(std::count_if(path.begin(), path.end(),
        [&](auto cell) { return avoid[cell] <= 6; }));
    return distinct >= std::max<std::size_t>(2, (primary_size + 3) / 5);
}

struct DepositAccess {
    ResourceRouteDeposit marker;
    std::vector<std::size_t> entrances;
};

std::vector<DepositAccess> find_resource_deposits(const FieldGrid& field, const RoutesGrid& grid,
                                                   const std::vector<bool>& terrain_passable) {
    std::vector<DepositAccess> deposits;
    // Find deposits from the FLD support flags, including fields that have no
    // visible resource object. The two resource types can overlap.
    const auto cells = field.cells();
    for (const uint32_t flag : {0x0800U, 0x1000U}) {
        std::vector<bool> seen(cells.size());
        for (std::size_t first = 0; first < cells.size(); ++first) {
            if (seen[first] || !(cells[first].flags_and_material & flag)) continue;
            std::vector<std::size_t> component{first};
            seen[first] = true;
            auto& deposit = deposits.emplace_back(DepositAccess{{first, flag == 0x1000U}, {}});
            double sum_x = 0, sum_y = 0;
            for (std::size_t i = 0; i < component.size(); ++i) {
                const auto cell = component[i];
                sum_x += cells[cell].world_x_q12;
                sum_y += cells[cell].world_y_q12;
                if (grid.passable[cell]) deposit.entrances.push_back(cell);
                grid.visit_neighbors(cell, [&](std::size_t neighbor) {
                    if (cells[neighbor].flags_and_material & flag) {
                        if (!seen[neighbor]) {
                            seen[neighbor] = true;
                            component.push_back(neighbor);
                        }
                    } else if (!grid.passable[cell] && terrain_passable[cell] && grid.passable[neighbor]) {
                        // A built extractor occupies the field itself. Use
                        // the adjacent ground when it is clear.
                        deposit.entrances.push_back(neighbor);
                    }
                }, false);
            }
            const auto size = static_cast<double>(component.size());
            const MovementPoint center{static_cast<int32_t>(sum_x / size), static_cast<int32_t>(sum_y / size)};
            deposit.marker.cell = *std::min_element(component.begin(), component.end(), [&](auto a, auto b) {
                return squared_distance(cells[a], center) < squared_distance(cells[b], center);
            });
            std::sort(deposit.entrances.begin(), deposit.entrances.end());
            deposit.entrances.erase(std::unique(deposit.entrances.begin(), deposit.entrances.end()),
                                    deposit.entrances.end());
        }
    }
    return deposits;
}

bool intersects_obstacle(MovementPoint from, MovementPoint to,
                         const AccessRouteObstacle& obstacle, int32_t vehicle_radius_q12) {
    if (obstacle.radius_q12 <= 0 || vehicle_radius_q12 <= 0) return false;
    const double x = double(from.x) - obstacle.center.x;
    const double y = double(from.y) - obstacle.center.y;
    const double dx = double(to.x) - from.x;
    const double dy = double(to.y) - from.y;
    const double length_squared = dx * dx + dy * dy;
    const double t = length_squared > 0
        ? std::clamp(-(x * dx + y * dy) / length_squared, 0.0, 1.0) : 0.0;
    const double cx = x + t * dx, cy = y + t * dy;
    const double radius = double(obstacle.radius_q12) + vehicle_radius_q12;
    return cx * cx + cy * cy <= radius * radius;
}

MovementPoint interpolate(MovementPoint from, MovementPoint to, double t) {
    return {static_cast<int32_t>(std::lround(std::lerp(double(from.x), double(to.x), t))),
            static_cast<int32_t>(std::lround(std::lerp(double(from.y), double(to.y), t)))};
}

MovementPoint footprint_edge(const FieldCell& cell, const AccessRouteBase& base, int32_t vehicle_radius_q12) {
    const double dx = double(cell.world_x_q12) - base.center.x;
    const double dy = double(cell.world_y_q12) - base.center.y;
    // Stop the vehicle center outside the collision circle, including Q12 rounding.
    const double scale = (double(base.footprint_radius_q12) + vehicle_radius_q12 + 2) / std::hypot(dx, dy);
    return {static_cast<int32_t>(std::lround(base.center.x + dx * scale)),
            static_cast<int32_t>(std::lround(base.center.y + dy * scale))};
}

bool has_opposing_factions(unsigned mask, const AccessRouteDefencePlacement& placement) {
    const auto share_group = [](uint32_t groups, unsigned pair) {
        for (; (groups & 0xFFU) != 0; groups >>= 8U)
            if ((groups & pair) == pair) return true;
        return false;
    };
    while (mask) {
        const auto first = mask & (~mask + 1U);
        mask ^= first;
        for (auto remaining = mask; remaining; remaining &= remaining - 1U) {
            const auto second = remaining & (~remaining + 1U);
            // Route masks start at faction 1, while LEV diplomacy includes faction 0.
            const auto pair = (first | second) << 1U;
            if (!share_group(placement.allied_groups, pair) && !share_group(placement.truce_groups, pair))
                return true;
        }
    }
    return false;
}

bool site_clear_of_placements(MovementPoint center, int32_t radius, std::span<const AccessRouteObstacle> obstacles,
    std::span<const AccessRouteObstacle> barriers, std::span<const AccessRouteFactory> factories) {
    const auto overlaps = [&](const auto& object) { return intersects_obstacle(center, center, object, radius); };
    return std::none_of(obstacles.begin(), obstacles.end(), overlaps) &&
        std::none_of(barriers.begin(), barriers.end(), overlaps) &&
        std::none_of(factories.begin(), factories.end(), [&](const auto& factory) {
            return intersects_obstacle(factory.spawn, factory.exit, {center, radius}, 0xC00);
        });
}

bool building_site_terrain_clear(const FieldGrid& field, const FieldCell& cell, AccessRouteBuildingFootprint profile) {
    if (profile.radius_q12 <= 0 || (cell.flags_and_material & 0x1800U)) return false;
    const Vec3f position{q12_to_float(cell.world_x_q12), q12_to_float(cell.world_y_q12), cell.terrain_height()};
    return simulation_placement_terrain_clear(field, position, cell.terrain_height_q12,
        profile.radius_q12, profile.contact_kind);
}

void find_defence_sites(const FieldGrid& field, const RoutesGrid& grid, const std::vector<bool>& terrain_passable,
    std::span<const AccessRouteObstacle> obstacles, std::span<const AccessRouteFactory> factories,
    int32_t vehicle_radius, const AccessRouteDefencePlacement& placement, bool find_positions, AccessRoutes& result) {
    const auto cells = field.cells();
    std::vector<uint8_t> links(cells.size()), factory_links(cells.size()), factions(cells.size());
    const auto add_edge = [&](std::vector<uint8_t>& network, std::size_t a, std::size_t b, uint8_t mask) {
        factions[a] |= mask;
        factions[b] |= mask;
        for (std::size_t direction = 0; direction < neighbors.size(); ++direction) {
            if (grid.neighbor_at(a, direction) != b) continue;
            network[a] |= static_cast<uint8_t>(1U << direction);
            network[b] |= static_cast<uint8_t>(1U << ((direction + 3) % neighbors.size()));
            break;
        }
    };
    for (const auto& edge : result.edges)
        add_edge(edge.kind == AccessRouteCell::Main ? links : factory_links, edge.first, edge.second, edge.factions);
    for (const auto& edge : result.alternate_base_edges)
        add_edge(links, edge.first, edge.second, edge.factions);
    for (const auto& edge : result.resource_edges)
        if (const auto mask = edge.factions | edge.alternate_factions)
            add_edge(links, edge.first, edge.second, static_cast<uint8_t>(mask));
    if (std::none_of(links.begin(), links.end(), [](auto value) { return value != 0; })) return;

    const auto natural_barrier = [&](std::size_t cell) {
        if (cell == no_cell || terrain_passable[cell] || (cells[cell].flags_and_material & 0x88006000U)) return false;
        const auto x = cell % grid.width, y = cell / grid.width;
        return x > 0 && y > 0 && x + 1 < static_cast<unsigned>(grid.width) &&
            y + 1 < static_cast<unsigned>(grid.height);
    };
    const auto clear_of_placements = [&](MovementPoint center, int32_t radius) {
        return site_clear_of_placements(center, radius, obstacles, placement.existing_barriers, factories);
    };
    const auto placeable = [&](std::size_t cell, int32_t radius, int32_t contact) {
        return grid.passable[cell] && building_site_terrain_clear(field, cells[cell], {radius, contact}) &&
            clear_of_placements(cell_point(cells[cell]), radius);
    };
    const auto nearby = [&](std::size_t a, std::size_t b, double distance) {
        return squared_distance(cells[a], cell_point(cells[b])) < distance * distance;
    };

    struct WallCandidate { AccessRouteWallSite site; std::size_t width; std::size_t axis; };
    std::vector<WallCandidate> walls;
    std::vector<bool> contested(cells.size());
    for (std::size_t cell = 0; cell < cells.size(); ++cell) {
        if (!links[cell]) continue;
        contested[cell] = has_opposing_factions(factions[cell], placement);
        if (std::popcount(links[cell]) < 2 || !placeable(cell, placement.wall_radius_q12, placement.wall_contact_kind))
            continue;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto opposite = axis + 3;
            const auto positive = (1U << ((axis + 1) % 6)) | (1U << ((axis + 2) % 6));
            const auto negative = (1U << ((axis + 4) % 6)) | (1U << ((axis + 5) % 6));
            if (!(links[cell] & positive) || !(links[cell] & negative)) continue;
            const auto find_flank = [&](std::size_t direction, std::vector<std::size_t>& side) {
                for (int distance = 1; distance <= 6; ++distance) {
                    const auto next = grid.neighbor_at(cell, direction, distance);
                    if (natural_barrier(next)) {
                        // A single isolated bad cell can be driven around immediately.
                        return natural_barrier(grid.neighbor_at(next, (direction + 1) % 6)) ||
                            natural_barrier(grid.neighbor_at(next, (direction + 5) % 6));
                    }
                    if (next == no_cell || !grid.passable[next]) return false;
                    side.push_back(next);
                }
                return false;
            };
            std::vector<std::size_t> left, right;
            if (!find_flank(axis, left) || !find_flank(opposite, right) || left.size() + right.size() > 8)
                continue;
            const auto width = left.size() + right.size() + 1;
            const auto trim_flank = [&](std::vector<std::size_t>& side) {
                const auto usable = [&](auto candidate) {
                    return placeable(candidate, placement.wall_radius_q12, placement.wall_contact_kind);
                };
                // One unbuildable shore or slope cell can be covered by the
                // adjacent wall's expanded vehicle collision circle.
                if (!side.empty() && !usable(side.back())) {
                    const auto last = side.size() == 1 ? cell : side[side.size() - 2];
                    if (!nearby(last, side.back(), double(placement.wall_radius_q12) + vehicle_radius))
                        return false;
                    side.pop_back();
                }
                return std::all_of(side.begin(), side.end(), usable);
            };
            if (!trim_flank(left) || !trim_flank(right)) continue;
            AccessRouteWallSite site{cell, {}};
            site.cells.insert(site.cells.end(), left.rbegin(), left.rend());
            site.cells.push_back(cell);
            site.cells.insert(site.cells.end(), right.begin(), right.end());
            bool valid_spacing = true;
            for (std::size_t i = 1; i < site.cells.size(); ++i) {
                const auto a = site.cells[i - 1], b = site.cells[i];
                if (nearby(a, b, 2.0 * placement.wall_radius_q12) ||
                    !nearby(a, b, 2.0 * (placement.wall_radius_q12 + vehicle_radius))) {
                    valid_spacing = false;
                    break;
                }
            }
            if (valid_spacing) walls.push_back({std::move(site), width, axis});
        }
    }
    std::sort(walls.begin(), walls.end(), [](const auto& a, const auto& b) {
        return std::tie(a.width, a.site.crossing) < std::tie(b.width, b.site.crossing);
    });
    const auto closes_gap = [&](const WallCandidate& wall) {
        // A row between two small patches of steep terrain is not a useful
        // choke if vehicles can immediately pass around either end.
        constexpr int reach = 8;
        const auto crossing = wall.site.crossing;
        const auto center = cell_point(cells[crossing]);
        const auto side = cell_point(cells[grid.neighbor_at(crossing, wall.axis)]);
        const double wx = double(side.x) - center.x, wy = double(side.y) - center.y;
        const double min_approach = 6144 * std::hypot(wx, wy);
        const int cx = static_cast<int>(crossing % grid.width), cy = static_cast<int>(crossing / grid.width);
        const int min_x = std::max(1, cx - reach), max_x = std::min(grid.width - 2, cx + reach);
        const int min_y = std::max(1, cy - reach), max_y = std::min(grid.height - 2, cy + reach);
        const int width = max_x - min_x + 1;
        const auto local = [&](std::size_t cell) {
            const int x = static_cast<int>(cell % grid.width), y = static_cast<int>(cell / grid.width);
            return x < min_x || x > max_x || y < min_y || y > max_y
                ? no_cell : static_cast<std::size_t>((y - min_y) * width + x - min_x);
        };
        std::vector<AccessRouteObstacle> segments;
        for (auto cell : wall.site.cells)
            segments.push_back({cell_point(cells[cell]), placement.wall_radius_q12});
        const auto blocked = [&](MovementPoint from, MovementPoint to) {
            return std::any_of(segments.begin(), segments.end(), [&](const auto& segment) {
                return intersects_obstacle(from, to, segment, vehicle_radius);
            });
        };
        std::array<std::size_t, 2> approaches{no_cell, no_cell};
        for (int y = min_y; y <= max_y; ++y)
            for (int x = min_x; x <= max_x; ++x) {
                const auto cell = grid.index(x, y);
                if (!links[cell]) continue;
                const auto p = cell_point(cells[cell]);
                const double across = wx * (double(p.y) - center.y) - wy * (double(p.x) - center.x);
                if (std::abs(across) < min_approach || blocked(p, p)) continue;
                auto& best = approaches[across > 0 ? 1 : 0];
                if (best == no_cell || squared_distance(cells[cell], center) < squared_distance(cells[best], center))
                    best = cell;
            }
        if (approaches[0] == no_cell || approaches[1] == no_cell) return false;
        const auto connected = [&](bool with_wall) {
            std::array<bool, (2 * reach + 1) * (2 * reach + 1)> seen{};
            std::vector<std::size_t> queue{approaches[0]};
            seen[local(approaches[0])] = true;
            for (std::size_t i = 0; i < queue.size(); ++i) {
                const auto cell = queue[i];
                if (cell == approaches[1]) return true;
                grid.visit_neighbors(cell, [&](auto next) {
                    const auto index = local(next);
                    if (index == no_cell || seen[index] || !grid.passable[next]) return;
                    if (with_wall && blocked(cell_point(cells[cell]), cell_point(cells[next]))) return;
                    seen[index] = true;
                    queue.push_back(next);
                });
            }
            return false;
        };
        return connected(false) && !connected(true);
    };
    for (auto& candidate : walls)
        if (std::none_of(result.wall_sites.begin(), result.wall_sites.end(), [&](const auto& other) {
            return nearby(candidate.site.crossing, other.crossing, 16384);
        }) && closes_gap(candidate)) result.wall_sites.push_back(std::move(candidate.site));
    if (!find_positions) return;

    std::vector<std::size_t> nearest(cells.size(), no_cell), frontier;
    std::vector<uint8_t> approach_distance(cells.size());
    for (std::size_t cell = 0; cell < cells.size(); ++cell)
        if (contested[cell]) {
            nearest[cell] = cell;
            frontier.push_back(cell);
        }
    for (std::size_t i = 0; i < frontier.size(); ++i) {
        const auto cell = frontier[i];
        if (approach_distance[cell] >= 5) continue;
        grid.visit_neighbors(cell, [&](auto neighbor) {
            if (!grid.passable[neighbor] || nearest[neighbor] != no_cell) return;
            nearest[neighbor] = nearest[cell];
            approach_distance[neighbor] = approach_distance[cell] + 1;
            frontier.push_back(neighbor);
        });
    }
    struct Position { std::size_t cell; int score; };
    std::vector<Position> defence, rally;
    for (auto cell : frontier) {
        if (links[cell] || (cells[cell].flags_and_material & 0x1800U)) continue;
        const auto anchor = nearest[cell];
        const auto center = cell_point(cells[cell]);
        const auto route_point = cell_point(cells[anchor]);
        std::size_t rear = 0;
        double best = -std::numeric_limits<double>::infinity();
        for (std::size_t direction = 0; direction < neighbors.size(); ++direction) {
            const auto step = grid.neighbor_at(cell, direction);
            if (step == no_cell) continue;
            const auto p = cell_point(cells[step]);
            const double dot = (double(center.x) - route_point.x) * (p.x - center.x) +
                (double(center.y) - route_point.y) * (p.y - center.y);
            if (dot > best) {
                best = dot;
                rear = direction;
            }
        }
        const auto backed = [&](std::size_t direction) {
            for (int distance = 1; distance <= 4; ++distance)
                if (natural_barrier(grid.neighbor_at(cell, direction, distance))) return true;
            return false;
        };
        const int cover = int(backed(rear)) + int(backed((rear + 1) % 6)) + int(backed((rear + 5) % 6));
        const bool near_crossing = std::any_of(result.wall_sites.begin(), result.wall_sites.end(), [&](const auto& wall) {
            return contested[wall.crossing] && nearby(anchor, wall.crossing, 12288);
        });
        if (cover < 2 && !(near_crossing && cover > 0)) continue;
        const auto wall_conflict = [&](int32_t radius) {
            return std::any_of(result.wall_sites.begin(), result.wall_sites.end(), [&](const auto& wall) {
                return std::any_of(wall.cells.begin(), wall.cells.end(), [&](auto wall_cell) {
                    return nearby(cell, wall_cell, double(placement.wall_radius_q12) + radius);
                });
            });
        };
        // Keep enough lateral space for traffic and for units assembling here.
        unsigned exits = 0;
        grid.visit_neighbors(cell, [&](auto neighbor) { exits += grid.passable[neighbor]; });
        const int score = 12 * cover + (near_crossing ? 16 : 0) - 2 * approach_distance[cell] -
            grid.terrain_cost[cell];
        const auto clear_of_route = [&](int32_t radius) {
            const auto probe = AccessRouteObstacle{center, radius};
            const int extent = (radius + vehicle_radius) / 1152 + 2;
            const int x = static_cast<int>(cell % grid.width), y = static_cast<int>(cell / grid.width);
            for (int row = std::max(1, y - extent); row < std::min(grid.height - 1, y + extent + 1); ++row)
                for (int col = std::max(1, x - extent); col < std::min(grid.width - 1, x + extent + 1); ++col) {
                    const auto a = grid.index(col, row);
                    for (std::size_t dir = 0; dir < neighbors.size(); ++dir)
                        if ((links[a] | factory_links[a]) & (1U << dir)) {
                            const auto b = grid.neighbor_at(a, dir);
                            if (intersects_obstacle(cell_point(cells[a]), cell_point(cells[b]), probe, vehicle_radius))
                                return false;
                        }
                }
            return true;
        };
        if (exits >= 5 && cells[cell].water_delta_q12 <= 0 && clear_of_placements(center, vehicle_radius) &&
            !wall_conflict(vehicle_radius) && clear_of_route(vehicle_radius))
            rally.push_back({cell, score + static_cast<int>(exits)});
        if (placeable(cell, placement.turret_radius_q12, placement.turret_contact_kind) &&
            !wall_conflict(placement.turret_radius_q12) && clear_of_route(placement.turret_radius_q12))
            defence.push_back({cell, score});
    }
    const auto select_sites = [&](auto& candidates, auto& selected, std::span<const std::size_t> reserved) {
        std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
            return a.score == b.score ? a.cell < b.cell : a.score > b.score;
        });
        for (const auto& candidate : candidates) {
            const auto close = [&](auto other) { return nearby(candidate.cell, other, 20480); };
            if (std::none_of(selected.begin(), selected.end(), close) &&
                std::none_of(reserved.begin(), reserved.end(), [&](auto other) {
                    return nearby(candidate.cell, other, 8192);
                })) selected.push_back(candidate.cell);
        }
    };
    select_sites(defence, result.defence_sites, {});
    select_sites(rally, result.rally_sites, result.defence_sites);
}

constexpr auto no_ground_path = std::numeric_limits<uint32_t>::max();

std::vector<uint32_t> ground_distances(const RoutesGrid& grid, std::span<const std::size_t> starts) {
    std::vector<uint32_t> distances(grid.passable.size(), no_ground_path);
    std::vector<std::size_t> queue;
    for (const auto cell : starts) {
        if (!grid.passable[cell] || distances[cell] == 0) continue;
        distances[cell] = 0;
        queue.push_back(cell);
    }
    for (std::size_t i = 0; i < queue.size(); ++i) {
        const auto cell = queue[i];
        grid.visit_neighbors(cell, [&](auto neighbor) {
            if (!grid.passable[neighbor] || distances[neighbor] != no_ground_path) return;
            distances[neighbor] = distances[cell] + 1;
            queue.push_back(neighbor);
        });
    }
    return distances;
}

void find_planning_sites(const FieldGrid& field, const RoutesGrid& grid, std::span<const std::optional<AccessRouteBase>> bases,
    std::span<const std::vector<std::size_t>> starts, std::span<const std::vector<std::size_t>> hostile_starts,
    std::span<const DepositAccess> deposits,
    std::span<const AccessRouteObstacle> obstacles, std::span<const AccessRouteFactory> factories,
    std::span<const uint32_t> step_cost, int32_t pioneer_radius, AccessRouteOptions options,
    const AccessRouteDefencePlacement& placement, AccessRoutes& result) {
    const auto cells = field.cells();
    if (std::none_of(starts.begin(), starts.end(), [](const auto& start) { return !start.empty(); })) return;
    std::vector<std::vector<uint32_t>> distances;
    for (const auto& start : starts) distances.push_back(ground_distances(grid, start));
    std::vector<std::vector<uint32_t>> hostile_distances;
    if (options.radar_sites || options.alternate_bases)
        for (const auto& start : hostile_starts) hostile_distances.push_back(ground_distances(grid, start));
    std::vector<uint8_t> reachable(cells.size()), traffic(cells.size()), route_factions(cells.size());
    const auto mark = [&](auto a, auto b, auto factions) {
        traffic[a] = traffic[b] = 1;
        route_factions[a] |= factions;
        route_factions[b] |= factions;
    };
    for (const auto& edge : result.edges) mark(edge.first, edge.second, edge.factions);
    for (const auto& edge : result.alternate_base_edges) mark(edge.first, edge.second, edge.factions);
    for (const auto& edge : result.resource_edges)
        mark(edge.first, edge.second, static_cast<uint8_t>(edge.factions | edge.alternate_factions));
    std::vector<uint8_t> contested(cells.size());
    for (std::size_t cell = 0; cell < cells.size(); ++cell) {
        for (std::size_t faction = 0; faction < starts.size(); ++faction)
            if (distances[faction][cell] != no_ground_path) reachable[cell] |= static_cast<uint8_t>(1U << faction);
        contested[cell] = has_opposing_factions(route_factions[cell], placement);
    }
    const auto near_marked = [&](std::size_t cell, int32_t radius, std::span<const uint8_t> marked) {
        const int extent = radius / 1152 + 1;
        const int x = static_cast<int>(cell % grid.width), y = static_cast<int>(cell / grid.width);
        const auto center = cell_point(cells[cell]);
        for (int row = std::max(1, y - extent); row < std::min(grid.height - 1, y + extent + 1); ++row)
            for (int col = std::max(1, x - extent); col < std::min(grid.width - 1, x + extent + 1); ++col) {
                const auto other = grid.index(col, row);
                if (marked[other] && squared_distance(cells[other], center) <= double(radius) * radius) return true;
            }
        return false;
    };
    const auto secure_factions = [&](std::size_t cell, uint32_t lead) {
        uint8_t secure = 0;
        for (std::size_t own = 0; own < distances.size(); ++own) {
            const auto travel = distances[own][cell];
            if (travel == no_ground_path) continue;
            bool closer_to_enemy = false;
            for (std::size_t enemy = 0; enemy < distances.size(); ++enemy) {
                const auto pair = (1U << own) | (1U << enemy);
                const auto hostile_travel = hostile_distances[enemy][cell];
                if (hostile_travel != no_ground_path && hostile_travel < travel + lead &&
                    has_opposing_factions(pair, placement)) {
                    closer_to_enemy = true;
                    break;
                }
            }
            if (!closer_to_enemy) secure |= static_cast<uint8_t>(1U << own);
        }
        return secure;
    };
    const auto clear = [&](std::size_t cell, int32_t radius) {
        return !near_marked(cell, radius + pioneer_radius + 1152, traffic) &&
            site_clear_of_placements(cell_point(cells[cell]), radius, obstacles, placement.existing_barriers, factories);
    };
    const auto dry_ground = [&](std::size_t cell) {
        return reachable[cell] && cells[cell].water_delta_q12 <= 0 && !(cells[cell].flags_and_material & 0x1800U);
    };

    if (options.pioneer_sites) {
        for (const auto turret : result.defence_sites) {
            const auto turret_center = cell_point(cells[turret]);
            const double separation = double(placement.turret_radius_q12) + pioneer_radius;
            std::vector<std::pair<std::size_t, unsigned>> nearby{{turret, 0}};
            for (std::size_t i = 0; i < nearby.size(); ++i) {
                const auto [cell, steps] = nearby[i];
                if (steps == 4) continue;
                grid.visit_neighbors(cell, [&](auto neighbor) {
                    if (grid.passable[neighbor] && std::none_of(nearby.begin(), nearby.end(),
                            [&](const auto& item) { return item.first == neighbor; }))
                        nearby.emplace_back(neighbor, steps + 1);
                });
            }
            std::size_t best = no_cell;
            unsigned best_score = std::numeric_limits<unsigned>::max();
            for (const auto [cell, steps] : nearby) {
                if (!dry_ground(cell) || !(reachable[cell] & reachable[turret]) || !clear(cell, pioneer_radius) ||
                    squared_distance(cells[cell], turret_center) <= separation * separation) continue;
                const auto overlaps = [&](std::size_t other, double distance) {
                    return squared_distance(cells[other], cell_point(cells[cell])) <= distance * distance;
                };
                if (std::any_of(result.wall_sites.begin(), result.wall_sites.end(), [&](const auto& wall) {
                        return std::any_of(wall.cells.begin(), wall.cells.end(), [&](auto other) {
                            return overlaps(other, double(placement.wall_radius_q12) + pioneer_radius);
                        });
                    }) || std::any_of(result.defence_sites.begin(), result.defence_sites.end(),
                        [&](auto other) { return overlaps(other, separation); }) ||
                    std::any_of(result.pioneer_sites.begin(), result.pioneer_sites.end(),
                        [&](const auto& site) { return overlaps(site.cell, 2.0 * pioneer_radius); })) continue;
                const unsigned score = 4U * steps + grid.terrain_cost[cell];
                if (score < best_score) {
                    best = cell;
                    best_score = score;
                }
            }
            if (best != no_cell)
                result.pioneer_sites.push_back({best, static_cast<uint8_t>(reachable[best] & reachable[turret])});
        }
    }
    if (!options.radar_sites && !options.alternate_bases) return;

    std::vector<uint8_t> wall_cells(cells.size());
    std::vector<std::size_t> wall_starts;
    if (options.radar_sites) {
        for (const auto& wall : result.wall_sites)
            for (const auto cell : wall.cells) {
                wall_cells[cell] = 1;
                wall_starts.push_back(cell);
            }
    }
    const auto wall_steps = wall_starts.empty() ? std::vector<uint32_t>{} : ground_distances(grid, wall_starts);
    const auto turret_steps = options.radar_sites && !result.defence_sites.empty()
        ? ground_distances(grid, result.defence_sites) : std::vector<uint32_t>{};
    struct Candidate { AccessRoutePlanningSite site; int64_t score; bool defended{}; };
    const int sector_width = std::max(10, (std::max(grid.width, grid.height) + 12) / 13);
    const int columns = (grid.width + sector_width - 1) / sector_width;
    const int rows = (grid.height + sector_width - 1) / sector_width;
    std::vector<std::vector<Candidate>> radar_sectors(static_cast<std::size_t>(columns) * rows);
    std::vector<Candidate> bases_ranked;
    std::vector<std::size_t> xenite, tritium;
    if (options.alternate_bases)
        for (const auto& deposit : deposits) {
            auto& targets = deposit.marker.tritium ? tritium : xenite;
            targets.insert(targets.end(), deposit.entrances.begin(), deposit.entrances.end());
        }
    const auto xenite_steps = xenite.empty() ? std::vector<uint32_t>{} : ground_distances(grid, xenite);
    const auto tritium_steps = tritium.empty() ? std::vector<uint32_t>{} : ground_distances(grid, tritium);
    const auto can_build = [&](std::size_t cell, AccessRouteBuildingFootprint profile) {
        return dry_ground(cell) && building_site_terrain_clear(field, cells[cell], profile) && clear(cell, profile.radius_q12);
    };
    const auto factory_pad = [&](std::size_t cell, uint8_t mask) {
        if (placement.factory.radius_q12 <= 0) return uint8_t{0};
        uint8_t served = 0;
        const auto center = cell_point(cells[cell]);
        const double separation = double(placement.headquarters.radius_q12) + placement.factory.radius_q12 + pioneer_radius;
        for (int step : {5, 6, 7}) {
            for (std::size_t direction = 0; direction < neighbors.size(); ++direction) {
                const auto pad = grid.neighbor_at(cell, direction, step);
                if (pad == no_cell || !(reachable[pad] & mask) ||
                    squared_distance(cells[pad], center) <= separation * separation ||
                    !can_build(pad, placement.factory)) continue;
                // Leave a drivable apron outside the proposed factory's footprint.
                const int apron_step = (placement.factory.radius_q12 + pioneer_radius) / 2300 + 1;
                const auto apron = grid.neighbor_at(pad, direction, apron_step);
                if (apron == no_cell || !(reachable[pad] & reachable[apron] & mask) || !clear(apron, pioneer_radius))
                    continue;
                auto previous = pad;
                for (int step_out = 1; step_out <= apron_step; ++step_out) {
                    const auto next = grid.neighbor_at(pad, direction, step_out);
                    bool accessible = false;
                    grid.visit_neighbors(previous, [&](auto neighbor) {
                        if (neighbor == next && grid.passable[next]) accessible = true;
                    });
                    if (!accessible) break;
                    previous = next;
                }
                if (previous == apron) {
                    served |= static_cast<uint8_t>(reachable[pad] & reachable[apron] & mask);
                    if (served == mask) return served;
                }
            }
        }
        return served;
    };
    constexpr double base_spacing = 10.0 * 4096;
    constexpr auto min_base_steps = 18U;
    std::vector<MovementPoint> existing_bases(placement.existing_headquarters.begin(), placement.existing_headquarters.end());
    if (options.alternate_bases)
        for (std::size_t faction = 0; faction < starts.size(); ++faction) {
            if (!starts[faction].empty()) result.alternate_base_factions |= static_cast<uint8_t>(1U << faction);
            if (bases[faction]) existing_bases.push_back(bases[faction]->center);
        }
    for (std::size_t cell = 0; cell < cells.size(); ++cell) {
        if (!dry_ground(cell)) continue;
        const bool by_wall = !wall_steps.empty() && wall_steps[cell] <= 7;
        const bool by_turret = !turret_steps.empty() && turret_steps[cell] <= 4;
        const bool defended = by_wall || by_turret;
        const bool exposed = near_marked(cell, 8192, contested);
        const auto secure = exposed ? uint8_t{0} : secure_factions(cell, 6);
        const auto radar_factions = defended ? static_cast<uint8_t>(secure | secure_factions(cell, 2)) : secure;
        const auto& ground = cells[cell];
        if (options.radar_sites && radar_factions && placement.radar_visibility_radius_q12 > 0 &&
            can_build(cell, placement.radar) &&
            (wall_starts.empty() || !near_marked(cell, placement.wall_radius_q12 + placement.radar.radius_q12, wall_cells))) {
            const auto sector = (cell / grid.width / sector_width) * columns + cell % grid.width / sector_width;
            const Candidate candidate{{cell, radar_factions},
                int64_t(ground.terrain_height_q12) - 128 * grid.terrain_cost[cell] +
                    (by_turret && turret_steps[cell] == 0 ? 2048 : 0), defended};
            radar_sectors[sector].push_back(candidate);
        }
        if (!options.alternate_bases || !secure ||
            std::any_of(existing_bases.begin(), existing_bases.end(), [&](auto base) {
                return squared_distance(ground, base) < base_spacing * base_spacing;
            }) || (cell + cell / grid.width) % 3 != 0 ||
            !can_build(cell, placement.headquarters))
            continue;
        auto candidates = secure;
        for (std::size_t faction = 0; faction < starts.size(); ++faction)
            if ((candidates & (1U << faction)) && distances[faction][cell] < min_base_steps)
                candidates &= static_cast<uint8_t>(~(1U << faction));
        if (!candidates) continue;
        const auto score = [&](const auto& steps) { return steps.empty() ? 0U : std::min(steps[cell], 60U); };
        bases_ranked.push_back({{cell, candidates}, -int64_t(3U * (score(xenite_steps) + score(tritium_steps)) +
            4U * step_cost[cell])});
    }
    std::vector<Candidate> radar_ranked;
    const auto existing = options.radar_sites
        ? build_projected_fog_preview_occupancy(field, placement.existing_observers) : std::vector<uint64_t>{};
    for (auto& sector : radar_sectors) {
        std::sort(sector.begin(), sector.end(), [](auto a, auto b) {
            return a.score == b.score ? a.site.cell < b.site.cell : a.score > b.score;
        });
        std::array<unsigned, 2> sampled{};
        std::vector<Candidate> shortlist;
        for (auto candidate : sector) {
            const auto kind = static_cast<std::size_t>(candidate.defended);
            if (sampled[kind] >= 3 || std::any_of(shortlist.begin(), shortlist.end(), [&](const auto& other) {
                return other.defended == candidate.defended && (other.site.factions & candidate.site.factions) &&
                    squared_distance(cells[candidate.site.cell], cell_point(cells[other.site.cell])) < 6144.0 * 6144.0;
            })) continue;
            ++sampled[kind];
            shortlist.push_back(candidate);
        }
        for (auto candidate : shortlist) {
            const auto& cell = cells[candidate.site.cell];
            const auto reference = std::clamp<int64_t>(int64_t(cell.terrain_height_q12) + placement.radar_reference_height_q12,
                std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max());
            const FogPreviewObserver observer{cell.world_x_q12, cell.world_y_q12, static_cast<int32_t>(reference),
                static_cast<uint32_t>(placement.radar_visibility_radius_q12), 1U};
            const auto visible = build_projected_fog_preview_occupancy(field, std::span(&observer, 1));
            std::array<uint32_t, 7> new_cells{};
            uint32_t total_visible = 0;
            for (std::size_t i = 0; i < visible.size(); ++i) {
                if (!visible[i]) continue;
                ++total_visible;
                for (std::size_t faction = 0; faction < starts.size(); ++faction)
                    if ((candidate.site.factions & (1U << faction)) &&
                        !retail_fog_currently_visible(fog_occupancy_byte(existing[i], static_cast<uint32_t>(faction + 1U))))
                        ++new_cells[faction];
            }
            uint32_t added = 0;
            for (std::size_t faction = 0; faction < starts.size(); ++faction) {
                if (!new_cells[faction]) candidate.site.factions &= static_cast<uint8_t>(~(1U << faction));
                added += new_cells[faction];
            }
            if (!candidate.site.factions) continue;
            // Prefer new sight, with total unoccluded coverage as the tie-break.
            const auto gain = (int64_t(added) * static_cast<int64_t>(cells.size() + 1U)) /
                std::popcount(candidate.site.factions);
            candidate.score = gain + (candidate.defended ? gain / 6 : 0) + total_visible;
            radar_ranked.push_back(candidate);
        }
    }
    const auto select_radars = [&] {
        auto& ranked = radar_ranked;
        auto& selected = result.radar_sites;
        std::sort(ranked.begin(), ranked.end(), [](auto a, auto b) {
            return a.score == b.score ? a.site.cell < b.site.cell : a.score > b.score;
        });
        std::array<unsigned, 7> per_faction{};
        for (auto candidate : ranked) {
            auto& site = candidate.site;
            for (std::size_t faction = 0; faction < starts.size(); ++faction)
                if (per_faction[faction] >= 4) site.factions &= static_cast<uint8_t>(~(1U << faction));
            if (!site.factions || std::any_of(selected.begin(), selected.end(), [&](const auto& previous) {
                    const double minimum = previous.factions & site.factions ? 22528 : 8192;
                    return squared_distance(cells[site.cell], cell_point(cells[previous.cell])) < minimum * minimum;
                })) continue;
            selected.push_back(site);
            for (std::size_t faction = 0; faction < starts.size(); ++faction)
                if (site.factions & (1U << faction)) ++per_faction[faction];
        }
    };
    select_radars();
    if (!result.alternate_base_factions) return;

    struct Expansion { std::size_t cell; uint32_t travel; int64_t score; };
    std::array<std::vector<Expansion>, 7> expansions;
    std::vector<uint32_t> distance_bands;
    for (const auto& candidate : bases_ranked) {
        const auto cell = candidate.site.cell;
        const auto served = factory_pad(cell, candidate.site.factions);
        for (std::size_t faction = 0; faction < starts.size(); ++faction) {
            if (!(served & (1U << faction))) continue;
            const auto travel = distances[faction][cell];
            expansions[faction].push_back({cell, travel, candidate.score - travel / 2U});
            distance_bands.push_back(travel);
        }
    }
    for (std::size_t faction = 0; faction < starts.size(); ++faction) {
        if (!(result.alternate_base_factions & (1U << faction))) continue;
        auto& choices = expansions[faction];
        if (choices.empty()) return;
        std::sort(choices.begin(), choices.end(), [](const auto& a, const auto& b) {
            return a.score == b.score ? a.cell < b.cell : a.score > b.score;
        });
    }
    std::sort(distance_bands.begin(), distance_bands.end());
    distance_bands.erase(std::unique(distance_bands.begin(), distance_bands.end()), distance_bands.end());
    const auto clear_of_expansions = [&](std::size_t cell, std::span<const AccessRoutePlanningSite> sites) {
        return std::none_of(sites.begin(), sites.end(), [&](const auto& site) {
            return squared_distance(cells[cell], cell_point(cells[site.cell])) < base_spacing * base_spacing;
        });
    };
    // Only publish complete rounds. Every grounded faction gets a distinct
    // site, and the longest approach in a round stays close to the shortest.
    for (unsigned round = 0; round < 2; ++round) {
        std::vector<AccessRoutePlanningSite> best_round;
        int64_t best_score = std::numeric_limits<int64_t>::min();
        for (auto low : distance_bands) {
            const auto high = low + std::max(6U, low / 3U);
            std::vector<AccessRoutePlanningSite> trial;
            auto remaining = result.alternate_base_factions;
            int64_t score = 0;
            while (remaining) {
                std::size_t selected_faction = 0;
                const Expansion* selected = nullptr;
                std::size_t fewest = std::numeric_limits<std::size_t>::max();
                for (std::size_t faction = 0; faction < starts.size(); ++faction) {
                    if (!(remaining & (1U << faction))) continue;
                    const Expansion* first = nullptr;
                    std::size_t count = 0;
                    for (const auto& choice : expansions[faction]) {
                        if (choice.travel < low || choice.travel > high ||
                            !clear_of_expansions(choice.cell, result.alternate_bases) ||
                            !clear_of_expansions(choice.cell, trial)) continue;
                        if (!first) first = &choice;
                        if (++count >= fewest) break;
                    }
                    if (count >= fewest) continue;
                    fewest = count;
                    selected_faction = faction;
                    selected = first;
                    if (!selected) break;
                }
                if (!selected) break;
                const auto bit = static_cast<uint8_t>(1U << selected_faction);
                trial.push_back({selected->cell, bit});
                remaining ^= bit;
                score += selected->score;
            }
            if (!remaining && score > best_score) {
                best_score = score;
                best_round = std::move(trial);
            }
        }
        if (best_round.empty()) break;
        result.alternate_bases.insert(result.alternate_bases.end(), best_round.begin(), best_round.end());
    }
}
}

MovementPoint access_route_curve_point(MovementPoint start, MovementPoint corner, MovementPoint end, double t) {
    // Keep the endpoint tangents aligned with the original cell edges.
    constexpr double control = 0.44;
    const auto first = interpolate(start, corner, control);
    const auto second = interpolate(end, corner, control);
    const auto a = interpolate(start, first, t);
    const auto b = interpolate(first, second, t);
    const auto c = interpolate(second, end, t);
    return interpolate(interpolate(a, b, t), interpolate(b, c, t), t);
}

AccessRoutes plan_access_routes(const FieldGrid& field, uint32_t traversal_band,
    std::span<const std::optional<AccessRouteBase>> bases, std::span<const AccessRouteFactory> factories,
    std::span<const AccessRouteObstacle> obstacles, int32_t vehicle_radius_q12, AccessRouteOptions options,
    AccessRouteDefencePlacement defence_placement) {
    AccessRoutes result;
    result.cells.resize(field.cells().size(), AccessRouteCell::None);
    result.base_count = static_cast<std::size_t>(std::count_if(bases.begin(), bases.end(),
        [](const auto& base) { return base.has_value(); }));
    result.factory_count = factories.size();
    if (field.width() < 3 || field.height() < 3) return result;

    RoutesGrid grid(field, traversal_band);
    const auto terrain_passable = grid.passable;
    vehicle_radius_q12 = std::max(0, vehicle_radius_q12);
    for (const auto& obstacle : obstacles) {
        const auto center = retail_fog_world_to_cell(field, obstacle.center.x, obstacle.center.y);
        if (!center || obstacle.radius_q12 <= 0 || vehicle_radius_q12 <= 0) continue;
        const auto radius = int64_t(obstacle.radius_q12) + vehicle_radius_q12;
        const int extent = static_cast<int>(radius * 3 / 4096) + 2;
        const int cx = static_cast<int>(center->x);
        const int cy = static_cast<int>(center->y);
        for (int y = std::max(0, cy - extent); y < std::min(grid.height, cy + extent + 1); ++y) {
            for (int x = std::max(0, cx - extent); x < std::min(grid.width, cx + extent + 1); ++x) {
                const auto cell = grid.index(x, y);
                const auto from = cell_point(field.cells()[cell]);
                if (squared_distance(field.cells()[cell], obstacle.center) <= double(radius) * double(radius)) {
                    grid.passable[cell] = false;
                    continue;
                }
                for (std::size_t direction = 0; direction < neighbors.size(); ++direction) {
                    const auto [dx, dy] = neighbors[direction];
                    const int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < grid.width && ny < grid.height &&
                        intersects_obstacle(from, cell_point(field.at(nx, ny)), obstacle, vehicle_radius_q12))
                        grid.blocked_edges[cell] |= static_cast<uint8_t>(1U << direction);
                }
            }
        }
    }

    const auto clear_segment = [&](MovementPoint from, MovementPoint to,
                                   std::optional<MovementPoint> own_footprint = {}) {
        const double length = std::hypot(double(to.x) - from.x, double(to.y) - from.y);
        const int steps = std::max(1, static_cast<int>(std::ceil(length / 512)));
        for (int step = 0; step <= steps; ++step) {
            const auto point = interpolate(from, to, static_cast<double>(step) / steps);
            const auto sample = retail_fog_world_to_cell(field, point.x, point.y);
            if (!sample || !terrain_passable[grid.index(sample->x, sample->y)])
                return false;
        }
        return std::none_of(obstacles.begin(), obstacles.end(), [&](const auto& obstacle) {
            const bool own_factory = own_footprint && obstacle.center.x == own_footprint->x &&
                obstacle.center.y == own_footprint->y;
            return !own_factory && intersects_obstacle(from, to, obstacle, vehicle_radius_q12);
        });
    };
    struct FactoryAccess {
        uint32_t faction;
        std::vector<FactoryApron> aprons;
    };
    std::vector<FactoryAccess> factory_access;
    for (const auto& factory : factories) {
        const double dx = double(factory.exit.x) - factory.spawn.x;
        const double dy = double(factory.exit.y) - factory.spawn.y;
        const double heading_length = std::hypot(dx, dy);
        if (heading_length == 0 || !clear_segment(factory.spawn, factory.exit, factory.footprint_center)) continue;
        const auto door = retail_fog_world_to_cell(field, factory.exit.x, factory.exit.y);
        if (!door) continue;
        const auto lane = result.production_lanes.size();
        result.production_lanes.push_back({factory.spawn, factory.exit, {}});
        auto& access = factory_access.emplace_back(FactoryAccess{factory.faction_index, {}});
        const double forward_x = dx / heading_length, forward_y = dy / heading_length;
        const double apron_depth = std::max(3072., 2. * vehicle_radius_q12);
        const double maximum_runout = apron_depth + 2048;
        const int extent = static_cast<int>(maximum_runout * 3 / 4096) + 2;
        const int door_x = static_cast<int>(door->x), door_y = static_cast<int>(door->y);
        for (int y = std::max(0, door_y - extent); y < std::min(grid.height, door_y + extent + 1); ++y) {
            for (int x = std::max(0, door_x - extent); x < std::min(grid.width, door_x + extent + 1); ++x) {
                const auto cell = grid.index(x, y);
                if (!grid.passable[cell]) continue;
                const auto point = cell_point(field.cells()[cell]);
                const double offset_x = double(point.x) - factory.exit.x;
                const double offset_y = double(point.y) - factory.exit.y;
                const double forward = offset_x * forward_x + offset_y * forward_y;
                const double lateral = std::abs(offset_x * forward_y - offset_y * forward_x);
                if (forward < apron_depth / 2 || forward > maximum_runout ||
                    lateral > std::min(2048., forward * 0.6)) continue;
                const double straight_length = std::clamp(forward - lateral * 0.6, apron_depth / 2, apron_depth);
                const MovementPoint runout{
                    static_cast<int32_t>(std::lround(factory.exit.x + straight_length * forward_x)),
                    static_cast<int32_t>(std::lround(factory.exit.y + straight_length * forward_y)),
                };
                if (!clear_segment(factory.exit, runout, factory.footprint_center) ||
                    !clear_segment(runout, point, factory.footprint_center)) continue;
                const double cost = (std::abs(straight_length - apron_depth) + 3 * lateral) / 512;
                access.aprons.push_back({cell, lane, factory.exit, runout, cost});
            }
        }
        if (!access.aprons.empty()) ++result.clear_factory_aprons;
    }

    struct BaseAccess {
        std::optional<AccessRouteBase> base;
        std::vector<Entrance> entrances;
        std::size_t regular_entrances{};
        bool has_factory{};
    };
    std::vector<BaseAccess> base_access(bases.size());
    // Rank relocated starts by distance and prefer a usable building edge over
    // any relocation, regardless of the length of the rest of the route.
    const uint64_t route_cost_limit = 128ULL * grid.passable.size() + 1U;
    for (std::size_t faction = 0; faction < bases.size(); ++faction) {
        const auto& base = bases[faction];
        if (!base) continue;
        auto& access = base_access[faction];
        access.base = base;
        const double footprint = std::max(0, base->footprint_radius_q12);
        if (footprint == 0) {
            const auto start = retail_fog_world_to_cell(field, base->center.x, base->center.y);
            if (start) {
                const auto cell = grid.index(start->x, start->y);
                if (grid.passable[cell] && clear_segment(base->center, cell_point(field.cells()[cell])))
                    access.entrances.push_back({cell});
            }
        } else {
            const double clearance = footprint + vehicle_radius_q12 + 2;
            const double perimeter = clearance + 0x980;
            const double sight = std::max(0, base->sight_radius_q12);
            std::vector<std::pair<double, std::size_t>> fallback;
            for (std::size_t cell = 0; cell < grid.passable.size(); ++cell) {
                if (!grid.passable[cell]) continue;
                const auto distance = squared_distance(field.cells()[cell], base->center);
                if (distance < clearance * clearance) continue;
                if (distance <= perimeter * perimeter) {
                    const auto edge = footprint_edge(field.cells()[cell], *base, vehicle_radius_q12);
                    if (clear_segment(edge, cell_point(field.cells()[cell]))) {
                        access.entrances.push_back({cell});
                        continue;
                    }
                }
                if (distance <= sight * sight) fallback.emplace_back(distance, cell);
            }
            std::sort(fallback.begin(), fallback.end());
            for (std::size_t rank = 0; rank < fallback.size(); ++rank) {
                access.entrances.push_back({fallback[rank].second,
                    (2ULL * grid.passable.size() + rank + 1U) * route_cost_limit});
            }
        }
        access.regular_entrances = access.entrances.size();
    }
    for (const auto& factory : factories)
        if (factory.faction_index > 0 && factory.faction_index <= base_access.size())
            base_access[factory.faction_index - 1].has_factory = true;
    for (const auto& factory : factory_access) {
        if (factory.faction == 0 || factory.faction > base_access.size()) continue;
        auto& access = base_access[factory.faction - 1];
        // In a mixed route, prefer a regular start where one can connect.
        for (const auto& apron : factory.aprons) {
            const uint64_t penalty = 8ULL * grid.passable.size() * route_cost_limit;
            access.entrances.push_back({apron.cell, penalty + static_cast<uint64_t>(std::lround(apron.cost)), &apron});
        }
    }
    result.base_count = static_cast<std::size_t>(std::count_if(base_access.begin(), base_access.end(),
        [](const auto& access) { return access.base || access.has_factory; }));
    result.usable_base_count = static_cast<std::size_t>(std::count_if(base_access.begin(), base_access.end(),
        [](const auto& access) { return !access.entrances.empty(); }));

    const auto step_cost = route_step_costs(grid);
    std::vector<std::size_t> network_root(result.cells.size(), no_cell);
    const auto find_root = [&](std::size_t cell) {
        while (network_root[cell] != cell) {
            network_root[cell] = network_root[network_root[cell]];
            cell = network_root[cell];
        }
        return cell;
    };
    const auto mark_main = [&](std::size_t cell) {
        if (result.cells[cell] == AccessRouteCell::Main) return;
        result.cells[cell] = AccessRouteCell::Main;
        network_root[cell] = cell;
        ++result.base_networks;
    };
    const auto mark_edge = [&](std::size_t from, std::size_t to, AccessRouteCell kind, uint8_t factions = 0) {
        result.edges.push_back({std::min(from, to), std::max(from, to), kind, factions});
        if (kind != AccessRouteCell::Main) return;
        mark_main(from);
        mark_main(to);
        const auto a = find_root(from), b = find_root(to);
        if (a != b) {
            network_root[b] = a;
            --result.base_networks;
        }
    };
    const auto mark_lead = [&](std::size_t cell, MovementPoint point, AccessRouteCell kind) {
        const auto& start = field.cells()[cell];
        if (start.world_x_q12 != point.x || start.world_y_q12 != point.y)
            result.leads.push_back({cell, point, kind});
    };
    const auto mark_apron = [&](const FactoryApron& apron) {
        auto& lane = result.production_lanes[apron.lane];
        const auto depth = [&](MovementPoint point) {
            return std::hypot(double(point.x) - lane.exit.x, double(point.y) - lane.exit.y);
        };
        if (!lane.runout || depth(*lane.runout) < depth(apron.runout))
            lane.runout = apron.runout;
        mark_lead(apron.cell, apron.runout, AccessRouteCell::Factory);
    };
    std::vector<bool> factory_starts(base_access.size());
    const auto mark_start = [&](std::size_t cell, std::size_t faction, std::span<const Entrance> entrances) {
        const auto& access = base_access[faction];
        const Entrance* entrance = nullptr;
        for (const auto& candidate : entrances)
            if (candidate.cell == cell && (!entrance || candidate.cost < entrance->cost))
                entrance = &candidate;
        if (entrance->apron) {
            mark_apron(*entrance->apron);
            factory_starts[faction] = true;
        } else if (entrance->cost != 0) {
            result.relocated_starts.push_back(cell);
        } else {
            const auto& base = *access.base;
            const auto endpoint = base.footprint_radius_q12 > 0
                ? footprint_edge(field.cells()[cell], base, vehicle_radius_q12) : base.center;
            mark_lead(cell, endpoint, AccessRouteCell::Main);
        }
    };
    std::vector<bool> connected(base_access.size());
    for (std::size_t from = 0; options.bases && from < base_access.size(); ++from) {
        const auto& start = base_access[from];
        if (start.entrances.empty()) continue;
        for (std::size_t to = base_access.size(); to > from + 1; --to) {
            const auto& target = base_access[to - 1];
            if (target.entrances.empty()) continue;
            const std::span<const Entrance> all_starts = start.entrances, all_targets = target.entrances;
            std::span<const Entrance> starts, targets;
            Distances distances;
            const auto try_pair = [&](std::span<const Entrance> from_entrances, std::span<const Entrance> to_entrances) {
                if (from_entrances.empty() || to_entrances.empty()) return;
                starts = from_entrances;
                targets = to_entrances;
                distances = find_routes(field, grid, result.cells, starts, targets, step_cost);
            };
            try_pair(all_starts.first(start.regular_entrances), all_targets.first(target.regular_entrances));
            if (distances.reached == no_cell)
                try_pair(all_starts.subspan(start.regular_entrances), all_targets.subspan(target.regular_entrances));
            if (distances.reached == no_cell &&
                (start.regular_entrances != all_starts.size() || target.regular_entrances != all_targets.size()))
                try_pair(all_starts, all_targets);
            if (distances.reached == no_cell) continue;
            const auto faction_mask = static_cast<uint8_t>((from < 8 ? 1U << from : 0U) |
                (to <= 8 ? 1U << (to - 1) : 0U));
            if (options.alternatives) {
                const auto primary = route_path(distances, distances.reached, distances.arrival_from);
                std::unordered_set<std::size_t> used;
                for (std::size_t i = 1; i < primary.size(); ++i)
                    used.insert(route_edge_key(primary[i - 1], primary[i], grid.passable.size()));
                const auto endpoint = [](std::span<const Entrance> candidates, std::size_t cell) {
                    Entrance best{cell, unreachable};
                    for (const auto& candidate : candidates)
                        if (candidate.cell == cell && candidate.cost < best.cost) best = candidate;
                    return best;
                };
                const auto fixed_start = endpoint(starts, primary.back());
                const auto fixed_target = endpoint(targets, primary.front());
                const auto avoid = alternate_route_cost(grid, used);
                const auto try_detour = [&](std::span<const uint8_t> cost, bool require_short_detour) {
                    const auto alternate = find_routes(field, grid, result.cells, std::span(&fixed_start, 1),
                        std::span(&fixed_target, 1), step_cost, cost);
                    if (alternate.reached == no_cell) return false;
                    const auto path = route_path(alternate, alternate.reached, alternate.arrival_from);
                    if (useful_alternate(path, primary.size(), avoid, require_short_detour)) {
                        ++result.alternate_base_pairs;
                        for (std::size_t i = 1; i < path.size(); ++i) {
                            const auto [first, second] = std::minmax(path[i - 1], path[i]);
                            if (!used.contains(route_edge_key(first, second, grid.passable.size())))
                                result.alternate_base_edges.push_back({first, second, AccessRouteCell::Main, faction_mask});
                        }
                        return true;
                    }
                    return false;
                };
                // Model a blocked central corridor while leaving the common
                // entrances available. A real backup may be much longer.
                bool backup = false;
                for (const auto& crossing : primary_route_crossings(field, grid, primary)) {
                    if (std::find(crossing.begin(), crossing.end(), fixed_start.cell) != crossing.end() ||
                        std::find(crossing.begin(), crossing.end(), fixed_target.cell) != crossing.end()) continue;
                    auto blocked = avoid;
                    for (const auto cell : crossing) blocked[cell] = closed_route_cell;
                    if (try_detour(blocked, false)) {
                        backup = true;
                        break;
                    }
                }
                if (!backup && primary.size() >= 9) {
                    const auto end_clearance = std::clamp(primary.size() / 10, std::size_t{3}, std::size_t{8});
                    std::unordered_set<std::size_t> corridor;
                    for (std::size_t i = end_clearance + 1; i < primary.size() - end_clearance; ++i)
                        corridor.insert(route_edge_key(primary[i - 1], primary[i], grid.passable.size()));
                    const auto interior = alternate_route_cost(grid, corridor);
                    auto blocked = avoid;
                    for (std::size_t cell = 0; cell < blocked.size(); ++cell)
                        if (interior[cell] >= 16) blocked[cell] = closed_route_cell;
                    blocked[fixed_start.cell] = blocked[fixed_target.cell] = 0;
                    backup = try_detour(blocked, false);
                }
                if (!backup) try_detour(avoid, true);
            }
            ++result.connected_pairs;
            connected[from] = connected[to - 1] = true;
            auto cell = distances.reached;
            mark_start(cell, to - 1, targets);
            if (distances.arrival_from != no_cell) {
                mark_edge(cell, distances.arrival_from, AccessRouteCell::Main, faction_mask);
                cell = distances.arrival_from;
            }
            while (distances.parent[cell] != no_cell) {
                const auto parent = distances.parent[cell];
                mark_edge(cell, parent, AccessRouteCell::Main, faction_mask);
                cell = parent;
            }
            mark_main(cell);
            mark_start(cell, from, starts);
        }
    }
    result.connected_bases = static_cast<std::size_t>(std::count(connected.begin(), connected.end(), true));
    result.factory_start_count = static_cast<std::size_t>(std::count(factory_starts.begin(), factory_starts.end(), true));
    if (result.base_networks && !factory_access.empty()) {
        const auto main_routes = find_routes(field, grid, result.cells, {}, {}, step_cost);
        for (const auto& factory : factory_access) {
            if (factory.aprons.empty() || result.production_lanes[factory.aprons.front().lane].runout) continue;
            const FactoryApron* selected = nullptr;
            std::size_t next = no_cell;
            double best_cost = std::numeric_limits<double>::infinity();
            for (const auto& apron : factory.aprons) {
                const auto cell = apron.cell;
                if (main_routes.cost[cell] == unreachable) continue;
                const auto consider = [&](std::size_t first_step, double cost) {
                    if (cost >= best_cost) return;
                    best_cost = cost;
                    selected = &apron;
                    next = first_step;
                };
                if (result.cells[cell] == AccessRouteCell::Main) {
                    consider(no_cell, apron.cost);
                    continue;
                }
                grid.visit_neighbors(cell, [&](std::size_t neighbor) {
                    if (main_routes.cost[neighbor] >= main_routes.cost[cell]) return;
                    const double alignment = factory_alignment(field, apron, neighbor);
                    if (alignment < -0.05) return;
                    consider(neighbor, apron.cost + (step_cost[cell] + step_cost[neighbor]) / 2.0 + 5 * (1 - alignment) +
                        static_cast<double>(main_routes.cost[neighbor]));
                });
            }
            if (!selected) {
                // The shortest path may approach the apron from behind. Try
                // a longer approach that meets the exit facing the right way.
                std::vector<Entrance> aprons;
                aprons.reserve(factory.aprons.size());
                for (const auto& apron : factory.aprons)
                    aprons.push_back({apron.cell, static_cast<uint64_t>(std::lround(apron.cost)), &apron});
                const auto approach = find_routes(field, grid, result.cells, {}, aprons, step_cost);
                if (approach.reached == no_cell) continue;
                const auto apron = std::find_if(factory.aprons.begin(), factory.aprons.end(), [&](const auto& a) {
                    return a.cell == approach.reached;
                });
                mark_apron(*apron);
                auto cell = approach.reached;
                auto parent = approach.arrival_from;
                while (parent != no_cell) {
                    if (result.cells[cell] == AccessRouteCell::None) result.cells[cell] = AccessRouteCell::Factory;
                    mark_edge(cell, parent, AccessRouteCell::Factory);
                    cell = parent;
                    parent = approach.parent[cell];
                }
                continue;
            }
            mark_apron(*selected);
            if (next != no_cell) {
                if (result.cells[selected->cell] == AccessRouteCell::None)
                    result.cells[selected->cell] = AccessRouteCell::Factory;
                mark_edge(selected->cell, next, AccessRouteCell::Factory);
                while (result.cells[next] != AccessRouteCell::Main) {
                    result.cells[next] = AccessRouteCell::Factory;
                    const auto parent = main_routes.parent[next];
                    mark_edge(next, parent, AccessRouteCell::Factory);
                    next = parent;
                }
            }
        }
    }
    result.connected_factories = static_cast<std::size_t>(std::count_if(
        result.production_lanes.begin(), result.production_lanes.end(),
        [](const auto& lane) { return lane.runout.has_value(); }));
    if (options.bases) {
        for (const auto& factory : factory_access) {
            if (factory.aprons.empty() || result.production_lanes[factory.aprons.front().lane].runout) continue;
            // Still show where a unit can leave the door on an isolated map.
            const auto nearest = std::min_element(factory.aprons.begin(), factory.aprons.end(),
                [](const auto& a, const auto& b) { return a.cost < b.cost; });
            mark_apron(*nearest);
        }
    }
    const auto edge_key = [](const AccessRouteEdge& edge) {
        return std::pair{edge.first, edge.second};
    };
    const auto merge_edges = [&](auto& edges) {
        std::sort(edges.begin(), edges.end(), [&](const auto& a, const auto& b) {
            return edge_key(a) == edge_key(b) ? a.kind > b.kind : edge_key(a) < edge_key(b);
        });
        auto unique = edges.begin();
        for (const auto& edge : edges) {
            if (unique != edges.begin() && edge_key(*(unique - 1)) == edge_key(edge))
                (unique - 1)->factions |= edge.factions;
            else *unique++ = edge;
        }
        edges.erase(unique, edges.end());
    };
    merge_edges(result.edges);
    merge_edges(result.alternate_base_edges);
    const auto lead_key = [](const AccessRouteLead& lead) {
        return std::tuple{lead.cell, lead.endpoint.x, lead.endpoint.y, lead.kind};
    };
    std::sort(result.leads.begin(), result.leads.end(), [&](const auto& a, const auto& b) {
        return lead_key(a) < lead_key(b);
    });
    result.leads.erase(std::unique(result.leads.begin(), result.leads.end(), [&](const auto& a, const auto& b) {
        return lead_key(a) == lead_key(b);
    }), result.leads.end());
    std::sort(result.relocated_starts.begin(), result.relocated_starts.end());
    result.relocated_starts.erase(std::unique(result.relocated_starts.begin(), result.relocated_starts.end()),
        result.relocated_starts.end());
    const auto deposits = options.resources || options.alternate_bases
        ? find_resource_deposits(field, grid, terrain_passable) : std::vector<DepositAccess>{};
    if (options.resources) {
        for (const auto& deposit : deposits) result.resource_deposits.push_back(deposit.marker);
        const auto cell_count = grid.passable.size();
        const std::vector<AccessRouteCell> empty_network(cell_count);
        std::unordered_map<std::size_t, ResourceRouteEdge> edges;
        for (std::size_t faction = 0; !deposits.empty() && faction < std::min<std::size_t>(8, base_access.size()); ++faction) {
            const auto& access = base_access[faction];
            if (access.entrances.empty()) continue;
            const uint8_t faction_bit = static_cast<uint8_t>(1U << faction);
            std::unordered_set<std::size_t> primary_edges;
            std::vector<const Entrance*> start_by_cell(cell_count);
            for (const auto& candidate : access.entrances) {
                auto& start = start_by_cell[candidate.cell];
                if (!start || candidate.cost < start->cost) start = &candidate;
            }
            std::vector<uint8_t> drawn_leads(cell_count);
            const auto mark_path = [&](std::span<const std::size_t> path, bool alternate) {
                for (std::size_t i = 1; i < path.size(); ++i) {
                    const auto [first, second] = std::minmax(path[i - 1], path[i]);
                    const auto key = route_edge_key(first, second, cell_count);
                    if (alternate && primary_edges.contains(key)) continue;
                    auto& edge = edges.try_emplace(key, ResourceRouteEdge{first, second}).first->second;
                    if (alternate) edge.alternate_factions |= faction_bit;
                    else {
                        edge.factions |= faction_bit;
                        primary_edges.insert(key);
                    }
                }
                const auto start = path.back();
                const uint8_t lead_kind = alternate ? 2U : 1U;
                if (drawn_leads[start] & lead_kind) return;
                drawn_leads[start] |= lead_kind;
                const auto* entrance = start_by_cell[start];
                const auto center = cell_point(field.cells()[start]);
                const auto add_lead = [&](MovementPoint from, MovementPoint to) {
                    if (from.x != to.x || from.y != to.y)
                        result.resource_leads.push_back({from, to, faction_bit, alternate});
                };
                if (entrance->apron) {
                    const auto& apron = *entrance->apron;
                    const auto& lane = result.production_lanes[apron.lane];
                    add_lead(lane.spawn, apron.runout);
                    add_lead(apron.runout, center);
                } else if (entrance->cost == 0) {
                    const auto& base = *access.base;
                    const auto endpoint = base.footprint_radius_q12 > 0
                        ? footprint_edge(field.cells()[start], base, vehicle_radius_q12) : base.center;
                    add_lead(endpoint, center);
                }
            };
            const auto primary = find_routes(field, grid, empty_network, access.entrances, {}, step_cost);
            std::vector<std::size_t> endpoints(deposits.size(), no_cell), lengths(deposits.size());
            for (std::size_t i = 0; i < deposits.size(); ++i) {
                uint64_t best = unreachable;
                for (auto cell : deposits[i].entrances) {
                    if (primary.cost[cell] >= best) continue;
                    best = primary.cost[cell];
                    endpoints[i] = cell;
                }
                if (endpoints[i] == no_cell) continue;
                result.resource_deposits[i].reachable_factions |= faction_bit;
                const auto path = route_path(primary, endpoints[i]);
                lengths[i] = path.size();
                mark_path(path, false);
            }
            if (!options.alternatives || primary_edges.empty()) continue;
            const auto avoid = alternate_route_cost(grid, primary_edges);
            const auto alternate = find_routes(field, grid, empty_network, access.entrances, {}, step_cost, avoid);
            for (std::size_t i = 0; i < endpoints.size(); ++i) {
                const auto cell = endpoints[i];
                if (cell == no_cell || alternate.cost[cell] == unreachable) continue;
                const auto path = route_path(alternate, cell);
                if (useful_alternate(path, lengths[i], avoid))
                    mark_path(path, true);
            }
        }
        result.resource_edges.reserve(edges.size());
        for (const auto& [key, edge] : edges) result.resource_edges.push_back(edge);
        std::sort(result.resource_edges.begin(), result.resource_edges.end(), [&](const auto& a, const auto& b) {
            return std::tie(a.first, a.second) < std::tie(b.first, b.second);
        });
        const auto resource_lead_key = [](const ResourceRouteLead& lead) {
            return std::tie(lead.first.x, lead.first.y, lead.second.x, lead.second.y, lead.faction, lead.alternate);
        };
        auto& leads = result.resource_leads;
        std::sort(leads.begin(), leads.end(), [&](const auto& a, const auto& b) {
            return resource_lead_key(a) < resource_lead_key(b);
        });
        leads.erase(std::unique(leads.begin(), leads.end(), [&](const auto& a, const auto& b) {
            return resource_lead_key(a) == resource_lead_key(b);
        }), leads.end());
    }
    if (options.defences || options.pioneer_sites || options.radar_sites)
        find_defence_sites(field, grid, terrain_passable, obstacles, factories, vehicle_radius_q12,
            defence_placement, options.defences || options.pioneer_sites || options.radar_sites, result);
    if (options.radar_sites || options.pioneer_sites || options.alternate_bases) {
        const auto count = std::min<std::size_t>(7, bases.size());
        std::vector<std::vector<std::size_t>> sources(count);
        std::vector<bool> placed_pioneer(count);
        for (const auto& pioneer : defence_placement.pioneers) {
            if (!pioneer.faction_index || pioneer.faction_index > count) continue;
            const auto index = pioneer.faction_index - 1U;
            placed_pioneer[index] = true;
            const auto cell = retail_fog_world_to_cell(field, pioneer.position.x, pioneer.position.y);
            if (cell) {
                const auto flat = grid.index(cell->x, cell->y);
                if (grid.passable[flat] && clear_segment(pioneer.position, cell_point(field.cells()[flat])))
                    sources[index].push_back(flat);
            }
        }
        for (std::size_t faction = 0; faction < count; ++faction)
            if (!placed_pioneer[faction])
                for (const auto& entrance : base_access[faction].entrances)
                    if (entrance.cost == 0 || entrance.apron) sources[faction].push_back(entrance.cell);
        auto hostile_sources = sources;
        if (options.radar_sites || options.alternate_bases)
            for (std::size_t faction = 0; faction < count; ++faction)
                if (placed_pioneer[faction])
                    for (const auto& entrance : base_access[faction].entrances)
                        if (entrance.cost == 0 || entrance.apron) hostile_sources[faction].push_back(entrance.cell);
        find_planning_sites(field, grid, bases.first(count), sources, hostile_sources, deposits, obstacles, factories,
            step_cost, vehicle_radius_q12, options, defence_placement, result);
    }
    // Validate the same rounded bend the viewport draws. A blocked neighbor
    // only prevents smoothing when the curve itself would run into it.
    struct Junction {
        std::array<MovementPoint, 2> points{};
        unsigned count{};
        AccessRouteCell kind{};
    };
    std::vector<Junction> junctions(result.cells.size());
    const auto add_connection = [&](std::size_t cell, MovementPoint endpoint, AccessRouteCell kind) {
        auto& junction = junctions[cell];
        if (junction.count >= junction.points.size() || (junction.count != 0 && junction.kind != kind)) {
            junction.count = 3;
            return;
        }
        junction.kind = kind;
        junction.points[junction.count++] = endpoint;
    };
    for (const auto& edge : result.edges) {
        add_connection(edge.first, cell_point(field.cells()[edge.second]), edge.kind);
        add_connection(edge.second, cell_point(field.cells()[edge.first]), edge.kind);
    }
    for (const auto& lead : result.leads)
        add_connection(lead.cell, lead.endpoint, lead.kind);

    const auto corner_trim = [&](std::size_t cell, const Junction& junction) {
        if (junction.count != 2) return 0.F;
        const auto center = cell_point(field.cells()[cell]);
        const double ax = double(junction.points[0].x) - center.x, ay = double(junction.points[0].y) - center.y;
        const double bx = double(junction.points[1].x) - center.x, by = double(junction.points[1].y) - center.y;
        if (std::abs(ax * by - ay * bx) < 0.002 * std::abs(ax * bx + ay * by)) return 0.F;
        for (const float trim : {0.5F, 0.28F}) {
            const auto start = interpolate(center, junction.points[0], trim);
            const auto end = interpolate(center, junction.points[1], trim);
            auto previous = start;
            bool clear = true;
            for (int step = 1; step <= access_route_curve_steps; ++step) {
                const auto t = double(step) / access_route_curve_steps;
                const auto next = access_route_curve_point(start, center, end, t);
                if (!clear_segment(previous, next)) {
                    clear = false;
                    break;
                }
                previous = next;
            }
            if (clear) return trim;
        }
        return 0.F;
    };
    const auto store_trims = [&](std::vector<float>& trims) {
        trims.resize(junctions.size());
        for (std::size_t cell = 0; cell < junctions.size(); ++cell)
            trims[cell] = corner_trim(cell, junctions[cell]);
    };
    store_trims(result.base_corner_trim);
    if (!result.alternate_base_edges.empty()) {
        std::fill(junctions.begin(), junctions.end(), Junction{});
        for (const auto& edge : result.alternate_base_edges) {
            add_connection(edge.first, cell_point(field.cells()[edge.second]), edge.kind);
            add_connection(edge.second, cell_point(field.cells()[edge.first]), edge.kind);
        }
        store_trims(result.alternate_base_corner_trim);
    }
    const auto resource_trims = [&](bool alternate, std::vector<float>& trims) {
        std::fill(junctions.begin(), junctions.end(), Junction{});
        for (const auto& edge : result.resource_edges) {
            if (!(alternate ? edge.alternate_factions : edge.factions)) continue;
            add_connection(edge.first, cell_point(field.cells()[edge.second]), AccessRouteCell::Main);
            add_connection(edge.second, cell_point(field.cells()[edge.first]), AccessRouteCell::Main);
        }
        for (const auto& lead : result.resource_leads) {
            if (lead.alternate != alternate) continue;
            const auto cell = retail_fog_world_to_cell(field, lead.second.x, lead.second.y);
            if (cell) junctions[grid.index(cell->x, cell->y)].count = 3;
        }
        store_trims(trims);
    };
    if (!result.resource_edges.empty()) {
        resource_trims(false, result.resource_corner_trim);
        if (options.alternatives) resource_trims(true, result.alternate_resource_corner_trim);
    }
    return result;
}

AccessRoutes plan_map_access_routes(const AssetCatalog& catalog, const MapDocument& document, AccessRouteOptions options) {
    const auto& field = document.field();
    const auto placements = document.level().placements();
    const auto faction_count = std::min(document.player_ai_configuration().active_factions, 7U);
    std::vector<std::optional<AccessRouteBase>> bases(faction_count);
    std::vector<int> base_priority(faction_count);
    std::vector<AccessRouteObstacle> obstacles;
    std::vector<AccessRouteObstacle> existing_barriers;
    std::vector<AccessRoutePioneerStart> pioneers;
    std::vector<FogPreviewObserver> existing_observers;
    std::vector<MovementPoint> existing_headquarters;
    std::vector<AccessRouteFactory> factories;
    const bool planning = options.radar_sites || options.pioneer_sites || options.alternate_bases;
    const auto fog_height_offset = [](const ArmyAssetPreset& preset) {
        const auto& hierarchy = preset.resolved_model_hierarchy.empty() ? preset.model_hierarchy : preset.resolved_model_hierarchy;
        int32_t offset = 0;
        for (const auto& node : hierarchy)
            if (node.creates_runtime_node()) offset = std::max(offset, node.simulation.fog_reference_height_offset_q12);
        return offset;
    };
    std::size_t factory_count = 0;
    for (const auto& placement : placements) {
        const auto* preset = catalog.army_preset(placement.army_asset_id);
        if (!preset) continue;
        const MovementPoint center{placement.world_y_q12, placement.world_x_q12};
        const bool start = preset->visual_kind == PlacementVisualKind::PlayerStart;
        const bool building = preset->browser_kind == PlacementBrowserKind::Building;
        const bool retractable_barrier = preset->runtime_model_class_id == 23U &&
            preset->vertical_deployment_step_q12_per_tick > 0 &&
            preset->vertical_deployment_travel_limit_q12 < 0;
        const auto radius = preset->simulation_motion_profile.collision_radius_q12;
        const auto faction = placement.faction_index;
        if (options.alternate_bases && preset->registry_id == 300U)
            existing_headquarters.push_back(center);
        if (planning && (start || preset->registry_id == 50U))
            pioneers.push_back({center, faction});
        if (options.radar_sites && faction > 0 && faction <= faction_count && preset->fog_uncover_radius_q12 > 0) {
            const auto contact = preset->simulation_motion_profile.placement_contact_kind;
            const auto reference = std::clamp<int64_t>(int64_t(retail_placement_height_q12(field, center, contact)) +
                fog_height_offset(*preset), std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max());
            existing_observers.push_back({center.x, center.y, static_cast<int32_t>(reference),
                static_cast<uint32_t>(preset->fog_uncover_radius_q12),
                retail_self_faction_visibility_mask(faction, retail_visibility_marker_for_model_class(preset->runtime_model_class_id))});
        }
        if (faction > 0 && faction <= faction_count) {
            const bool ground_unit = preset->browser_kind == PlacementBrowserKind::Unit &&
                preset->has_traversal_profile && preset->traversal_high_band_index != 0U;
            const bool headquarters = preset->registry_id == 300U;
            const int priority = headquarters ? 4 : start ? 3 :
                building && preset->runtime_model_class_id != 23U ? 2 : ground_unit ? 1 : 0;
            if (priority > base_priority[faction - 1]) {
                bases[faction - 1] = AccessRouteBase{center, building ? radius : 0,
                    headquarters ? preset->fog_uncover_radius_q12 : 0};
                base_priority[faction - 1] = priority;
            }
        }
        // Retractable walls and tank obstacles can lower out of the corridor.
        // Parked units can move away and crushable scenery can be driven over.
        if (!start && !retractable_barrier && preset->browser_kind != PlacementBrowserKind::Unit && radius > 0 &&
            !simulation_lab_crushable_scenery(*preset, faction))
            obstacles.push_back({center, radius});
        if ((options.defences || planning) && retractable_barrier && radius > 0)
            existing_barriers.push_back({center, radius});
        if (!building || preset->runtime_model_class_id != 13U ||
            preset->simulation_motion_profile.placement_contact_kind == 1) continue;
        ++factory_count;
        try {
            auto hierarchy = resolve_faction_technology_hierarchy(*preset,
                document.faction_technology_mask(std::min(faction, 7U)));
            if (hierarchy.empty())
                hierarchy = preset->resolved_model_hierarchy.empty() ? preset->model_hierarchy : preset->resolved_model_hierarchy;
            const auto root = std::find_if(hierarchy.begin(), hierarchy.end(), [](const auto& node) {
                return node.parent_index == UINT32_MAX;
            });
            if (root == hierarchy.end() || root->sprite_path.empty()) continue;
            const auto sprite = SpriteAsset::parse(document.load_asset(catalog, root->sprite_path));
            const auto* spawn = sprite.find_lookup_record(
                ModelLookupKeyIndex{0U}, ModelLookupKeyClass{5U});
            const auto* exit = sprite.find_lookup_record(
                ModelLookupKeyIndex{1U}, ModelLookupKeyClass{5U});
            if (!spawn || !exit) continue;
            const auto world_point = [&](Vec3f local) {
                const auto point = simulation_lab_factory_world_point(field,
                    {placement.render_x(), placement.render_y(), 0.0F}, placement.orientation_turn16,
                    root->simulation.placement_contact_kind, sprite.placement_height_offset(), local);
                return MovementPoint{float_to_q12(point.x), float_to_q12(point.y)};
            };
            factories.push_back({world_point(spawn->local_translation), world_point(exit->local_translation), center, faction});
        } catch (const std::exception&) {
            // A factory with missing model data is reported as unconnected.
        }
    }
    const auto* pioneer = catalog.army_preset(50U);
    const auto traversal_band = pioneer && pioneer->has_traversal_profile
        ? pioneer->traversal_high_band_index : 4U;
    AccessRouteDefencePlacement defence_placement;
    if (options.defences || planning) {
        if (const auto* wall = catalog.army_preset(381U)) {
            defence_placement.wall_radius_q12 = wall->simulation_motion_profile.collision_radius_q12;
            defence_placement.wall_contact_kind = wall->simulation_motion_profile.placement_contact_kind;
        }
        if (const auto* turret = catalog.army_preset(360U)) {
            defence_placement.turret_radius_q12 = turret->simulation_motion_profile.collision_radius_q12;
            defence_placement.turret_contact_kind = turret->simulation_motion_profile.placement_contact_kind;
        }
        defence_placement.existing_barriers = existing_barriers;
        defence_placement.allied_groups = document.level().relation_state8_faction_group_masks();
        defence_placement.truce_groups = document.level().relation_state4_faction_group_masks();
    }
    if (planning) {
        const auto footprint = [&](uint32_t id) -> AccessRouteBuildingFootprint {
            const auto* preset = catalog.army_preset(id);
            if (!preset) return {};
            return {preset->simulation_motion_profile.collision_radius_q12,
                preset->simulation_motion_profile.placement_contact_kind};
        };
        defence_placement.radar = footprint(340U);
        defence_placement.headquarters = footprint(300U);
        defence_placement.factory = footprint(301U);
        defence_placement.pioneers = pioneers;
        defence_placement.existing_observers = existing_observers;
        defence_placement.existing_headquarters = existing_headquarters;
        if (const auto* radar = catalog.army_preset(340U)) {
            defence_placement.radar_visibility_radius_q12 = radar->fog_uncover_radius_q12;
            defence_placement.radar_reference_height_q12 = fog_height_offset(*radar);
        }
    }
    auto routes = plan_access_routes(field, traversal_band, bases, factories, obstacles,
        pioneer ? pioneer->simulation_motion_profile.collision_radius_q12 : 1500, options, defence_placement);
    routes.factory_count = factory_count;
    return routes;
}
}
