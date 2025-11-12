#include "map_generator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <string>
#include <vector>

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

    if (logger) {
        logger->info("[MapGenerator] Phase 1: Calculating elevation and geography...");
    }

    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            Tile& tile = grid.get_tile(x, y);

            calculate_lat_lon(x, y, base_latitude, base_longitude, tile.latitude, tile.longitude);

            const double elevation_noise = static_cast<double>(m_elevation_noise.GetNoise(static_cast<float>(x), static_cast<float>(y)));
            tile.elevation = elevation_noise;

            const double moisture_noise = static_cast<double>(m_moisture_noise.GetNoise(static_cast<float>(x), static_cast<float>(y)));
            tile.moisture = moisture_noise;
        }
    }

    if (logger) {
        logger->info("[MapGenerator] Phase 2: Simulating hydrology...");
    }

    // Phase 3 -----------------------------------------------------------------
    if (logger) {
        logger->info("[MapGenerator] Phase 3: Post-processing moisture (Ocean Proximity)...");
    }

    // Precompute commonly used sizes to avoid referencing undeclared identifiers
    const std::size_t width_sz = static_cast<std::size_t>(m_width);
    const std::size_t map_size = width_sz * static_cast<std::size_t>(m_height);

    std::vector<int> distance_to_water(map_size, -1);
    std::queue<std::pair<int, int>> bfs_queue;
    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            const Tile& tile = grid.get_tile(x, y);
            if (tile.terrain == TerrainType::SHALLOW_OCEAN || tile.terrain == TerrainType::DEEP_OCEAN) {
                const std::size_t idx = static_cast<std::size_t>(y) * width_sz + static_cast<std::size_t>(x);
                distance_to_water[idx] = 0;
                bfs_queue.emplace(x, y);
            }
        }
    }

    if (logger) {
        logger->info("[MapGenerator]   BFS queue initialized with {} ocean tiles.", static_cast<std::size_t>(bfs_queue.size()));
    }

    const std::array<std::pair<int, int>, 4> cardinal_dirs = {{{0, 1}, {0, -1}, {1, 0}, {-1, 0}}};
    while (!bfs_queue.empty()) {
        const auto [cx, cy] = bfs_queue.front();
        bfs_queue.pop();
        const std::size_t current_idx = static_cast<std::size_t>(cy) * width_sz + static_cast<std::size_t>(cx);
        const int current_dist = distance_to_water[current_idx];

        for (const auto& [dx, dy] : cardinal_dirs) {
            const int nx = cx + dx;
            const int ny = cy + dy;
            if (!grid.is_valid_coord(nx, ny)) {
                continue;
            }
            const std::size_t next_idx = static_cast<std::size_t>(ny) * width_sz + static_cast<std::size_t>(nx);
            if (distance_to_water[next_idx] != -1) {
                continue;
            }
            distance_to_water[next_idx] = current_dist + 1;
            bfs_queue.emplace(nx, ny);
        }
    }

    constexpr double kMaxInfluenceDistance = 100.0;
    constexpr double kMoistureReductionScale = 1.2;
    constexpr int kCoastalBufferTiles = 6;
    int modified_tiles = 0;
    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * width_sz + static_cast<std::size_t>(x);
            const int dist = distance_to_water[idx];
            if (dist <= 0 || dist <= kCoastalBufferTiles) {
                continue;
            }

            Tile& tile = grid.get_tile(x, y);
            const double continental_factor = std::clamp(static_cast<double>(dist) / kMaxInfluenceDistance, 0.0, 1.0);
            const double moisture_reduction = continental_factor * kMoistureReductionScale;
            const double adjusted_moisture = tile.moisture - moisture_reduction;
            tile.moisture = std::clamp(adjusted_moisture, -1.0, 1.0);
            ++modified_tiles;
        }
    }
    if (logger) {
        logger->info("[MapGenerator]   Moisture modified for {} land tiles based on distance.", modified_tiles);
    }

    // ------------------------------------------------------------------
    // --- 修改：Phase 3.5 - 盛行风（仅增湿模型） ---
    // ------------------------------------------------------------------
    if (logger) {
        logger->info("[MapGenerator] Phase 3.5: Simulating prevailing winds (Additive, Direction: {})...", m_config.wind_direction);
    }

    if (m_config.wind_direction != "None") {
        const double wind_strength = std::clamp(m_config.wind_strength, 0.0, 1.0);

        if (wind_strength > 0.0) {
            const bool wind_blows_west = (m_config.wind_direction == "West");
            const int windward_x = wind_blows_west ? (m_width - 1) : 0;

            std::vector<int> distance_to_windward_coast(map_size, -1);
            std::queue<std::pair<int, int>> wind_q;

            for (int y = 0; y < m_height; ++y) {
                const Tile& tile = grid.get_tile(windward_x, y);
                if (tile.terrain == TerrainType::SHALLOW_OCEAN || tile.terrain == TerrainType::DEEP_OCEAN) {
                    const std::size_t idx = static_cast<std::size_t>(y) * width_sz + static_cast<std::size_t>(windward_x);
                    distance_to_windward_coast[idx] = 0;
                    wind_q.push({windward_x, y});
                }
            }

            if (logger) {
                logger->info("[MapGenerator]   Wind BFS queue initialized with {} windward ocean tiles.", static_cast<std::size_t>(wind_q.size()));
            }

            const std::array<std::pair<int, int>, 4> cardinal_dirs_wind = {{{0, 1}, {0, -1}, {1, 0}, {-1, 0}}};

            while (!wind_q.empty()) {
                const auto [cx, cy] = wind_q.front();
                wind_q.pop();
                const std::size_t current_idx = static_cast<std::size_t>(cy) * width_sz + static_cast<std::size_t>(cx);
                const int current_dist = distance_to_windward_coast[current_idx];

                for (const auto& [dx, dy] : cardinal_dirs_wind) {
                    const int nx = cx + dx;
                    const int ny = cy + dy;
                    if (!grid.is_valid_coord(nx, ny)) {
                        continue;
                    }
                    const std::size_t next_idx = static_cast<std::size_t>(ny) * width_sz + static_cast<std::size_t>(nx);

                    if (distance_to_windward_coast[next_idx] != -1) {
                        continue;
                    }

                    const Tile& next_tile = grid.get_tile(nx, ny);
                    if (next_tile.terrain == TerrainType::SHALLOW_OCEAN || next_tile.terrain == TerrainType::DEEP_OCEAN) {
                        distance_to_windward_coast[next_idx] = 0;
                    } else {
                        distance_to_windward_coast[next_idx] = current_dist + 1;
                    }
                    wind_q.push({nx, ny});
                }
            }

            constexpr double kMaxWindInfluenceDistance = 150.0;
            int wind_modified_tiles = 0;

            for (int y = 0; y < m_height; ++y) {
                for (int x = 0; x < m_width; ++x) {
                    const std::size_t idx = static_cast<std::size_t>(y) * width_sz + static_cast<std::size_t>(x);
                    const int dist = distance_to_windward_coast[idx];

                    if (dist > 0) {
                        Tile& tile = grid.get_tile(x, y);

                        double wind_bonus_factor = std::clamp(1.0 - (static_cast<double>(dist) / kMaxWindInfluenceDistance), 0.0, 1.0);
                        const double wind_target_moisture = 1.0;
                        double moisture_boost = (wind_target_moisture - tile.moisture) * wind_bonus_factor * wind_strength;

                        if (moisture_boost > 0.0) {
                            tile.moisture += moisture_boost;
                            tile.moisture = std::clamp(tile.moisture, -1.0, 1.0);
                            ++wind_modified_tiles;
                        }
                    }
                }
            }
            if (logger) {
                logger->info("[MapGenerator]   Wind passively increased moisture for {} land tiles.", wind_modified_tiles);
            }
        }
    }
    // --- 盛行风阶段结束 ---
    // ------------------------------------------------------------------

    // Phase 4 -----------------------------------------------------------------
    if (logger) {
        logger->info("[MapGenerator] Phase 4: Simulating hydrology (using corrected moisture)...");
    }
    std::vector<std::pair<int, int>> flow_directions(map_size, {0, 0});
    std::vector<float> flow_map(map_size, 1.0f);

    CalculateFlowDirections(grid, flow_directions);
    CalculateFlowAccumulation(grid, flow_directions, flow_map);

    if (logger) {
        logger->info("[MapGenerator] Phase 3: Assigning terrain and biomes...");
    }
    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            Tile& tile = grid.get_tile(x, y);
            const std::size_t idx = static_cast<std::size_t>(y) * width_sz + static_cast<std::size_t>(x);
            const float flow = flow_map[idx];

            const double base_temp = 30.0 - (std::abs(tile.latitude) / 90.0) * 40.0;
            double temp_drop = 0.0;
            if (tile.elevation > 0.0) {
                constexpr double kMetersPerElevationUnit = 4000.0;
                constexpr double kLapseRatePerKm = 6.5;
                temp_drop = tile.elevation * kMetersPerElevationUnit * (kLapseRatePerKm / 1000.0);
            }

            const double effective_temperature = base_temp - temp_drop;
            tile.temperature = effective_temperature;

            const double scaled_moisture = std::clamp((tile.moisture + 1.0) * 0.5, 0.0, 1.0);

            tile.biome = assign_biome(effective_temperature, scaled_moisture);
            tile.terrain = assign_terrain(tile.elevation, flow);
        }
    }

    if (logger) {
        logger->info("[MapGenerator] Phase 3: Widening rivers (Erosion Pass)...");
    }

    const int widening_iterations = 2;
    const double carve_threshold = 0.02;

    for (int i = 0; i < widening_iterations; ++i) {
        std::vector<std::pair<int, int>> tiles_to_make_river;

        for (int y = 0; y < m_height; ++y) {
            for (int x = 0; x < m_width; ++x) {
                Tile& tile = grid.get_tile(x, y);

                if (tile.terrain == TerrainType::LAND || tile.terrain == TerrainType::HILLS) {
                    bool adjacent_to_river = false;
                    double lowest_river_neighbor_elevation = std::numeric_limits<double>::max();

                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (dx == 0 && dy == 0) {
                                continue;
                            }

                            const int nx = x + dx;
                            const int ny = y + dy;

                            if (!grid.is_valid_coord(nx, ny)) {
                                continue;
                            }

                            const Tile& neighbor = grid.get_tile(nx, ny);
                            if (neighbor.terrain == TerrainType::SHALLOW_RIVER || neighbor.terrain == TerrainType::DEEP_RIVER) {
                                adjacent_to_river = true;
                                lowest_river_neighbor_elevation = std::min(lowest_river_neighbor_elevation, neighbor.elevation);
                            }
                        }
                    }

                    if (adjacent_to_river && tile.elevation < (lowest_river_neighbor_elevation + carve_threshold)) {
                        tiles_to_make_river.emplace_back(x, y);
                    }
                }
            }
        }

        if (tiles_to_make_river.empty()) {
            if (logger) {
                logger->info("[MapGenerator]   Widening iteration {} had no effect, stopping.", i + 1);
            }
            break;
        }

        for (const auto& coords : tiles_to_make_river) {
            grid.get_tile(coords.first, coords.second).terrain = TerrainType::SHALLOW_RIVER;
        }

        if (logger) {
            logger->info(
                "[MapGenerator]   Widening iteration {}: converted {} LAND/HILLS tiles to SHALLOW_RIVER.",
                i + 1,
                tiles_to_make_river.size());
        }
    }

    if (logger) {
        logger->info("[MapGenerator] Phase 4: Generating coastal sand (Pass 2)...");
    }

    auto is_adjacent_to = [&](int x, int y, TerrainType targetTerrain) -> bool {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) {
                    continue;
                }

                const int nx = x + dx;
                const int ny = y + dy;

                if (grid.is_valid_coord(nx, ny)) {
                    if (grid.get_tile(nx, ny).terrain == targetTerrain) {
                        return true;
                    }
                }
            }
        }
        return false;
    };

    std::vector<std::pair<int, int>> tiles_to_make_sand;
    tiles_to_make_sand.reserve(static_cast<std::size_t>(m_width) * static_cast<std::size_t>(m_height) / 8);
    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            Tile& tile = grid.get_tile(x, y);
            if (tile.terrain != TerrainType::LAND) {
                continue;
            }

            if (is_adjacent_to(x, y, TerrainType::SHALLOW_OCEAN) ||
                is_adjacent_to(x, y, TerrainType::SHALLOW_RIVER) ||
                is_adjacent_to(x, y, TerrainType::DEEP_RIVER)) {
                tiles_to_make_sand.emplace_back(x, y);
            }
        }
    }

    for (const auto& coords : tiles_to_make_sand) {
        grid.get_tile(coords.first, coords.second).terrain = TerrainType::SAND;
    }

    if (logger) {
        logger->info("[MapGenerator]   Converted {} LAND tiles to SAND.", tiles_to_make_sand.size());
    }

    if (logger) {
        logger->info("[MapGenerator] Phase 5: Refining terrain based on biomes (Pass 5)...");
    }

    int refined_tiles = 0;
    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            Tile& tile = grid.get_tile(x, y);

            if (tile.biome == BiomeType::Desert && tile.terrain == TerrainType::LAND) {
                tile.terrain = TerrainType::INLAND_SAND;
                ++refined_tiles;
            } else if (tile.biome == BiomeType::Tundra && tile.terrain == TerrainType::LAND) {
                tile.terrain = TerrainType::INLAND_SAND;
                ++refined_tiles;
            } else if (tile.biome == BiomeType::PolarIce) {
                if (tile.terrain == TerrainType::LAND || tile.terrain == TerrainType::HILLS || tile.terrain == TerrainType::SAND) {
                    tile.terrain = TerrainType::INLAND_SAND;
                    ++refined_tiles;
                }
            }
        }
    }

    if (logger) {
        logger->info("[MapGenerator]   Refined {} tiles based on biome coupling.", refined_tiles);
        logger->info("[MapGenerator] Generation complete.");
    }
}

void MapGenerator::CalculateFlowDirections(WorldGrid& grid, std::vector<std::pair<int, int>>& flow_directions) const {
    const std::array<std::pair<int, int>, 8> neighbors = {{{-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}}};
    const std::size_t width_sz = static_cast<std::size_t>(m_width);

    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            const Tile& tile = grid.get_tile(x, y);
            double min_elevation = tile.elevation;
            std::pair<int, int> lowest_neighbor{0, 0};

            for (const auto& [dx, dy] : neighbors) {
                const int nx = x + dx;
                const int ny = y + dy;
                if (nx < 0 || nx >= m_width || ny < 0 || ny >= m_height) {
                    continue;
                }

                const double neighbor_elevation = grid.get_tile(nx, ny).elevation;
                if (neighbor_elevation < min_elevation) {
                    min_elevation = neighbor_elevation;
                    lowest_neighbor = {dx, dy};
                }
            }

            flow_directions[static_cast<std::size_t>(y) * width_sz + static_cast<std::size_t>(x)] = lowest_neighbor;
        }
    }
}

void MapGenerator::CalculateFlowAccumulation(
    WorldGrid& grid,
    const std::vector<std::pair<int, int>>& flow_directions,
    std::vector<float>& flow_map) const {
    const std::size_t map_size = flow_map.size();
    std::vector<std::size_t> indices(map_size);
    std::iota(indices.begin(), indices.end(), 0);

    const std::size_t width_sz = static_cast<std::size_t>(m_width);

    const auto get_elevation = [&](std::size_t idx) {
        const int x = static_cast<int>(idx % width_sz);
        const int y = static_cast<int>(idx / width_sz);
        return grid.get_tile(x, y).elevation;
    };

    std::sort(indices.begin(), indices.end(), [&](std::size_t a, std::size_t b) {
        return get_elevation(a) > get_elevation(b);
    });

    for (const std::size_t idx : indices) {
        const auto& direction = flow_directions[idx];
        if (direction.first == 0 && direction.second == 0) {
            continue;
        }

        const int x = static_cast<int>(idx % width_sz);
        const int y = static_cast<int>(idx / width_sz);
        const int next_x = x + direction.first;
        const int next_y = y + direction.second;

        if (next_x < 0 || next_x >= m_width || next_y < 0 || next_y >= m_height) {
            continue;
        }

        const std::size_t next_idx = static_cast<std::size_t>(next_y) * width_sz + static_cast<std::size_t>(next_x);
        flow_map[next_idx] += flow_map[idx];
    }
}

TerrainType MapGenerator::assign_terrain_pre_pass(double elevation) const {
    if (elevation < m_config.deep_sea_level) {
        return TerrainType::DEEP_OCEAN;
    }
    if (elevation < m_config.sea_level) {
        return TerrainType::SHALLOW_OCEAN;
    }
    if (elevation > 0.85) {
        return TerrainType::MOUNTAIN;
    }
    if (elevation > 0.7) {
        return TerrainType::HILLS;
    }
    return TerrainType::LAND;
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

BiomeType MapGenerator::assign_biome(double temperature, double moisture) const {
    if (temperature < -5.0) {
        return BiomeType::PolarIce;
    }

    if (temperature < 2.0) {
        return BiomeType::Tundra;
    }

    if (temperature < 8.0) {
        if (moisture < 0.3) {
            return BiomeType::Grassland;
        }
        return BiomeType::BorealForest;
    }

    if (temperature < 18.0) {
        if (moisture < 0.15) {
            return BiomeType::Desert;
        }
        if (moisture < 0.4) {
            return BiomeType::Grassland;
        }
        if (moisture < 0.75) {
            return BiomeType::TemperateForest;
        }
        return BiomeType::TemperateRainforest;
    }

    if (moisture < 0.15) {
        return BiomeType::Desert;
    }
    if (moisture < 0.5) {
        return BiomeType::Savanna;
    }
    return BiomeType::TropicalForest;
}

TerrainType MapGenerator::assign_terrain(double elevation, float flow_accumulation) const {
    if (elevation < m_config.deep_sea_level) {
        return TerrainType::DEEP_OCEAN;
    }
    if (elevation < m_config.sea_level) {
        return TerrainType::SHALLOW_OCEAN;
    }

    if (elevation > 0.85) {
        return TerrainType::MOUNTAIN;
    }
    if (elevation > 0.7) {
        return TerrainType::HILLS;
    }

    if (flow_accumulation >= m_config.flow_river_threshold) {
        if (flow_accumulation >= m_config.flow_river_threshold * 5.0f) {
            return TerrainType::DEEP_RIVER;
        }
        return TerrainType::SHALLOW_RIVER;
    }

    return TerrainType::LAND;
}
