#pragma once

#include <vector>

class ThingBase;

enum class BiomeType {
    LAND,
    WATER
};

struct Tile {
    BiomeType biome { BiomeType::LAND };
    double elevation {0.0};
    double moisture {0.0};
    std::vector<ThingBase*> things;
};
