#pragma once

#include "ecosystem.h"
#include "tile.h"
#include "utils.h"
#include "world_grid.h"
#include "FastNoiseLite.h"

#include <random>

class MapGenerator {
public:
    explicit MapGenerator(const EcosystemConfig& config);

    void generate_map(WorldGrid& grid, std::mt19937& rng);

private:
    FastNoiseLite m_elevation_noise;
    FastNoiseLite m_moisture_noise;
    FastNoiseLite m_river_noise;

    const MapGenConfig m_config;

    const int m_width;
    const int m_height;
    Position m_world_center;

    void calculate_lat_lon(int x, int y, double base_lat, double base_lon, double& out_lat, double& out_lon) const;
    BiomeType assign_biome(double latitude, double elevation, double moisture) const;
    TerrainType assign_terrain(double elevation, double river_value) const;
};
