/*
生产者数据模型 - Producer 声明
将 Producer 从 species.h 迁移至本文件，并继承 ThingBase
*/

#pragma once

#include <random>
#include <optional>
#include <vector>
#include <utility>
#include "utils.h"
#include "thing_base.h"

// 前向声明
class EcosystemState;
struct PlantParams;

// 生产者（植物）基类，继承自 ThingBase，抽象出生产者通用逻辑
class Producer : public ThingBase {
public:
    double base_growth_rate;
    double reproduction_chance;
    double competition_radius;
    double max_competition_effect;
    int base_reproduction_cooldown;
    double interval_growth_rate;
    // 参数化竞争与时间缩放（通用）
    double expansion_boost;
    double min_growth_factor;
    double pending_growth{0.0};
    // 累积式繁殖模型：阈值、速率与当前积累值
    double repro_energy_threshold{0.0};
    double repro_energy_accumulation_rate{0.0};
    double reproduction_energy_accumulated{0.0};

    // 构造函数（带 RNG，用于初始化随机 Tick 偏移）
    Producer(Position pos, const PlantParams& params, std::mt19937& rng);

    // 通用更新流程
    void decide(EcosystemState& ecosystem_state, std::mt19937& rng) override;
    void apply(const EcosystemState& ecosystem_state) override;

    // 通用繁殖判断与实现（可被子类覆盖）
    bool can_reproduce() const override;
    std::unique_ptr<ThingBase> reproduce(const EcosystemState& ecosystem_state) override;

protected:
    // 每个实例的随机 Tick 偏移，用于与全局 Tick 解耦
    int m_tick_offset{0};
    // 生长逻辑：根据邻居密度计算 pending_growth
    void compute_growth(const EcosystemState& ecosystem_state);
    // 繁殖逻辑：根据条件尝试设置 pending_spawn_position 并提交请求
    void attempt_reproduction(EcosystemState& ecosystem_state, std::mt19937& rng);
    // 邻居偏移构建（Moore 邻域，8方向）
    static std::vector<std::pair<int, int>> build_neighbor_offsets();
    static constexpr int GROWTH_CHECK_INTERVAL = 180;
    static constexpr int REPRODUCTION_CHECK_INTERVAL = 360;
    static constexpr int MAX_INTERVAL = 360;
};