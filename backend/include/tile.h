#pragma once

#include <vector>
#include <cstddef>

class ThingBase;

// 地形类型（原 BiomeType）
enum class TerrainType {
    LAND,
    WATER,
    SHALLOW_RIVER,
    DEEP_RIVER,
    SHALLOW_OCEAN,
    DEEP_OCEAN,
    SAND,
    INLAND_SAND,
    HILLS,
    MOUNTAIN
};

struct TerrainTypeHash {
    std::size_t operator()(TerrainType t) const noexcept {
        return static_cast<std::size_t>(t);
    }
};

// 生物群系类型（原 ClimateZone）
enum class BiomeType {
    Temperate,
    Tropical,
    Frigid,
    Polar
};

struct Tile {
    TerrainType terrain { TerrainType::LAND };
    double elevation {0.0};
    double moisture {0.0};
    BiomeType biome { BiomeType::Temperate };
    int fertility {0};
    double longitude {0.0};
    double latitude {0.0};
    double temperature {0.0};
    int local_hour {0};
    double local_hour_fraction {0.0};
    // 局部亮度 (0.0 = 夜晚, 1.0 = 正午)
    double brightness {1.0};
    std::vector<ThingBase*> things;
};
