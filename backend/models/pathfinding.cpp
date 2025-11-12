#include "pathfinding.h"
#include "world_grid.h"
#include "tile.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <queue>
#include <unordered_map>

namespace pathfinding {
namespace {

struct GridPoint {
    int x;
    int y;

    bool operator==(const GridPoint& other) const {
        return x == other.x && y == other.y;
    }
};

struct GridPointHash {
    std::size_t operator()(const GridPoint& p) const noexcept {
        std::size_t seed = static_cast<std::size_t>(p.x);
        seed ^= static_cast<std::size_t>(p.y) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        return seed;
    }
};

struct Node {
    GridPoint pos{};
    double g_cost{0.0};
    double f_cost{0.0};
    GridPoint parent{};

    bool operator>(const Node& other) const {
        return f_cost > other.f_cost;
    }
};

constexpr double kDiagonalCostMultiplier = 1.4142135623730951; // sqrt(2)

inline double terrain_cost(TerrainType terrain) {
    switch (terrain) {
        case TerrainType::LAND:          return 1.0;
        case TerrainType::SAND:          return 1.5;
        case TerrainType::INLAND_SAND:   return 1.5;
        case TerrainType::HILLS:         return 3.0;
        case TerrainType::SHALLOW_RIVER: return 5.0;
        case TerrainType::MOUNTAIN:
        case TerrainType::DEEP_RIVER:
        case TerrainType::WATER:
        case TerrainType::SHALLOW_OCEAN:
        case TerrainType::DEEP_OCEAN:
        default:
            return std::numeric_limits<double>::infinity();
    }
}

inline double heuristic(const GridPoint& a, const GridPoint& b) {
    const int dx = std::abs(a.x - b.x);
    const int dy = std::abs(a.y - b.y);
    // Octile distance heuristic suits 8-neighbour grids
    const int min_d = std::min(dx, dy);
    const int max_d = std::max(dx, dy);
    return static_cast<double>(min_d) * kDiagonalCostMultiplier + static_cast<double>(max_d - min_d);
}

inline GridPoint to_grid_point(const Position& pos) {
    return GridPoint{static_cast<int>(std::floor(pos.x)), static_cast<int>(std::floor(pos.y))};
}

inline Position to_world_position(const GridPoint& gp) {
    return Position{static_cast<double>(gp.x) + 0.5, static_cast<double>(gp.y) + 0.5};
}

} // namespace

std::optional<PathResult> find_path_a_star(const Position& start_pos,
                                           const Position& goal_pos,
                                           const WorldGrid& grid,
                                           double max_cost) {
    const GridPoint start = to_grid_point(start_pos);
    const GridPoint goal = to_grid_point(goal_pos);

    if (!grid.is_valid_coord(start.x, start.y) || !grid.is_valid_coord(goal.x, goal.y)) {
        return std::nullopt;
    }

    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open_set;
    std::unordered_map<GridPoint, Node, GridPointHash> node_records;
    std::unordered_map<GridPoint, double, GridPointHash> best_g;

    Node start_node;
    start_node.pos = start;
    start_node.g_cost = 0.0;
    start_node.f_cost = heuristic(start, goal);
    start_node.parent = start;

    open_set.push(start_node);
    node_records[start] = start_node;
    best_g[start] = 0.0;

    const GridPoint directions[8] = {
        {0, 1}, {1, 0}, {0, -1}, {-1, 0},
        {1, 1}, {1, -1}, {-1, -1}, {-1, 1}
    };

    while (!open_set.empty()) {
        Node current = open_set.top();
        open_set.pop();

        const auto best_it = best_g.find(current.pos);
        if (best_it != best_g.end() && current.g_cost - best_it->second > 1e-9) {
            continue; // outdated entry
        }

        if (max_cost >= 0.0 && current.g_cost > max_cost) {
            continue;
        }

        if (current.pos == goal) {
            PathResult result;
            result.cost = current.g_cost;
            std::vector<Position> path;
            Node trace = current;
            while (!(trace.pos == start)) {
                path.push_back(to_world_position(trace.pos));
                trace = node_records.at(trace.parent);
            }
            std::reverse(path.begin(), path.end());
            path.push_back(goal_pos);
            result.path = std::move(path);
            return result;
        }

        for (int i = 0; i < 8; ++i) {
            const GridPoint neighbor{current.pos.x + directions[i].x, current.pos.y + directions[i].y};
            if (!grid.is_valid_coord(neighbor.x, neighbor.y)) {
                continue;
            }

            const double base_cost = terrain_cost(grid.get_tile(neighbor.x, neighbor.y).terrain);
            if (!std::isfinite(base_cost)) {
                continue;
            }

            double step_cost = base_cost;
            if (i >= 4) {
                step_cost *= kDiagonalCostMultiplier;
            }

            const double new_g = current.g_cost + step_cost;
            if (max_cost >= 0.0 && new_g > max_cost) {
                continue;
            }

            const auto best_neighbor = best_g.find(neighbor);
            if (best_neighbor != best_g.end() && new_g - best_neighbor->second >= 1e-9) {
                continue;
            }

            Node neighbor_node;
            neighbor_node.pos = neighbor;
            neighbor_node.g_cost = new_g;
            neighbor_node.f_cost = new_g + heuristic(neighbor, goal);
            neighbor_node.parent = current.pos;

            open_set.push(neighbor_node);
            node_records[neighbor] = neighbor_node;
            best_g[neighbor] = new_g;
        }
    }

    return std::nullopt;
}

} // namespace pathfinding
