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

namespace behavior {

using namespace bt;

// 主节点：吃草动作（内联 Action），在近场范围内提交吃草交互

std::unique_ptr<BehaviorTree> build_tree_for_animal(Animal& self) {
    // 根：序列(UpdateState -> Succeeder(PrioritySelector(交配/觅食/游荡)) -> Finalize)
    // 使用 Succeeder 包裹优先选择器，确保无论其返回 Running/Success 都继续执行 Finalize，
    // 避免 Running 阻断导致 skip_movement 等临时标记无法在本 tick 清理。
    auto root_seq = std::make_shared<Sequence>();
    auto root_selector = std::make_shared<PrioritySelector>();

    // --- 通用：UpdateStateTick ---
    auto act_update = std::make_shared<Action>([&self](TickContext& ctx){
        auto* world = static_cast<EcosystemState*>(ctx.world);
        if (!world || !self.alive) return Status::Failure;

        // 说明：交配/分娩不再在更新阶段强制设置 skip_movement，
        // 而是交由“最高优先级占位节点”阻塞本 tick 的其他动作。

        // 饱食状态更新（速度/能耗不再全局调整，改由具体 Action 的倍率控制）
        self.update_hunger_state();

        // 求偶意图锁定与释放
        if (self.mating_intent_lock_ticks > 0) {
            self.mating_intent_lock_ticks -= 1;
        } else {
            if (self.mating_target.has_value() && self.hunger_state == HungerState::STARVING) {
                self.mating_target.reset();
            }
        }

        // 进行中的交配计时器推进
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
                // 在黑板标记“本 tick 分娩”，交由最高优先级节点阻塞移动
                if (ctx.blackboard) {
                    ctx.blackboard->ints["birthing_this_tick"] = 1;
                }
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
            switch (self.hunger_state) {
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
                ? std::max(0.0, self.detection_range * 0.1)
                : self.detection_range;
            if (bb.doubles.find("threat_threshold") == bb.doubles.end()) {
                bb.doubles["threat_threshold"] = default_threat_threshold;
            }
            const double threat_threshold = (bb.doubles.find("threat_threshold") != bb.doubles.end())
                ? bb.doubles["threat_threshold"]
                : default_threat_threshold;

            // 探测威胁（仅在阈值范围内），物种由黑板配置 danger_species（逗号分隔），默认 "tiger"
            bool danger = false;
            double threat_dist = std::numeric_limits<double>::max();
            Position threat_pos = self.position;
            {
                // 构造威胁物种集合
                std::unordered_set<std::string> danger_set;
                std::string conf = "tiger";
                if (bb.strings.find("danger_species") != bb.strings.end() && !bb.strings["danger_species"].empty()) {
                    conf = bb.strings["danger_species"];
                }
                size_t start = 0;
                while (start < conf.size()) {
                    size_t comma = conf.find(',', start);
                    std::string item = conf.substr(start, comma == std::string::npos ? std::string::npos : (comma - start));
                    // 修剪空格
                    size_t l = item.find_first_not_of(" \t\n\r");
                    size_t r = item.find_last_not_of(" \t\n\r");
                    if (l != std::string::npos && r != std::string::npos) {
                        danger_set.insert(item.substr(l, r - l + 1));
                    } else if (!item.empty()) {
                        danger_set.insert(item);
                    }
                    if (comma == std::string::npos) break;
                    start = comma + 1;
                }
                const auto nearby = world->get_nearby_races_broad(self.position, threat_threshold);
                for (const auto& r : nearby) {
                    if (!r || !r->alive) continue;
                    if (r.get() == &self) continue;
                    if (danger_set.find(r->species_name) != danger_set.end() && r->species_name != self.species_name) {
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

            // 缓存最近食物（坐标），减少后续重复查询
            int food_found = 0;
            Position nearest_food_pos = self.position;
            double min_food_dist = std::numeric_limits<double>::max();
            if (!self.food_types.empty() && self.hunger_state != HungerState::SATISFIED) {
                const auto nearby_races2 = world->get_nearby_races_broad(self.position, self.detection_range);
                const auto nearby_things2 = world->get_nearby_things_broad(self.position, self.detection_range);
                const auto consider_food = [&](const auto& entity) {
                    if (!entity || !entity->alive) return;
                    if (std::find(self.food_types.begin(), self.food_types.end(), entity->species_name) == self.food_types.end()) return;
                    double d = self.position.distance_to(entity->position);
                    if (d <= self.detection_range && d < min_food_dist) {
                        min_food_dist = d;
                        nearest_food_pos = entity->position;
                        food_found = 1;
                    }
                };
                for (const auto& race : nearby_races2) consider_food(race);
                for (const auto& thing : nearby_things2) consider_food(thing);
            }
            bb.ints["nearest_food_found"] = food_found;
            bb.doubles["nearest_food_pos_x"] = nearest_food_pos.x;
            bb.doubles["nearest_food_pos_y"] = nearest_food_pos.y;
        }

        return Status::Success;
    });

    // --- 交配序列：条件 -> 追配偶/提交交互 ---
    auto seq_mate = std::make_shared<Sequence>();
    seq_mate->add_child(std::make_shared<Condition>([&self](TickContext&){
        return self.sex == Sex::MALE && self.can_reproduce() && self.hunger_state != HungerState::STARVING;
    }));
    seq_mate->add_child(std::make_shared<Action>([&self](TickContext& ctx){
        auto* world = static_cast<EcosystemState*>(ctx.world);
        if (!world || !self.alive) return Status::Failure;

        // 求偶意愿概率
        auto& rng_local = world->get_thread_local_rng();
        std::uniform_real_distribution<> desire_dist(0.0, 1.0);
        if (desire_dist(rng_local) >= self.mating_desire_probability) {
            return Status::Failure; // 未命中意愿，交由后续分支
        }

        auto mate_opt = self.find_available_mate(*world);
        if (!mate_opt.has_value()) {
            return Status::Failure;
        }
        auto mate = mate_opt.value();
        if (!mate || !mate->alive) {
            return Status::Failure;
        }

        // 将当前拟定伴侣位置写入黑板，便于后续接近与调试
        if (ctx.blackboard) {
            auto& bb = *ctx.blackboard;
            bb.doubles["mate_target_pos_x"] = mate->position.x;
            bb.doubles["mate_target_pos_y"] = mate->position.y;
        }

        if (self.position.distance_to(mate->position) <= self.mating_range) {
            // 在范围内，提交交配请求并跳过移动
            world->submit_interaction_request(AttemptToMateRequest{mate, std::dynamic_pointer_cast<Animal>(self.shared_from_this())});
            return Status::Running;
        } else {
            // 未到范围内：锁定交配意图并前往配偶位置
            self.mating_target = mate->position;
            self.mating_intent_lock_ticks = self.mating_intent_lock_duration;
            self.set_movement_target(self.mating_target.value(), true);
            // 将当前移动目标写入黑板，保持一致的调试显示
            if (ctx.blackboard) {
                auto& bb = *ctx.blackboard;
                bb.doubles["target_pos_x"] = self.mating_target->x;
                bb.doubles["target_pos_y"] = self.mating_target->y;
            }
            // 直接执行一步移动并结算能量（统一封装）
            const int ww = world->config.world_width;
            const int wh = world->config.world_height;
            self.execute_movement_step(ww, wh);
            return Status::Running;
        }
    }));

    // --- 觅食/捕食序列：条件 -> 近场吃草(进度) -> 远处目标与移动/捕食 ---
    auto seq_forage = std::make_shared<Sequence>();
    seq_forage->add_child(std::make_shared<Condition>([&self](TickContext&){
        return self.hunger_state != HungerState::SATISFIED && !self.food_types.empty();
    }));
    // 近场吃草：由进度装饰器控制持续时间（仅当主食为 grass 时）
    // 并通过 Selector 保障非草食动物（如老虎）不会被该动作阻塞
    auto sel_forage_inner = std::make_shared<Selector>();
    {
        const int default_eat_ticks = 300; // 可配置：行为树编辑器参数 ${total_ticks}
        auto eat_action = std::make_shared<Action>([&self](TickContext& ctx){
            auto* world = static_cast<EcosystemState*>(ctx.world);
            if (!world || !self.alive) return Status::Failure;
            if (self.food_types.empty() || self.hunger_state == HungerState::SATISFIED) {
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
                // 进度相关：若存在黑板，保持 current/total 用于显示
                (void)ctx.blackboard;
                return Status::Success;
            }
            return Status::Failure;
        });
        auto eat_with_progress = std::make_shared<ProgressDecorator>(eat_action,
            "eat_grass_total_ticks",
            "eat_grass_current_ticks",
            default_eat_ticks);
        // 仅在主食为 grass 时执行进度吃草，否则跳过以尝试后续分支
        auto grass_only_seq = std::make_shared<Sequence>();
        grass_only_seq->add_child(std::make_shared<Condition>([&self](TickContext&){
            if (self.food_types.empty()) return false;
            const std::string& primary_food = self.food_types.front();
            return primary_food == std::string("grass");
        }));
        // 仅当近场确有草可吃时才进入进度阶段，避免在无草时被阻塞
        grass_only_seq->add_child(std::make_shared<Condition>([&self](TickContext& ctx){
            auto* world = static_cast<EcosystemState*>(ctx.world);
            if (!world || !self.alive) return false;
            if (self.eating_range <= 0.0) return false;
            auto nearby_things = world->get_things_in_range("grass", self.position, self.eating_range);
            return !nearby_things.empty();
        }));
        grass_only_seq->add_child(eat_with_progress);
        sel_forage_inner->add_child(grass_only_seq);
    }

    // 远处移动/捕食与兜底逻辑
    auto action_hunt_or_move = std::make_shared<Action>([&self](TickContext& ctx){
        auto* world = static_cast<EcosystemState*>(ctx.world);
        if (!world || !self.alive) return Status::Failure;

        if (self.food_types.empty() || self.hunger_state == HungerState::SATISFIED) {
            return Status::Failure;
        }
        const std::string& primary_food = self.food_types.front();

        // 捕食：对牛的狩猎（与现有逻辑一致）
        if (primary_food == std::string("cow") && self.hunting_range > 0.0 && self.hunting_cooldown <= 0) {
            const double desire = self.get_hunting_desire();
            if (desire > 0.0) {
                auto& rng_local = world->get_thread_local_rng();
                std::uniform_real_distribution<> hunt_dist(0.0, 1.0);
                if (hunt_dist(rng_local) < self.hunting_success_rate * desire) {
                    auto nearby_races = world->get_nearby_races_broad(self.position, self.hunting_range);
                    for (const auto& race : nearby_races) {
                        if (!race || !race->alive) continue;
                        if (race.get() == &self) continue;
                        if (race->species_name != "cow") continue;
                        if (self.position.distance_to(race->position) > self.hunting_range) continue;
                        world->submit_interaction_request(AttemptToEatRaceRequest{self.shared_from_this(), race});
                        self.start_hunting_cooldown();
                        // 近场捕食命中：本 tick 不移动，直接返回 Running 阻塞后续移动
                        return Status::Running;
                    }
                }
            }
        }

        // 远处食物：选择最近食物为目标并前往；若无目标，交由游荡分支
        // 在探测范围内选择最近的可食目标（行为内联实现，替代 Animal::select_target_point）
        if (!self.food_types.empty() && self.hunger_state != HungerState::SATISFIED) {
            std::optional<Position> nearest_food;
            if (ctx.blackboard && ctx.blackboard->ints.find("nearest_food_found") != ctx.blackboard->ints.end() && ctx.blackboard->ints["nearest_food_found"] != 0) {
                nearest_food = Position{ctx.blackboard->doubles["nearest_food_pos_x"], ctx.blackboard->doubles["nearest_food_pos_y"]};
            } else {
                double min_distance = std::numeric_limits<double>::max();
                const auto nearby_races = world->get_nearby_races_broad(self.position, self.detection_range);
                const auto nearby_things = world->get_nearby_things_broad(self.position, self.detection_range);
                const auto consider_entity = [&](const auto& entity) {
                    if (!entity || !entity->alive) return;
                    if (std::find(self.food_types.begin(), self.food_types.end(), entity->species_name) == self.food_types.end()) return;
                    double distance = self.position.distance_to(entity->position);
                    if (distance <= self.detection_range && distance < min_distance) {
                        min_distance = distance;
                        nearest_food = entity->position;
                    }
                };
                for (const auto& race : nearby_races) consider_entity(race);
                for (const auto& thing : nearby_things) consider_entity(thing);
            }

            if (nearest_food.has_value()) {
                self.set_movement_target(nearest_food.value(), true);
            } else {
                self.clear_movement_target();
            }
        }

        if (self.current_target.has_value()) {
            const int ww = world->config.world_width;
            const int wh = world->config.world_height;
            // 远处移动默认倍率，可通过黑板覆盖（如 chase_*_multiplier）
            const double speed_mul = (ctx.blackboard && ctx.blackboard->doubles.count("chase_speed_multiplier")) ? ctx.blackboard->doubles["chase_speed_multiplier"] : 1.0;
            const double energy_mul = (ctx.blackboard && ctx.blackboard->doubles.count("chase_energy_multiplier")) ? ctx.blackboard->doubles["chase_energy_multiplier"] : 1.0;
            double speed_effective = speed_mul;
            if (self.is_pregnant) {
                speed_effective *= std::max(0.0, self.pregnancy_speed_penalty);
            }
            self.execute_movement_step(ww, wh, speed_effective, energy_mul);
            return Status::Running;
        }

        // 无目标：返回 Failure，让 Selector 切到游荡
        return Status::Failure;
    });
    sel_forage_inner->add_child(action_hunt_or_move);

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
    // 行为：计算安全点并移动；清理繁殖相关黑板键
    seq_flee->add_child(std::make_shared<Action>([&self](TickContext& ctx){
        auto* world = static_cast<EcosystemState*>(ctx.world);
        if (!world || !self.alive) return Status::Failure;
        if (!ctx.blackboard) return Status::Failure;
        auto& bb = *ctx.blackboard;

        // 从黑板获取威胁位置，沿反方向采样安全点
        const double tx = (bb.doubles.find("threat_pos_x") != bb.doubles.end()) ? bb.doubles["threat_pos_x"] : self.position.x;
        const double ty = (bb.doubles.find("threat_pos_y") != bb.doubles.end()) ? bb.doubles["threat_pos_y"] : self.position.y;
        Position threat{tx, ty};
        Position dir{ self.position.x - threat.x, self.position.y - threat.y };
        const double inv_len = 1.0 / std::max(1e-9, std::sqrt(dir.x*dir.x + dir.y*dir.y));
        dir.x *= inv_len;
        dir.y *= inv_len;
        const double flee_step = std::max(self.step_distance_per_tick, self.movement_speed);
        Position safe_spot{
            std::max(0.0, std::min(static_cast<double>(world->config.world_width), self.position.x + dir.x * flee_step)),
            std::max(0.0, std::min(static_cast<double>(world->config.world_height), self.position.y + dir.y * flee_step))
        };
        // 将目标写入黑板（便于 UI/调试）
        bb.doubles["target_pos_x"] = safe_spot.x;
        bb.doubles["target_pos_y"] = safe_spot.y;

        // 逃离状态：提高移速与能量消耗（可由黑板配置）
        double speed_mul = (bb.doubles.find("flee_speed_multiplier") != bb.doubles.end()) ? bb.doubles["flee_speed_multiplier"] : 1.5;
        const double energy_mul = (bb.doubles.find("flee_energy_multiplier") != bb.doubles.end()) ? bb.doubles["flee_energy_multiplier"] : 1.5;
        if (self.is_pregnant) {
            speed_mul *= std::max(0.0, self.pregnancy_speed_penalty);
        }
        self.set_movement_target(safe_spot, false);
        self.execute_movement_step(world->config.world_width, world->config.world_height, speed_mul, energy_mul);

        // 清理繁殖上下文（黑板与临时目标）
        self.current_target.reset();
        self.planned_path.clear();
        self.planned_path_index = 0;
        self.wander_target.reset();
        self.mating_target.reset();
        bb.ints.erase("mate_target_id");
        bb.doubles.erase("mate_target_pos_x");
        bb.doubles.erase("mate_target_pos_y");
        bb.ints.erase("mating_timer_ticks");

        return Status::Running;
    }));

    // --- 游荡行为：无条件行动 ---
    auto act_wander = std::make_shared<Action>([&self](TickContext& ctx){
        auto* world = static_cast<EcosystemState*>(ctx.world);
        if (!world || !self.alive) return Status::Failure;

        const int world_width = world->config.world_width;
        const int world_height = world->config.world_height;

        // 若已有游荡目标，直接推进一步
        if (self.wander_target.has_value()) {
            const int ww = world->config.world_width;
            const int wh = world->config.world_height;
            const Position target = self.wander_target.value();
            // 游荡倍率：可在黑板配置，默认较低速度与能耗
            double speed_mul = (ctx.blackboard && ctx.blackboard->doubles.count("wander_speed_multiplier")) ? ctx.blackboard->doubles["wander_speed_multiplier"] : 0.8;
            const double energy_mul = (ctx.blackboard && ctx.blackboard->doubles.count("wander_energy_multiplier")) ? ctx.blackboard->doubles["wander_energy_multiplier"] : 0.6;
            if (self.is_pregnant) {
                speed_mul *= std::max(0.0, self.pregnancy_speed_penalty);
            }
            self.set_movement_target(target, false);
            self.execute_movement_step(ww, wh, speed_mul, energy_mul);
            // 满足状态下步长较小，降低到达阈值，避免“未动就判定到达”
            const double arrival_threshold = std::max(0.2, self.current_step_distance * 0.5);
            if (self.position.distance_to(target) <= arrival_threshold) {
                self.wander_target.reset();
            }
            return Status::Running;
        }

        // 采样新游荡目标（与现有 decide 中一致）
        auto& rng_local = world->get_thread_local_rng();
        std::uniform_real_distribution<> angle_dist(0.0, 2 * M_PI);
        std::uniform_real_distribution<> unit01(0.0, 1.0);

        for (int tries = 0; tries < 6 && !self.wander_target.has_value(); ++tries) {
            const double angle = angle_dist(rng_local);
            const double r = std::max(self.movement_speed, std::sqrt(unit01(rng_local)) * self.wander_radius);
            Position candidate{
                self.position.x + std::cos(angle) * r,
                self.position.y + std::sin(angle) * r
            };
            candidate.x = std::max(0.0, std::min(static_cast<double>(world_width), candidate.x));
            candidate.y = std::max(0.0, std::min(static_cast<double>(world_height), candidate.y));
            if (self.position.distance_to(candidate) < 1e-6) continue;
            self.wander_target = candidate;
        }
        if (!self.wander_target.has_value()) {
            // 兜底：若未选中合法目标，执行一次小幅随机移动
            std::uniform_real_distribution<> angle2(0.0, 2 * M_PI);
            const double a2 = angle2(rng_local);
            Position fallback{
                std::max(0.0, std::min(static_cast<double>(world_width), self.position.x + std::cos(a2) * self.movement_speed)),
                std::max(0.0, std::min(static_cast<double>(world_height), self.position.y + std::sin(a2) * self.movement_speed))
            };
            self.wander_target = fallback;
        }

        // 推进一步并结算能量（统一封装）
        const int ww2 = world->config.world_width;
        const int wh2 = world->config.world_height;
        const Position target2 = self.wander_target.value();
        self.set_movement_target(target2, false);
        self.execute_movement_step(ww2, wh2);
        const double arrival_threshold2 = std::max(0.2, self.current_step_distance * 0.5);
        if (self.position.distance_to(target2) <= arrival_threshold2) {
            self.wander_target.reset();
        }
        return Status::Running;
    });

    // 最高优先级占位：交配进行中（阻塞其他行为）
    auto seq_mating_hold = std::make_shared<Sequence>();
    seq_mating_hold->add_child(std::make_shared<Condition>([&self](TickContext&){
        return self.mating_timer > 0;
    }));
    seq_mating_hold->add_child(std::make_shared<Action>([](TickContext&){
        return Status::Running; // 本 tick 不移动，仅占位阻塞
    }));

    // 次高优先级占位：分娩当帧（阻塞其他行为）
    auto seq_birthing_hold = std::make_shared<Sequence>();
    seq_birthing_hold->add_child(std::make_shared<Condition>([](TickContext& ctx){
        if (!ctx.blackboard) return false;
        auto& bb = *ctx.blackboard;
        return bb.ints.find("birthing_this_tick") != bb.ints.end() && bb.ints["birthing_this_tick"] != 0;
    }));
    seq_birthing_hold->add_child(std::make_shared<Action>([](TickContext& ctx){
        if (ctx.blackboard) ctx.blackboard->ints["birthing_this_tick"] = 0; // 清除占位标记
        return Status::Running;
    }));

    // 优先级：交配占位 > 分娩占位 > 逃逸 > 繁殖 > 觅食/捕猎 > 游荡
    root_selector->add_child(seq_mating_hold);
    root_selector->add_child(seq_birthing_hold);
    root_selector->add_child(seq_flee);
    root_selector->add_child(seq_mate);
    root_selector->add_child(seq_forage);
    root_selector->add_child(act_wander);

    // --- 通用：FinalizeTick ---
    auto act_finalize = std::make_shared<Action>([&self](TickContext& ctx){
        (void)ctx;
        if (!self.alive) return Status::Failure;
        // 无需跨帧全局 skip_movement 清理；占位节点已阻塞本帧动作
        return Status::Success;
    });

    // 确保优先选择器的返回不会阻断 Finalize
    auto selector_succeeder = std::make_shared<Succeeder>(root_selector);

    root_seq->add_child(act_update);
    root_seq->add_child(selector_succeeder);
    root_seq->add_child(act_finalize);

    return std::make_unique<BehaviorTree>(root_seq);
}

} // namespace behavior