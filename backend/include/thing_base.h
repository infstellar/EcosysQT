/*
网格物体基类 - ThingBase
从 Species 基类提炼，移除 Position；坐标由其所在 Tile 决定
*/

#pragma once

#include <memory>
#include <optional>
#include <random>
#include <string>
#include <algorithm>
#include "utils.h"

// 前向声明
class EcosystemState;

// 网格静态/半静态物体的中间基类：独立于 Species（目前仍持有 position）
class ThingBase : public std::enable_shared_from_this<ThingBase> {
public:
    Position position;
    double energy;
    double max_energy;
    // 植物/事物的营养值：用于被食用时提供的能量结算基数
    double nutrition_value;
    int age;
    int max_age;
    bool alive;
    int reproduction_cooldown;
    std::string death_reason;
    std::string species_name;
    double min_reproduction_energy;
    // 网格索引（所在空间网格单元坐标），-1 表示未绑定
    int m_grid_x = -1;
    int m_grid_y = -1;
    std::optional<Position> pending_spawn_position;
    // 可选的渲染变体索引（由后端在创建时随机分配，例如草的贴图变体 0..2）
    int variant_index = -1;

    // 构造函数
    ThingBase(Position pos,
              double energy = 100,
              int max_age = 100,
              double min_reproduction_energy = 50);

    virtual ~ThingBase() = default;

    // 决策阶段
    virtual void decide(EcosystemState& ecosystem_state, std::mt19937& rng);
    // 应用阶段
    virtual void apply(const EcosystemState& ecosystem_state);

    // 繁殖能力/行为（默认不繁殖）
    virtual bool can_reproduce() const;
    virtual std::unique_ptr<ThingBase> reproduce(const EcosystemState& ecosystem_state);

    // 生命周期（保持覆盖能力）
    virtual void age_one_step();
    virtual void die(const std::string& reason = "Unknown");
    virtual void die_from_old_age();
    virtual void die_from_starvation();
    virtual void die_from_predation(const std::string& predator_name);

    // 被食用时的营养值访问接口（与当前 energy 脱钩）
    virtual double get_nutrition_value() const { return std::max(0.0, nutrition_value); }

    std::optional<Position> consume_pending_spawn_position();
};