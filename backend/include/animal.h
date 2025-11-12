/*
动物数据模型 - Animal 声明
将 Animal 从 species.h 迁移至本文件，并继承 RaceBase
*/

#pragma once

#include <vector>
#include <string>
#include <optional>
#include <memory>
#include <functional>
#include "utils.h"
#include "race_base.h"
#include "animal_behavior.h" // 提供行为树构建函数声明，用于 friend 授权访问

#ifdef ECOSIM_ENABLE_UI_DEBUG
#include <mutex>
#include "animal_ui_snapshot.h"
#endif

// 前向声明
class EcosystemState;
struct AnimalParams;
// 不再需要 ThingBase 缓存，移除前向声明

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
    double energy_consumption;
    double hunting_range;
    double hunting_success_rate;
    double detection_range;
    double threat_detection_range;
    double mate_detection_range;
    double food_detection_range;
    std::vector<std::string> food_types;
    int hunting_cooldown;
    int hunting_cooldown_duration;
    int min_reproduction_age;
    int base_reproduction_cooldown;
    double eating_range;
    double energy_efficiency;

    // 捕食结算用营养值（与当前 energy 脱钩）
    double get_nutrition_value() const override;

    // 捕食energy加成参数
    double nutrition_bonus_max{0.0};
    double nutrition_bonus_curve_alpha{1.0};

#ifdef ECOSIM_ENABLE_UI_DEBUG
    AnimalUiSnapshot get_ui_snapshot() const;
    void update_ui_snapshot(const AnimalUiSnapshot& snapshot);
    void add_interaction_log(const std::string& message, bool success, int timestamp);
#endif

    // 构造函数（默认物种名为 "RaceBase"）
    Animal(Position pos, const AnimalParams& params, std::mt19937& rng);
    // 新增构造函数：允许在构造时传入物种名，便于加载 YAML 行为树
    Animal(Position pos, const std::string& species_name, const AnimalParams& params, std::mt19937& rng);
    virtual ~Animal();

    // 更新流程
    void decide(EcosystemState& ecosystem_state, std::mt19937& rng) override;
    void apply(const EcosystemState& ecosystem_state) override;

    // 向目标位置移动
    void move_towards_target(const Position& target_position, int world_width, int world_height);
    // 路径规划（占位）
    virtual void plan_path_to_target(const EcosystemState& ecosystem_state, const std::optional<Position>& target);
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
    // 设置/控制 HP 恢复的全局倍率（可由行为树根据情境覆盖，例如遇到威胁时降低恢复速度）
    void set_hp_regen_multiplier(double m);
    // 逃跑疲惫状态更新：当动物持续处于被追击状态时，计时并在达到阈值后进入疲惫状态
    // - is_fleeing: 当前帧是否处于逃逸冷却（FleeModeCooldownTicks>0）
    // - ticks_per_day: world ticks_per_day，用于将秒转换为 ticks
    // - tired_after_seconds: 持续逃跑多少秒后变为疲惫
    // - tired_speed_multiplier: 疲惫后的速度倍率（0..1），例如 0.8
    void update_flee_ticks(bool is_fleeing, int ticks_per_day, double tired_after_seconds, double tired_speed_multiplier);
    // 是否当前处于疲惫状态
    bool is_tired() const;
    // 获取疲惫时的速度倍率（仅在 is_tired()==true 时有意义）
    double get_tired_speed_multiplier() const;
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

    // 用于被击杀时提供捕食能量的营养值
    double nutrition_value;

    // --- HP 恢复参数 ---
    double hp_regen_base_per_day{0.0};
    double hp_regen_mul_satisfied{2.0};
    double hp_regen_mul_normal{1.0};
    double hp_regen_mul_starving{0.25};
    // HP 恢复触发间隔（按天比例），仿照饥饿伤害间隔
    double regan_interval_ratio{0.25};
    // 自增计时器：自上次恢复以来的 tick 数
    int ticks_since_last_regen{0};

    // 全局 HP 恢复倍率（可被行为树在 Update 阶段按情境覆盖，例如遇到威胁时降低恢复速度）
    double hp_regen_multiplier{1.0};


    // 交配相关配置
    int mating_duration;
    int pregnancy_duration;
    double mating_range;
    double pregnancy_speed_penalty;
    double mating_desire_probability;
    // 怀孕期间能量消耗倍率
    double pregnant_energy_multiplier{1.0};

    // 交配意图锁定，防止与捕食来回切换

    // 本 tick 感知缓存（弱引用，避免循环与跨帧残留）
    std::vector<std::weak_ptr<RaceBase>> cached_food_races;
    std::vector<std::weak_ptr<Animal>> cached_mates;

    // 饱食状态更新与属性调整
    void update_hunger_state();
    // 连续逃跑计数与疲惫状态
    int consecutive_flee_ticks{0};
    bool tired{false};
    double current_tired_speed_multiplier{1.0};
    // 旧 FSM 清理：移除 is_wandering / wandering_cooldown / PendingMoveMode 等成员
    std::optional<Position> wander_target;
    bool skip_movement{false};

    // 抽象“一步移动 + 能量结算”的通用内核，供两个对外接口复用
    void perform_step_move_common(double speed_multiplier, double energy_multiplier,
                                  const char* log_tag,
                                  const std::function<void()>& advance_fn);

    // 每 tick 执行 HP 恢复逻辑
    void apply_hp_regen(const EcosystemState& ecosystem_state);

#ifdef ECOSIM_ENABLE_UI_DEBUG
    mutable std::mutex m_ui_snapshot_mutex;
    AnimalUiSnapshot m_ui_snapshot;
#endif
};