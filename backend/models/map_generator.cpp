#include "map_generator.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <spdlog/spdlog.h>

MapGenerator::MapGenerator(const EcosystemConfig& config)
    : m_config(config.map_gen_config),
      m_width(config.world_width),
      m_height(config.world_height),
      m_world_center{static_cast<double>(config.world_width) / 2.0, static_cast<double>(config.world_height) / 2.0} {}

void MapGenerator::generate_map(WorldGrid& grid, std::mt19937& rng) {
    auto logger = spdlog::get("ecosim");
    if (logger) {
        logger->info("[MapGenerator] Starting map generation {}x{}...", m_width, m_height);
    }

    std::uniform_int_distribution<int> seed_dist(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
    m_elevation_noise.SetSeed(seed_dist(rng));
    m_moisture_noise.SetSeed(seed_dist(rng));
    m_river_noise.SetSeed(seed_dist(rng));

    double base_latitude = m_config.base_latitude;
    double base_longitude = m_config.base_longitude;
    constexpr double RANDOM_SENTINEL = 999.0;
    if (std::abs(base_latitude - RANDOM_SENTINEL) < 1e-6) {
        std::uniform_real_distribution<double> lat_dist(-90.0, 90.0);
        base_latitude = lat_dist(rng);
    }
    if (std::abs(base_longitude - RANDOM_SENTINEL) < 1e-6) {
        std::uniform_real_distribution<double> lon_dist(-180.0, 180.0);
        base_longitude = lon_dist(rng);
    }
    if (logger) {
        logger->info("[MapGenerator] World center at Lat: {:.2f}, Lon: {:.2f} (from config/random)", base_latitude, base_longitude);
    }

    m_elevation_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_elevation_noise.SetFractalType(FastNoiseLite::FractalType_FBm);
    m_elevation_noise.SetFractalOctaves(5);
    m_elevation_noise.SetFractalLacunarity(2.0f);
    m_elevation_noise.SetFractalGain(0.5f);
    m_elevation_noise.SetFrequency(m_config.elevation_frequency);

    m_moisture_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_moisture_noise.SetFrequency(m_config.moisture_frequency);

    m_river_noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    m_river_noise.SetFractalType(FastNoiseLite::FractalType_Ridged);
    m_river_noise.SetFractalOctaves(3);
    m_river_noise.SetFrequency(m_config.river_frequency);

    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            Tile& tile = grid.get_tile(x, y);

            calculate_lat_lon(x, y, base_latitude, base_longitude, tile.latitude, tile.longitude);

            tile.elevation = static_cast<double>(m_elevation_noise.GetNoise(static_cast<float>(x), static_cast<float>(y)));
            tile.moisture = static_cast<double>(m_moisture_noise.GetNoise(static_cast<float>(x), static_cast<float>(y)));

            float river_value = std::abs(m_river_noise.GetNoise(static_cast<float>(x), static_cast<float>(y)));

            tile.biome = assign_biome(tile.latitude, tile.elevation, tile.moisture);
            tile.terrain = assign_terrain(tile.elevation, static_cast<double>(river_value));
        }
    }

    if (logger) {
        logger->info("[MapGenerator] Generation complete.");
    }
}

void MapGenerator::calculate_lat_lon(int x, int y, double base_lat, double base_lon, double& out_lat, double& out_lon) const {
    const double dx = static_cast<double>(x) - m_world_center.x;
    const double dy = static_cast<double>(y) - m_world_center.y;

    const double tiles_per_degree = (m_config.tiles_per_degree > 0.0) ? m_config.tiles_per_degree : 100.0;

    out_lon = base_lon + (dx / tiles_per_degree);
    out_lat = base_lat - (dy / tiles_per_degree);

    out_lat = std::clamp(out_lat, -90.0, 90.0);
    out_lon = std::fmod(out_lon + 540.0, 360.0) - 180.0;
}

BiomeType MapGenerator::assign_biome(double latitude, double elevation, double moisture) const {
    static_cast<void>(elevation);
    static_cast<void>(moisture);
    const double abs_lat = std::abs(latitude);
    if (abs_lat > 65.0) {
        return BiomeType::Polar;
    }
    if (abs_lat > 45.0) {
        return BiomeType::Frigid;
    }
    if (abs_lat > 20.0) {
        return BiomeType::Temperate;
    }
    return BiomeType::Tropical;
}

TerrainType MapGenerator::assign_terrain(double elevation, double river_value) const {
    if (river_value < static_cast<double>(m_config.river_threshold)) {
        return TerrainType::SHALLOW_RIVER;
    }

    if (elevation > 0.7) {
        return TerrainType::HILLS;
    }
    if (elevation < -0.6) {
        return TerrainType::SAND;
    }
    return TerrainType::LAND;
}
