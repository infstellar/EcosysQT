/*
生产者（植物）基类实现
抽象生产者通用逻辑，供草/树/灌木等具体植物继承
*/

#include "producer.h"
#include "thing_base.h"
#include "species_params.h"
#include "ecosystem.h"
#include "tracy/Tracy.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <utility>
#include <vector>

// --- Producer ---

// Producer类的构造函数
Producer::Producer(Position pos, const PlantParams& params)
    // 初始化基类ThingBase的成员变量
    : ThingBase(pos, params.energy, params.max_age, params.reproduction_energy_cost),
      // 初始化Producer自身的成员变量
      base_growth_rate(params.base_growth_rate),
      reproduction_chance(params.reproduction_chance),
      competition_radius(params.competition_radius),
      max_competition_effect(params.max_competition_effect),
      base_reproduction_cooldown(params.reproduction_cooldown),
      expansion_boost(params.expansion_boost),
      min_growth_factor(params.min_growth_factor),
      growth_time_scale_ms(params.growth_time_scale_ms) {
}

// Producer的决策函数，每个tick调用一次
void Producer::decide(EcosystemState& ecosystem_state, std::mt19937& rng) {
    ZoneScoped; // Tracy性能分析作用域
    ThingBase::decide(ecosystem_state, rng); // 调用基类的决策逻辑
    if (!alive) return; // 如果已经死亡，则不执行任何操作
    // 单位制对齐：1秒=30 ticks；按推进的tick数量进行缩放，兼容不同帧率/速度
    const double dt_ticks = ecosystem_state.get_delta_ticks(); // 获取时间增量
    // 定义四个基本方向（上、下、左、右）的偏移量
    static constexpr std::array<std::pair<int, int>, 4> kCardinalOffsets{{\
        {0, -1}, {1, 0}, {0, 1}, {-1, 0}\
    }};
    // 定义四个对角线方向的偏移量
    static constexpr std::array<std::pair<int, int>, 4> kDiagonalOffsets{{\
        {1, -1}, {1, 1}, {-1, 1}, {-1, -1}\
    }};

    // 固定检查周围八个格子（Moore 邻域），不再依赖 competition_radius
    std::vector<std::pair<int, int>> neighbor_offsets; // 存储邻居的偏移量
    neighbor_offsets.reserve(8);
    // 插入基本方向与对角线方向（共8邻居）
    neighbor_offsets.insert(neighbor_offsets.end(), kCardinalOffsets.begin(), kCardinalOffsets.end());
    neighbor_offsets.insert(neighbor_offsets.end(), kDiagonalOffsets.begin(), kDiagonalOffsets.end());

    int nearby_same_species = 0; // 周围同种种子的数量
    // 遍历所有邻居位置
    for (const auto& [dx, dy] : neighbor_offsets) {
        const int nx = m_grid_x + dx; // 计算邻居的x坐标
        const int ny = m_grid_y + dy; // 计算邻居的y坐标
        // 检查坐标是否有效
        if (!ecosystem_state.is_valid_grid_coord(nx, ny)) {
            continue;
        }
        const Tile& tile = ecosystem_state.get_tile(nx, ny); // 获取对应的地块
        // 遍历地块上的所有物体
        for (ThingBase* occupant : tile.things) {
            if (!occupant || !occupant->alive || occupant == this) {
                continue; // 忽略无效、死亡或自身的物体
            }
            // 如果是同种植物，则计数器加一
            if (occupant->species_name == species_name) {
                ++nearby_same_species;
            }
        }
    }

    const double neighbor_slots = static_cast<double>(neighbor_offsets.size()); // 邻居位置的总数
    // 计算密度
    double density = neighbor_slots > 0.0 ? std::min(1.0, nearby_same_species / neighbor_slots) : 0.0;
    double competition_factor = 1.0; // 竞争因子
    // 如果密度很低，则有扩张增益
    if (density <= std::numeric_limits<double>::epsilon()) {
        competition_factor = expansion_boost;
    } else {
        // 否则，根据密度计算竞争因子
        competition_factor = 1.0 - (std::pow(density, 0.3) * max_competition_effect);
    }
    // 调整生长速率
    double adjusted_growth_rate = base_growth_rate * competition_factor;
    double min_growth_rate = base_growth_rate * min_growth_factor; // 最小生长速率
    // 计算待处理的生长量
    pending_growth = std::max(min_growth_rate, adjusted_growth_rate) * dt_ticks;

    // 检查是否满足繁殖条件
    const bool ready_for_birth = alive && energy >= reproduction_energy_cost * 2 && reproduction_cooldown <= 0;
    if (ready_for_birth && !pending_spawn_position.has_value()) {
        std::uniform_real_distribution<> chance_dist(0.0, 1.0); // 创建一个均匀分布的随机数生成器
        // 如果随机数小于等于繁殖概率
        if (chance_dist(rng) <= reproduction_chance) {
            std::vector<std::pair<int, int>> candidate_tiles; // 候选的出生地块
            candidate_tiles.reserve(neighbor_offsets.size()); // 预分配内存
            // 遍历邻居位置
            for (const auto& [dx, dy] : neighbor_offsets) {
                const int nx = m_grid_x + dx; // 计算邻居的x坐标
                const int ny = m_grid_y + dy; // 计算邻居的y坐标
                // 检查坐标是否有效
                if (!ecosystem_state.is_valid_grid_coord(nx, ny)) {
                    continue;
                }
                const Tile& tile = ecosystem_state.get_tile(nx, ny); // 获取对应的地块
                // 检查地块是否为陆地
                if (tile.biome != BiomeType::LAND) {
                    continue;
                }
                // 检查地块是否被占用
                const bool occupied = std::any_of(tile.things.begin(), tile.things.end(), [](ThingBase* existing) {
                    return existing && existing->alive;
                });
                if (!occupied) {
                    // 如果未被占用，则添加到候选地块列表
                    candidate_tiles.emplace_back(nx, ny);
                }
            }

            if (!candidate_tiles.empty()) {
                // 如果有候选地块
                std::shuffle(candidate_tiles.begin(), candidate_tiles.end(), rng); // 随机打乱候选地块
                const auto [spawn_x, spawn_y] = candidate_tiles.front(); // 选择第一个作为出生地
                // 计算出生位置
                Position spawn_pos{static_cast<double>(spawn_x) + 0.5, static_cast<double>(spawn_y) + 0.5};
                pending_spawn_position = spawn_pos; // 设置待处理的出生位置
                energy -= reproduction_energy_cost; // 消耗繁殖能量
                reproduction_cooldown = base_reproduction_cooldown; // 重置繁殖冷却时间
                // 提交繁殖请求
                ecosystem_state.submit_interaction_request(AttemptToReproduceThingRequest{shared_from_this()});
            }
        }
    }
}

// Producer的应用函数，在决策之后调用
void Producer::apply(const EcosystemState& ecosystem_state) {
    ZoneScoped; // Tracy性能分析作用域
    ThingBase::apply(ecosystem_state); // 调用基类的apply函数
    if (!alive) { pending_growth = 0.0; return; } // 如果死亡，则重置待处理生长量并返回
    // 增加能量，但不超过最大能量
    energy = std::min(max_energy, energy + pending_growth);
    pending_growth = 0.0; // 重置待处理生长量
}

// 检查是否可以繁殖
bool Producer::can_reproduce() const {
    return ThingBase::can_reproduce(); // 调用基类的can_reproduce函数
}

// 繁殖函数
std::unique_ptr<ThingBase> Producer::reproduce(const EcosystemState& ecosystem_state) {
    (void)ecosystem_state; // 避免未使用参数的警告
    return nullptr; // Producer本身不直接繁殖，由子类实现
}
