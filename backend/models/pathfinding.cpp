#include "pathfinding.h"

#include "world_grid.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <unordered_map>
#include <utility>

namespace pathfinding {

namespace {
constexpr double kDiagonalMultiplier = 1.41421356237; // sqrt(2)

struct GridPoint {
    int x{0};
    int y{0};
    bool operator==(const GridPoint& other) const noexcept {
        return x == other.x && y == other.y;
    }
};

struct GridPointHash {
    std::size_t operator()(const GridPoint& p) const noexcept {
        const std::size_t hx = static_cast<std::size_t>(p.x);
        const std::size_t hy = static_cast<std::size_t>(p.y);
        return hx ^ (hy << 1);
    }
};

struct Node {
    GridPoint pos;
    double g_cost{0.0};
    double f_cost{0.0};
    GridPoint parent;

    bool operator>(const Node& other) const noexcept {
        return f_cost > other.f_cost;
    }
};

double base_terrain_cost(TerrainType terrain) {
    switch (terrain) {
        case TerrainType::LAND:          return 1.0;
        case TerrainType::SAND:          return 2.5;
        case TerrainType::INLAND_SAND:   return 3.5;
        case TerrainType::HILLS:         return 3.5;
        case TerrainType::SHALLOW_RIVER: return 10.0;
        case TerrainType::MOUNTAIN:
        case TerrainType::DEEP_RIVER:
        case TerrainType::WATER:
        case TerrainType::SHALLOW_OCEAN:
        case TerrainType::DEEP_OCEAN:
        default:
            return std::numeric_limits<double>::infinity();
    }
}

double terrain_cost(TerrainType terrain, const PathfindingSettings& settings) {
    if (settings.terrain_cost_overrides) {
        const auto it = settings.terrain_cost_overrides->find(terrain);
        if (it != settings.terrain_cost_overrides->end()) {
            return it->second;
        }
    }
    return base_terrain_cost(terrain);
}

inline bool is_traversable(TerrainType terrain, const PathfindingSettings& settings) {
    const double cost = terrain_cost(terrain, settings);
    return std::isfinite(cost) && cost > 0.0;
}

double heuristic(const GridPoint& a, const GridPoint& b, const PathfindingSettings& settings) {
    const int dx = std::abs(a.x - b.x);
    const int dy = std::abs(a.y - b.y);
    const double base_cost = std::max(settings.min_traversal_cost, 1e-6);
    const double straight_cost = base_cost;
    const double diagonal_cost = base_cost * kDiagonalMultiplier;
    return straight_cost * static_cast<double>(dx + dy) + (diagonal_cost - 2.0 * straight_cost) * static_cast<double>(std::min(dx, dy));
}

GridPoint clamp_to_grid(const Position& pos, const WorldGrid& grid) {
    const int max_x = std::max(0, grid.width() - 1);
    const int max_y = std::max(0, grid.height() - 1);
    const int clamped_x = std::clamp(static_cast<int>(std::floor(pos.x)), 0, max_x);
    const int clamped_y = std::clamp(static_cast<int>(std::floor(pos.y)), 0, max_y);
    return {clamped_x, clamped_y};
}

bool has_line_of_sight(const GridPoint& start,
                       const GridPoint& goal,
                       const WorldGrid& grid,
                       const PathfindingSettings& settings) {
    int x0 = start.x;
    int y0 = start.y;
    int x1 = goal.x;
    int y1 = goal.y;

    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    int x = x0;
    int y = y0;

    while (true) {
        if (!grid.is_valid_coord(x, y)) {
            return false;
        }
        if (!is_traversable(grid.get_tile(x, y).terrain, settings)) {
            return false;
        }
        if (x == x1 && y == y1) {
            break;
        }

        const int e2 = err << 1;
        const int prev_x = x;
        const int prev_y = y;

        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }

        if (x != prev_x && y != prev_y) {
            const int adj1_x = prev_x;
            const int adj1_y = y;
            const int adj2_x = x;
            const int adj2_y = prev_y;
            if (!grid.is_valid_coord(adj1_x, adj1_y) || !grid.is_valid_coord(adj2_x, adj2_y)) {
                return false;
            }
            if (!is_traversable(grid.get_tile(adj1_x, adj1_y).terrain, settings) ||
                !is_traversable(grid.get_tile(adj2_x, adj2_y).terrain, settings)) {
                return false;
            }
        }
    }

    return true;
}

std::vector<GridPoint> smooth_path(const std::vector<GridPoint>& points,
                                   const WorldGrid& grid,
                                   const PathfindingSettings& settings) {
    if (!settings.enable_smoothing || points.size() <= 2) {
        return points;
    }

    std::vector<GridPoint> smoothed;
    smoothed.reserve(points.size());
    smoothed.push_back(points.front());

    std::size_t anchor_idx = 0;
    while (anchor_idx + 1 < points.size()) {
        std::size_t furthest = anchor_idx + 1;
        for (std::size_t candidate = furthest + 1; candidate < points.size(); ++candidate) {
            if (has_line_of_sight(points[anchor_idx], points[candidate], grid, settings)) {
                furthest = candidate;
            } else {
                break;
            }
        }
        smoothed.push_back(points[furthest]);
        anchor_idx = furthest;
    }

    return smoothed;
}

} // namespace

std::optional<PathResult> find_path_a_star(const Position& start_pos,
                                           const Position& goal_pos,
                                           const WorldGrid& grid,
                                           double max_cost,
                                           PathfindingSettings settings) {
    if (grid.width() <= 0 || grid.height() <= 0) {
        return std::nullopt;
    }

    const GridPoint start = clamp_to_grid(start_pos, grid);
    const GridPoint goal = clamp_to_grid(goal_pos, grid);

    if (!grid.is_valid_coord(start.x, start.y) || !grid.is_valid_coord(goal.x, goal.y)) {
        return std::nullopt;
    }

    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open_set;
    std::unordered_map<GridPoint, Node, GridPointHash> nodes;

    const double heuristic_start = heuristic(start, goal, settings);
    open_set.push(Node{start, 0.0, heuristic_start, start});
    nodes[start] = Node{start, 0.0, heuristic_start, start};

    constexpr GridPoint kDirections[8] = {
        {0, 1}, {1, 0}, {0, -1}, {-1, 0},
        {1, 1}, {1, -1}, {-1, -1}, {-1, 1}
    };

    while (!open_set.empty()) {
        Node current = open_set.top();
        open_set.pop();

        if (current.pos == goal) {
            PathResult result;
            result.cost = current.g_cost;

            std::vector<GridPoint> grid_points;
            Node trace = current;
            while (true) {
                grid_points.push_back(trace.pos);
                if (trace.pos == start) {
                    break;
                }
                trace = nodes[trace.parent];
            }
            std::reverse(grid_points.begin(), grid_points.end());

            if (grid_points.empty()) {
                grid_points.push_back(start);
                grid_points.push_back(goal);
            } else if (!(grid_points.back() == goal)) {
                grid_points.push_back(goal);
            }

            std::vector<GridPoint> final_points = settings.enable_smoothing
                ? smooth_path(grid_points, grid, settings)
                : grid_points;

            std::vector<Position> path_positions;
            path_positions.reserve(final_points.size());
            for (std::size_t idx = 1; idx < final_points.size(); ++idx) {
                const GridPoint& gp = final_points[idx];
                path_positions.push_back(Position{
                    static_cast<double>(gp.x) + 0.5,
                    static_cast<double>(gp.y) + 0.5
                });
            }

            if (path_positions.empty()) {
                path_positions.push_back(goal_pos);
            } else {
                Position& last = path_positions.back();
                const double dx = last.x - goal_pos.x;
                const double dy = last.y - goal_pos.y;
                if ((dx * dx + dy * dy) > 1e-6) {
                    path_positions.push_back(goal_pos);
                } else {
                    last = goal_pos;
                }
            }

            result.path = std::move(path_positions);
            return result;
        }

        if (current.g_cost > max_cost) {
            continue;
        }

        for (int dir_idx = 0; dir_idx < 8; ++dir_idx) {
            const GridPoint neighbor{
                current.pos.x + kDirections[dir_idx].x,
                current.pos.y + kDirections[dir_idx].y
            };

            if (!grid.is_valid_coord(neighbor.x, neighbor.y)) {
                continue;
            }

            const TerrainType terrain = grid.get_tile(neighbor.x, neighbor.y).terrain;
            double move_cost = terrain_cost(terrain, settings);
            if (!std::isfinite(move_cost) || move_cost <= 0.0) {
                continue;
            }

            if (dir_idx >= 4) {
                move_cost *= kDiagonalMultiplier;
            }

            const double tentative_g = current.g_cost + move_cost;
            if (tentative_g > max_cost) {
                continue;
            }

            auto it = nodes.find(neighbor);
            if (it == nodes.end() || tentative_g < it->second.g_cost) {
                Node neighbor_node;
                neighbor_node.pos = neighbor;
                neighbor_node.parent = current.pos;
                neighbor_node.g_cost = tentative_g;
                neighbor_node.f_cost = tentative_g + heuristic(neighbor, goal, settings);

                open_set.push(neighbor_node);
                nodes[neighbor] = neighbor_node;
            }
        }
    }

    return std::nullopt;
}

} // namespace pathfinding
