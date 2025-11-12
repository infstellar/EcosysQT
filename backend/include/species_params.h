// 强类型物种参数结构体定义
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
// 反射支持：用于自动从 YAML 键匹配到成员名
#include <boost/describe.hpp>

// 基础物种参数
struct SpeciesBaseParams {
    double energy = 100.0;
    int max_age = 100;
    double min_reproduction_energy = 50.0;
    // 战斗：最大生命值
    double hp_max = 100.0;
};

// 动物通用参数（继承基础物种）
struct AnimalParams : SpeciesBaseParams {
    bool use_bt = false;            // 是否启用行为树
    double movement_speed = 1.0;
    double energy_consumption = 1.0;
    double hunting_range = 5.0;
    double hunting_success_rate = 0.5;
    double detection_range = 300.0;
    std::vector<std::string> food_types = {};
    int hunting_cooldown_duration = 0;
    int min_reproduction_age = 0;
    int reproduction_cooldown = 0;
    double eating_range = 0.0;
    double energy_efficiency = 1.0; //能量利用率，默认为100%
    double satisfied_threshold_ratio = 0.8;   // 吃饱阈值比例
    double starving_threshold_ratio = 0.2;    // 饥饿阈值比例
    int wandering_duration = 50;              // 逛街持续时间
    double wander_radius = 40.0;              // 游荡目标选择半径

    // 战斗：攻击伤害
    double attack_damage = 10.0;
    // 战斗：攻击伤害区间，优先于默认攻击伤害
    double attack_damage_min = 0.0;
    double attack_damage_max = 0.0;

    // 捕食结算：营养值（与当前 energy 脱钩，用于被击杀后的能量提供）
    double nutrition_value = 100.0;
    // 捕食ENERGY加成
    double nutrition_bonus_max = 0.0;           // 最高额外营养加成
    double nutrition_bonus_curve_alpha = 1.0;   // 幂次曲线形状（>=0）

    // 饥饿伤害配置（外化到 YAML）
    double starvation_damage = 1.0;                    // 饥饿状态下的周期性伤害（扣 HP）
    double starvation_damage_interval_ratio = 0.25;    // 伤害触发周期（天）

    // --- 生命恢复机制 ---
    double hp_regen_base_per_day = 2.0;         // 基础恢复量
    // 按饥饿状态的生命恢复倍数
    double hp_regen_mul_satisfied = 2.0;        // 吃饱状态恢复倍数
    double hp_regen_mul_normal = 1.0;           // 正常状态恢复倍数
    double hp_regen_mul_starving = 0.25;        // 饥饿状态恢复倍数
    // 恢复触发周期（天）
    double regan_interval_ratio = 0.25;

    // --- 新增：交配与怀孕参数 ---
    int mating_duration = 30;      // 交配持续时间 (ticks)
    int pregnancy_duration = 100;  // 怀孕持续时间 (ticks)
    double mating_range = 5.0;       // 发起交配的距离
    double pregnancy_speed_penalty = 0.5; // 怀孕期间速度惩罚系数 (例如0.5代表速度减半)
    // 怀孕期间能量消耗倍率（>1 表示更费能，=1 表示无额外消耗）
    double pregnant_energy_multiplier = 1.0;
    // 新生个体初始能量比例（0-1，乘以 max_energy）
    double newborn_energy_ratio = 0.5;
    // 每 tick 有多少概率会主动寻找配偶
    double mating_desire_probability = 0.5; // 默认 50%

    // --- 行为树黑板参数：来自 YAML / 编辑器的键值，直接影响装饰器等 ---
    std::unordered_map<std::string, int> bt_params_ints;      // 例如：eat_grass_total_ticks: 300
    std::unordered_map<std::string, double> bt_params_doubles; // 例如：mate_total_ticks: 150.0
    std::unordered_map<std::string, std::string> bt_params_strings; // 备用：字符串型
};

// 植物通用参数（继承基础物种）
struct PlantParams : SpeciesBaseParams {
    double base_growth_rate = 0.2;
    double reproduction_chance = 0.4;
    double competition_radius = 30.0;
    double max_competition_effect = 2.0;
    int reproduction_cooldown = 10;
    // 新增：引入 delta_time 相关与可调竞争因子
    double expansion_boost = 1.0;         // 低密度扩张加成（原固定2.0）
    double min_growth_factor = 0.001;     // 最低生长比例（原固定0.001）
    // 被食用时提供的基础营养值（独立于 energy）
    double nutrition_value = 100.0;
    // 累积式繁殖：满能量时开始累积
    double repro_energy_threshold = 25.0; //阈值
    double repro_energy_accumulation_rate = 0.1; //速率
    // 子代初始能量比例（0-1，乘以 max_energy）
    double newborn_energy_ratio = 0.25;
};

// 为自动匹配提供成员名与继承关系描述（一次性声明，保持 DRY）
BOOST_DESCRIBE_STRUCT(SpeciesBaseParams, (),
    (energy, max_age, min_reproduction_energy, hp_max))
BOOST_DESCRIBE_STRUCT(AnimalParams, (SpeciesBaseParams),
    (use_bt, movement_speed, energy_consumption, hunting_range, hunting_success_rate, detection_range, food_types, hunting_cooldown_duration, min_reproduction_age, reproduction_cooldown, eating_range, energy_efficiency, satisfied_threshold_ratio, starving_threshold_ratio, wandering_duration, wander_radius, attack_damage, attack_damage_min, attack_damage_max, nutrition_value, nutrition_bonus_max, nutrition_bonus_curve_alpha, starvation_damage, starvation_damage_interval_ratio, hp_regen_base_per_day, hp_regen_mul_satisfied, hp_regen_mul_normal, hp_regen_mul_starving, regan_interval_ratio, mating_duration, pregnancy_duration, mating_range, pregnancy_speed_penalty, pregnant_energy_multiplier, newborn_energy_ratio,
    mating_desire_probability, bt_params_ints, bt_params_doubles, bt_params_strings))
BOOST_DESCRIBE_STRUCT(PlantParams, (SpeciesBaseParams),
    (base_growth_rate, reproduction_chance, competition_radius, max_competition_effect, reproduction_cooldown, expansion_boost, min_growth_factor, nutrition_value, repro_energy_threshold, repro_energy_accumulation_rate, newborn_energy_ratio))