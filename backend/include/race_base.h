/*
移动物体基类 - RaceBase
从 Species 基类提炼，保留 Position 与常用生命周期/行为接口
*/

#pragma once

#include <random>
#include <optional>
#include <memory>
#include <string>
#include "utils.h"
#include "species.h"

// 前向声明
class EcosystemState;

// 移动物体的中间基类：继承自 Species，保留移动相关扩展
class RaceBase : public Species {
public:
    // 继承 Species 的通用属性与接口

    // 构造函数（转发到 Species）
    RaceBase(Position pos,
             double energy = 100,
             int max_age = 100,
             double reproduction_energy_cost = 50);

    // 决策阶段
    void decide(EcosystemState& ecosystem_state, std::mt19937& rng) override;
    // 应用阶段
    void apply(const EcosystemState& ecosystem_state) override;

    // 繁殖能力/行为（默认不繁殖）
    bool can_reproduce() const override;
    std::unique_ptr<Species> reproduce(const EcosystemState& ecosystem_state) override;

    // 随机移动（仅移动类需要）
    virtual void move_randomly(int world_width, int world_height, double speed, std::mt19937& rng);

    // 生命周期（保持覆盖能力）
    void age_one_step() override;
    void die(const std::string& reason = "Unknown") override;
    void die_from_old_age() override;
    void die_from_starvation() override;
    void die_from_predation(const std::string& predator_name) override;
};