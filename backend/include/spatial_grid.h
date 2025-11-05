#pragma once

#include <memory>
#include <vector>

#include "species.h"
#include "utils.h"

class SpeciesRegistry;

class SpatialGrid {
public:
    SpatialGrid(int world_width, int world_height, double cell_size);

    void clear();
    void add(const std::shared_ptr<Species>& species);
    void build(const SpeciesRegistry& registry);

    std::vector<std::shared_ptr<Species>> get_nearby_species_broad(const Position& center, double radius) const;

    const auto& cells() const { return grid; }
    double get_cell_size() const { return cell_size; }
    int get_width() const { return grid_width; }
    int get_height() const { return grid_height; }

private:
    std::vector<std::vector<std::vector<std::shared_ptr<Species>>>> grid;
    double cell_size;
    int grid_width;
    int grid_height;
};
