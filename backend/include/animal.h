/*
动物数据模型 - Animal 声明
将 Animal 从 species.h 迁移至本文件，并继承 RaceBase
*/

#pragma once

#include <vector>
#include <string>
#include <optional>
#include <memory>
#include "utils.h"
#include "race_base.h"
#include "animal_behavior.h" // 提供行为树构建函数声明，用于 friend 授权访问

// 前向声明
class EcosystemState;
struct AnimalParams;

// 动物饱食状态枚举
enum class HungerState {
    SATISFIED,
    NORMAL,
    STARVING
};

// 性别枚举
enum class Sex { MALE, FEMALE };

// 动物类，继承自 RaceBase，添加移动和交互逻辑
namespace bt { class BehaviorTree; }
class Animal : public RaceBase {
public:
    // 交配属性
    Sex sex;
    bool is_pregnant;
    int pregnancy_timer;
    int mating_timer;
    std::weak_ptr<Animal> mating_partner;

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
    double energy_efficiency;

    // 构造函数
    Animal(Position pos, const AnimalParams& params, std::mt19937& rng);
    virtual ~Animal();

    // 更新流程
    void decide(EcosystemState& ecosystem_state, std::mt19937& rng) override;
    void apply(const EcosystemState& ecosystem_state) override;

    // 寻找最近的食物来源
    virtual std::optional<Position> find_nearest_food(const EcosystemState& ecosystem_state);
    // 向目标位置移动
    void move_towards_target(const Position& target_position, int world_width, int world_height);
    // 智能移动：目标选择与移动执行
    virtual void intelligent_move(const EcosystemState& ecosystem_state);
    // 目标选择
    virtual void select_target_point(const EcosystemState& ecosystem_state);
    // 路径规划（占位）
    virtual void plan_path_to_target(const EcosystemState& ecosystem_state, const std::optional<Position>& target);
    // 执行向当前目标点移动
    void move_to_target_point(int world_width, int world_height);
    // 开始狩猎冷却
    void start_hunting_cooldown();
    // 通用繁殖判断（含年龄门槛）
    bool can_reproduce() const override;
    // 开始繁殖冷却
    void start_reproduction_cooldown();
    // 统一的繁殖实现：基于当前物种键创建子代
    std::unique_ptr<RaceBase> reproduce(const EcosystemState& ecosystem_state) override;

    // 交配相关接口
    void begin_mating_with(std::shared_ptr<Animal> partner);
    void become_pregnant();
    virtual std::optional<std::shared_ptr<Animal>> find_available_mate(const EcosystemState& ecosystem_state);
    virtual double get_hunting_desire() const;

protected:
    // 行为构建函数作为友元，允许访问受保护成员以设置目标与移动模式
    friend std::unique_ptr<bt::BehaviorTree> behavior::build_tree_for_animal(Animal& self);
    // 行为树脚手架（默认关闭）
    std::unique_ptr<bt::BehaviorTree> behavior_tree;
    bool use_bt{false};
    void build_behavior_tree();
    // 繁殖偏移半径
    virtual double reproduction_spawn_radius() const { return 10.0; }
    // 当前移动目标与路径
    std::optional<Position> current_target;
    std::vector<Position> planned_path;
    size_t planned_path_index;

    // 交配追踪目标
    std::optional<Position> mating_target;

    // 状态管理成员
    HungerState hunger_state;
    double satisfied_threshold;
    double starving_threshold;
    double base_movement_speed;
    double base_energy_consumption;
    bool is_wandering;
    int wandering_cooldown;
    double wander_radius;
    double step_distance_per_tick;
    double current_step_distance;

    // 交配相关配置
    int mating_duration;
    int pregnancy_duration;
    double mating_range;
    double pregnancy_speed_penalty;
    double mating_desire_probability;

    // 交配意图锁定，防止与捕食来回切换
    int mating_intent_lock_ticks{0};
    int mating_intent_lock_duration{30};

    // 饱食状态更新与属性调整
    void update_hunger_state();
    void adjust_stats_by_state();

    enum class PendingMoveMode { None, Wander, Path };
    PendingMoveMode pending_move_mode{PendingMoveMode::None};
    std::optional<Position> wander_target;
    bool skip_movement{false};
};