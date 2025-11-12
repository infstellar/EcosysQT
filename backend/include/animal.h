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
class ThingBase;

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

    // 向目标位置移动
    void move_towards_target(const Position& target_position, int world_width, int world_height);
    // 路径规划（占位）
    virtual void plan_path_to_target(const EcosystemState& ecosystem_state, const std::optional<Position>& target);
    void plan_path_to_target(const std::vector<Position>& path);
    // 执行向当前目标点移动
    void move_to_target_point(int world_width, int world_height);

    // --- 能量与一步移动的统一封装（供行为树动作复用） ---
    // 扣除能量并在耗尽时触发饥饿死亡；乘数用于在特殊状态（如逃离）叠加消耗
    void consume_energy(double multiplier = 1.0);
    // 执行“一步”朝任意目标点移动，并进行能量结算；可按乘数临时提高速度/消耗
    void perform_step_move_to(const Position& target, int world_width, int world_height,
                              double speed_multiplier = 1.0, double energy_multiplier = 1.0);
    // 执行“一步”沿当前规划路径/目标移动，并进行能量结算；可按乘数临时提高速度/消耗
    void perform_step_move_path(int world_width, int world_height,
                                double speed_multiplier = 1.0, double energy_multiplier = 1.0);
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

    // 将 YAML/编辑器提供的 bt_params 写入行为树黑板
    void apply_bt_params_to_blackboard(const AnimalParams& params);

    // ---- 公共访问接口（供行为树使用，替代对 protected 成员的直接访问） ----
    // 饥饿状态
    HungerState get_hunger_state() const;
    void refresh_hunger_state();
    // 参数读取
    double get_mating_range() const;
    double get_wander_radius() const;
    double get_mating_desire_probability() const;
    double get_detection_range() const;
    double get_threat_detection_range() const;
    double get_mate_detection_range() const;
    double get_food_detection_range() const;
    double get_pregnancy_speed_penalty() const;
    // 移动控制
    bool get_skip_movement() const;
    void set_skip_movement(bool v);
    // 意图锁定
    // 感知缓存操作
    void clear_sensor_caches();
    void cache_mate(const std::shared_ptr<Animal>& mate);
    void cache_food_race(const std::shared_ptr<RaceBase>& race);
    std::vector<std::weak_ptr<Animal>> get_cached_mates_snapshot() const;
    std::vector<std::weak_ptr<RaceBase>> get_cached_food_races_snapshot() const;
    // 目标/路径管理
    void set_current_target(const std::optional<Position>& p);
    std::optional<Position> get_current_target() const;
    void clear_current_target();
    void set_mating_target(const std::optional<Position>& p);
    std::optional<Position> get_mating_target() const;
    void clear_mating_target();
    void set_wander_target(const std::optional<Position>& p);
    std::optional<Position> get_wander_target() const;
    void clear_wander_target();
    void clear_path();
    bool has_planned_path() const;
    std::optional<Position> get_planned_path_final_point() const;
    // 步长
    double get_current_step_distance() const;
    double get_step_distance_per_tick() const;

    // 路径快照：返回当前规划路径的副本，供 UI 调试快照使用
    std::vector<Position> get_planned_path_snapshot() const;

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

    // 觅食意图锁定，防止与交配来回切换
    int forage_intent_lock_ticks{0};
    int forage_intent_lock_duration{20};

    // 本 tick 感知缓存（弱引用，避免循环与跨帧残留）
    std::vector<std::weak_ptr<RaceBase>> cached_food_races;
    std::vector<std::weak_ptr<ThingBase>> cached_food_things;
    std::vector<std::weak_ptr<Animal>> cached_mates;

    // 饱食状态更新与属性调整
    void update_hunger_state();
    // 旧 FSM 清理：移除 is_wandering / wandering_cooldown / PendingMoveMode 等成员
    std::optional<Position> wander_target;
    bool skip_movement{false};
};