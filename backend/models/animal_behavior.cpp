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
// 注册表所需容器
#include <unordered_map>
#include <functional>

namespace behavior {

using namespace bt;

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
    if (name == "mating_desire_probability") return self.get_mating_desire_probability();
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
            const std::string key = params["probability_param"] ? params["probability_param"].as<std::string>() : std::string("mating_desire_probability");
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
            const std::string danger_key = params["danger_param"] ? params["danger_param"].as<std::string>() : std::string("danger_nearby");
            const std::string dist_key = params["distance_param"] ? params["distance_param"].as<std::string>() : std::string("threat_distance");
            const std::string th_key = params["threshold_param"] ? params["threshold_param"].as<std::string>() : std::string("threat_threshold");
            return std::make_shared<Condition>([danger_key, dist_key, th_key](TickContext& ctx){
                if (!ctx.blackboard) return false;
                auto& bb = *ctx.blackboard;
                const bool danger = (bb.ints.find(danger_key) != bb.ints.end() && bb.ints[danger_key] != 0);
                const double dist = (bb.doubles.find(dist_key) != bb.doubles.end()) ? bb.doubles[dist_key] : std::numeric_limits<double>::max();
                const double threshold = (bb.doubles.find(th_key) != bb.doubles.end()) ? bb.doubles[th_key] : 0.0;
                return danger || (threshold > 0.0 && dist <= threshold);
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
                    if (ctx.blackboard) {
                        ctx.blackboard->doubles["mate_target_pos_x"] = self.get_mating_target()->x;
                        ctx.blackboard->doubles["mate_target_pos_y"] = self.get_mating_target()->y;
                    }
                    return Status::Success;
                }
                return Status::Failure;
            });
        }
    },
    {
        "approach_or_mate",
        [](const YAML::Node& params, Animal& self){
            YAML::Node p = params; // 捕获参数以支持自定义键
            return std::make_shared<Action>([&self, p](TickContext& ctx){
                return behavior::actions::ApproachOrMate(self, ctx, p);
            });
        }
    },
    {
        "eat_nearby_thing",
        [](const YAML::Node& params, Animal& self) -> std::shared_ptr<Node> {
            YAML::Node p = params;
            auto act = std::make_shared<Action>([&self, p](TickContext& ctx){
                return behavior::actions::EatNearbyThing(self, ctx, p);
            });
            // 若 YAML 指定吃的是 grass：使用“循环进度装饰器”，每 tick 执行子节点；
            // - 远距子节点返回 Failure，装饰器也返回 Failure（不推进度），允许后续分支执行移动
            // - 近距子节点返回 Running/Success，推进进度；达到总时长后返回 Success
            const std::string kind = (p["kind"] ? p["kind"].as<std::string>() : std::string(""));
            if (kind == std::string("grass")) {
                const int default_eat_ticks = 20; // 若黑板未提供则默认 20
                auto decorator = std::make_shared<ProgressLoopDecorator>(
                    act,
                    "eat_grass_total_ticks",
                    "eat_grass_current_ticks",
                    default_eat_ticks
                );
                return std::static_pointer_cast<Node>(decorator);
            }
            return std::static_pointer_cast<Node>(act);
        }
    },
    {
        "hunt_nearby_race",
        [](const YAML::Node& params, Animal& self){
            YAML::Node p = params;
            return std::make_shared<Action>([&self, p](TickContext& ctx){
                return behavior::actions::HuntNearbyRace(self, ctx, p);
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
        "plan_path_to_target",
        [](const YAML::Node& params, Animal& self){
            YAML::Node p = params;
            return std::make_shared<Action>([&self, p](TickContext& ctx){
                return behavior::actions::PlanPathToTarget(self, ctx, p);
            });
        }
    },
    {
        "flee_from_threat",
        [](const YAML::Node& params, Animal& self){
            YAML::Node p = params;
            return std::make_shared<Action>([&self, p](TickContext& ctx){
                return behavior::actions::FleeFromThreat(self, ctx, p);
            });
        }
    },
    {
        "wander_anywhere",
        [](const YAML::Node& params, Animal& self) -> std::shared_ptr<Node> {
            YAML::Node p = params;
            auto act = std::make_shared<Action>([&self, p](TickContext& ctx){
                return behavior::actions::WanderAnywhere(self, ctx, p);
            });
            if (p["duration_param"]) {
                auto decorator = std::make_shared<ProgressLoopDecorator>(act, "wander_total_ticks", "wander_current_ticks", 50);
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

static std::shared_ptr<Node> make_action_node(const std::string& name, const YAML::Node& params, Animal& self) {
    auto it = kActionFactories.find(name);
    if (it != kActionFactories.end()) {
        return it->second(params, self);
    }
    return std::make_shared<Action>([](TickContext&){ return Status::Failure; });
}

// 递归解析 YAML 节点并实例化为可执行节点（不绑定到具体上下文之外）
static std::shared_ptr<Node> parse_bt_yaml_node(const YAML::Node& n, Animal& self) {
    if (!n || !n["type"]) return nullptr;
    const std::string type = n["type"].as<std::string>();
    if (type == "PrioritySelector") {
        auto s = std::make_shared<PrioritySelector>();
        const YAML::Node children = n["children"];
        if (children && children.IsSequence()) {
            for (const auto& ch : children) {
                auto node = parse_bt_yaml_node(ch, self);
                if (node) s->add_child(node);
            }
        }
        return s;
    }
    if (type == "Selector") {
        auto s = std::make_shared<Selector>();
        const YAML::Node children = n["children"];
        if (children && children.IsSequence()) {
            for (const auto& ch : children) {
                auto node = parse_bt_yaml_node(ch, self);
                if (node) s->add_child(node);
            }
        }
        return s;
    }
    if (type == "Sequence") {
        auto s = std::make_shared<Sequence>();
        const YAML::Node children = n["children"];
        if (children && children.IsSequence()) {
            for (const auto& ch : children) {
                auto node = parse_bt_yaml_node(ch, self);
                if (node) s->add_child(node);
            }
        }
        return s;
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

// 若存在 bt/<species>_bt.yaml 则构建 YAML 行为树，否则返回空指针以回退到代码版
static std::unique_ptr<BehaviorTree> build_tree_from_yaml_if_available(Animal& self) {
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

        // 包裹统一的 Update -> Succeeder(用户定义根) -> Finalize 序列
        auto root_seq = std::make_shared<Sequence>();
        auto user_root = parse_bt_yaml_node(root, self);
        if (!user_root) return nullptr;

        // 通用 Update（复用代码版语义）
        auto act_update = std::make_shared<Action>([&self](TickContext& ctx){
            auto* world = static_cast<EcosystemState*>(ctx.world);
            if (!world || !self.alive) return Status::Failure;

            self.set_skip_movement(false);
            SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[BT YAML] Update: reset skip_movement=false for '{}'", self.species_name);
            self.clear_sensor_caches();
            self.refresh_hunger_state();
            if (ctx.blackboard) {
                auto& bb = *ctx.blackboard;
                // 读取意图锁时长（若黑板提供）
                if (auto it = bb.ints.find("mating_intent_lock_duration"); it != bb.ints.end()) self.set_mating_intent_lock_duration(it->second);
                if (auto it2 = bb.ints.find("forage_intent_lock_duration"); it2 != bb.ints.end()) self.set_forage_intent_lock_duration(it2->second);
            }
            Blackboard dummy;
            auto& bb = ctx.blackboard ? *ctx.blackboard : dummy;

            // 意图锁推进与饥饿打断处理（与代码版保持一致）
            if (self.get_mating_intent_lock_ticks() > 0) {
                self.set_mating_intent_lock_ticks(self.get_mating_intent_lock_ticks() - 1);
            } else {
                if (self.get_mating_target().has_value() && self.get_hunger_state() == HungerState::STARVING) {
                    self.clear_mating_target();
                }
            }
            if (self.get_forage_intent_lock_ticks() > 0) {
                self.set_forage_intent_lock_ticks(self.get_forage_intent_lock_ticks() - 1);
            }

            // 进行中的交配计时器：仅推进计时，不再全程禁止移动
            // 交配受理当 tick 的静止由 ApproachOrMate 动作负责；此处不再强制停滞
            if (self.mating_timer > 0) {
                self.mating_timer -= 1;
                if (ctx.blackboard) {
                    ctx.blackboard->ints["mating_timer_ticks"] = std::max(0, self.mating_timer);
                }
                // 诊断：雌性交配期间的移动锁与能量变化观察
                if (self.sex == Sex::FEMALE) {
                    SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"),
                        "[BT YAML] Female mating_timer={} skip_movement={} energy={:.1f} pos=({:.1f},{:.1f})",
                        self.mating_timer, self.get_skip_movement(), self.energy, self.position.x, self.position.y);
                }
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
                    self.set_skip_movement(true);
                }
            }

            // 集中怀孕速度惩罚：本 tick 的基础速度倍率（供移动 Action 使用）
            {
                double base_speed_multiplier = 1.0;
                if (self.is_pregnant) {
                    base_speed_multiplier *= std::max(0.0, self.get_pregnancy_speed_penalty());
                }
                bb.doubles["current_speed_multiplier"] = base_speed_multiplier;
                if (self.is_pregnant) {
                    SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"),
                        "[BT YAML] Pregnant speed: penalty={:.2f} base_mul={:.2f} movement_speed={:.2f}",
                        self.get_pregnancy_speed_penalty(), base_speed_multiplier, self.movement_speed);
                }
            }

            // 感知：附近同种雌性
            {
                const auto nearby_races = world->get_nearby_races_broad(self.position, self.get_detection_range());
                for (const auto& r : nearby_races) {
                    if (!r || !r->alive) continue;
                    if (r.get() == &self) continue;
                    if (r->species_name != self.species_name) continue;
                    auto female = std::dynamic_pointer_cast<Animal>(r);
                    if (!female) continue;
                    if (female->sex != Sex::FEMALE || !female->can_reproduce()) continue;
                    if (self.position.distance_to(female->position) > self.get_detection_range()) continue;
                    self.cache_mate(female);
                }
                bb.ints["perceived_mates_count"] = static_cast<int>(self.get_cached_mates_snapshot().size());
            }
            // 感知：附近食物（race/things）
            {
                const auto nearby_races = world->get_nearby_races_broad(self.position, self.get_detection_range());
                for (const auto& r : nearby_races) {
                    if (!r || !r->alive) continue;
                    if (std::find(self.food_types.begin(), self.food_types.end(), r->species_name) == self.food_types.end()) continue;
                    if (self.position.distance_to(r->position) > self.get_detection_range()) continue;
                    self.cache_food_race(r);
                }
                for (const auto& k : self.food_types) {
                    auto nearby_things = world->get_things_in_range(k, self.position, self.get_detection_range());
                    for (const auto& t : nearby_things) {
                        if (!t || !t->alive) continue;
                        self.cache_food_thing(t);
                    }
                }
                bb.ints["perceived_food_count"] = static_cast<int>(self.get_cached_food_races_snapshot().size() + self.get_cached_food_things_snapshot().size());
            }
            // 维护滞后/冷却计数器：最近进食计时与强制游荡倒计时
            if (ctx.blackboard) {
                auto& bb2 = *ctx.blackboard;
                int ticks = 0;
                if (bb2.ints.find("ticks_since_last_meal") != bb2.ints.end()) {
                    ticks = bb2.ints["ticks_since_last_meal"];
                }
                bb2.ints["ticks_since_last_meal"] = std::max(0, ticks + 1);
                if (bb2.ints.find("force_wander_ticks") != bb2.ints.end()) {
                    bb2.ints["force_wander_ticks"] = std::max(0, bb2.ints["force_wander_ticks"] - 1);
                }
            }
            return Status::Success;
        });

        auto selector_succeeder = std::make_shared<Succeeder>(user_root);

        // Finalize（复用代码版语义）
        auto act_finalize = std::make_shared<Action>([&self](TickContext& ctx){
            auto* world = static_cast<EcosystemState*>(ctx.world);
            (void)world;
            if (!self.alive) return Status::Failure;
            if (self.get_skip_movement()) {
                self.clear_current_target();
                self.clear_path();
                self.clear_wander_target();
                self.clear_mating_target();
                if (ctx.blackboard) {
                    auto& bb = *ctx.blackboard;
                    bb.ints.erase("mate_target_id");
                    bb.doubles.erase("mate_target_pos_x");
                    bb.doubles.erase("mate_target_pos_y");
                    bb.ints.erase("mating_timer_ticks");
                    bb.doubles.erase("target_pos_x");
                    bb.doubles.erase("target_pos_y");
                }
            }
            self.set_skip_movement(false);
            SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[BT YAML] Finalize: reset skip_movement=false for '{}'", self.species_name);
            return Status::Success;
        });

        root_seq->add_child(act_update);
        root_seq->add_child(selector_succeeder);
        root_seq->add_child(act_finalize);
        auto tree = std::make_unique<BehaviorTree>(root_seq);
        // 在黑板标记来源，便于 UI/调试观察
        tree->blackboard().strings["bt_source"] = std::string("yaml:") + self.species_name;
        SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[BT YAML] Built YAML tree for '{}'", self.species_name);
        return tree;
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_WARN(spdlog::get("ecosim"), "[BT YAML] Failed to build YAML tree for '{}': {}. Falling back to code tree.", self.species_name, e.what());
        return nullptr;
    }
}

// 主节点：吃草动作（内联 Action），在近场范围内提交吃草交互

std::unique_ptr<BehaviorTree> build_tree_for_animal(Animal& self) {
    // 优先使用 YAML 蓝图构建，失败则回退到代码版
    if (!self.species_name.empty()) {
        if (auto yaml_tree = build_tree_from_yaml_if_available(self)) {
            SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[BT] Using YAML tree for '{}'", self.species_name);
            return yaml_tree;
        }
    }
    // 根：序列(UpdateState -> Succeeder(PrioritySelector(交配/觅食/游荡)) -> Finalize)
    // 使用 Succeeder 包裹优先选择器，确保无论其返回 Running/Success 都继续执行 Finalize，
    // 避免 Running 阻断导致 skip_movement 等临时标记无法在本 tick 清理。
    auto root_seq = std::make_shared<Sequence>();
    auto root_selector = std::make_shared<PrioritySelector>();

    // --- 通用：UpdateStateTick ---
    auto act_update = std::make_shared<Action>([&self](TickContext& ctx){
        auto* world = static_cast<EcosystemState*>(ctx.world);
        if (!world || !self.alive) return Status::Failure;

        // 本 tick 开始先清除跨 tick 残留的移动跳过标记，避免卡住
        self.set_skip_movement(false);
        SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[BT] Update: reset skip_movement=false for '{}'", self.species_name);
        self.clear_sensor_caches();

        // 注：skip_movement 仅由本 tick 的具体动作设置（交配、分娩、近场吃草/捕食），
        // 不再从黑板读取跨 tick 标记，避免装饰器未被执行时残留导致卡住。

        // 饱食状态更新（速度/能耗不再全局调整，改由具体 Action 的倍率控制）
        self.refresh_hunger_state();

        // 从黑板读取意图锁定时长（可由 YAML 配置覆盖）
        if (ctx.blackboard) {
            auto& bb = *ctx.blackboard;
            if (bb.ints.find("mating_intent_lock_duration") != bb.ints.end()) {
                self.set_mating_intent_lock_duration(std::max(0, bb.ints["mating_intent_lock_duration"]));
            }
            if (bb.ints.find("forage_intent_lock_duration") != bb.ints.end()) {
                self.set_forage_intent_lock_duration(std::max(0, bb.ints["forage_intent_lock_duration"]));
            }
        }

        // 求偶/觅食意图锁定推进与释放
        if (self.get_mating_intent_lock_ticks() > 0) {
            self.set_mating_intent_lock_ticks(self.get_mating_intent_lock_ticks() - 1);
        } else {
            if (self.get_mating_target().has_value() && self.get_hunger_state() == HungerState::STARVING) {
                self.clear_mating_target();
            }
        }
        if (self.get_forage_intent_lock_ticks() > 0) {
            self.set_forage_intent_lock_ticks(self.get_forage_intent_lock_ticks() - 1);
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

        // --- 将规范黑板键写入（供分支条件与装饰器使用） ---
        if (ctx.blackboard) {
            auto& bb = *ctx.blackboard;
            // 饥饿状态（枚举以 int 存储：0=SATISFIED,1=NORMAL,2=STARVING）
            int hunger_code = 1;
            switch (self.get_hunger_state()) {
                case HungerState::SATISFIED: hunger_code = 0; break;
                case HungerState::NORMAL: hunger_code = 1; break;
                case HungerState::STARVING: hunger_code = 2; break;
            }
            bb.ints["hunger_state"] = hunger_code;

            // 交配计时器（用于 UI 展示或进度装饰器）
            bb.ints["mating_timer_ticks"] = std::max(0, self.mating_timer);

            // 繁殖守卫相关键：最低能量、最低年龄、冷却剩余
            // 能量阈值以 RaceBase 判定近似：reproduction_energy_cost*2
            bb.doubles["repro_energy_min"] = self.reproduction_energy_cost * 2.0;
            bb.ints["repro_age_min"] = self.min_reproduction_age;
            bb.ints["repro_cooldown_ticks"] = self.reproduction_cooldown;

            // 逃逸阈值：按物种设置。默认=探测范围；牛用较小比例（不影响其他用途的探测范围）
            const double default_threat_threshold = (self.species_name == std::string("cow"))
                ? std::max(0.0, self.get_detection_range() * 0.1)
                : self.get_detection_range();
            if (bb.doubles.find("threat_threshold") == bb.doubles.end()) {
                bb.doubles["threat_threshold"] = default_threat_threshold;
            }
            const double threat_threshold = (bb.doubles.find("threat_threshold") != bb.doubles.end())
                ? bb.doubles["threat_threshold"]
                : default_threat_threshold;

            // 探测威胁（仅在阈值范围内），目前将“tiger”视为威胁对象
            bool danger = false;
            double threat_dist = std::numeric_limits<double>::max();
            Position threat_pos = self.position;
            {
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
                        // 仅当威胁进入阈值范围，才标记 danger_nearby
                        if (d <= threat_threshold) {
                            danger = true;
                        }
                    }
                }
            }
            bb.ints["danger_nearby"] = danger ? 1 : 0;
            bb.doubles["threat_distance"] = std::isfinite(threat_dist) ? threat_dist : (threat_threshold + 1.0);
            bb.doubles["threat_pos_x"] = threat_pos.x;
            bb.doubles["threat_pos_y"] = threat_pos.y;

            // 集中怀孕速度惩罚：本 tick 的基础速度倍率
            double base_speed_multiplier = 1.0;
            if (self.is_pregnant) {
                base_speed_multiplier *= std::max(0.0, self.get_pregnancy_speed_penalty());
            }
            bb.doubles["current_speed_multiplier"] = base_speed_multiplier;
            if (self.is_pregnant) {
                SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"),
                    "[BT Code] Pregnant speed: penalty={:.2f} base_mul={:.2f} movement_speed={:.2f}",
                    self.get_pregnancy_speed_penalty(), base_speed_multiplier, self.movement_speed);
            }

            // --- 缓存：探测范围内的食物/配偶列表 ---
            // mates: 同种雌性，且可繁殖
            {
                const auto nearby_races = world->get_nearby_races_broad(self.position, self.get_detection_range());
                for (const auto& r : nearby_races) {
                    if (!r || !r->alive) continue;
                    if (r.get() == &self) continue;
                    if (r->species_name != self.species_name) continue;
                    auto female = std::dynamic_pointer_cast<Animal>(r);
                    if (!female) continue;
                    if (female->sex != Sex::FEMALE || !female->can_reproduce()) continue;
                    if (self.position.distance_to(female->position) > self.get_detection_range()) continue;
                    self.cache_mate(female);
                }
                bb.ints["perceived_mates_count"] = static_cast<int>(self.get_cached_mates_snapshot().size());
            }

            // food: races 可食 + things 可食（依据 food_types 列表）
            {
                const auto nearby_races = world->get_nearby_races_broad(self.position, self.get_detection_range());
                for (const auto& r : nearby_races) {
                    if (!r || !r->alive) continue;
                    if (std::find(self.food_types.begin(), self.food_types.end(), r->species_name) == self.food_types.end()) continue;
                    if (self.position.distance_to(r->position) > self.get_detection_range()) continue;
                    self.cache_food_race(r);
                }
                const auto nearby_things = world->get_nearby_things_broad(self.position, self.get_detection_range());
                for (const auto& t : nearby_things) {
                    if (!t || !t->alive) continue;
                    if (std::find(self.food_types.begin(), self.food_types.end(), t->species_name) == self.food_types.end()) continue;
                    if (self.position.distance_to(t->position) > self.get_detection_range()) continue;
                    self.cache_food_thing(t);
                }
                bb.ints["perceived_food_races_count"] = static_cast<int>(self.get_cached_food_races_snapshot().size());
                bb.ints["perceived_food_things_count"] = static_cast<int>(self.get_cached_food_things_snapshot().size());
            }
            // 维护滞后/冷却计数器：最近进食计时与强制游荡倒计时
            {
                int meal_ticks = (bb.ints.find("ticks_since_last_meal") != bb.ints.end()) ? bb.ints["ticks_since_last_meal"] : 0;
                bb.ints["ticks_since_last_meal"] = std::max(0, meal_ticks + 1);
                if (bb.ints.find("force_wander_ticks") != bb.ints.end()) {
                    bb.ints["force_wander_ticks"] = std::max(0, bb.ints["force_wander_ticks"] - 1);
                }
            }
        }

        return Status::Success;
    });

    // --- 交配序列：条件 -> 追配偶/提交交互 ---
    auto seq_mate = std::make_shared<Sequence>();
    seq_mate->add_child(std::make_shared<Condition>([&self](TickContext&){
        // 交配条件：雄性、可繁殖、非极度饥饿，且未被觅食意图锁阻断
        return self.sex == Sex::MALE && self.can_reproduce() && self.get_hunger_state() != HungerState::STARVING && self.get_forage_intent_lock_ticks() <= 0;
    }));
    // 额外条件：基于求偶意愿概率的抽样（与 YAML 中的 desire_below_param 对齐）
    seq_mate->add_child(std::make_shared<Condition>([&self](TickContext& ctx){
        auto* world = static_cast<EcosystemState*>(ctx.world);
        if (!world || !self.alive) return false;
        auto& rng_local = world->get_thread_local_rng();
        std::uniform_real_distribution<> desire_dist(0.0, 1.0);
        return desire_dist(rng_local) < self.get_mating_desire_probability();
    }));
    // 行为：复用封装的 ApproachOrMate
    seq_mate->add_child(std::make_shared<Action>([&self](TickContext& ctx){
        YAML::Node p; // 默认为使用黑板键 'mating_range'
        return behavior::actions::ApproachOrMate(self, ctx, p);
    }));

    // --- 觅食/捕食序列：条件 -> 近场吃草(进度) -> 远处目标与移动/捕食 ---
    auto seq_forage = std::make_shared<Sequence>();
    seq_forage->add_child(std::make_shared<Condition>([&self](TickContext&){
        return self.get_hunger_state() != HungerState::SATISFIED && !self.food_types.empty();
    }));
    // 冷却门控：若 force_wander_ticks > 0，则暂时不进入觅食序列，避免高频切换
    seq_forage->add_child(std::make_shared<Condition>([](TickContext& ctx){
        if (!ctx.blackboard) return true;
        auto& bb = *ctx.blackboard;
        int fwt = (bb.ints.find("force_wander_ticks") != bb.ints.end()) ? bb.ints["force_wander_ticks"] : 0;
        return fwt <= 0;
    }));
    // 近场吃草：由进度装饰器控制持续时间（仅当主食为 grass 时）
    // 并通过 Selector 保障非草食动物（如老虎）不会被该动作阻塞
    auto sel_forage_inner = std::make_shared<Selector>();
    {
        auto eat_action = std::make_shared<Action>([&self](TickContext& ctx){
            auto* world = static_cast<EcosystemState*>(ctx.world);
            if (!world || !self.alive) return Status::Failure;
            if (self.get_skip_movement()) return Status::Failure;
            if (self.food_types.empty() || self.get_hunger_state() == HungerState::SATISFIED) {
                return Status::Failure;
            }
            const std::string& primary_food = self.food_types.front();
            if (primary_food != std::string("grass")) {
                return Status::Failure;
            }
            if (self.eating_range <= 0.0) {
                return Status::Failure;
            }
            auto nearby_things = world->get_things_in_range("grass", self.position, self.eating_range);
            for (const auto& thing_ptr : nearby_things) {
                if (!thing_ptr || !thing_ptr->alive) continue;
                world->submit_interaction_request(AttemptToEatThingRequest{self.shared_from_this(), thing_ptr});
                self.set_skip_movement(true); // 本 tick 不移动
                // 与通用 EatNearbyThing 行为保持一致：重置最近进食计时，避免立即切回远程觅食
                if (ctx.blackboard) {
                    ctx.blackboard->ints["ticks_since_last_meal"] = 0;

                    // 调试：显示当前吃草进度 current/total
                    int cur = 0;
                    int tot = 0;
                    auto it_cur = ctx.blackboard->ints.find("eat_grass_current_ticks");
                    if (it_cur != ctx.blackboard->ints.end()) cur = it_cur->second;
                    auto it_tot = ctx.blackboard->ints.find("eat_grass_total_ticks");
                    if (it_tot != ctx.blackboard->ints.end()) tot = it_tot->second;
                    double pct = (tot > 0) ? (static_cast<double>(cur) / static_cast<double>(tot) * 100.0) : 0.0;
                    SPDLOG_LOGGER_INFO(spdlog::get("ecosim"),
                        "[EatNearbyGrass] progress {}/{} ({:.0f}%) range={:.1f}",
                        cur, tot, pct, self.eating_range);
                }
                // 进度相关：若存在黑板，保持 current/total 用于显示
                (void)ctx.blackboard;
                return Status::Success;
            }
            return Status::Failure;
        });
        // 近场吃草使用“循环进度装饰器”以跨 tick 维持进度
        const int default_eat_ticks = 20; // 若 YAML 未提供，默认 20 tick
        auto eat_with_progress = std::make_shared<ProgressLoopDecorator>(
            eat_action,
            "eat_grass_total_ticks",
            "eat_grass_current_ticks",
            default_eat_ticks
        );
        // 仅在主食为 grass 时执行进度吃草，否则跳过以尝试后续分支
        auto grass_only_seq = std::make_shared<Sequence>();
        grass_only_seq->add_child(std::make_shared<Condition>([&self](TickContext&){
            if (self.food_types.empty()) return false;
            const std::string& primary_food = self.food_types.front();
            return primary_food == std::string("grass");
        }));
        // 仅当“足够接近”草时才执行吃草，避免刚入 eating_range 就频繁打断追食
        // 硬停止半径：优先读取黑板键 eat_hard_stop_range；若未设置则使用 min(eating_range, max(3.0, eating_range*0.5))
        grass_only_seq->add_child(std::make_shared<Condition>([&self](TickContext& ctx){
            auto* world = static_cast<EcosystemState*>(ctx.world);
            if (!world || !self.alive) return false;
            if (self.eating_range <= 0.0) return false;
            double stop_range = self.eating_range;
            if (ctx.blackboard) {
                auto& bb = *ctx.blackboard;
                if (bb.doubles.find("eat_hard_stop_range") != bb.doubles.end()) {
                    stop_range = std::max(0.0, bb.doubles["eat_hard_stop_range"]);
                } else {
                    stop_range = std::min(self.eating_range, std::max(3.0, self.eating_range * 0.5));
                }
            } else {
                stop_range = std::min(self.eating_range, std::max(3.0, self.eating_range * 0.5));
            }
            auto nearby_things = world->get_things_in_range("grass", self.position, stop_range);
            const bool ok = !nearby_things.empty();
            SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"),
                "[Gate EatNear] stop_range={:.1f} eating_range={:.1f} candidates={}",
                stop_range, self.eating_range, static_cast<int>(nearby_things.size()));
            return ok;
        }));
        // 在有草可吃的前提下，执行带进度的吃草动作
        grass_only_seq->add_child(eat_with_progress);
        sel_forage_inner->add_child(grass_only_seq);
    }

    // 近场捕食分支：职责单一
    auto seq_hunt_near = std::make_shared<Sequence>();
    seq_hunt_near->add_child(std::make_shared<Condition>([&self](TickContext&){
        if (self.food_types.empty()) return false;
        const std::string& primary_food = self.food_types.front();
        if (primary_food != std::string("cow")) return false;
        return self.hunting_range > 0.0 && self.hunting_cooldown <= 0 && self.get_hunting_desire() > 0.0;
    }));
    seq_hunt_near->add_child(std::make_shared<Action>([&self](TickContext& ctx){
        YAML::Node p; p["race"] = "cow"; p["range_param"] = "hunting_range"; p["success_rate_param"] = "hunting_success_rate";
        return behavior::actions::HuntNearbyRace(self, ctx, p);
    }));
    sel_forage_inner->add_child(seq_hunt_near);

    // 远处追食分支：选择目标并移动
    auto seq_chase_food = std::make_shared<Sequence>();
    seq_chase_food->add_child(std::make_shared<Condition>([&self](TickContext&){
        // 觅食条件：有食物类型，非吃饱，且未被交配意图锁阻断
        return !self.food_types.empty() && self.get_hunger_state() != HungerState::SATISFIED && self.get_mating_intent_lock_ticks() <= 0;
    }));
    // 选择最近可食目标并移动，改为调用封装函数
    seq_chase_food->add_child(std::make_shared<Action>([&self](TickContext& ctx){
        YAML::Node p; // 默认选择范围内最近食物并写入黑板 target_pos
        return behavior::actions::SelectTargetPoint(self, ctx, p);
    }));
    seq_chase_food->add_child(std::make_shared<Action>([&self](TickContext& ctx){
        YAML::Node p; p["speed_multiplier_key"] = "chase_speed_multiplier"; p["energy_multiplier_key"] = "chase_energy_multiplier";
        return behavior::actions::PlanPathToTarget(self, ctx, p);
    }));
    sel_forage_inner->add_child(seq_chase_food);

    // 将内层 Selector 作为觅食序列的第二个子节点
    seq_forage->add_child(sel_forage_inner);

    // --- 逃逸序列：条件 -> 逃逸一步并清理繁殖上下文 ---
    auto seq_flee = std::make_shared<Sequence>();
    // 条件：danger_nearby == true 或 threat_distance <= threat_threshold
    seq_flee->add_child(std::make_shared<Condition>([](TickContext& ctx){
        if (!ctx.blackboard) return false;
        auto& bb = *ctx.blackboard;
        const bool danger = (bb.ints.find("danger_nearby") != bb.ints.end() && bb.ints["danger_nearby"] != 0);
        const double dist = (bb.doubles.find("threat_distance") != bb.doubles.end()) ? bb.doubles["threat_distance"] : std::numeric_limits<double>::max();
        const double threshold = (bb.doubles.find("threat_threshold") != bb.doubles.end()) ? bb.doubles["threat_threshold"] : 0.0;
        return danger || (threshold > 0.0 && dist <= threshold);
    }));
    // 行为：调用封装的 FleeFromThreat
    seq_flee->add_child(std::make_shared<Action>([&self](TickContext& ctx){
        YAML::Node p; p["pos_x_param"] = "threat_pos_x"; p["pos_y_param"] = "threat_pos_y"; p["speed_multiplier_param"] = "flee_speed_multiplier"; p["energy_multiplier_param"] = "flee_energy_multiplier";
        return behavior::actions::FleeFromThreat(self, ctx, p);
    }));

    // --- 游荡行为：复用封装函数 ---
    auto act_wander = std::make_shared<Action>([&self](TickContext& ctx){
        YAML::Node p; return behavior::actions::WanderAnywhere(self, ctx, p);
    });

    // 为游荡行为添加“循环进度装饰器”：在累计到指定时长前，每 tick 执行游荡并返回 Running；
    // 达到时长后返回 Success，同时重置进度。总时长从黑板键 `wander_total_ticks` 读取，
    // 若未设置则使用默认值（建议与 YAML 的 `wandering_duration` 对齐）。
    const int default_wander_ticks = 50; // 与 base_animal.yaml / species_params 默认值一致
    auto wander_with_progress = std::make_shared<ProgressLoopDecorator>(
        act_wander,
        "wander_total_ticks",
        "wander_current_ticks",
        default_wander_ticks
    );

    // 优先级：逃逸 > 繁殖 > 觅食/捕猎 > 游荡
    root_selector->add_child(seq_flee);
    root_selector->add_child(seq_mate);
    root_selector->add_child(seq_forage);
    root_selector->add_child(wander_with_progress);

    // --- 通用：FinalizeTick ---
    auto act_finalize = std::make_shared<Action>([&self](TickContext& ctx){
        auto* world = static_cast<EcosystemState*>(ctx.world);
        (void)world;
        if (!self.alive) return Status::Failure;

        // 统一清理：当本 tick 被请求占用或交配/分娩进行时，清理临时目标与路径
        if (self.get_skip_movement()) {
            self.clear_current_target();
            self.clear_path();
            self.clear_wander_target();
            self.clear_mating_target();
            // 清理繁殖相关黑板键（避免残留）
            if (ctx.blackboard) {
                auto& bb = *ctx.blackboard;
                bb.ints.erase("mate_target_id");
                bb.doubles.erase("mate_target_pos_x");
                bb.doubles.erase("mate_target_pos_y");
                bb.ints.erase("mating_timer_ticks");
                bb.doubles.erase("target_pos_x");
                bb.doubles.erase("target_pos_y");
            }
        }
        // 本 tick 结束，重置仅当 tick 内使用的标记
        self.set_skip_movement(false);
        SPDLOG_LOGGER_DEBUG(spdlog::get("ecosim"), "[BT] Finalize: reset skip_movement=false for '{}'", self.species_name);
        return Status::Success;
    });

    // 确保优先选择器的返回不会阻断 Finalize
    auto selector_succeeder = std::make_shared<Succeeder>(root_selector);

    root_seq->add_child(act_update);
    root_seq->add_child(selector_succeeder);
    root_seq->add_child(act_finalize);

    auto tree = std::make_unique<BehaviorTree>(root_seq);
    tree->blackboard().strings["bt_source"] = std::string("code:") + self.species_name;
    SPDLOG_LOGGER_INFO(spdlog::get("ecosim"), "[BT] Built code tree for '{}' (YAML not found or failed)", self.species_name);
    return tree;
}

} // namespace behavior