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
Producer::Producer(Position pos, const PlantParams& params, std::mt19937& rng)
    // 初始化基类ThingBase的成员变量
    : ThingBase(pos, params.energy, params.max_age, params.min_reproduction_energy),
      // 初始化Producer自身的成员变量
      base_growth_rate(params.base_growth_rate),
      reproduction_chance(params.reproduction_chance),
      competition_radius(params.competition_radius),
      max_competition_effect(params.max_competition_effect),
      base_reproduction_cooldown(params.reproduction_cooldown),
      expansion_boost(params.expansion_boost),
      min_growth_factor(params.min_growth_factor) {
    // 累积式繁殖参数初始化
    repro_energy_threshold = std::max(0.0, params.repro_energy_threshold);
    repro_energy_accumulation_rate = std::max(0.0, params.repro_energy_accumulation_rate);
    reproduction_energy_accumulated = 0.0;
    // 初始化随机 Tick 偏移（用于与全局 Tick 解耦）
    std::uniform_int_distribution<> dist(0, MAX_INTERVAL - 1);
    m_tick_offset = dist(rng);
    // 植物营养值：用于被食用时的能量结算
    nutrition_value = std::max(0.0, params.nutrition_value);
}

// Producer的决策函数，每个tick调用一次
void Producer::decide(EcosystemState& ecosystem_state, std::mt19937& rng) {
    // ZoneScoped; // Tracy性能分析作用域
    ThingBase::decide(ecosystem_state, rng); // 调用基类的决策逻辑
    if (!alive) return; // 如果已经死亡，则不执行任何操作

    const int current_tick = ecosystem_state.clock().time_step();
    // 生长逻辑：根据邻居密度计算 pending_growth
    if ((current_tick + m_tick_offset) % GROWTH_CHECK_INTERVAL == 0) {
        compute_growth(ecosystem_state);
        
    }
    
    if ((current_tick + m_tick_offset) % REPRODUCTION_CHECK_INTERVAL == 0) {
        attempt_reproduction(ecosystem_state, rng);
    }
    
}

// 生长逻辑：根据邻居密度计算 pending_growth
void Producer::compute_growth(const EcosystemState& ecosystem_state) {

    // base_growth_rate的单位是1250tick，所以我们需要
    interval_growth_rate = base_growth_rate;
    const auto neighbor_offsets = Producer::build_neighbor_offsets();
    int nearby_same_species = 0; // 周围同种种子的数量
    // 遍历所有邻居位置
    for (const auto& [dx, dy] : neighbor_offsets) {
        const int nx = m_grid_x + dx; // 计算邻居的x坐标
        const int ny = m_grid_y + dy; // 计算邻居的y坐标
        // 检查坐标是否有效
        if (!ecosystem_state.world_grid().is_valid_coord(nx, ny)) {
            continue;
        }
        const Tile& tile = ecosystem_state.world_grid().get_tile(nx, ny); // 获取对应的地块
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
    double adjusted_growth_rate = interval_growth_rate * competition_factor;
    double min_growth_rate = interval_growth_rate * min_growth_factor; // 最小生长速率
    // 计算待处理的生长量
    
    
    pending_growth = std::max(min_growth_rate, adjusted_growth_rate);
}

// 繁殖逻辑：无繁殖冷却+积攒足够能量时尝试繁殖
void Producer::attempt_reproduction(EcosystemState& ecosystem_state, std::mt19937& rng) {
    const auto neighbor_offsets = Producer::build_neighbor_offsets();
    const bool ready_for_birth = alive && reproduction_cooldown <= 0
        && reproduction_energy_accumulated >= repro_energy_threshold;
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
                if (!ecosystem_state.world_grid().is_valid_coord(nx, ny)) {
                    continue;
                }
                const Tile& tile = ecosystem_state.world_grid().get_tile(nx, ny); // 获取对应的地块
                // 检查地块是否为陆地
                if (tile.terrain != TerrainType::LAND) {
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
                // 不再扣能量：使用积累模型，繁殖后清零积累与重置冷却
                reproduction_cooldown = base_reproduction_cooldown; // 重置繁殖冷却时间
                reproduction_energy_accumulated = 0.0;              // 清零积累值
                // 提交繁殖请求
                ecosystem_state.submit_interaction_request(AttemptToReproduceThingRequest{shared_from_this()});
            }
        }
    }
}

// 邻居偏移构建（Moore 邻域，8方向）
std::vector<std::pair<int, int>> Producer::build_neighbor_offsets() {
    // 定义四个基本方向（上、下、左、右）的偏移量
    static constexpr std::array<std::pair<int, int>, 4> kCardinalOffsets{{
        {0, -1}, {1, 0}, {0, 1}, {-1, 0}
    }};
    // 定义四个对角线方向的偏移量
    static constexpr std::array<std::pair<int, int>, 4> kDiagonalOffsets{{
        {1, -1}, {1, 1}, {-1, 1}, {-1, -1}
    }};

    std::vector<std::pair<int, int>> neighbor_offsets;
    neighbor_offsets.reserve(8);
    neighbor_offsets.insert(neighbor_offsets.end(), kCardinalOffsets.begin(), kCardinalOffsets.end());
    neighbor_offsets.insert(neighbor_offsets.end(), kDiagonalOffsets.begin(), kDiagonalOffsets.end());
    return neighbor_offsets;
}

// Producer的应用函数，在决策之后调用
void Producer::apply(const EcosystemState& ecosystem_state) {
    ZoneScoped; // Tracy性能分析作用域
    ThingBase::apply(ecosystem_state); // 调用基类的apply函数
    if (!alive) { pending_growth = 0.0; return; } // 如果死亡，则重置待处理生长量并返回
    // 满能量时按速率累积“繁殖能量”
    if (energy + pending_growth >= max_energy ) {
        reproduction_energy_accumulated = std::min(repro_energy_threshold,
                                                reproduction_energy_accumulated + pending_growth);
        energy = max_energy;
    }else{
        energy = energy + pending_growth;
    }
    pending_growth = 0.0; // 重置待处理生长量
    
}

// 检查是否可以繁殖
bool Producer::can_reproduce() const {
    // 采用基于积累的判定：冷却结束且积累达到阈值
    return alive && reproduction_cooldown <= 0
        && reproduction_energy_accumulated >= repro_energy_threshold;
}

// 繁殖函数
std::unique_ptr<ThingBase> Producer::reproduce(const EcosystemState& ecosystem_state) {
    (void)ecosystem_state; // 避免未使用参数的警告
    return nullptr; // Producer本身不直接繁殖，由子类实现
}
