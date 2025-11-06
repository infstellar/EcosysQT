/*
物种数据模型
定义生态系统中的基础物种类和具体物种实现 (C++ 迁移版本)
*/

#pragma once

#include <random>
#include <optional>
#include "utils.h"

// 前向声明
class EcosystemState;

// 物种类型枚举 (用于统计、注册等)
enum class SpeciesType {
    GRASS,
    COW,
    TIGER
};

// 生态系统中所有物种的基类
class Species : public std::enable_shared_from_this<Species> {
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

    // 构造函数
    Species(Position pos, double energy = 100, int max_age = 100, double reproduction_energy_cost = 50);

    // 阶段 2: 决策阶段 - 仅读共享状态，允许修改自身局部状态
    virtual void decide(EcosystemState& ecosystem_state, std::mt19937& rng);
    // 阶段 4: 应用阶段 - 只写自身状态，读取共享状态
    virtual void apply(const EcosystemState& ecosystem_state);

    // 检查物种是否可以繁殖
    virtual bool can_reproduce() const;
    // 繁殖以创建新个体
    virtual std::unique_ptr<Species> reproduce(const EcosystemState& ecosystem_state);
    // 在世界边界内随机移动
    virtual void move_randomly(int world_width, int world_height, double speed, std::mt19937& rng);
    // 年龄增加一步
    virtual void age_one_step();
    // 标记为死亡并记录原因
    virtual void die(const std::string& reason = "Unknown");
    // 标记为老死
    virtual void die_from_old_age();
    // 标记为饿死
    virtual void die_from_starvation();
    // 标记为被捕食死亡
    virtual void die_from_predation(const std::string& predator_name);
    // 虚析构函数，用于安全的多态删除
    virtual ~Species() = default;

    // --- 阶段化更新暂存 ---
    std::optional<Position> pending_spawn_position;
    std::optional<Position> consume_pending_spawn_position();
};

// 草、牛、虎等具体物种类已迁移至独立头文件 animal.h / producer.h。