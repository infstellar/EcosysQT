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

    // 额外步骤：使用轻量随机游走算法生成少数河流覆盖到 tile.terrain
    generate_rivers(grid, rng);

    if (logger) {
        logger->info("[MapGenerator] Generation complete.");
    }
}

// 轻量河流生成器实现：从随机边缘点开始，做带偏向的随机漫步，直到到达另一边或超长。
void MapGenerator::generate_rivers(WorldGrid& grid, std::mt19937& rng) const {
    auto logger = spdlog::get("ecosim");
    std::uniform_real_distribution<double> prob(0.0, 1.0);
    std::uniform_int_distribution<int> edge_choice(0, 3);
    std::uniform_int_distribution<int> width_variation(1, 3);

    // 简单参数：尝试次数与期望河流数基于地图规模
    const int max_attempts = std::max(3, (m_width + m_height) / 200);
    const int desired_rivers = std::max(1, std::min(3, (m_width * m_height) / (200 * 200)));

    int created = 0;
    for (int attempt = 0; attempt < max_attempts && created < desired_rivers; ++attempt) {
        // 随机选边并生成起点
        int side = edge_choice(rng);
        int x = 0, y = 0;
        switch (side) {
            case 0: x = 0; y = rng() % m_height; break; // left
            case 1: x = m_width - 1; y = rng() % m_height; break; // right
            case 2: x = rng() % m_width; y = 0; break; // top
            default: x = rng() % m_width; y = m_height - 1; break; // bottom
        }

        // 目标是任意其他边界
        std::uniform_int_distribution<int> step_choice(0, 2); // 0: straight (bias), 1: turn left, 2: turn right

        const int max_steps = std::max(10, (m_width + m_height) / 2);
        int steps = 0;
        int curx = x, cury = y;
        // 初始方向：朝向地图中心偏向
        double dirx = (m_width / 2.0) - curx;
        double diry = (m_height / 2.0) - cury;
        // normalize
        double len = std::sqrt(dirx*dirx + diry*diry);
        if (len == 0) { dirx = 1.0; diry = 0.0; }
        else { dirx /= len; diry /= len; }

        std::vector<std::pair<int,int>> path;
        path.reserve(max_steps);
        while (steps < max_steps) {
            path.emplace_back(curx, cury);
            // 停止条件：到达任一边界（且不是起点边）
            if ((curx == 0 || curx == m_width - 1 || cury == 0 || cury == m_height - 1) && !(curx == x && cury == y)) {
                break;
            }

            // 基于方向偏好与噪声决定步向
            // 候选方向为 8 邻域中的几个，优先靠近 dirx/diry
            double best_score = -1e9;
            int best_dx = 0, best_dy = 0;
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = curx + dx;
                    int ny = cury + dy;
                    if (nx < 0 || nx >= m_width || ny < 0 || ny >= m_height) continue;
                    double dot = (dx * dirx + dy * diry);
                    double jitter = (static_cast<double>(rng() % 100) / 100.0) - 0.5; // -0.5..0.5
                    double score = dot + jitter * 0.7; // 保留偏向，允许随机
                    if (score > best_score) {
                        best_score = score;
                        best_dx = dx; best_dy = dy;
                    }
                }
            }

            // 移动
            curx += best_dx;
            cury += best_dy;
            // 缓解过度停留
            if (curx < 0) curx = 0; if (curx >= m_width) curx = m_width - 1;
            if (cury < 0) cury = 0; if (cury >= m_height) cury = m_height - 1;

            // 逐步调整偏向，向当前位置到中心的方向靠拢
            dirx = (m_width / 2.0) - curx;
            diry = (m_height / 2.0) - cury;
            double l2 = std::sqrt(dirx*dirx + diry*diry);
            if (l2 != 0.0) { dirx /= l2; diry /= l2; }

            ++steps;
        }

        if (path.size() < 6) continue; // 太短的忽略

        // 给路径上每个点扩展宽度并标记为河
        int base_width = width_variation(rng); // 1..3
        for (const auto& p : path) {
            int px = p.first; int py = p.second;
            for (int wy = -base_width; wy <= base_width; ++wy) {
                for (int wx = -base_width; wx <= base_width; ++wx) {
                    int tx = px + wx; int ty = py + wy;
                    if (tx < 0 || tx >= m_width || ty < 0 || ty >= m_height) continue;
                    Tile& t = grid.get_tile(tx, ty);
                    // 不覆盖高海拔山脉
                    if (t.terrain == TerrainType::MOUNTAIN) continue;
                    // 深河 vs 浅河基于偏好与随机
                    double chance = static_cast<double>(rng() % 100) / 100.0;
                    if (chance < 0.25) t.terrain = TerrainType::DEEP_RIVER;
                    else t.terrain = TerrainType::SHALLOW_RIVER;
                }
            }
        }
        ++created;
        if (logger) logger->info("[MapGenerator] Generated river with {} path points", path.size());
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
