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
#include "tile.h"
#include <random>
#include <cmath>
// YAML 解析与路径访问
#include <yaml-cpp/yaml.h>
#include <spdlog/spdlog.h>
#include "race_factory.h"
#include "logger_once.hpp"
#include "species_config_provider.h"
// 资源兜底需要 Qt 文件读取
#include <QFile>
#include <QIODevice>
#include <QString>
// 新的可复用行为动作封装
#include "bt_actions.h"
#include "bt_keys.h"
// 注册表所需容器
#include <unordered_map>
#include <functional>
#include <utility>
#include <stdexcept>
#include <cstdint>
#include <algorithm>
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

static bool IsInSleepWindow(Animal& self, TickContext& ctx, const YAML::Node&) {
    auto* world = static_cast<EcosystemState*>(ctx.world);
    if (!world || !self.alive) {
        return false;
    }

    const int tile_x = static_cast<int>(std::floor(self.position.x));
    const int tile_y = static_cast<int>(std::floor(self.position.y));
    auto& grid = world->world_grid();
    if (!grid.is_valid_coord(tile_x, tile_y)) {
        return false;
    }

    const Tile& tile = grid.get_tile(tile_x, tile_y);
    const int hour = tile.local_hour;
    return (hour >= 6 && hour <= 8) || (hour >= 20 && hour <= 22);
}

static Status SleepAction(Animal& self, TickContext& ctx, const YAML::Node& params) {
    if (!self.alive) {
        return Status::Failure;
    }

    self.is_sleeping = true;
    self.set_skip_movement(true);

    Blackboard* bb = ctx.blackboard;
    const std::string energy_key = params["energy_multiplier_key"] ? params["energy_multiplier_key"].as<std::string>() : std::string("sleeping_energy_multiplier");
    const double sleep_mul = std::max(0.0, bb_get_double(bb, energy_key, self.get_sleeping_energy_multiplier()));
    if (bb) {
        bb->doubles["active_sleep_energy_multiplier"] = sleep_mul;
        bb->ints["sleep_branch_active"] = 1;
        bb->ints.erase("sleep_check_interval_status");
    }

    if (auto* world = static_cast<EcosystemState*>(ctx.world)) {
        const int tx = static_cast<int>(std::floor(self.position.x));
        const int ty = static_cast<int>(std::floor(self.position.y));
        auto& grid = world->world_grid();
        if (grid.is_valid_coord(tx, ty) && bb) {
            bb->ints["sleep_local_hour"] = grid.get_tile(tx, ty).local_hour;
        }
    }

    return Status::Success;
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
            self.is_sleeping = false;
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
                // 更新连续逃跑计数 / 疲惫状态：当黑板记录存在威胁时视为正在被追击
                const bool is_fleeing = (bb.ints.find(bt::keys::DangerNearby) != bb.ints.end())
                    ? (bb.ints[bt::keys::DangerNearby] > 0)
                    : false;
                const int tpd = world ? world->config.ticks_per_day : 3000;
                const double tired_after_secs = bb_get_double(&bb, "tired_after_seconds", 5.0);
                const double tired_speed_mul = bb_get_double(&bb, "tired_speed_multiplier", 0.8);
                self.update_flee_ticks(is_fleeing, tpd, tired_after_secs, tired_speed_mul);
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

                // 繁殖守卫相关键：最低能量、最低年龄、冷却剩余（使用直观阈值，不再乘2）
                bb.doubles["repro_energy_min"] = self.min_reproduction_energy;
                bb.ints["repro_age_min"] = self.min_reproduction_age;
                bb.ints["repro_cooldown_ticks"] = self.reproduction_cooldown;

                // 攻击后摇剩余tick（调试可视化用，可选）
                bb.ints["attack_recovery_ticks_remaining"] = std::max(0, self.hunting_cooldown);
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
                // 攻击后摇减速：冷却期间乘以配置的速度倍率
                if (self.hunting_cooldown > 0) {
                    double recovery_mul = 1.0;
                    if (auto it3 = bb.doubles.find("attack_recovery_speed_multiplier"); it3 != bb.doubles.end()) {
                        recovery_mul = it3->second;
                    }
                    base_speed_multiplier *= std::max(0.0, recovery_mul);
                }
                // 如果动物处于疲惫状态，则将速度封顶为疲惫速度倍率（取较小者，保证疲惫时速度下降）
                if (self.is_tired()) {
                    base_speed_multiplier = std::min(base_speed_multiplier, self.get_tired_speed_multiplier());
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

                // 饥饿伤害：在能量耗尽 (energy<=0) 后按间隔扣减 HP
                {
                    ZoneScopedN("BT::Update::StarvationDamage");
                    const bool out_of_energy = (self.energy <= 0.0);
                    const double ratio = bb_get_double(&bb, "starvation_damage_interval_ratio", 0.25);
                    const double damage = bb_get_double(&bb, "starvation_damage", 0.0);
                    const int tpd = world ? world->config.ticks_per_day : 3000;
                    const double clamped_ratio = std::max(0.0, std::min(1.0, ratio));
                    int interval = std::max(1, static_cast<int>(std::floor(clamped_ratio * static_cast<double>(tpd))));
                    bb.ints["starvation_damage_interval_ticks"] = interval;

                    int sd_ticks = (bb.ints.find("ticks_since_last_starvation_damage") != bb.ints.end())
                        ? bb.ints["ticks_since_last_starvation_damage"]
                        : 0;
                    if (out_of_energy) {
                        sd_ticks = std::max(0, sd_ticks + 1);
                        if (damage > 0.0 && sd_ticks >= interval) {
                            self.take_damage(damage, "Starvation");
                            sd_ticks = 0;
                            SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"),
                                "[BT {}] Starvation dmg (energy depleted): '{}' -{:.1f} every {} ticks (tpd={})",
                                source_tag, self.species_name, damage, interval, tpd);
                        }
                    } else {
                        sd_ticks = 0;
                    }
                    bb.ints["ticks_since_last_starvation_damage"] = sd_ticks;
                }
            }

            // 根据是否检测到威胁动态调整 HP 恢复倍率（例如被虎威胁时降低恢复速度）
            // 默认为 1.0；若黑板表明存在危险（DangerNearby==1），则读取键 "threat_hp_regen_multiplier"（默认 0.3）
            double threat_hp_mul = 1.0;
            if (bb.ints.find(bt::keys::DangerNearby) != bb.ints.end() && bb.ints[bt::keys::DangerNearby] > 0) {
                threat_hp_mul = bb_get_double(&bb, "threat_hp_regen_multiplier", 0.3);
            }
            // 将倍率写入 Animal，使 apply_hp_regen 生效
            self.set_hp_regen_multiplier(threat_hp_mul);

            bool sleeping_now = false;
            if (auto it_status = bb.ints.find("sleep_check_interval_status"); it_status != bb.ints.end()) {
                const Status cached_status = static_cast<Status>(it_status->second);
                sleeping_now = (cached_status == Status::Success);
            }

            if (sleeping_now && !IsInSleepWindow(self, ctx, YAML::Node())) {
                sleeping_now = false;
                bb.ints["sleep_check_interval_status"] = static_cast<int>(Status::Failure);
                bb.ints["sleep_check_interval_counter"] = 0;
            }

            if (sleeping_now) {
                self.is_sleeping = true;
                self.set_skip_movement(true);
                const double sleep_mul = std::max(0.0, bb_get_double(&bb, "sleeping_energy_multiplier", self.get_sleeping_energy_multiplier()));
                bb.doubles["active_sleep_energy_multiplier"] = sleep_mul;
                bb.ints["sleep_branch_active"] = 1;
                self.consume_energy(sleep_mul);
            } else {
                self.is_sleeping = false;
                bb.doubles.erase("active_sleep_energy_multiplier");
                bb.ints.erase("sleep_branch_active");
            }
        }

        // 基础代谢：无论是否移动，每 tick 都扣除少量能量
        const double basal_multiplier = bb_get_double(ctx.blackboard, "basal_energy_multiplier", 0.1);
        if (!self.is_sleeping && basal_multiplier > 0.0) {
            self.consume_energy(basal_multiplier);
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
            self.clear_mating_target();
            if (ctx.blackboard) {
                auto& bb = *ctx.blackboard;
                bb.ints.erase("mate_target_id");
                bb.strings.erase("mate_target_id");
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
            // 记录当前移速（每 tick 步长）
            snapshot.current_speed = self.get_current_step_distance();
            // 寻路调试：将当前目标与规划路径写入快照
            snapshot.current_target = self.get_current_target();
            snapshot.planned_path = self.get_planned_path_snapshot();
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
    if (self.species_name.empty()) return nullptr;
    auto provider = g_race_factory.get_config_provider();
    const std::string root_dir = provider ? provider->get_config_root_dir() : std::string(".");
    const std::string fs_path = root_dir + "/config/species/animals/bt/" + self.species_name + "_bt.yaml";
    SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[BT YAML] Try FS for '{}': {}", self.species_name, fs_path);
    YAML::Node doc;
    try {
        doc = YAML::LoadFile(fs_path);
        SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[BT YAML] Loaded from FS: '{}'", fs_path);
    } catch (const std::exception& e) {
        // 回退到资源别名
        const QString alias = QString::fromLatin1(":/config/species/animals/bt/") + QString::fromStdString(self.species_name) + QStringLiteral("_bt.yaml");
        SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[BT YAML] FS missing for '{}', try resource '{}' (reason: {})", self.species_name, alias.toStdString(), e.what());
        QFile f(alias);
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray content = f.readAll();
            f.close();
            try {
                doc = YAML::Load(std::string(content.constData(), static_cast<size_t>(content.size())));
                SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[BT YAML] Loaded from resource: '{}'", alias.toStdString());
            } catch (const std::exception& e2) {
                SPDLOG_LOGGER_WARN(spdlog::get("ecosim"), "[BT YAML] Failed to parse resource YAML for '{}': {}", self.species_name, e2.what());
                return nullptr;
            }
        } else {
            SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[BT YAML] Resource not available for '{}': '{}'", self.species_name, alias.toStdString());
            return nullptr;
        }
    }

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
    if (name == "threat_detection_range") return self.get_threat_detection_range();
    if (name == "mate_detection_range") return self.get_mate_detection_range();
    if (name == "food_detection_range") return self.get_food_detection_range();
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
        "is_in_sleep_window",
        [](const YAML::Node& params, Animal& self){
            YAML::Node p = params;
            return std::make_shared<Condition>([&self, p](TickContext& ctx){
                return IsInSleepWindow(self, ctx, p);
            });
        }
    },
    {
        "has_food_types",
        [](const YAML::Node&, Animal& self){
            return std::make_shared<Condition>([&self](TickContext&){ return !self.food_types.empty(); });
        }
    },
    {
        "has_current_target",
        [](const YAML::Node&, Animal& self){
            return std::make_shared<Condition>([&self](TickContext&){
                return self.get_current_target().has_value();
            });
        }
    },
    {
        "is_at_target",
        [](const YAML::Node& params, Animal& self){
            const std::string stop_param = params["stop_range_param"] ? params["stop_range_param"].as<std::string>() : std::string();
            return std::make_shared<Condition>([&self, stop_param](TickContext& ctx){
                if (!ctx.blackboard) {
                    return false;
                }

                auto& bb = *ctx.blackboard;
                const auto it_x = bb.doubles.find(bt::keys::TargetPosX);
                const auto it_y = bb.doubles.find(bt::keys::TargetPosY);
                if (it_x == bb.doubles.end() || it_y == bb.doubles.end()) {
                    return false;
                }

                const Position target{it_x->second, it_y->second};
                double stop_range = 0.0;
                if (!stop_param.empty()) {
                    stop_range = read_double_param(self, ctx.blackboard, stop_param, 0.0);
                }
                if (stop_range <= 0.0) {
                    stop_range = std::max(self.get_step_distance_per_tick(), self.get_current_step_distance());
                }
                if (stop_range <= 0.0) {
                    stop_range = 1.0;
                }

                return self.position.distance_to(target) <= stop_range;
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
                if (mate.has_value() && mate.value()) {
                    auto mate_ptr = mate.value();
                    self.mating_partner = mate_ptr;
                    const Position target_pos = mate_ptr->position;
                    self.set_mating_target(target_pos);
                    self.set_current_target(target_pos);
                    if (ctx.blackboard) {
                        auto& bb = *ctx.blackboard;
                        bb.doubles[bt::keys::TargetPosX] = target_pos.x;
                        bb.doubles[bt::keys::TargetPosY] = target_pos.y;
                        const auto ptr_value = reinterpret_cast<std::uintptr_t>(mate_ptr.get());
                        bb.strings["mate_target_id"] = std::to_string(static_cast<unsigned long long>(ptr_value));
                    }
                    return Status::Success;
                }
                self.mating_partner.reset();
                self.clear_mating_target();
                self.clear_current_target();
                self.clear_path();
                if (ctx.blackboard) {
                    auto& bb = *ctx.blackboard;
                    bb.doubles.erase(bt::keys::TargetPosX);
                    bb.doubles.erase(bt::keys::TargetPosY);
                    bb.strings.erase("mate_target_id");
                }
                return Status::Failure;
            });
        }
    },
    {
        "update_mate_target_position",
        [](const YAML::Node& params, Animal& self){
            YAML::Node p = params;
            return std::make_shared<Action>([&self, p](TickContext& ctx){
                return behavior::actions::UpdateMateTargetPosition(self, ctx, p);
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
        "update_hunt_target_position",
        [](const YAML::Node& params, Animal& self){
            YAML::Node p = params;
            return std::make_shared<Action>([&self, p](TickContext& ctx){
                return behavior::actions::UpdateHuntTargetPosition(self, ctx, p);
            });
        }
    },
    {
        "seek_thing_with_path",
        [](const YAML::Node& params, Animal& self){
            YAML::Node p = params;
            return std::make_shared<Action>([&self, p](TickContext& ctx){
                return behavior::actions::SeekThingWithPath(self, ctx, p);
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
        "check_for_threats",
        [](const YAML::Node& params, Animal& self){
            YAML::Node p = params;
            return std::make_shared<Action>([&self, p](TickContext& ctx){
                ZoneScopedN("BT::Action::CheckForThreats");
                auto* world = static_cast<EcosystemState*>(ctx.world);
                if (!world || !self.alive || !ctx.blackboard) {
                    return Status::Failure;
                }

                auto& bb = *ctx.blackboard;

                const double default_threat_threshold = std::max(0.0, self.get_threat_detection_range());
                if (bb.doubles.find(bt::keys::ThreatThreshold) == bb.doubles.end()) {
                    bb.doubles[bt::keys::ThreatThreshold] = default_threat_threshold;
                }
                const double base_threshold = bb_get_double(&bb, bt::keys::ThreatThreshold, default_threat_threshold);

                const std::string hp_ratio_key = p["hp_ratio_param"] ? p["hp_ratio_param"].as<std::string>() : std::string(bt::keys::HpRatio);
                const double flee_hp_ratio = p["flee_hp_ratio"] ? p["flee_hp_ratio"].as<double>() : -1.0;
                const double low_hp_threshold_boost = p["low_hp_threshold_boost"] ? p["low_hp_threshold_boost"].as<double>() : 0.0;

                double effective_threshold = base_threshold;
                if (flee_hp_ratio >= 0.0) {
                    const double hp_ratio = (bb.doubles.find(hp_ratio_key) != bb.doubles.end()) ? bb.doubles[hp_ratio_key] : 1.0;
                    if (hp_ratio <= flee_hp_ratio) {
                        effective_threshold *= (1.0 + std::max(0.0, low_hp_threshold_boost));
                    }
                }

                bool danger = false;
                double threat_dist = std::numeric_limits<double>::max();
                Position threat_pos = self.position;
                {
                    ZoneScopedN("BT::Action::CheckForThreats::Query");
                    const auto nearby = world->get_nearby_races_broad(self.position, effective_threshold);
                    for (const auto& r : nearby) {
                        if (!r || !r->alive) continue;
                        if (r.get() == &self) continue;
                        if (r->species_name == std::string("tiger") && self.species_name != std::string("tiger")) {
                            const double d = self.position.distance_to(r->position);
                            if (d < threat_dist) {
                                threat_dist = d;
                                threat_pos = r->position;
                            }
                            if (d <= effective_threshold) {
                                danger = true;
                            }
                        }
                    }
                }

                bb.ints[bt::keys::DangerNearby] = danger ? 1 : 0;
                bb.doubles[bt::keys::ThreatDistance] = std::isfinite(threat_dist) ? threat_dist : (effective_threshold + 1.0);
                bb.doubles[bt::keys::ThreatPosX] = threat_pos.x;
                bb.doubles[bt::keys::ThreatPosY] = threat_pos.y;

                const bool edge_trigger = danger || (std::isfinite(threat_dist) && (threat_dist <= effective_threshold));
                return edge_trigger ? Status::Success : Status::Failure;
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
    {
        "select_wander_target",
        [](const YAML::Node& params, Animal& self){
            YAML::Node p = params;
            return std::make_shared<Action>([&self, p](TickContext& ctx){
                return behavior::actions::SelectWanderTarget(self, ctx, p);
            });
        }
    },
    {
        "clear_blackboard_target",
        [](const YAML::Node& params, Animal& self){
            YAML::Node p = params;
            return std::make_shared<Action>([&self, p](TickContext& ctx){
                return behavior::actions::ClearBlackboardTarget(self, ctx, p);
            });
        }
    },
    {
        "sleep",
        [](const YAML::Node& params, Animal& self){
            YAML::Node p = params;
            return std::make_shared<Action>([&self, p](TickContext& ctx){
                return SleepAction(self, ctx, p);
            });
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
        std::string interval_param_key;
        if (n["interval_param"]) {
            interval_param_key = n["interval_param"].as<std::string>();
        }
        return std::make_shared<TickIntervalDecorator>(child, interval, prefix, interval_param_key);
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
    SPDLOG_INFO_ONCE(spdlog::get("ecosim"), "[BT] Loaded tree for '{}' from YAML", self.species_name);
    return tree;
}

} // namespace behavior