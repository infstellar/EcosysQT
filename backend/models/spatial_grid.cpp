#include "spatial_grid.h"

#include <algorithm>
#include <cmath>

#include "races_registry.h"

SpatialGrid::SpatialGrid(int world_width, int world_height, double cell)
    : cell_size(cell),
      grid_width(static_cast<int>(std::ceil(static_cast<double>(world_width) / cell_size))),
      grid_height(static_cast<int>(std::ceil(static_cast<double>(world_height) / cell_size))) {
    if (grid_width < 0) {
        grid_width = 0;
    }
    if (grid_height < 0) {
        grid_height = 0;
    }

    if (grid_width > 0 && grid_height > 0) {
        grid.resize(static_cast<std::size_t>(grid_width),
            std::vector<std::vector<std::shared_ptr<Species>>>(static_cast<std::size_t>(grid_height)));
    }
}

void SpatialGrid::clear() {
    for (auto& column : grid) {
        for (auto& cell : column) {
            cell.clear();
        }
    }
}

void SpatialGrid::add(const std::shared_ptr<Species>& species) {
    if (!species || !species->alive || cell_size <= 0.0 || grid_width <= 0 || grid_height <= 0) {
        return;
    }

    const double normalized_x = species->position.x / cell_size;
    const double normalized_y = species->position.y / cell_size;
    int cell_x = static_cast<int>(std::floor(normalized_x));
    int cell_y = static_cast<int>(std::floor(normalized_y));

    cell_x = std::clamp(cell_x, 0, grid_width - 1);
    cell_y = std::clamp(cell_y, 0, grid_height - 1);

    grid[static_cast<std::size_t>(cell_x)][static_cast<std::size_t>(cell_y)].push_back(species);
}

void SpatialGrid::build(const RacesRegistry& registry) {
    clear();

    if (cell_size <= 0.0 || grid_width <= 0 || grid_height <= 0) {
        return;
    }

    const auto species_names = registry.get_all_species_names();
    for (const auto& name : species_names) {
        const auto& list = registry.get_species_list(name);
        for (const auto& individual : list) {
            add(individual);
        }
    }
}

std::vector<std::shared_ptr<Species>> SpatialGrid::get_nearby_species_broad(const Position& center, double radius) const {
    std::vector<std::shared_ptr<Species>> nearby;
    if (cell_size <= 0.0 || grid_width <= 0 || grid_height <= 0) {
        return nearby;
    }

    const double inverse_cell = 1.0 / cell_size;
    const double min_x = (center.x - radius) * inverse_cell;
    const double max_x = (center.x + radius) * inverse_cell;
    const double min_y = (center.y - radius) * inverse_cell;
    const double max_y = (center.y + radius) * inverse_cell;

    const int x_min = static_cast<int>(std::floor(min_x));
    const int x_max = static_cast<int>(std::floor(max_x));
    const int y_min = static_cast<int>(std::floor(min_y));
    const int y_max = static_cast<int>(std::floor(max_y));

    for (int x = x_min; x <= x_max; ++x) {
        if (x < 0 || x >= grid_width) {
            continue;
        }
        for (int y = y_min; y <= y_max; ++y) {
            if (y < 0 || y >= grid_height) {
                continue;
            }
            const auto& cell = grid[static_cast<std::size_t>(x)][static_cast<std::size_t>(y)];
            nearby.insert(nearby.end(), cell.begin(), cell.end());
        }
    }

    return nearby;
}
