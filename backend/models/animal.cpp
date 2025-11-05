/*
物种数据模型 - Animal 基类实现
定义生态系统中的动物基类，继承自Species并添加智能移动
*/

#include "species.h"
#include "ecosystem.h"
#include "tracy/Tracy.hpp"
#include <random>
#include <algorithm>
#include <cmath>

// --- Animal ---
// 动物基类 - 继承自Species并添加智能移动
Animal::Animal(Position pos,
               double energy,
               int max_age,
               double reproduction_energy_cost,
               double movement_speed,
               int energy_consumption,
               double hunting_range,
               double hunting_success_rate,
               double detection_range,
               std::vector<std::string> food_types,
               int hunting_cooldown_duration,
               int min_reproduction_age,
               int base_reproduction_cooldown,
               double eating_range,
               double max_energy,
               double satisfied_threshold_ratio,
               double starving_threshold_ratio,
               int wandering_duration,
               double wander_radius,
               double energy_efficiency)
    : Species(pos, energy, max_age, reproduction_energy_cost),
      base_movement_speed(movement_speed),
      movement_speed(movement_speed),
      base_energy_consumption(energy_consumption),
      energy_consumption(energy_consumption),
      hunting_range(hunting_range),
      hunting_success_rate(hunting_success_rate),
      detection_range(detection_range),
      food_types(std::move(food_types)),
      hunting_cooldown(0),
      hunting_cooldown_duration(hunting_cooldown_duration),
      min_reproduction_age(min_reproduction_age),
      base_reproduction_cooldown(base_reproduction_cooldown),
      eating_range(eating_range),
      current_target(std::nullopt),
      planned_path(),
      planned_path_index(0),
      hunger_state(HungerState::NORMAL),
      satisfied_threshold(max_energy * satisfied_threshold_ratio),
      starving_threshold(max_energy * starving_threshold_ratio),
      is_wandering(false),
      wandering_cooldown(wandering_duration),
      wander_radius(wander_radius),
      energy_efficiency(energy_efficiency){}

void Animal::decide(EcosystemState& ecosystem_state, std::mt19937& rng) {
    ZoneScoped;
    Species::decide(ecosystem_state, rng);
    if (!alive) {
        return;
    }

    // 每轮决策前重置待执行动作，避免残留状态污染。
    pending_move_mode = PendingMoveMode::None;
    skip_movement = false;

    update_hunger_state();
    adjust_stats_by_state();

    if (hunting_cooldown > 0) {
        // 冷却只限制捕猎动作，不应阻止行走或游荡
        hunting_cooldown -= 1;
    }

    if (!skip_movement) {
        select_target_point(ecosystem_state);
        if (current_target.has_value()) {
            plan_path_to_target(ecosystem_state);
            pending_move_mode = PendingMoveMode::Path;
            // 追踪目标时清空游荡意图
            wander_target.reset();
        } else {
            // 游走行为：选定半径内的随机目标，未到达前保持该目标
            const int world_width = ecosystem_state.config.world_width;
            const int world_height = ecosystem_state.config.world_height;
            if (wander_target.has_value()) {
                pending_move_mode = PendingMoveMode::Wander;
            } else {
                std::uniform_real_distribution<> angle_dist(0.0, 2 * M_PI);
                std::uniform_real_distribution<> unit01(0.0, 1.0);
                // 最多尝试若干次以找到可行走点
                for (int tries = 0; tries < 6 && !wander_target.has_value(); ++tries) {
                    const double angle = angle_dist(rng);
                    // 面积均匀采样半径，并避免极小半径造成近点抖动
                    const double r = std::max(movement_speed, std::sqrt(unit01(rng)) * wander_radius);
                    Position candidate{
                        position.x + std::cos(angle) * r,
                        position.y + std::sin(angle) * r
                    };
                    // 边界约束（可行走区域）
                    candidate.x = std::max(0.0, std::min(static_cast<double>(world_width), candidate.x));
                    candidate.y = std::max(0.0, std::min(static_cast<double>(world_height), candidate.y));
                    // 简易避障：避免目标落在当前个体非常近处（无意义）
                    if (position.distance_to(candidate) < 1e-6) continue;
                    // 可在此处扩展更多地形/障碍检查（例如网格标记、不可通行区域等）
                    wander_target = candidate;
                }
                if (!wander_target.has_value()) {
                    // 兜底：若未选中合法目标，执行一次小幅随机移动
                    std::uniform_real_distribution<> angle2(0.0, 2 * M_PI);
                    const double a2 = angle2(rng);
                    Position fallback{
                        std::max(0.0, std::min(static_cast<double>(world_width), position.x + std::cos(a2) * movement_speed)),
                        std::max(0.0, std::min(static_cast<double>(world_height), position.y + std::sin(a2) * movement_speed))
                    };
                    wander_target = fallback;
                }
                pending_move_mode = PendingMoveMode::Wander;
            }
        }
    }

    // 基础繁殖逻辑：满足条件即可提交繁殖请求。
    if (can_reproduce() && !pending_spawn_position.has_value()) {
        const double radius = reproduction_spawn_radius();
        std::uniform_real_distribution<> dist_angle(0.0, 2 * M_PI);
        std::uniform_real_distribution<> dist_radius(0.0, radius);
        const double angle = dist_angle(rng);
        const double distance = dist_radius(rng);
        Position spawn_candidate{
            std::max(0.0, std::min(static_cast<double>(ecosystem_state.config.world_width), position.x + std::cos(angle) * distance)),
            std::max(0.0, std::min(static_cast<double>(ecosystem_state.config.world_height), position.y + std::sin(angle) * distance))
        };
        pending_spawn_position = spawn_candidate;
        energy -= reproduction_energy_cost;
        start_reproduction_cooldown();
        ecosystem_state.submit_interaction_request(AttemptToReproduceRequest{shared_from_this()});
    }
}

void Animal::apply(const EcosystemState& ecosystem_state) {
    ZoneScoped;
    Species::apply(ecosystem_state);
    if (!alive) {
        return;
    }

    const int world_width = ecosystem_state.config.world_width;
    const int world_height = ecosystem_state.config.world_height;

    switch (pending_move_mode) {
        case PendingMoveMode::Path:
            move_to_target_point(world_width, world_height);
            break;
        case PendingMoveMode::Wander:
            if (wander_target.has_value()) {
                const Position target = wander_target.value();
                move_towards_target(target, world_width, world_height);
                // 只有完全到达目标才清除并允许选择下一个游荡点
                if (position.distance_to(target) <= movement_speed) {
                    wander_target.reset();
                }
            }
            break;
        case PendingMoveMode::None:
        default:
            break;
    }

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
            movement_speed = base_movement_speed * 0.8;
            energy_consumption = base_energy_consumption * 0.8;
            break;
        case HungerState::STARVING:
            is_wandering = false;
            movement_speed = base_movement_speed * 2.0;
            energy_consumption = base_energy_consumption * 2.0;
            break;
        case HungerState::NORMAL:
        default:
            if (wandering_cooldown > 0) {
                wandering_cooldown--;
            } else {
                is_wandering = false;
            }
            movement_speed = base_movement_speed;
            energy_consumption = base_energy_consumption;
            break;
    }
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

std::optional<Position> Animal::find_nearest_food(const EcosystemState& ecosystem_state) {
    // 寻找最近的食物源
    std::optional<Position> nearest_food;
    double min_distance = std::numeric_limits<double>::max();

    for (const auto& food_type : food_types) {
        auto it = ecosystem_state.get_ecosystem_state().species_lists.find(food_type); // 查找食物类型
        if (it == ecosystem_state.get_ecosystem_state().species_lists.end()) continue; // 未找到食物类型
        
        const auto& food_list = it->second; // 获取食物列表
        for (const auto& food : food_list) { // 遍历食物列表
            if (food->alive) {
                double distance = position.distance_to(food->position);
                if (distance <= detection_range && distance < min_distance) { // 检测范围内且更近
                    min_distance = distance;
                    nearest_food = food->position;
                }
            }
        }
    }
    return nearest_food;
}

void Animal::move_towards_target(const Position& target_position, int world_width, int world_height) {
    // 朝目标位置移动
    if (!alive) return;
    double dx = target_position.x - position.x;
    double dy = target_position.y - position.y;
    double distance = std::sqrt(dx * dx + dy * dy);

    if (distance > 0) {
        // 步长限制：避免越过目标导致来回抖动
        const double step = std::min(movement_speed, distance);
        dx = (dx / distance) * step;
        dy = (dy / distance) * step;
        // 更新位置，确保不超出边界
        position.x = std::max(0.0, std::min((double)world_width, position.x + dx));
        position.y = std::max(0.0, std::min((double)world_height, position.y + dy));
    }
}

void Animal::intelligent_move(const EcosystemState& ecosystem_state) {
    // 智能移动 - 分离为目标选择与移动执行
    if (!alive) return;
    if (hunting_cooldown > 0) {
        hunting_cooldown -= 1;
        return;
    }

    // 先选择目标点（可能为空）
    select_target_point(ecosystem_state);

    int world_width = ecosystem_state.config.world_width;
    int world_height = ecosystem_state.config.world_height;

    if (current_target.has_value()) {
        // 为目标规划路径（占位，未来可替换为 A*）
        plan_path_to_target(ecosystem_state);
        // 执行沿路径移动一步
        move_to_target_point(world_width, world_height);
    } else {
        // 无目标时采用随机游走
    auto& rng = const_cast<EcosystemState&>(ecosystem_state).get_thread_local_rng();
    move_randomly(world_width, world_height, movement_speed, rng);
    }
}

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

    const auto nearby_entities = ecosystem_state.get_nearby_species_broad(position, detection_range);
    for (const auto& entity : nearby_entities) {
        if (!entity || !entity->alive) {
            continue;
        }
        if (std::find(food_types.begin(), food_types.end(), entity->species_name) == food_types.end()) {
            continue;
        }

        double distance = position.distance_to(entity->position);
        if (distance <= detection_range && distance < min_distance) {
            min_distance = distance;
            nearest_food = entity->position;
        }
    }

    if (nearest_food.has_value()) {
        current_target = nearest_food.value();
    } else {
        current_target.reset();
        planned_path.clear();
        planned_path_index = 0;
    }
}

void Animal::plan_path_to_target(const EcosystemState& ecosystem_state) {
    // 路径规划占位：目前直接使用直线目标点，未来可替换为 A*
    if (!current_target.has_value()) return;
    planned_path.clear();
    planned_path.push_back(current_target.value());
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
    if (remain <= movement_speed) {
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
    // 动物通用繁殖判断：基础条件 + 年龄门槛
    return Species::can_reproduce() && age > min_reproduction_age;
}

void Animal::start_reproduction_cooldown() {
    // 开始繁殖冷却：使用基础冷却值
    reproduction_cooldown = base_reproduction_cooldown;
}

std::unique_ptr<Species> Animal::reproduce(const EcosystemState& ecosystem_state) {
    (void)ecosystem_state;
    return nullptr;
}