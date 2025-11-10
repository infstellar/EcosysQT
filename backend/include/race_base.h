/*
移动物体基类 - RaceBase
从 Species 基类提炼，保留 Position 与常用生命周期/行为接口
*/

#pragma once

#include <memory>
#include <optional>
#include <random>
#include <string>
#include "utils.h"

// 前向声明
class EcosystemState;

// 移动物体的中间基类：独立于 Species，保留移动相关扩展
class RaceBase : public std::enable_shared_from_this<RaceBase> {
public:
    Position position;
    double energy;
    double max_energy;
    int age;
    int max_age;
    bool alive;
    int reproduction_cooldown;
    std::string death_reason;
    std::string species_name;
    double reproduction_energy_cost;
    std::optional<Position> pending_spawn_position;

    // 构造函数
    RaceBase(Position pos,
             double energy = 100,
             int max_age = 100,
             double reproduction_energy_cost = 50);

    virtual ~RaceBase() = default;

    // 决策阶段
    virtual void decide(EcosystemState& ecosystem_state, std::mt19937& rng);
    // 应用阶段
    virtual void apply(const EcosystemState& ecosystem_state);

    // 繁殖能力/行为（默认不繁殖）
    virtual bool can_reproduce() const;
    virtual std::unique_ptr<RaceBase> reproduce(const EcosystemState& ecosystem_state);

    // 随机移动（仅移动类需要）
    virtual void move_randomly(int world_width, int world_height, double speed, std::mt19937& rng);

    // 生命周期（保持覆盖能力）
    virtual void age_one_step();
    virtual void die(const std::string& reason = "Unknown");
    virtual void die_from_old_age();
    virtual void die_from_starvation();
    virtual void die_from_predation(const std::string& predator_name);

    std::optional<Position> consume_pending_spawn_position();
};