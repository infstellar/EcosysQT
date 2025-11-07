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
#include "tracy/Tracy.hpp"
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

    // --- 1. 状态更新与意图重置 ---
    pending_move_mode = PendingMoveMode::None;
    skip_movement = false;
    current_target.reset(); // 每轮决策前清空最终目标
    // mating_target 不再每帧重置；通过锁定与条件释放控制

    update_hunger_state();
    adjust_stats_by_state();

    // 交配意图锁定与释放策略：
    // - 锁定期间保持交配目标，不进入觅食分支，避免来回切换
    // - 若进入饥饿严重状态（STARVING）且锁定已结束，则释放交配目标让位觅食
    if (mating_intent_lock_ticks > 0) {
        mating_intent_lock_ticks -= 1;
    } else {
        if (mating_target.has_value() && hunger_state == HungerState::STARVING) {
            mating_target.reset();
        }
    }

    // --- 2. 处理进行中的高优先级状态 (交配/怀孕) ---
    if (mating_timer > 0) {
        mating_timer--;
        skip_movement = true;
        return; // 交配中，不做任何其他事
    }
    if (is_pregnant) {
        pregnancy_timer--;
        if (pregnancy_timer <= 0) {
            // 怀孕结束，进入分娩流程
            is_pregnant = false;

            // --- 使用您提供的繁殖逻辑来处理分娩 ---
            // 1. 计算新生儿出生位置
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

            // 2. 提交分娩请求
            // 注意：能量消耗和冷却已在交配时处理，此处不再重复
            ecosystem_state.submit_interaction_request(AttemptToReproduceRaceRequest{self});
            
            // 分娩时通常会暂停移动
            skip_movement = true; 
        }
    }

    // --- 3. 行为决策 (按优先级进行) ---

    // 优先级 1: 交配意图
    if (sex == Sex::MALE && can_reproduce()) {
        // --- 新增：求偶意愿概率判断 ---
        std::uniform_real_distribution<> desire_dist(0.0, 1.0);
        if (desire_dist(rng) < mating_desire_probability) { // 只有在随机数小于意愿概率时才去寻找配偶
            auto mate_opt = find_available_mate(ecosystem_state);
            if (mate_opt.has_value()) {
                auto mate = mate_opt.value();
                if (position.distance_to(mate->position) <= mating_range) {
                    // 在范围内，提交交配请求
                    ecosystem_state.submit_interaction_request(AttemptToMateRequest{mate, std::dynamic_pointer_cast<Animal>(self)});
                    skip_movement = true;
                } else {
                    // 不在范围内，将配偶设为最高优先级目标
                    mating_target = mate->position;
                    mating_intent_lock_ticks = mating_intent_lock_duration; // 锁定意图一段时间
                }
            }
        }
    }

    // 优先级 2: 觅食意图 (仅在没有交配目标时考虑)
    if (!mating_target.has_value()) {
        if (hunger_state != HungerState::SATISFIED && !food_types.empty()) {
            const std::string& primary_food = food_types.front();
            if (primary_food == "grass" && eating_range > 0.0) {
                if (ecosystem_state.config.world_width > 0 && ecosystem_state.config.world_height > 0) {
                    const int max_x = ecosystem_state.config.world_width - 1;
                    const int max_y = ecosystem_state.config.world_height - 1;
                    int tile_x = static_cast<int>(std::floor(position.x));
                    int tile_y = static_cast<int>(std::floor(position.y));
                    tile_x = std::clamp(tile_x, 0, max_x);
                    tile_y = std::clamp(tile_y, 0, max_y);

                    if (ecosystem_state.is_valid_grid_coord(tile_x, tile_y)) {
                        Tile& current_tile = ecosystem_state.get_tile(tile_x, tile_y);
                        for (ThingBase* thing : current_tile.things) {
                            if (!thing || !thing->alive) {
                                continue;
                            }
                            if (thing->species_name != "grass") {
                                continue;
                            }

                            auto target = thing->shared_from_this();
                            if (!target) {
                                continue;
                            }

                            ecosystem_state.submit_interaction_request(
                                AttemptToEatThingRequest{self, target});
                            break; // 单次觅食
                        }
                    }
                }
            }

            if (primary_food == "cow" && hunting_range > 0.0 && hunting_cooldown <= 0) {
                const double desire = get_hunting_desire();
                if (desire > 0.0) {
                    std::uniform_real_distribution<> hunt_dist(0.0, 1.0);
                    if (hunt_dist(rng) < hunting_success_rate * desire) {
                        auto nearby_races = ecosystem_state.get_nearby_races_broad(position, hunting_range);
                        for (const auto& race : nearby_races) {
                            if (!race || !race->alive) {
                                continue;
                            }
                            if (race.get() == this) {
                                continue;
                            }
                            if (race->species_name != "cow") {
                                continue;
                            }
                            if (position.distance_to(race->position) > hunting_range) {
                                continue;
                            }

                            ecosystem_state.submit_interaction_request(AttemptToEatRaceRequest{self, race});
                            start_hunting_cooldown();
                            break; // 单次狩猎
                        }
                    }
                }
            }
        }

        // --- 这里是您现有的寻找远处食物的逻辑，保持不变 ---
        select_target_point(ecosystem_state); // 这个函数会尝试为觅食设置 current_target
    }

    // --- 4. 最终目标确定与移动规划 ---
    if (skip_movement) {
        // 如果因提交请求而跳过移动，则清空所有目标
        current_target.reset();
        wander_target.reset();
        mating_target.reset(); // 提交交配或分娩后，释放交配目标避免残留
    } else {
        // 按照优先级，将最高意图的目标赋给 current_target
        if (mating_target.has_value()) {
            current_target = mating_target; // 交配是最高优先级
        }
        // 如果没有交配目标，current_target 可能已经被 select_target_point 设置为食物目标

        if (current_target.has_value()) {
            // 如果最终确定了目标（无论是配偶还是食物），则规划路径
            plan_path_to_target(ecosystem_state, current_target);
            pending_move_mode = PendingMoveMode::Path;
        } else {
            // 优先级 3: 游荡意图 (如果没有任何目标)
            // --- 这里是您现有的游荡逻辑，保持不变 ---
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

    if (hunting_cooldown > 0) {
        hunting_cooldown -= 1;
    }
}

void Animal::apply(const EcosystemState& ecosystem_state) {
    ZoneScoped;
    RaceBase::apply(ecosystem_state);
    if (!alive) {
        return;
    }

    // 单步锁步：每次应用阶段推进一个固定步长
    current_step_distance = step_distance_per_tick;

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
                // 平滑到达阈值：避免门槛效应导致抖动
                const double arrival_threshold = std::max(1.0, current_step_distance * 0.5);
                if (position.distance_to(target) <= arrival_threshold) {
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
            movement_speed = base_movement_speed * 0.2;
            energy_consumption = base_energy_consumption * 0.5;
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
    const EcosystemStateData snapshot = ecosystem_state.get_ecosystem_state();

    for (const auto& food_type : food_types) {
        const auto race_it = snapshot.race_lists.find(food_type);
        if (race_it != snapshot.race_lists.end()) {
            for (const auto& food : race_it->second) {
                if (food && food->alive) {
                    double distance = position.distance_to(food->position);
                    if (distance <= detection_range && distance < min_distance) {
                        min_distance = distance;
                        nearest_food = food->position;
                    }
                }
            }
            continue;
        }

        const auto thing_it = snapshot.thing_lists.find(food_type);
        if (thing_it == snapshot.thing_lists.end()) {
            continue;
        }

        for (const auto& food : thing_it->second) {
            if (food && food->alive) {
                double distance = position.distance_to(food->position);
                if (distance <= detection_range && distance < min_distance) {
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
        // 到达减速（Arrive）：临近目标时按比例减速，平滑收敛
    const double slow_radius = std::max(current_step_distance * 8.0, step_distance_per_tick * 4.0);
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

    // 单步锁步：智能移动与 apply 使用一致步长
    current_step_distance = step_distance_per_tick;

    if (current_target.has_value()) {
        // 为目标规划路径（占位，未来可替换为 A*）
        plan_path_to_target(ecosystem_state,current_target);
        // 执行沿路径移动一步
        move_to_target_point(world_width, world_height);
    } else {
        // 无目标时采用随机游走
    auto& rng = const_cast<EcosystemState&>(ecosystem_state).get_thread_local_rng();
    move_randomly(world_width, world_height, current_step_distance, rng);
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
        start_reproduction_cooldown();
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
    using namespace bt;
    // 根：优先级选择器（高优先级在前）
    auto root = std::make_shared<PrioritySelector>();

    // 交配序列：条件 -> 追配偶/提交交互（占位行动）
    auto seq_mate = std::make_shared<Sequence>();
    seq_mate->add_child(std::make_shared<Condition>([this](TickContext&){
        return sex == Sex::MALE && can_reproduce();
    }));
    seq_mate->add_child(std::make_shared<Action>([this](TickContext&){
        // 占位：在完整迁移时将调用寻找配偶与路径规划
        // 目前返回 Running 以表示此分支可持续执行
        return Status::Running;
    }));

    // 觅食/捕食序列：条件 -> 搜索/进食（占位行动）
    auto seq_forage = std::make_shared<Sequence>();
    seq_forage->add_child(std::make_shared<Condition>([this](TickContext&){
        return hunger_state != HungerState::SATISFIED && !food_types.empty();
    }));
    seq_forage->add_child(std::make_shared<Action>([this](TickContext&){
        return Status::Running;
    }));

    // 游荡行为：无条件行动（占位）
    auto act_wander = std::make_shared<Action>([this](TickContext&){
        return Status::Running;
    });

    root->add_child(seq_mate);
    root->add_child(seq_forage);
    root->add_child(act_wander);

    behavior_tree = std::make_unique<BehaviorTree>(root);
}