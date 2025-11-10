#include "world_grid.h"

#include "thing_base.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

WorldGrid::WorldGrid(int width, int height) {
    resize(width, height);
}

void WorldGrid::resize(int width, int height) {
    const int new_width = std::max(0, width);
    const int new_height = std::max(0, height);
    const std::size_t expected_size = static_cast<std::size_t>(new_width) * static_cast<std::size_t>(new_height);
    if (m_width != new_width || m_height != new_height || m_tiles.size() != expected_size) {
        m_tiles.assign(expected_size, Tile{});
    }
    m_width = new_width;
    m_height = new_height;
}

void WorldGrid::clear_things() {
    for (auto& tile : m_tiles) {
        tile.things.clear();
    }
}

bool WorldGrid::is_valid_coord(int x, int y) const {
    return x >= 0 && x < m_width && y >= 0 && y < m_height;
}

std::size_t WorldGrid::get_index(int x, int y) const {
    if (!is_valid_coord(x, y)) {
        throw std::out_of_range("Grid coordinate out of range");
    }
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width) + static_cast<std::size_t>(x);
}

Tile& WorldGrid::get_tile(int x, int y) {
    return m_tiles[get_index(x, y)];
}

const Tile& WorldGrid::get_tile(int x, int y) const {
    return m_tiles[get_index(x, y)];
}

void WorldGrid::add_thing_to_tile(ThingBase* thing) {
    if (!thing) {
        return;
    }
    if (!is_valid_coord(thing->m_grid_x, thing->m_grid_y)) {
        throw std::out_of_range("Thing grid coordinate out of range");
    }
    Tile& tile = get_tile(thing->m_grid_x, thing->m_grid_y);
    tile.things.push_back(thing);
}

void WorldGrid::remove_thing_from_tile(ThingBase& thing) {
    if (!is_valid_coord(thing.m_grid_x, thing.m_grid_y)) {
        return;
    }
    Tile& tile = get_tile(thing.m_grid_x, thing.m_grid_y);
    auto it = std::remove(tile.things.begin(), tile.things.end(), &thing);
    if (it != tile.things.end()) {
        tile.things.erase(it, tile.things.end());
    }
}

std::vector<std::shared_ptr<ThingBase>> WorldGrid::get_nearby_things_broad(const Position& center, double radius) const {
    std::vector<std::shared_ptr<ThingBase>> nearby;
    if (radius < 0.0 || m_width <= 0 || m_height <= 0) {
        return nearby;
    }

    const double radius_sq = radius * radius;
    const int min_x = std::clamp(static_cast<int>(std::floor(center.x - radius)), 0, m_width - 1);
    const int max_x = std::clamp(static_cast<int>(std::floor(center.x + radius)), 0, m_width - 1);
    const int min_y = std::clamp(static_cast<int>(std::floor(center.y - radius)), 0, m_height - 1);
    const int max_y = std::clamp(static_cast<int>(std::floor(center.y + radius)), 0, m_height - 1);

    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            const Tile& tile = get_tile(x, y);
            for (ThingBase* thing_ptr : tile.things) {
                if (!thing_ptr || !thing_ptr->alive) {
                    continue;
                }

                const double dx = thing_ptr->position.x - center.x;
                const double dy = thing_ptr->position.y - center.y;
                if ((dx * dx + dy * dy) <= radius_sq) {
                    nearby.push_back(thing_ptr->shared_from_this());
                }
            }
        }
    }

    return nearby;
}
