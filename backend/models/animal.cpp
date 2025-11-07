/*
物种数据模型 - Animal 基类实现
定义生态系统中的动物基类，继承自Species并添加智能移动
*/

#include "animal.h"
#include "race_base.h"
#include "thing_base.h"
#include "species_params.h"
#include "ecosystem.h"
#include "behavior_tree.h"
#include "animal_behavior.h"
#include "tracy/Tracy.hpp"
#include <spdlog/spdlog.h>
#include <random>
#include <algorithm>
#include <cmath>

// --- Animal ---
// 动物基类 - 继承自Species并添加智能移动
Animal::Animal(Position pos, const AnimalParams& params, std::mt19937& rng)
        : RaceBase(pos, params.energy, params.max_age, params.reproduction_energy_cost),
            base_movement_speed(params.movement_speed),
            movement_speed(params.movement_speed),
            base_energy_consumption(params.energy_consumption),
            energy_consumption(params.energy_consumption),
            hunting_range(params.hunting_range),
            hunting_success_rate(params.hunting_success_rate),
            detection_range(params.detection_range),
            food_types(params.food_types),
            hunting_cooldown(0),
            hunting_cooldown_duration(params.hunting_cooldown_duration),
            min_reproduction_age(params.min_reproduction_age),
            base_reproduction_cooldown(params.reproduction_cooldown),
            eating_range(params.eating_range),
            energy_efficiency(params.energy_efficiency),
            current_target(std::nullopt),
            planned_path(),
            planned_path_index(0),
            hunger_state(HungerState::NORMAL),
            satisfied_threshold(params.energy * params.satisfied_threshold_ratio),
            starving_threshold(params.energy * params.starving_threshold_ratio),
            is_wandering(false),
            wandering_cooldown(params.wandering_duration),
            wander_radius(params.wander_radius),
            mating_desire_probability(params.mating_desire_probability) {
    // 交配/怀孕相关参数初始化
    mating_duration = params.mating_duration;
    pregnancy_duration = params.pregnancy_duration;
    mating_range = params.mating_range;
    pregnancy_speed_penalty = params.pregnancy_speed_penalty;
    // 初始化每tick步长为当前移动速度（tick制）
    step_distance_per_tick = movement_speed;
    current_step_distance = step_distance_per_tick; // 首帧近似为1 tick

    // 使用传递进来的 rng，而不是线程本地的
    std::uniform_int_distribution<> dist(0, 1);
    sex = (dist(rng) == 0) ? Sex::MALE : Sex::FEMALE;

    is_pregnant = false;
    pregnancy_timer = 0;
    mating_timer = 0;
    // 初始化交配意图锁定时长（可按需调整或从参数映射）
    mating_intent_lock_ticks = 0;
    mating_intent_lock_duration = 30;

    // 行为树脚手架构建（默认关闭）
    use_bt = params.use_bt;
    build_behavior_tree();
}

Animal::~Animal() = default;

void Animal::decide(EcosystemState& ecosystem_state, std::mt19937& rng) {
    ZoneScoped;
    RaceBase::decide(ecosystem_state, rng);
    if (!alive) {
        return;
    }

    auto self = shared_from_this();

    // 行为树路径：将状态更新、计时器推进与副作用统一在 BT 的通用 Action 中
    if (use_bt && behavior_tree) {
        bt::TickContext ctx;
        ctx.self = this;
        ctx.world = &ecosystem_state;
        ctx.blackboard = &behavior_tree->blackboard();
        (void)behavior_tree->tick(ctx);
        return;
    }
}

void Animal::apply(const EcosystemState& ecosystem_state) {
    ZoneScoped;
    RaceBase::apply(ecosystem_state);
    if (!alive) {
        return;
    }

    // 当使用行为树时，移动与消耗由 BT Action 执行；此处不再运行旧移动分支
    if (use_bt) {
        pending_move_mode = PendingMoveMode::None;
        return;
    }

    // 非 BT 路径：不再包含旧的 Path/Wander 移动逻辑，仅处理基础能量结算
    pending_move_mode = PendingMoveMode::None;
    energy -= energy_consumption;
    if (energy <= 0.0) {
        die_from_starvation();
    }
}

void Animal::update_hunger_state() {
    if (energy >= satisfied_threshold) {
        hunger_state = HungerState::SATISFIED;
    } else if (energy <= starving_threshold) {
        hunger_state = HungerState::STARVING;
    } else {
        hunger_state = HungerState::NORMAL;
    }
}

void Animal::adjust_stats_by_state() {
    switch (hunger_state) {
        case HungerState::SATISFIED:
            is_wandering = true;
            wandering_cooldown = 100; // Example value, should be configurable
            // 满足状态仍应缓慢移动，避免视觉上“卡住”
            movement_speed = base_movement_speed * 0.8;
            energy_consumption = base_energy_consumption * 0.6;
            break;
        case HungerState::STARVING:
            is_wandering = false;
            movement_speed = base_movement_speed * 0.4;
            energy_consumption = base_energy_consumption * 0.1;
            break;
        case HungerState::NORMAL:
        default:
            if (wandering_cooldown > 0) {
                wandering_cooldown--;
            } else {
                is_wandering = false;
            }
            movement_speed = base_movement_speed * 1.0;
            energy_consumption = base_energy_consumption * 1.0;
            break;
    }

    // 怀孕速度惩罚：在饥饿状态调整后叠加
    if (is_pregnant) {
        movement_speed *= std::max(0.0, pregnancy_speed_penalty);
    }

    // 保持每 tick 步长与当前速度一致
    step_distance_per_tick = movement_speed;
}

double Animal::get_hunting_desire() const {
    switch (hunger_state) {
        case HungerState::STARVING:
            return 1.0;
        case HungerState::NORMAL:
            return 0.5;
        case HungerState::SATISFIED:
        default:
            return 0.0;
    }
}

// 已移除：find_nearest_food（简化为行为树目标选择）

void Animal::move_towards_target(const Position& target_position, int world_width, int world_height) {
    // 朝目标位置移动
    if (!alive) return;
    double dx = target_position.x - position.x;
    double dy = target_position.y - position.y;
    double distance = std::sqrt(dx * dx + dy * dy);

    if (distance > 0) {
        // 到达减速（Arrive）：临近目标时按比例减速，平滑收敛
    const double slow_radius = std::max(current_step_distance * 4.0, step_distance_per_tick * 2.0);
    const double ratio = std::min(1.0, distance / std::max(1e-9, slow_radius));
    const double desired = current_step_distance * ratio;
        const double step = std::min(desired, distance);
        dx = (dx / distance) * step;
        dy = (dy / distance) * step;
        // 更新位置，确保不超出边界
        position.x = std::max(0.0, std::min((double)world_width, position.x + dx));
        position.y = std::max(0.0, std::min((double)world_height, position.y + dy));
    }
}

// 已移除：intelligent_move（简化为行为树驱动的移动）

void Animal::select_target_point(const EcosystemState& ecosystem_state) {
    // 选择当前目标点：在探测范围内寻找最近的食物
    // 吃饱状态下不主动找食物，改为散步
    if (hunger_state == HungerState::SATISFIED) {
        current_target.reset();
        planned_path.clear();
        planned_path_index = 0;
        return;
    }
    std::optional<Position> nearest_food;
    double min_distance = std::numeric_limits<double>::max();

    const auto nearby_races = ecosystem_state.get_nearby_races_broad(position, detection_range);
    const auto nearby_things = ecosystem_state.get_nearby_things_broad(position, detection_range);

    const auto consider_entity = [&](const auto& entity) {
        if (!entity || !entity->alive) {
            return;
        }
        if (std::find(food_types.begin(), food_types.end(), entity->species_name) == food_types.end()) {
            return;
        }

        double distance = position.distance_to(entity->position);
        if (distance <= detection_range && distance < min_distance) {
            min_distance = distance;
            nearest_food = entity->position;
        }
    };

    for (const auto& race : nearby_races) {
        consider_entity(race);
    }
    for (const auto& thing : nearby_things) {
        consider_entity(thing);
    }

    if (nearest_food.has_value()) {
        current_target = nearest_food.value();
    } else {
        current_target.reset();
        planned_path.clear();
        planned_path_index = 0;
    }
}

void Animal::plan_path_to_target(const EcosystemState& ecosystem_state, const std::optional<Position>& target) {
    if (!target.has_value()) return;
    planned_path.clear();
    planned_path.push_back(target.value());
    planned_path_index = 0;
}

void Animal::move_to_target_point(int world_width, int world_height) {
    // 沿规划路径或直接朝目标移动一步
    if (!current_target.has_value()) return;

    // 若存在路径，按路径点逐步移动；否则直接朝目标
    Position goal = current_target.value();
    if (!planned_path.empty() && planned_path_index < planned_path.size()) {
        goal = planned_path[planned_path_index];
    }

    // 执行移动
    move_towards_target(goal, world_width, world_height);

    // 达到当前路径点后推进到下一个点
    double remain = position.distance_to(goal);
    const double arrival_threshold = std::max(1.0, current_step_distance * 0.5);
    if (remain <= arrival_threshold) {
        if (!planned_path.empty() && planned_path_index < planned_path.size()) {
            planned_path_index += 1;
            if (planned_path_index >= planned_path.size()) {
                // 路径完成
                planned_path.clear();
                planned_path_index = 0;
            }
        } else {
            // 直接目标已到达（近似判断），清空目标以触发重新选择
            current_target.reset();
        }
    }
}

void Animal::start_hunting_cooldown() {
    // 开始狩猎冷却 - 动物将保持静止一段时间
    hunting_cooldown = hunting_cooldown_duration;
}

bool Animal::can_reproduce() const {
    if (sex == Sex::MALE) {
        // 雄性检查自身状态（能量、年龄、冷却）
        return RaceBase::can_reproduce() && age > min_reproduction_age;
    }
    if (sex == Sex::FEMALE) {
        // 雌性检查是否“可受孕”
        return !is_pregnant && mating_timer <= 0 && RaceBase::can_reproduce() && age > min_reproduction_age;
    }
    return false;
}

void Animal::start_reproduction_cooldown() {
    // 开始繁殖冷却：使用基础冷却值
    reproduction_cooldown = base_reproduction_cooldown;
}

std::unique_ptr<RaceBase> Animal::reproduce(const EcosystemState& ecosystem_state) {
    (void)ecosystem_state;
    return nullptr;
}

void Animal::begin_mating_with(std::shared_ptr<Animal> partner) {
    mating_timer = mating_duration;
    mating_partner = partner;
}

void Animal::become_pregnant() {
    if (sex == Sex::FEMALE) {
        is_pregnant = true;
        pregnancy_timer = pregnancy_duration;
    }
}

std::optional<std::shared_ptr<Animal>> Animal::find_available_mate(const EcosystemState& ecosystem_state) {
    if (sex == Sex::FEMALE) return std::nullopt;
    std::optional<std::shared_ptr<Animal>> nearest_mate;
    double min_distance = std::numeric_limits<double>::max();
    const auto nearby_entities = ecosystem_state.get_nearby_races_broad(position, detection_range);
    for (const auto& entity_ptr : nearby_entities) {
        if (!entity_ptr || !entity_ptr->alive || entity_ptr.get() == this || entity_ptr->species_name != this->species_name) continue;
        auto potential_mate = std::dynamic_pointer_cast<Animal>(entity_ptr);
        if (potential_mate && potential_mate->sex == Sex::FEMALE && potential_mate->can_reproduce()) {
            double distance = position.distance_to(potential_mate->position);
            if (distance < min_distance) {
                min_distance = distance;
                nearest_mate = potential_mate;
            }
        }
    }
    return nearest_mate;
}
void Animal::build_behavior_tree() {
    // 通过独立模块构建行为树，保持 Animal 仅承载数据与生命周期
    behavior_tree = behavior::build_tree_for_animal(*this);
}

void Animal::apply_bt_params_to_blackboard(const AnimalParams& params) {
    if (!behavior_tree) return;
    auto& bb = behavior_tree->blackboard();
    // 注入整数参数
    for (const auto& kv : params.bt_params_ints) {
        bb.ints[kv.first] = kv.second;
    }
    // 注入浮点参数
    for (const auto& kv : params.bt_params_doubles) {
        bb.doubles[kv.first] = kv.second;
    }
    // 注入字符串参数
    for (const auto& kv : params.bt_params_strings) {
        bb.strings[kv.first] = kv.second;
    }
}