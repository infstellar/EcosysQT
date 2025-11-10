#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "tile.h"
#include "utils.h"

class ThingBase;

class WorldGrid {
public:
    WorldGrid() = default;
    WorldGrid(int width, int height);

    void resize(int width, int height);
    void clear_things();

    bool is_valid_coord(int x, int y) const;
    std::size_t get_index(int x, int y) const;

    Tile& get_tile(int x, int y);
    const Tile& get_tile(int x, int y) const;

    void add_thing_to_tile(ThingBase* thing);
    void remove_thing_from_tile(ThingBase& thing);

    std::vector<std::shared_ptr<ThingBase>> get_nearby_things_broad(const Position& center, double radius) const;

    int width() const noexcept { return m_width; }
    int height() const noexcept { return m_height; }

private:
    int m_width{0};
    int m_height{0};
    std::vector<Tile> m_tiles;
};
