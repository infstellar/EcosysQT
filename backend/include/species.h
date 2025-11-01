/*
物种数据模型
定义生态系统中的基础物种类和具体物种实现 (C++ 迁移版本)
*/

#pragma once

#include "utils.h"

// 前向声明
class EcosystemState;

// 生态系统中所有物种的基类
class Species {
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

    // 更新物种状态 (虚函数用于多态)
    virtual void update(const EcosystemState& ecosystem_state);
    // 检查物种是否可以繁殖
    virtual bool can_reproduce() const;
    // 繁殖以创建新个体
    virtual std::unique_ptr<Species> reproduce(const EcosystemState& ecosystem_state);
    // 在世界边界内随机移动
    virtual void move_randomly(int world_width, int world_height, double speed = 1.0);
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
};

// 动物类，继承自Species，添加移动和狩猎逻辑
class Animal : public Species {
public:
    double movement_speed;
    int energy_consumption;
    double hunting_range;
    double hunting_success_rate;
    double detection_range;
    std::vector<std::string> food_types;
    int hunting_cooldown;
    int hunting_cooldown_duration;
    int min_reproduction_age;
    int base_reproduction_cooldown;
    double eating_range;

    Animal(Position pos, double energy = 100, int max_age = 100, double reproduction_energy_cost = 50,
           double movement_speed = 1.0, int energy_consumption = 1, double hunting_range = 5.0,
           double hunting_success_rate = 0.5, double detection_range = 500.0,
           std::vector<std::string> food_types = {}, int hunting_cooldown_duration = 0,
           int min_reproduction_age = 0, int base_reproduction_cooldown = 0,
           double eating_range = 0.0);

    // 寻找最近的食物来源
    virtual std::optional<Position> find_nearest_food(const EcosystemState& ecosystem_state);
    // 向目标位置移动
    void move_towards_target(const Position& target_position, int world_width, int world_height);
    // 智能移动：向食物移动或随机移动
    virtual void intelligent_move(const class EcosystemState& ecosystem_state);
    // 开始狩猎冷却
    void start_hunting_cooldown();
    // 通用繁殖判断（含年龄门槛）
    bool can_reproduce() const override;
    // 开始繁殖冷却（使用基础冷却值）
    void start_reproduction_cooldown();
    // 统一的繁殖实现：基于当前物种键创建子代
    std::unique_ptr<Species> reproduce(const EcosystemState& ecosystem_state) override;

protected:
    // 子类可覆盖的繁殖偏移半径（用于随机生成子代位置）
    virtual double reproduction_spawn_radius() const { return 10.0; }
};

// 草类，继承自Species，实现生产者逻辑
class Grass : public Species {
public:
    double base_growth_rate;
    double reproduction_chance;
    double competition_radius;
    double max_competition_effect;
    int base_reproduction_cooldown;
    Grass(Position pos, const struct GrassParams& params);

    // 计算附近草的密度 (优化版本)
    double calculate_nearby_grass_density_optimized(const EcosystemState& ecosystem_state);
    // 计算附近草的密度 (备用版本)
    double calculate_nearby_grass_density(const EcosystemState& ecosystem_state);
    // 获取根据竞争调整的生长率
    double get_competition_adjusted_growth_rate(const EcosystemState& ecosystem_state);
    // 更新草的状态
    void update(const class EcosystemState& ecosystem_state) override;
    // 检查草是否可以繁殖
    bool can_reproduce() const override;
    // 繁殖以创建新草
    std::unique_ptr<Species> reproduce(const EcosystemState& ecosystem_state) override;
};

// 牛类，继承自Animal，实现初级消费者逻辑
class Cow : public Animal {
public:
    Cow(Position pos, const struct CowParams& params);

    // 更新牛的状态
    void update(const class EcosystemState& ecosystem_state) override;
    // 从列表中吃草
    void _eat_grass(const std::vector<Grass*>& grass_list);
    // 检查牛是否可以繁殖
    bool can_reproduce() const override;
protected:
    double reproduction_spawn_radius() const override { return 10.0; }
};

// 老虎类，继承自Animal，实现次级消费者逻辑
class Tiger : public Animal {
public:
    Tiger(Position pos, const struct TigerParams& params);

    // 更新老虎的状态
    void update(const class EcosystemState& ecosystem_state) override;
    // 从列表中狩猎牛
    void _hunt_cows(const std::vector<Cow*>& cow_list);
    // 检查老虎是否可以繁殖
    bool can_reproduce() const override;
protected:
    double reproduction_spawn_radius() const override { return 40.0; }
};