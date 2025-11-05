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
struct PlantParams;

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

// 动物饱食状态枚举
enum class HungerState {
    SATISFIED,   // 吃饱了，低欲望，倾向于随机移动（逛街）
    NORMAL,      // 正常状态，根据物种习性决定行为
    STARVING     // 饥饿状态，高欲望，主动寻找食物
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
    double energy_efficiency; //能量利用率

    Animal(Position pos, double energy = 100, int max_age = 100, double reproduction_energy_cost = 50,
           double movement_speed = 1.0, int energy_consumption = 1, double hunting_range = 5.0,
           double hunting_success_rate = 0.5, double detection_range = 500.0,
           std::vector<std::string> food_types = {}, int hunting_cooldown_duration = 0,
           int min_reproduction_age = 0, int base_reproduction_cooldown = 0,
           double eating_range = 0.0, double max_energy = 100.0, double satisfied_threshold_ratio = 0.8,
           double starving_threshold_ratio = 0.2, int wandering_duration = 50, double wander_radius = 200.0, double energy_efficiency = 1.0);

    void decide(EcosystemState& ecosystem_state, std::mt19937& rng) override;
    void apply(const EcosystemState& ecosystem_state) override;
    // 寻找最近的食物来源
    virtual std::optional<Position> find_nearest_food(const EcosystemState& ecosystem_state);
    // 向目标位置移动
    void move_towards_target(const Position& target_position, int world_width, int world_height);
    // 智能移动：分离为目标选择与移动执行
    virtual void intelligent_move(const class EcosystemState& ecosystem_state);
    // 目标选择：设置当前目标点（若无可用目标则置空）
    virtual void select_target_point(const class EcosystemState& ecosystem_state);
    // 路径规划（占位以便未来接入 A* 等算法）
    virtual void plan_path_to_target(const class EcosystemState& ecosystem_state);
    // 执行向当前目标点移动（沿规划路径或直接朝向）
    void move_to_target_point(int world_width, int world_height);
    // 开始狩猎冷却
    void start_hunting_cooldown();
    // 通用繁殖判断（含年龄门槛）
    bool can_reproduce() const override;
    // 开始繁殖冷却（使用基础冷却值）
    void start_reproduction_cooldown();
    // 统一的繁殖实现：基于当前物种键创建子代
    std::unique_ptr<Species> reproduce(const EcosystemState& ecosystem_state) override;

    // 获取当前捕食意愿（0-1），应为虚函数以支持不同动物的特殊逻辑
    virtual double get_hunting_desire() const;

protected:
    // 子类可覆盖的繁殖偏移半径（用于随机生成子代位置）
    virtual double reproduction_spawn_radius() const { return 10.0; }
    // 当前移动目标与路径（为未来寻路预留空间）
    std::optional<Position> current_target;
    std::vector<Position> planned_path;
    size_t planned_path_index;

    // 新增状态管理成员
    HungerState hunger_state;
    double satisfied_threshold;    // 吃饱阈值（基于最大能量的比例）
    double starving_threshold;     // 饥饿阈值（基于最大能量的比例）
    double base_movement_speed;    // 基础移动速度
    double base_energy_consumption; // 基础能量消耗
    bool is_wandering;             // 是否处于逛街状态
    int wandering_cooldown;        // 逛街冷却/持续时间
    double wander_radius;          // 游荡目标选择半径

    // 更新饱食状态
    void update_hunger_state();
    // 根据状态调整能耗和速度
    void adjust_stats_by_state();

    enum class PendingMoveMode { None, Wander, Path };
    PendingMoveMode pending_move_mode{PendingMoveMode::None};
    std::optional<Position> wander_target;
    bool skip_movement{false};
};

// 植物基类，继承自Species，抽象出生产者通用逻辑
class Plant : public Species {
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

    Plant(Position pos, const struct PlantParams& params);
    // 可覆盖：根据竞争调整的生长率
    virtual double get_competition_adjusted_growth_rate(const EcosystemState& ecosystem_state);
    // 通用更新流程
    void decide(EcosystemState& ecosystem_state, std::mt19937& rng) override;
    void apply(const class EcosystemState& ecosystem_state) override;
    // 通用繁殖判断与实现（可被子类覆盖）
    bool can_reproduce() const override;
    std::unique_ptr<Species> reproduce(const EcosystemState& ecosystem_state) override;
};

// 草类，继承自Species，实现生产者逻辑
class Grass : public Plant {
public:
    Grass(Position pos, const struct GrassParams& params);
    // 获取根据竞争调整的生长率
    double get_competition_adjusted_growth_rate(const EcosystemState& ecosystem_state);
    // 更新草的状态
    void decide(EcosystemState& ecosystem_state, std::mt19937& rng) override;
    void apply(const class EcosystemState& ecosystem_state) override;
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
    void decide(EcosystemState& ecosystem_state, std::mt19937& rng) override;
    void apply(const class EcosystemState& ecosystem_state) override;
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
    void decide(EcosystemState& ecosystem_state, std::mt19937& rng) override;
    void apply(const class EcosystemState& ecosystem_state) override;
    // 从列表中狩猎牛
    void _hunt_cows(const std::vector<Cow*>& cow_list);
    // 检查老虎是否可以繁殖
    bool can_reproduce() const override;
protected:
    double reproduction_spawn_radius() const override { return 200.0; }
};