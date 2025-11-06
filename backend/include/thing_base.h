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

// 前向声明
class EcosystemState;

// 网格静态/半静态物体的基类（不直接持有浮点坐标）
class ThingBase : public std::enable_shared_from_this<ThingBase> {
public:
    // 注意：不包含 Position，位置由所属 Tile 管理

    // 通用生命/能量属性
    double energy;
    double max_energy;
    int age;
    int max_age;
    bool alive;
    int reproduction_cooldown;
    std::string death_reason;
    std::string species_name;
    double reproduction_energy_cost;

    // 构造函数（无 Position）
    ThingBase(double energy = 100,
              int max_age = 100,
              double reproduction_energy_cost = 50);

    // 决策阶段
    virtual void decide(EcosystemState& ecosystem_state, std::mt19937& rng);
    // 应用阶段
    virtual void apply(const EcosystemState& ecosystem_state);

    // 繁殖能力/行为（默认不繁殖）
    virtual bool can_reproduce() const;
    virtual std::unique_ptr<ThingBase> reproduce(const EcosystemState& ecosystem_state);

    // 生命周期
    virtual void age_one_step();
    virtual void die(const std::string& reason = "Unknown");
    virtual void die_from_old_age();
    virtual void die_from_starvation();
    virtual void die_from_predation(const std::string& predator_name);
    virtual ~ThingBase() = default;

    // 阶段化更新暂存
    std::optional<Position> pending_spawn_position;
    std::optional<Position> consume_pending_spawn_position();
};