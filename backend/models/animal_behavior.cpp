/*
动物行为模块实现 - 行为树构建
按既定方案构建三大分支：
1) 交配（雄性触发，意愿概率，近距离提交交互，远距离锁定目标并路径前往）
2) 觅食/捕食（近场吃草/狩猎，失败则远处目标选择与路径）
3) 游荡（采样游荡目标，平滑到达与模式标记）
迁移阶段说明：在 use_bt=true 时，行为树的 Action 将直接执行一步移动并进行能量结算；apply 作为兼容层收口。
*/

#include "animal_behavior.h"
#include "behavior_tree.h"
#include "animal.h"
#include "ecosystem.h"
#include "interaction_requests.h"
#include "thing_base.h"
#include <random>
#include <cmath>
// YAML 解析与路径访问
#include <yaml-cpp/yaml.h>
#include <spdlog/spdlog.h>
#include "race_factory.h"
#include "species_config_provider.h"
// 新的可复用行为动作封装
#include "bt_actions.h"
#include "bt_keys.h"
// 注册表所需容器
#include <unordered_map>
#include <functional>
#include <utility>
#include <stdexcept>
#include "tracy/Tracy.hpp"

#ifdef ECOSIM_ENABLE_UI_DEBUG
#include "animal_ui_snapshot.h"
#endif

namespace behavior {

using namespace bt;

static inline double bb_get_double(Blackboard* bb, const std::string& key, double def_v = 0.0) {
    if (!bb) return def_v;
    auto it = bb->doubles.find(key);
    return it == bb->doubles.end() ? def_v : it->second;
}

#ifdef ECOSIM_ENABLE_UI_DEBUG
static inline int bb_get_int(Blackboard* bb, const std::string& key, int def_v = 0) {
    if (!bb) return def_v;
    auto it = bb->ints.find(key);
    return it == bb->ints.end() ? def_v : it->second;
}
#endif

// 默认行为参数常量：集中管理以替代魔法数字
namespace defaults {
    static constexpr int EAT_TICKS = 20;
    static constexpr int WANDER_TICKS = 50;
}

// 前向声明：YAML 节点解析器（在后文定义）
static std::shared_ptr<Node> parse_bt_yaml_node(const YAML::Node& n, Animal& self);

// 通用 Update 节点构建：复用 YAML/代码两种来源的相同行为
static std::shared_ptr<Node> create_update_node(Animal& self, const char* source_tag = "Code") {
    return std::make_shared<Action>([&self, source_tag](TickContext& ctx){
        ZoneScopedN("Animal::BT::Update");
        auto* world = static_cast<EcosystemState*>(ctx.world);
        if (!world || !self.alive) return Status::Failure;

#ifdef ECOSIM_ENABLE_UI_DEBUG
        if (ctx.blackboard) {
            ctx.blackboard->strings["_ui_current_action"] = "Idle";
        }
#endif

        {
            ZoneScopedN("BT::Update::State");
            // 本 tick 开始先清除跨 tick 残留的移动跳过标记，避免卡住
            self.set_skip_movement(false);
            SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[BT {}] Update: reset skip_movement=false for '{}'", source_tag, self.species_name);
            // 饱食状态更新（速度/能耗不再全局调整，改由具体 Action 的倍率控制）
            self.refresh_hunger_state();

            // 从黑板读取意图锁定时长（可由 YAML 配置覆盖）
            if (self.get_mating_target().has_value() && self.get_hunger_state() == HungerState::STARVING) {
                self.clear_mating_target();
            }

            // 进行中的交配计时器（仅推进，不再强制停滞）
            if (self.mating_timer > 0) {
                self.mating_timer -= 1;
            }

            // 怀孕推进与分娩提交
            if (self.is_pregnant) {
                self.pregnancy_timer -= 1;
                if (self.pregnancy_timer <= 0) {
                    self.is_pregnant = false;
                    // 生成分娩位置候选
                    auto& rng_local = world->get_thread_local_rng();
                    std::uniform_real_distribution<> dist_angle(0.0, 2 * M_PI);
                    std::uniform_real_distribution<> dist_radius(std::max(0.2, self.movement_speed * 0.2), std::max(0.5, self.movement_speed * 2.5));
                    const double angle = dist_angle(rng_local);
                    const double distance = dist_radius(rng_local);
                    Position spawn_candidate{
                        std::max(0.0, std::min(static_cast<double>(world->config.world_width), self.position.x + std::cos(angle) * distance)),
                        std::max(0.0, std::min(static_cast<double>(world->config.world_height), self.position.y + std::sin(angle) * distance))
                    };
                    self.pending_spawn_position = spawn_candidate;
                    // 提交分娩请求并进入产后冷却
                    world->submit_interaction_request(AttemptToReproduceRaceRequest{self.shared_from_this()});
                    self.start_reproduction_cooldown();
                    self.set_skip_movement(true); // 分娩本 tick 不移动
                }
            }

            // 冷却推进
            if (self.hunting_cooldown > 0) {
                self.hunting_cooldown -= 1;
            }
        }

        // --- 将规范黑板键写入（供分支条件与装饰器使用） ---
        if (ctx.blackboard) {
            auto& bb = *ctx.blackboard;
            {
                ZoneScopedN("BT::Update::State");
                // 饥饿状态（枚举以 int 存储：0=SATISFIED,1=NORMAL,2=STARVING）
                int hunger_code = 1;
                switch (self.get_hunger_state()) {
                    case HungerState::SATISFIED: hunger_code = 0; break;
                    case HungerState::NORMAL: hunger_code = 1; break;
                    case HungerState::STARVING: hunger_code = 2; break;
                }
                bb.ints["hunger_state"] = hunger_code;

                // HP 比率（供条件与参数动态调整使用）
                const double hp_ratio = (self.hp_max > 0.0) ? std::max(0.0, std::min(1.0, self.hp_current / self.hp_max)) : 1.0;
                bb.doubles[bt::keys::HpRatio] = hp_ratio;

                // 交配计时器（用于 UI 展示或进度装饰器）
                bb.ints[bt::keys::MatingTimerTicks] = std::max(0, self.mating_timer);

                // 繁殖守卫相关键：最低能量、最低年龄、冷却剩余
                bb.doubles["repro_energy_min"] = self.reproduction_energy_cost * 2.0;
                bb.ints["repro_age_min"] = self.min_reproduction_age;
                bb.ints["repro_cooldown_ticks"] = self.reproduction_cooldown;
            }

            {
                ZoneScopedN("BT::Update::Threat");
                // 逃逸阈值：按物种设置。默认=探测范围；牛用较小比例（不影响其他用途的探测范围）
                const double default_threat_threshold = (self.species_name == std::string("cow"))
                    ? std::max(0.0, self.get_detection_range() * 0.1)
                    : self.get_detection_range();
                if (bb.doubles.find(bt::keys::ThreatThreshold) == bb.doubles.end()) {
                    bb.doubles[bt::keys::ThreatThreshold] = default_threat_threshold;
                }
                const double threat_threshold = (bb.doubles.find(bt::keys::ThreatThreshold) != bb.doubles.end())
                    ? bb.doubles[bt::keys::ThreatThreshold]
                    : default_threat_threshold;

                // 探测威胁（仅在阈值范围内），目前将“tiger”视为威胁对象
                bool danger = false;
                double threat_dist = std::numeric_limits<double>::max();
                Position threat_pos = self.position;
                {
                    ZoneScopedN("BT::Update::Threat::Query");
                    const auto nearby = world->get_nearby_races_broad(self.position, threat_threshold);
                    for (const auto& r : nearby) {
                        if (!r || !r->alive) continue;
                        if (r.get() == &self) continue;
                        if (r->species_name == std::string("tiger") && self.species_name != std::string("tiger")) {
                            double d = self.position.distance_to(r->position);
                            if (d < threat_dist) {
                                threat_dist = d;
                                threat_pos = r->position;
                            }
                            if (d <= threat_threshold) {
                                danger = true;
                            }
                        }
                    }
                }
                bb.ints[bt::keys::DangerNearby] = danger ? 1 : 0;
                bb.doubles[bt::keys::ThreatDistance] = std::isfinite(threat_dist) ? threat_dist : (threat_threshold + 1.0);
                bb.doubles[bt::keys::ThreatPosX] = threat_pos.x;
                bb.doubles[bt::keys::ThreatPosY] = threat_pos.y;

                // 逃逸冷却：一旦触发危险或进入阈值范围，维持一段时间的“逃逸模式”，避免边界抖动
                // - flee_total_duration_ticks: 总维持时长（YAML 可覆盖），默认 20
                // - flee_mode_cooldown_ticks: 当前剩余冷却（>0 表示仍处于逃逸模式）
                int flee_total = 20;
                if (auto it_total = bb.ints.find("flee_total_duration_ticks"); it_total != bb.ints.end()) {
                    flee_total = std::max(0, it_total->second);
                }
                int flee_cd = 0;
                if (auto it_cd = bb.ints.find(bt::keys::FleeModeCooldownTicks); it_cd != bb.ints.end()) {
                    flee_cd = std::max(0, it_cd->second);
                }
                const bool edge_trigger = danger || (std::isfinite(threat_dist) && (threat_dist <= threat_threshold));
                if (edge_trigger) {
                    flee_cd = flee_total; // 进入或继续维持逃逸模式
                } else if (flee_cd > 0) {
                    flee_cd -= 1; // 按 tick 递减，逐帧稳定退出
                }
                bb.ints[bt::keys::FleeModeCooldownTicks] = flee_cd;
            }

            {
                ZoneScopedN("BT::Update::State");
                // 集中孕期与饥饿速度惩罚：本 tick 的基础速度倍率
                double base_speed_multiplier = 1.0;
                if (self.is_pregnant) {
                    base_speed_multiplier *= std::max(0.0, self.get_pregnancy_speed_penalty());
                }
                if (self.get_hunger_state() == HungerState::STARVING) {
                    double starving_mul = 1.0;
                    if (auto it = bb.doubles.find("starving_speed_multiplier"); it != bb.doubles.end()) {
                        starving_mul = it->second;
                    }
                    base_speed_multiplier *= std::max(0.0, starving_mul);
                }
                bb.doubles[bt::keys::CurrentSpeedMultiplier] = base_speed_multiplier;

                // 饥饿状态能耗降低：本 tick 的基础能量倍率（供移动 Action 使用）
                double base_energy_multiplier = 1.0;
                if (self.get_hunger_state() == HungerState::STARVING) {
                    double starving_energy_mul = 1.0;
                    if (auto it2 = bb.doubles.find("starving_energy_multiplier"); it2 != bb.doubles.end()) {
                        starving_energy_mul = it2->second;
                    }
                    base_energy_multiplier *= std::max(0.0, starving_energy_mul);
                }
                bb.doubles[bt::keys::CurrentEnergyMultiplier] = base_energy_multiplier;

                if (self.is_pregnant) {
                    SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"),
                        "[BT {}] Pregnant speed: penalty={:.2f} base_mul={:.2f} movement_speed={:.2f}",
                        source_tag, self.get_pregnancy_speed_penalty(), base_speed_multiplier, self.movement_speed);
                }
            }
            {
                ZoneScopedN("BT::Update::State");
                const double base_range = bb_get_double(&bb, "stop_range_base_range", 0.0);
                const double factor = bb_get_double(&bb, "stop_range_factor", 0.5);
                const double stop_range = std::max(0.0, base_range * factor);
                bb.doubles["eat_hard_stop_range"] = stop_range;

                // 维护滞后/冷却计数器：最近进食计时与强制游荡倒计时
                int meal_ticks = (bb.ints.find("ticks_since_last_meal") != bb.ints.end()) ? bb.ints["ticks_since_last_meal"] : 0;
                bb.ints["ticks_since_last_meal"] = std::max(0, meal_ticks + 1);

                // 饥饿伤害：在 STARVING 状态下按间隔扣减 HP
                {
                    ZoneScopedN("BT::Update::StarvationDamage");
                    const bool starving = (self.get_hunger_state() == HungerState::STARVING);
                    const double ratio = bb_get_double(&bb, "starvation_damage_interval_ratio", 0.25);
                    const double damage = bb_get_double(&bb, "starvation_damage", 0.0);
                    const int tpd = world ? world->config.ticks_per_day : 3000;
                    const double clamped_ratio = std::max(0.0, std::min(1.0, ratio));
                    int interval = std::max(1, static_cast<int>(std::floor(clamped_ratio * static_cast<double>(tpd))));
                    bb.ints["starvation_damage_interval_ticks"] = interval;

                    int sd_ticks = (bb.ints.find("ticks_since_last_starvation_damage") != bb.ints.end())
                        ? bb.ints["ticks_since_last_starvation_damage"]
                        : 0;
                    if (starving) {
                        sd_ticks = std::max(0, sd_ticks + 1);
                        if (damage > 0.0 && sd_ticks >= interval) {
                            self.take_damage(damage, "Starvation");
                            sd_ticks = 0;
                            SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"),
                                "[BT {}] Starvation dmg: '{}' -{:.1f} every {} ticks (tpd={})",
                                source_tag, self.species_name, damage, interval, tpd);
                        }
                    } else {
                        sd_ticks = 0;
                    }
                    bb.ints["ticks_since_last_starvation_damage"] = sd_ticks;
                }
            }
        }

        return Status::Success;
    });
}

// 通用 Finalize 节点构建：复用 YAML/代码两种来源的相同行为
static std::shared_ptr<Node> create_finalize_node(Animal& self, const char* source_tag = "Code") {
    return std::make_shared<Action>([&self, source_tag](TickContext& ctx){
        ZoneScopedN("Animal::BT::Finalize");
        auto* world = static_cast<EcosystemState*>(ctx.world);
        (void)world;
        if (!self.alive) return Status::Failure;

        // 统一清理：当本 tick 被请求占用或交配/分娩进行时，清理临时目标与路径
        if (self.get_skip_movement()) {
            self.clear_current_target();
            self.clear_path();
            self.clear_wander_target();
            self.clear_mating_target();
            if (ctx.blackboard) {
                auto& bb = *ctx.blackboard;
                bb.ints.erase("mate_target_id");
                bb.ints.erase(bt::keys::MatingTimerTicks);
                bb.doubles.erase(bt::keys::TargetPosX);
                bb.doubles.erase(bt::keys::TargetPosY);
            }
        }
#ifdef ECOSIM_ENABLE_UI_DEBUG
        if (ctx.blackboard) {
            auto& bb = *ctx.blackboard;
            AnimalUiSnapshot snapshot;
            if (auto it = bb.strings.find("_ui_current_action"); it != bb.strings.end()) {
                snapshot.current_bt_action = it->second;
            }
            snapshot.is_pregnant = self.is_pregnant;
            snapshot.hunger_state = bb_get_int(&bb, "hunger_state", 1);
            snapshot.danger_nearby = bb_get_int(&bb, bt::keys::DangerNearby, 0);
            snapshot.perceived_mates = bb_get_int(&bb, "perceived_mates_count", 0);
            snapshot.perceived_food = bb_get_int(&bb, "perceived_food_races_count", 0)
                + bb_get_int(&bb, "perceived_food_things_count", 0);
            snapshot.wander_current_ticks = bb_get_int(&bb, bt::keys::WanderCurrentTicks, 0);
            snapshot.wander_total_ticks = bb_get_int(&bb, bt::keys::WanderTotalTicks, 50);
            snapshot.hp_current = self.hp_current;
            snapshot.hp_max = self.hp_max;
            self.update_ui_snapshot(snapshot);
        }
#endif
        self.set_skip_movement(false);
        SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[BT {}] Finalize: reset skip_movement=false for '{}'", source_tag, self.species_name);
        return Status::Success;
    });
}

// 代码版行为逻辑核心：返回优先级选择器（逃逸>繁殖>觅食>游荡）

// 解析 YAML 并返回用户定义的行为逻辑根节点（不含骨架）
static std::shared_ptr<Node> parse_bt_yaml_logic_root_if_available(Animal& self) {
    try {
        if (self.species_name.empty()) return nullptr;
        auto provider = g_race_factory.get_config_provider();
        const std::string root_dir = provider ? provider->get_config_root_dir() : std::string(".");
        const std::string path = root_dir + "/config/species/animals/bt/" + self.species_name + "_bt.yaml";
        SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[BT YAML] Try load '{}': {}", self.species_name, path);
        YAML::Node doc = YAML::LoadFile(path);
        if (!doc) {
            SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[BT YAML] Empty YAML doc for '{}'", self.species_name);
            return nullptr;
        }
        const YAML::Node def = doc["BehaviorTreeDef"];
        if (!def) {
            SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[BT YAML] Missing 'BehaviorTreeDef' for '{}'", self.species_name);
            return nullptr;
        }
        const YAML::Node root = def["root"];
        if (!root) {
            SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[BT YAML] Missing 'root' node for '{}'", self.species_name);
            return nullptr;
        }
        auto user_root = parse_bt_yaml_node(root, self);
        return user_root;
    } catch (const std::exception& e) {
    SPDLOG_LOGGER_WARN(spdlog::get("ecosim"), "[BT YAML] Failed to parse YAML for '{}': {}.", self.species_name, e.what());
        return nullptr;
    }
}
// --- YAML 构建辅助：从黑板/Animal 成员读取参数 ---
static double read_double_param(Animal& self, Blackboard* bb, const std::string& name, double fallback) {
    if (bb) {
        auto it = bb->doubles.find(name);
        if (it != bb->doubles.end()) return it->second;
    }
    if (name == "hunting_range") return self.hunting_range;
    if (name == "hunting_success_rate") return self.hunting_success_rate;
    if (name == "eating_range") return self.eating_range;
    if (name == "mating_range") return self.get_mating_range();
    if (name == "wander_radius") return self.get_wander_radius();
    if (name == std::string(bt::keys::MatingDesireProbability)) return self.get_mating_desire_probability();
    if (name == "detection_range") return self.get_detection_range();
    return fallback;
}

static int read_int_param(Animal& self, Blackboard* bb, const std::string& name, int fallback) {
    if (bb) {
        auto it = bb->ints.find(name);
        if (it != bb->ints.end()) return it->second;
    }
    return fallback;
}

// --- 条件工厂注册表 ---
static const std::unordered_map<std::string, std::function<std::shared_ptr<Node>(const YAML::Node&, Animal&)>> kConditionFactories = {
    {
        "sex_is_male",
        [](const YAML::Node&, Animal& self){
            return std::make_shared<Condition>([&self](TickContext&){ return self.sex == Sex::MALE; });
        }
    },
    {
        "can_reproduce",
        [](const YAML::Node&, Animal& self){
            return std::make_shared<Condition>([&self](TickContext&){ return self.can_reproduce(); });
        }
    },
    {
        "desire_below_param",
        [](const YAML::Node& params, Animal& self){
            const std::string key = params["probability_param"] ? params["probability_param"].as<std::string>() : std::string(bt::keys::MatingDesireProbability);
            return std::make_shared<Condition>([&self, key](TickContext& ctx){
                auto* world = static_cast<EcosystemState*>(ctx.world);
                if (!world || !self.alive) return false;
                auto& rng_local = world->get_thread_local_rng();
                std::uniform_real_distribution<> dist(0.0, 1.0);
                const double p = read_double_param(self, ctx.blackboard, key, self.get_mating_desire_probability());
                return dist(rng_local) < std::max(0.0, std::min(1.0, p));
            });
        }
    },
    {
        "is_hungry",
        [](const YAML::Node&, Animal& self){
            return std::make_shared<Condition>([&self](TickContext&){ return self.get_hunger_state() != HungerState::SATISFIED; });
        }
    },
    {
        "has_food_types",
        [](const YAML::Node&, Animal& self){
            return std::make_shared<Condition>([&self](TickContext&){ return !self.food_types.empty(); });
        }
    },
    {
        "should_flee",
        [](const YAML::Node& params, Animal&){
            const std::string danger_key = params["danger_param"] ? params["danger_param"].as<std::string>() : std::string(bt::keys::DangerNearby);
            const std::string dist_key = params["distance_param"] ? params["distance_param"].as<std::string>() : std::string(bt::keys::ThreatDistance);
            const std::string th_key = params["threshold_param"] ? params["threshold_param"].as<std::string>() : std::string(bt::keys::ThreatThreshold);
            const std::string cd_key = params["cooldown_param"] ? params["cooldown_param"].as<std::string>() : std::string(bt::keys::FleeModeCooldownTicks);
            const std::string hp_ratio_key = params["hp_ratio_param"] ? params["hp_ratio_param"].as<std::string>() : std::string(bt::keys::HpRatio);
            const double flee_hp_ratio = params["flee_hp_ratio"] ? params["flee_hp_ratio"].as<double>() : -1.0; // <0 表示不启用
            const double low_hp_threshold_boost = params["low_hp_threshold_boost"] ? params["low_hp_threshold_boost"].as<double>() : 0.0;
            return std::make_shared<Condition>([danger_key, dist_key, th_key, cd_key, hp_ratio_key, flee_hp_ratio, low_hp_threshold_boost](TickContext& ctx){
                if (!ctx.blackboard) return false;
                auto& bb = *ctx.blackboard;
                const bool danger = (bb.ints.find(danger_key) != bb.ints.end() && bb.ints[danger_key] != 0);
                const double dist = (bb.doubles.find(dist_key) != bb.doubles.end()) ? bb.doubles[dist_key] : std::numeric_limits<double>::max();
                double threshold = (bb.doubles.find(th_key) != bb.doubles.end()) ? bb.doubles[th_key] : 0.0;
                // 低 HP 时可选地放宽阈值，提升逃逸意愿（默认不启用）
                if (flee_hp_ratio >= 0.0) {
                    const double hp_ratio = (bb.doubles.find(hp_ratio_key) != bb.doubles.end()) ? bb.doubles[hp_ratio_key] : 1.0;
                    if (hp_ratio <= flee_hp_ratio) {
                        threshold *= (1.0 + std::max(0.0, low_hp_threshold_boost));
                    }
                }
                const int cooldown_ticks = (bb.ints.find(cd_key) != bb.ints.end()) ? bb.ints[cd_key] : 0;
                return danger || (threshold > 0.0 && dist <= threshold) || (cooldown_ticks > 0);
            });
        }
    }
};

// --- 动作工厂注册表 ---
static const std::unordered_map<std::string, std::function<std::shared_ptr<Node>(const YAML::Node&, Animal&)>> kActionFactories = {
    {
        "find_available_mate",
        [](const YAML::Node&, Animal& self){
            return std::make_shared<Action>([&self](TickContext& ctx){
                auto* world = static_cast<EcosystemState*>(ctx.world);
                if (!world || !self.alive) return Status::Failure;
                auto mate = self.find_available_mate(*world);
                if (mate.has_value()) {
                    self.set_mating_target(mate.value()->position);
                    // 同步通用当前移动目标，便于后续复用通用追逐动作
                    self.set_current_target(self.get_mating_target());
                    if (ctx.blackboard) {
                        // 写入通用黑板目标，供 plan_path_to_target 读取
                        ctx.blackboard->doubles[bt::keys::TargetPosX] = self.get_mating_target()->x;
                        ctx.blackboard->doubles[bt::keys::TargetPosY] = self.get_mating_target()->y;
                    }
                    return Status::Success;
                }
                return Status::Failure;
            });
        }
    },
    {
        "attempt_to_mate",
        [](const YAML::Node& params, Animal& self){
            YAML::Node p = params;
            return std::make_shared<Action>([&self, p](TickContext& ctx){
                return behavior::actions::AttemptToMate(self, ctx, p);
            });
        }
    },
    {
        "eat_target_thing",
        [](const YAML::Node& params, Animal& self) -> std::shared_ptr<Node> {
            YAML::Node p = params;
            auto act = std::make_shared<Action>([&self, p](TickContext& ctx){
                return behavior::actions::EatTargetThing(self, ctx, p);
            });
            // 若 YAML 指定吃的是 grass：使用“循环进度装饰器”，每 tick 执行子节点；
            // - 远距子节点返回 Failure，装饰器也返回 Failure（不推进度），允许后续分支执行移动
            // - 近距子节点返回 Running/Success，推进进度；达到总时长后返回 Success
            const std::string kind = (p["kind"] ? p["kind"].as<std::string>() : std::string(""));
            if (kind == std::string("grass")) {
                const int default_eat_ticks = defaults::EAT_TICKS; // 若黑板未提供则默认 20
                    auto decorator = std::make_shared<ProgressLoopDecorator>(
                        act,
                        bt::keys::EatGrassTotalTicks,
                        bt::keys::EatGrassCurrentTicks,
                        default_eat_ticks
                    );
                return std::static_pointer_cast<Node>(decorator);
            }
            return std::static_pointer_cast<Node>(act);
        }
    },
    {
        "hunt_target_race",
        [](const YAML::Node& params, Animal& self){
            YAML::Node p = params;
            return std::make_shared<Action>([&self, p](TickContext& ctx){
                return behavior::actions::HuntTargetRace(self, ctx, p);
            });
        }
    },
    {
        "select_target_point",
        [](const YAML::Node& params, Animal& self){
            YAML::Node p = params;
            return std::make_shared<Action>([&self, p](TickContext& ctx){
                return behavior::actions::SelectTargetPoint(self, ctx, p);
            });
        }
    },
    {
        "select_flee_destination",
        [](const YAML::Node& params, Animal& self){
            YAML::Node p = params;
            return std::make_shared<Action>([&self, p](TickContext& ctx){
                return behavior::actions::SelectFleeDestination(self, ctx, p);
            });
        }
    },
    {
        "plan_path_to_target",
        [](const YAML::Node& params, Animal& self){
            YAML::Node p = params;
            return std::make_shared<Action>([&self, p](TickContext& ctx){
                return behavior::actions::PlanPathToTarget(self, ctx, p);
            });
        }
    },
    // 移除旧版 flee_from_threat，统一采用 "select_flee_destination" + "plan_path_to_target"
    {
        "wander_anywhere",
        [](const YAML::Node& params, Animal& self) -> std::shared_ptr<Node> {
            YAML::Node p = params;
            auto act = std::make_shared<Action>([&self, p](TickContext& ctx){
                return behavior::actions::WanderAnywhere(self, ctx, p);
            });
            if (p["duration_param"]) {
                auto decorator = std::make_shared<ProgressLoopDecorator>(act, bt::keys::WanderTotalTicks, bt::keys::WanderCurrentTicks, defaults::WANDER_TICKS);
                return std::static_pointer_cast<Node>(decorator);
            }
            return std::static_pointer_cast<Node>(act);
        }
    }
};

// --- 条件与动作工厂：根据 YAML 名字生成节点 ---
static std::shared_ptr<Node> make_condition_node(const std::string& name, const YAML::Node& params, Animal& self) {
    auto it = kConditionFactories.find(name);
    if (it != kConditionFactories.end()) {
        return it->second(params, self);
    }
    return std::make_shared<Condition>([](TickContext&){ return false; });
}

class StatusReportingDecorator final : public Decorator {
public:
    StatusReportingDecorator(std::shared_ptr<Node> child_node, Animal& owner, std::string status_label)
        : Decorator(std::move(child_node)), self(owner), status_name(std::move(status_label)) {}

    Status tick(TickContext& ctx) override {
        if (!child) {
            return Status::Failure;
        }
        const auto result = child->tick(ctx);
        if ((result == Status::Running || result == Status::Success) && !status_name.empty()) {
#ifdef ECOSIM_ENABLE_UI_DEBUG
            if (ctx.blackboard) {
                ctx.blackboard->strings["_ui_current_action"] = status_name;
            }
#endif
        }
        return result;
    }

    void reset() override {
        if (child) {
            child->reset();
        }
    }

private:
    Animal& self;
    std::string status_name;
};

static std::shared_ptr<Node> make_action_node(const std::string& name, const YAML::Node& params, Animal& self) {
    auto it = kActionFactories.find(name);
    if (it != kActionFactories.end()) {
        auto original_node = it->second(params, self);

        std::string status_name;
        if (params["status_name"] && params["status_name"].IsScalar()) {
            status_name = params["status_name"].as<std::string>();
        }

        if (status_name.empty()) {
            return original_node;
        }

        return std::make_shared<StatusReportingDecorator>(std::move(original_node), self, std::move(status_name));
    }
    return std::make_shared<Action>([](TickContext&){ return Status::Failure; });
}

// 递归解析 YAML 节点并实例化为可执行节点（不绑定到具体上下文之外）
static std::shared_ptr<Node> parse_bt_yaml_node(const YAML::Node& n, Animal& self) {
    if (!n || !n["type"]) return nullptr;
    const std::string type = n["type"].as<std::string>();
    // 复合节点统一工厂：减少重复分支并遵循开闭原则
    static const std::unordered_map<std::string, std::function<std::shared_ptr<Composite>()>> kCompositeFactories = {
        {"PrioritySelector", [](){ return std::make_shared<PrioritySelector>(); }},
        {"Selector",         [](){ return std::make_shared<Selector>(); }},
        {"Sequence",         [](){ return std::make_shared<Sequence>(); }}
    };
    if (auto it = kCompositeFactories.find(type); it != kCompositeFactories.end()) {
        auto composite_node = it->second();
        const YAML::Node children = n["children"];
        if (children && children.IsSequence()) {
            for (const auto& ch : children) {
                auto node = parse_bt_yaml_node(ch, self);
                if (node) composite_node->add_child(node);
            }
        }
        return composite_node;
    }
    if (type == "TickIntervalDecorator") {
        const int interval = n["interval"] ? n["interval"].as<int>() : 1;
        std::string prefix;
        if (n["key_prefix"]) {
            prefix = n["key_prefix"].as<std::string>();
        } else if (n["name"]) {
            prefix = n["name"].as<std::string>();
        } else {
            prefix = "interval";
        }
        const YAML::Node child_node = n["child"];
        if (!child_node) {
            SPDLOG_LOGGER_ERROR(spdlog::get("ecosim"), "[BT YAML] TickIntervalDecorator '{}' missing 'child' node", prefix);
            return nullptr;
        }
        auto child = parse_bt_yaml_node(child_node, self);
        if (!child) {
            SPDLOG_LOGGER_ERROR(spdlog::get("ecosim"), "[BT YAML] TickIntervalDecorator '{}' child failed to parse", prefix);
            return nullptr;
        }
        return std::make_shared<TickIntervalDecorator>(child, interval, prefix);
    }
    if (type == "Condition") {
        const std::string cond_name = n["cond"] ? n["cond"].as<std::string>() : std::string();
        // 兼容两种参数写法：
        // 1) 传统：将所有键放在 params 映射下
        // 2) 扁平：将参数键与 type/cond 同级
        YAML::Node p = (n["params"] && n["params"].IsMap()) ? n["params"] : YAML::Node(YAML::NodeType::Map);
        // 合并同级的参数键（排除保留字段）
        if (n.IsMap()) {
            for (auto it : n) {
                const std::string k = it.first.as<std::string>();
                if (k == "type" || k == "children" || k == "name" || k == "cond" || k == "action" || k == "params") {
                    continue;
                }
                p[k] = it.second;
            }
        }
        return make_condition_node(cond_name, p, self);
    }
    if (type == "Action") {
        const std::string action_name = n["action"] ? n["action"].as<std::string>() : std::string();
        // 同 Condition：支持 params 映射和同级扁平参数
        YAML::Node p = (n["params"] && n["params"].IsMap()) ? n["params"] : YAML::Node(YAML::NodeType::Map);
        if (n.IsMap()) {
            for (auto it : n) {
                const std::string k = it.first.as<std::string>();
                if (k == "type" || k == "children" || k == "name" || k == "cond" || k == "action" || k == "params") {
                    continue;
                }
                p[k] = it.second;
            }
        }
        return make_action_node(action_name, p, self);
    }
    return nullptr;
}

// 主节点：吃草动作（内联 Action），在近场范围内提交吃草交互

std::unique_ptr<BehaviorTree> build_tree_for_animal(Animal& self) {
    auto root_seq = std::make_shared<Sequence>();
    std::shared_ptr<Node> logic_root = nullptr;

    logic_root = parse_bt_yaml_logic_root_if_available(self);

    if (!logic_root) {
        SPDLOG_LOGGER_CRITICAL(spdlog::get("ecosim"),
            "[BT] Failed to load behavior tree logic for '{}'.",
            self.species_name);
        SPDLOG_LOGGER_CRITICAL(spdlog::get("ecosim"),
            "       Check if 'config/species/animals/bt/{}_bt.yaml' exists and is valid.",
            self.species_name);
        throw std::runtime_error(std::string("Behavior tree configuration missing or invalid for ") + self.species_name);
    }

    SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[BT] Using YAML logic for '{}'", self.species_name);

    auto act_update = create_update_node(self, "YAML");
    auto act_finalize = create_finalize_node(self, "YAML");

    auto selector_succeeder = std::make_shared<Succeeder>(logic_root);
    auto profiled_logic = std::make_shared<Action>([selector_succeeder](TickContext& ctx) {
        ZoneScopedN("Animal::BT::Logic (YAML)");
        return selector_succeeder->tick(ctx);
    });

    root_seq->add_child(act_update);
    root_seq->add_child(profiled_logic);
    root_seq->add_child(act_finalize);

    auto tree = std::make_unique<BehaviorTree>(root_seq);
    tree->blackboard().strings["bt_source"] = std::string("yaml:") + self.species_name;
    SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[BT] Loaded tree for '{}' from YAML", self.species_name);
    return tree;
}

} // namespace behavior