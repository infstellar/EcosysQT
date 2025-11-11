/*
物种数据模型 - Animal 基类实现
定义生态系统中的动物基类，继承自Species并添加智能移动
*/

#include "animal.h"
#include "race_base.h"
#include "species_params.h"
#include "ecosystem.h"
#include "behavior_tree.h"
#include "animal_behavior.h"
#include "bt_keys.h"
#include "tracy/Tracy.hpp"
#include <spdlog/spdlog.h>
#include <random>
#include <algorithm>
#include <cmath>

// --- Animal ---
// 动物基类 - 继承自Species并添加智能移动
// 兼容构造：未提供物种名时，默认使用 "RaceBase"（将回退到代码版行为树）
Animal::Animal(Position pos, const AnimalParams& params, std::mt19937& rng)
        : Animal(pos, std::string("RaceBase"), params, rng) {}

// 主构造：在构造时设置物种名，便于立即加载 YAML 行为树
Animal::Animal(Position pos, const std::string& species_name, const AnimalParams& params, std::mt19937& rng)
        : RaceBase(pos, species_name, params.energy, params.max_age, params.reproduction_energy_cost, params.hp_max),
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
            wander_radius(params.wander_radius),
            mating_desire_probability(params.mating_desire_probability),
            nutrition_value(params.nutrition_value) {
    // 交配/怀孕相关参数初始化
    mating_duration = params.mating_duration;
    pregnancy_duration = params.pregnancy_duration;
    mating_range = params.mating_range;
    pregnancy_speed_penalty = params.pregnancy_speed_penalty;
    // 初始化每tick步长为当前移动速度（tick制）
    step_distance_per_tick = movement_speed;
    current_step_distance = step_distance_per_tick; // 首帧近似为1 tick
    // 诊断：构造时确认孕速惩罚与移动速度
    SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"),
        "[Animal Ctor] '{}' created: pregnancy_speed_penalty={:.2f}, movement_speed={:.2f}",
        species_name, pregnancy_speed_penalty, movement_speed);

    // energy加成 ~ https://doi.org/10.1093/conphys/coac083
    // 加成公式 = nutrition_bonus_max * pow(prey.energy / prey.max_energy, nutrition_bonus_curve_alpha)
    // 最终获得能量 gained_energy = (prey.nutrition_value + bonus) * predator.energy_efficiency
    nutrition_bonus_max = std::max(0.0, params.nutrition_bonus_max);
    nutrition_bonus_curve_alpha = std::max(0.0, params.nutrition_bonus_curve_alpha);

    // 基础生命恢复量
    hp_regen_base_per_day = std::max(0.0, params.hp_regen_base_per_day);
    // 各状态恢复倍率
    hp_regen_mul_satisfied = std::max(0.0, params.hp_regen_mul_satisfied);
    hp_regen_mul_normal    = std::max(0.0, params.hp_regen_mul_normal);
    hp_regen_mul_starving  = std::max(0.0, params.hp_regen_mul_starving);
    // 恢复触发间隔比例
    regan_interval_ratio   = std::clamp(params.regan_interval_ratio, 0.0, 1.0);
    ticks_since_last_regen = 0;

    // 使用传递进来的 rng，而不是线程本地的
    std::uniform_int_distribution<> dist(0, 1);
    sex = (dist(rng) == 0) ? Sex::MALE : Sex::FEMALE;

    is_pregnant = false;
    pregnancy_timer = 0;
    mating_timer = 0;
    // 初始化交配意图锁定时长（可按需调整或从参数映射）
    // 行为树脚手架构建（默认关闭，若 species_name 有对应 YAML 则加载并使用）
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

    // 每 tick 执行 HP 恢复
    apply_hp_regen(ecosystem_state);

    // 当使用行为树时，移动与消耗由 BT Action 执行；此处不再运行旧移动分支
    if (use_bt) {
        return;
    }

    // 非 BT 路径：不再包含旧的 Path/Wander 移动逻辑，仅处理基础能量结算
    energy -= energy_consumption;
    if (energy < 0.0) energy = 0.0; // 统一：不直接死亡，改由饥饿伤害扣 HP
}

// 根据当前饥饿状态恢复生命
void Animal::apply_hp_regen(const EcosystemState& ecosystem_state) {
    update_hunger_state();
    const int tpd = std::max(1, ecosystem_state.config.ticks_per_day);
    const double base_per_tick = hp_regen_base_per_day / static_cast<double>(tpd);
    double hunger_mul = 0.0;
    switch (hunger_state) {
        // 满足：快速恢复
        case HungerState::SATISFIED: hunger_mul = hp_regen_mul_satisfied; break;
        // 正常：基础值
        case HungerState::NORMAL:    hunger_mul = hp_regen_mul_normal; break;
        // 饥饿：恢复缓慢
        case HungerState::STARVING:  hunger_mul = hp_regen_mul_starving; break;
        default: hunger_mul = 0.0; break;
    }
    // 根据比例计算触发间隔的 tick 数（至少为 1）
    const int interval_ticks = std::max(1, static_cast<int>(std::floor(std::max(0.0, regan_interval_ratio) * static_cast<double>(tpd))));
    // 自增计时器；当达到间隔后批量结算该段累计的恢复量
    ticks_since_last_regen++;
    if (ticks_since_last_regen < interval_ticks) {
        return;
    }
    // 批量恢复：保持每日期望不变（按间隔汇总 base_per_tick * ticks）
    const double regen_amount = base_per_tick * hunger_mul * static_cast<double>(ticks_since_last_regen);
    ticks_since_last_regen = 0;
    if (regen_amount > 0.0 && hp_current < hp_max) {
        hp_current = std::min(hp_max, hp_current + regen_amount);
        SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"),
            "[HP Regen] '{}' +{:.3f} (tpd={}, interval_ticks={}, base/day={:.2f}, hunger_mul={:.2f})",
            species_name, regen_amount, tpd, interval_ticks, hp_regen_base_per_day, hunger_mul);
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

// ---- 新增：公共访问接口实现（供行为树使用） ----
HungerState Animal::get_hunger_state() const { return hunger_state; }
void Animal::refresh_hunger_state() { update_hunger_state(); }
double Animal::get_mating_range() const { return mating_range; }
double Animal::get_wander_radius() const { return wander_radius; }
double Animal::get_mating_desire_probability() const { return mating_desire_probability; }
double Animal::get_detection_range() const { return detection_range; }
double Animal::get_pregnancy_speed_penalty() const { return pregnancy_speed_penalty; }
bool Animal::get_skip_movement() const { return skip_movement; }
void Animal::set_skip_movement(bool v) { skip_movement = v; }
void Animal::clear_sensor_caches() {
    cached_food_races.clear();
    cached_mates.clear();
}
void Animal::cache_mate(const std::shared_ptr<Animal>& mate) { cached_mates.emplace_back(mate); }
void Animal::cache_food_race(const std::shared_ptr<RaceBase>& race) { cached_food_races.emplace_back(race); }
std::vector<std::weak_ptr<Animal>> Animal::get_cached_mates_snapshot() const { return cached_mates; }
std::vector<std::weak_ptr<RaceBase>> Animal::get_cached_food_races_snapshot() const { return cached_food_races; }
void Animal::set_current_target(const std::optional<Position>& p) { current_target = p; }
std::optional<Position> Animal::get_current_target() const { return current_target; }
void Animal::clear_current_target() { current_target.reset(); }
void Animal::set_mating_target(const std::optional<Position>& p) { mating_target = p; }
std::optional<Position> Animal::get_mating_target() const { return mating_target; }
void Animal::clear_mating_target() { mating_target.reset(); }
void Animal::set_wander_target(const std::optional<Position>& p) { wander_target = p; }
std::optional<Position> Animal::get_wander_target() const { return wander_target; }
void Animal::clear_wander_target() { wander_target.reset(); }
void Animal::clear_path() { planned_path.clear(); planned_path_index = 0; }
double Animal::get_current_step_distance() const { return current_step_distance; }
double Animal::get_step_distance_per_tick() const { return step_distance_per_tick; }

std::vector<Position> Animal::get_planned_path_snapshot() const {
    return planned_path;
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
        // 优化：缩小减速半径，避免过早减速导致“靠近非常慢”的体验
        const double slow_radius = std::max(current_step_distance * 2.0, step_distance_per_tick * 1.0);
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
    const double arrival_threshold = std::max(0.2, current_step_distance * 0.5);
    if (remain <= arrival_threshold) {
        if (!planned_path.empty() && planned_path_index < planned_path.size()) {
            planned_path_index += 1;
            if (planned_path_index >= planned_path.size()) {
                // 路径完成：保留 current_target，交由上层行为（如 EatNearbyThing）处理近场交互
                planned_path.clear();
                planned_path_index = 0;
            }
        } else {
            // 直接目标已到达：不再主动清空 current_target，避免“到达-清空-重选-再移动”的停顿感
            // 保持目标以便上层优先选择器首先尝试近场动作（吃草/交互），从而平滑收敛
        }
    }
}

void Animal::start_hunting_cooldown() {
    // 开始狩猎冷却 - 动物将保持静止一段时间
    hunting_cooldown = hunting_cooldown_duration;
}

bool Animal::can_reproduce() const {
    if (sex == Sex::MALE) {
        // 雄性检查自身状态（能量、年龄、冷却）；若正在交配则不可重复触发
        return (mating_timer <= 0) && RaceBase::can_reproduce() && age > min_reproduction_age;
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
    if (behavior_tree) {
        auto& bb = behavior_tree->blackboard();
        const std::string source = (bb.strings.find("bt_source") != bb.strings.end()) ? bb.strings.at("bt_source") : std::string("unknown");
        SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[BT] Loaded tree for '{}' from {}", species_name, source);
    } else {
        SPDLOG_LOGGER_ERROR(spdlog::get("ecosim"), "[BT] Failed to build behavior tree for '{}'", species_name);
    }
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
    // 确保攻击伤害存在于黑板（若 YAML 未提供，则使用物种默认值）
    if (bb.doubles.find("attack_damage") == bb.doubles.end()) {
        bb.doubles["attack_damage"] = params.attack_damage;
    }
    // 注入字符串参数
    for (const auto& kv : params.bt_params_strings) {
        bb.strings[kv.first] = kv.second;
    }

    // 饥饿伤害参数（若 YAML 未在 bt_params 指定，则回退到 species 默认值）
    if (bb.doubles.find("starvation_damage") == bb.doubles.end()) {
        bb.doubles["starvation_damage"] = std::max(0.0, params.starvation_damage);
    }
    if (bb.doubles.find("starvation_damage_interval_ratio") == bb.doubles.end()) {
        bb.doubles["starvation_damage_interval_ratio"] = std::max(0.0, std::min(1.0, params.starvation_damage_interval_ratio));
    }

    // 注入游荡总时长到黑板，供进度装饰器读取（若 YAML 未提供则使用 species_params 默认值）
    if (params.wandering_duration > 0) {
        bb.ints[bt::keys::WanderTotalTicks] = params.wandering_duration;
    }
    // 初始化游荡当前进度为 0，确保首次可见且不受之前残留影响
    if (bb.ints.find(bt::keys::WanderCurrentTicks) == bb.ints.end()) {
        bb.ints[bt::keys::WanderCurrentTicks] = 0;
    }

    // 追草多步推进的默认值（未在 YAML 指定时），缓解“逐帧小步·放大似瞬移”问题
    if (bb.ints.find("chase_substeps_per_tick") == bb.ints.end()) {
        bb.ints["chase_substeps_per_tick"] = 3; // 默认每 tick 连续推进 3 步
    }

    // 打印调试信息：eat_grass_total_ticks 来源与当前黑板值
    {
        int eat_total = -1;
        auto it = bb.ints.find(bt::keys::EatGrassTotalTicks);
        if (it != bb.ints.end()) eat_total = it->second;
        SPDLOG_LOGGER_INFO(spdlog::get("ecosim"),
            "[BT Params] '{}' eat_grass_total_ticks={} (after injection)",
            species_name, eat_total);
        // 初始化吃草当前进度键，便于进度装饰器与日志显示
        if (bb.ints.find(bt::keys::EatGrassCurrentTicks) == bb.ints.end()) {
            bb.ints[bt::keys::EatGrassCurrentTicks] = 0;
        }
    }
}

// --- 统一能量与一步移动封装（供行为树动作复用） ---
void Animal::consume_energy(double multiplier) {
    const double m = std::max(0.0, multiplier);
    energy -= (static_cast<double>(energy_consumption) * m);
    // 统一生命机制：能量耗尽不直接死亡，改由行为树 Update 里的饥饿伤害扣 HP
    if (energy < 0.0) energy = 0.0;
}

double Animal::get_nutrition_value() const {
    return std::max(0.0, nutrition_value);
}

// 通用一步移动 + 能量结算内核：由调用者提供具体推进实现
void Animal::perform_step_move_common(double speed_multiplier, double energy_multiplier,
                                      const char* log_tag,
                                      const std::function<void()>& advance_fn) {
    const double prev_x = position.x;
    const double prev_y = position.y;
    current_step_distance = step_distance_per_tick * std::max(0.0, speed_multiplier);
    advance_fn();
    const double moved_dx = std::abs(position.x - prev_x);
    const double moved_dy = std::abs(position.y - prev_y);
    {
        const double moved_len = std::sqrt(moved_dx * moved_dx + moved_dy * moved_dy);
        SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"),
            "[MoveStep->{}] '{}' step={:.2f} moved={:.2f} prev=({:.1f},{:.1f}) now=({:.1f},{:.1f}) mul(speed={:.2f}, energy={:.2f})",
            log_tag,
            species_name,
            current_step_distance,
            moved_len,
            prev_x, prev_y,
            position.x, position.y,
            std::max(0.0, speed_multiplier), std::max(0.0, energy_multiplier));
    }
    if (moved_dx > 1e-9 || moved_dy > 1e-9) {
        consume_energy(energy_multiplier);
    }
}

void Animal::perform_step_move_to(const Position& target, int world_width, int world_height,
                                  double speed_multiplier, double energy_multiplier) {
    perform_step_move_common(speed_multiplier, energy_multiplier, "To",
        [this, target, world_width, world_height]() {
            move_towards_target(target, world_width, world_height);
        }
    );
}

#ifdef ECOSIM_ENABLE_UI_DEBUG

void Animal::update_ui_snapshot(const AnimalUiSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(m_ui_snapshot_mutex);
    auto history = std::move(m_ui_snapshot.interaction_history);
    m_ui_snapshot = snapshot;
    m_ui_snapshot.interaction_history = std::move(history);
}

AnimalUiSnapshot Animal::get_ui_snapshot() const {
    std::lock_guard<std::mutex> lock(m_ui_snapshot_mutex);
    return m_ui_snapshot;
}

void Animal::add_interaction_log(const std::string& message, bool success, int timestamp) {
    std::lock_guard<std::mutex> lock(m_ui_snapshot_mutex);

    constexpr std::size_t kMaxHistorySize = 50;

    m_ui_snapshot.interaction_history.push_back({timestamp, message, success});

    if (m_ui_snapshot.interaction_history.size() > kMaxHistorySize) {
        const auto overflow = m_ui_snapshot.interaction_history.size() - kMaxHistorySize;
        m_ui_snapshot.interaction_history.erase(
            m_ui_snapshot.interaction_history.begin(),
            m_ui_snapshot.interaction_history.begin() + overflow
        );
    }
}

#endif

void Animal::perform_step_move_path(int world_width, int world_height,
                                    double speed_multiplier, double energy_multiplier) {
    perform_step_move_common(speed_multiplier, energy_multiplier, "Path",
        [this, world_width, world_height]() {
            move_to_target_point(world_width, world_height);
        }
    );
}
