#pragma once

#include "utils.h"
#include "tile.h"
#include <cstddef>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

class WorldGrid;

namespace pathfinding {

struct PathResult {
    std::vector<Position> path;
    double cost;
};

struct GridPoint {
    int x{0};
    int y{0};

    constexpr bool operator==(const GridPoint& other) const noexcept {
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

struct PathfindingSettings {
    bool enable_smoothing{false};
    const std::unordered_map<TerrainType, double, TerrainTypeHash>* terrain_cost_overrides{nullptr};
    double min_traversal_cost{1.0};
    std::size_t max_iterations{0}; // 0 = unlimited
};

using GoalCondition = std::function<std::optional<Position>(const GridPoint&, const WorldGrid&)>;

std::optional<PathResult> find_path_a_star(
    const Position& start_pos,
    const Position& goal_pos,
    const WorldGrid& grid,
    double max_cost,
    PathfindingSettings settings = {});

std::optional<PathResult> find_path_a_star_to_condition(
    const Position& start_pos,
    const GoalCondition& is_goal,
    const WorldGrid& grid,
    double max_cost,
    PathfindingSettings settings = {});

} // namespace pathfinding
