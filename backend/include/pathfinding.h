#pragma once

#include "utils.h"
#include <optional>
#include <vector>

class WorldGrid;

namespace pathfinding {

struct PathResult {
    std::vector<Position> path;
    double cost{0.0};
};

std::optional<PathResult> find_path_a_star(
    const Position& start_pos,
    const Position& goal_pos,
    const WorldGrid& grid,
    double max_cost);

} // namespace pathfinding
