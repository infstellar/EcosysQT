/*
网格物体基类 - ThingBase
从 Species 基类提炼，移除 Position；坐标由其所在 Tile 决定
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

// 网格静态/半静态物体的中间基类：继承自 Species（目前仍持有 position）
class ThingBase : public Species {
public:
    // 网格索引（所在空间网格单元坐标），-1 表示未绑定
    int m_grid_x = -1;
    int m_grid_y = -1;

    // 构造函数（带 Position，转发到 Species）
    ThingBase(Position pos,
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

    // 生命周期（保持覆盖能力）
    void age_one_step() override;
    void die(const std::string& reason = "Unknown") override;
    void die_from_old_age() override;
    void die_from_starvation() override;
    void die_from_predation(const std::string& predator_name) override;
};