#pragma once

#include <memory>
#include <vector>

#include "utils.h"

class RacesRegistry;
class RaceBase;

class SpatialGrid {
public:
    SpatialGrid(int world_width, int world_height, double cell_size);

    void clear();
    void add(const std::shared_ptr<RaceBase>& race);
    void build(const RacesRegistry& registry);

    std::vector<std::shared_ptr<RaceBase>> get_nearby_races_broad(const Position& center, double radius) const;

    const auto& cells() const { return grid; }
    double get_cell_size() const { return cell_size; }
    int get_width() const { return grid_width; }
    int get_height() const { return grid_height; }

private:
    std::vector<std::vector<std::vector<std::shared_ptr<RaceBase>>>> grid;
    double cell_size;
    int grid_width;
    int grid_height;
};
