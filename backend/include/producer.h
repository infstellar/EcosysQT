/*
生产者数据模型 - Producer 声明
将 Producer 从 species.h 迁移至本文件，并继承 ThingBase
*/

#pragma once

#include <random>
#include <optional>
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
    // 参数化竞争与时间缩放（通用）
    double expansion_boost;
    double min_growth_factor;
    double growth_time_scale_ms;
    double pending_growth{0.0};

    // 构造函数
    Producer(Position pos, const PlantParams& params);

    // 通用更新流程
    void decide(EcosystemState& ecosystem_state, std::mt19937& rng) override;
    void apply(const EcosystemState& ecosystem_state) override;

    // 通用繁殖判断与实现（可被子类覆盖）
    bool can_reproduce() const override;
    std::unique_ptr<ThingBase> reproduce(const EcosystemState& ecosystem_state) override;
};