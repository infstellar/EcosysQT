#pragma once

#include "utils.h"
#include "tile.h"
#include <optional>
#include <unordered_map>
#include <vector>

class WorldGrid;

namespace pathfinding {

struct PathResult {
    std::vector<Position> path;
    double cost;
};

struct PathfindingSettings {
    bool enable_smoothing{false};
    const std::unordered_map<TerrainType, double, TerrainTypeHash>* terrain_cost_overrides{nullptr};
    double min_traversal_cost{1.0};
};

std::optional<PathResult> find_path_a_star(
    const Position& start_pos,
    const Position& goal_pos,
    const WorldGrid& grid,
    double max_cost,
    PathfindingSettings settings = {});

} // namespace pathfinding
