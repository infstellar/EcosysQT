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

        // 本 tick 开始先清除跨 tick 残留的移动跳过标记，避免卡住
        self.skip_movement = false;
        // 清理上一帧感知缓存
        self.cached_food_races.clear();
        self.cached_food_things.clear();
        self.cached_mates.clear();

        // 注：skip_movement 仅由本 tick 的具体动作设置（交配、分娩、近场吃草/捕食），
        // 不再从黑板读取跨 tick 标记，避免装饰器未被执行时残留导致卡住。

        // 饱食状态更新（速度/能耗不再全局调整，改由具体 Action 的倍率控制）
        self.update_hunger_state();

        // 从黑板读取意图锁定时长（可由 YAML 配置覆盖）
        if (ctx.blackboard) {
            auto& bb = *ctx.blackboard;
            if (bb.ints.find("mating_intent_lock_duration") != bb.ints.end()) {
                self.mating_intent_lock_duration = std::max(0, bb.ints["mating_intent_lock_duration"]);
            }
            if (bb.ints.find("forage_intent_lock_duration") != bb.ints.end()) {
                self.forage_intent_lock_duration = std::max(0, bb.ints["forage_intent_lock_duration"]);
            }
        }

        // 求偶/觅食意图锁定推进与释放
        if (self.mating_intent_lock_ticks > 0) {
            self.mating_intent_lock_ticks -= 1;
        } else {
            if (self.mating_target.has_value() && self.hunger_state == HungerState::STARVING) {
                self.mating_target.reset();
            }
        }
        if (self.forage_intent_lock_ticks > 0) {
            self.forage_intent_lock_ticks -= 1;
        }

        // 进行中的交配计时器
        if (self.mating_timer > 0) {
            self.mating_timer -= 1;
            self.skip_movement = true; // 交配中本 tick 不移动
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
                self.skip_movement = true; // 分娩本 tick 不移动
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
                base_speed_multiplier *= std::max(0.0, self.pregnancy_speed_penalty);
            }
            bb.doubles["current_speed_multiplier"] = base_speed_multiplier;

            // --- 缓存：探测范围内的食物/配偶列表 ---
            // mates: 同种雌性，且可繁殖
            {
                const auto nearby_races = world->get_nearby_races_broad(self.position, self.detection_range);
                for (const auto& r : nearby_races) {
                    if (!r || !r->alive) continue;
                    if (r.get() == &self) continue;
                    if (r->species_name != self.species_name) continue;
                    auto female = std::dynamic_pointer_cast<Animal>(r);
                    if (!female) continue;
                    if (female->sex != Sex::FEMALE || !female->can_reproduce()) continue;
                    if (self.position.distance_to(female->position) > self.detection_range) continue;
                    self.cached_mates.emplace_back(female);
                }
                bb.ints["perceived_mates_count"] = static_cast<int>(self.cached_mates.size());
            }

            // food: races 可食 + things 可食（依据 food_types 列表）
            {
                const auto nearby_races = world->get_nearby_races_broad(self.position, self.detection_range);
                for (const auto& r : nearby_races) {
                    if (!r || !r->alive) continue;
                    if (std::find(self.food_types.begin(), self.food_types.end(), r->species_name) == self.food_types.end()) continue;
                    if (self.position.distance_to(r->position) > self.detection_range) continue;
                    self.cached_food_races.emplace_back(r);
                }
                const auto nearby_things = world->get_nearby_things_broad(self.position, self.detection_range);
                for (const auto& t : nearby_things) {
                    if (!t || !t->alive) continue;
                    if (std::find(self.food_types.begin(), self.food_types.end(), t->species_name) == self.food_types.end()) continue;
                    if (self.position.distance_to(t->position) > self.detection_range) continue;
                    self.cached_food_things.emplace_back(t);
                }
                bb.ints["perceived_food_races_count"] = static_cast<int>(self.cached_food_races.size());
                bb.ints["perceived_food_things_count"] = static_cast<int>(self.cached_food_things.size());
            }
        }

        return Status::Success;
    });

    // --- 交配序列：条件 -> 追配偶/提交交互 ---
    auto seq_mate = std::make_shared<Sequence>();
    seq_mate->add_child(std::make_shared<Condition>([&self](TickContext&){
        // 交配条件：雄性、可繁殖、非极度饥饿，且未被觅食意图锁阻断
        return self.sex == Sex::MALE && self.can_reproduce() && self.hunger_state != HungerState::STARVING && self.forage_intent_lock_ticks <= 0;
    }));
    seq_mate->add_child(std::make_shared<Action>([&self](TickContext& ctx){
        auto* world = static_cast<EcosystemState*>(ctx.world);
        if (!world || !self.alive) return Status::Failure;
        if (self.skip_movement) return Status::Failure;

        // 求偶意愿概率
        auto& rng_local = world->get_thread_local_rng();
        std::uniform_real_distribution<> desire_dist(0.0, 1.0);
        if (desire_dist(rng_local) >= self.mating_desire_probability) {
            return Status::Failure; // 未命中意愿，交由后续分支
        }

        // 使用缓存的配偶列表选择最近者
        std::shared_ptr<Animal> mate;
        double min_d = std::numeric_limits<double>::max();
        for (auto& wptr : self.cached_mates) {
            auto cand = wptr.lock();
            if (!cand || !cand->alive) continue;
            const double d = self.position.distance_to(cand->position);
            if (d < min_d) {
                min_d = d;
                mate = cand;
            }
        }
        if (!mate) {
            return Status::Failure;
        }
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
            self.skip_movement = true;
            return Status::Running;
        } else {
            // 未到范围内：锁定交配意图并前往配偶位置
            self.mating_target = mate->position;
            self.mating_intent_lock_ticks = self.mating_intent_lock_duration;
            self.current_target = self.mating_target;
            self.plan_path_to_target(*world, self.current_target);
            // 将当前移动目标写入黑板，保持一致的调试显示
            if (ctx.blackboard) {
                auto& bb = *ctx.blackboard;
                bb.doubles["target_pos_x"] = self.current_target->x;
                bb.doubles["target_pos_y"] = self.current_target->y;
            }
            // 直接执行一步移动并结算能量（统一封装）
            const int ww = world->config.world_width;
            const int wh = world->config.world_height;
            self.perform_step_move_path(ww, wh);
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
            if (self.skip_movement) return Status::Failure;
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
                self.skip_movement = true; // 本 tick 不移动
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

    // 近场捕食分支：职责单一
    auto seq_hunt_near = std::make_shared<Sequence>();
    seq_hunt_near->add_child(std::make_shared<Condition>([&self](TickContext&){
        if (self.food_types.empty()) return false;
        const std::string& primary_food = self.food_types.front();
        if (primary_food != std::string("cow")) return false;
        return self.hunting_range > 0.0 && self.hunting_cooldown <= 0 && self.get_hunting_desire() > 0.0;
    }));
    seq_hunt_near->add_child(std::make_shared<Action>([&self](TickContext& ctx){
        auto* world = static_cast<EcosystemState*>(ctx.world);
        if (!world || !self.alive) return Status::Failure;
        if (self.skip_movement) return Status::Failure;
        auto& rng_local = world->get_thread_local_rng();
        std::uniform_real_distribution<> hunt_dist(0.0, 1.0);
        const double desire = self.get_hunting_desire();
        if (hunt_dist(rng_local) < self.hunting_success_rate * desire) {
            for (auto& wptr : self.cached_food_races) {
                auto race = wptr.lock();
                if (!race || !race->alive) continue;
                if (race.get() == &self) continue;
                if (race->species_name != "cow") continue;
                if (self.position.distance_to(race->position) > self.hunting_range) continue;
                world->submit_interaction_request(AttemptToEatRaceRequest{self.shared_from_this(), race});
                self.start_hunting_cooldown();
                self.forage_intent_lock_ticks = self.forage_intent_lock_duration; // 近场捕食命中后短暂锁定觅食意图
                self.skip_movement = true;
                return Status::Running;
            }
        }
        return Status::Failure; // 未命中或无猎物
    }));
    sel_forage_inner->add_child(seq_hunt_near);

    // 远处追食分支：选择目标并移动
    auto seq_chase_food = std::make_shared<Sequence>();
    seq_chase_food->add_child(std::make_shared<Condition>([&self](TickContext&){
        // 觅食条件：有食物类型，非吃饱，且未被交配意图锁阻断
        return !self.food_types.empty() && self.hunger_state != HungerState::SATISFIED && self.mating_intent_lock_ticks <= 0;
    }));
    seq_chase_food->add_child(std::make_shared<Action>([&self](TickContext& ctx){
        auto* world = static_cast<EcosystemState*>(ctx.world);
        if (!world || !self.alive) return Status::Failure;
        if (self.skip_movement) return Status::Failure;
        // 使用缓存选择最近的可食目标
        std::optional<Position> nearest_food;
        double min_distance = std::numeric_limits<double>::max();
        for (auto& wptr : self.cached_food_races) {
            auto race = wptr.lock();
            if (!race || !race->alive) continue;
            if (std::find(self.food_types.begin(), self.food_types.end(), race->species_name) == self.food_types.end()) continue;
            double distance = self.position.distance_to(race->position);
            if (distance <= self.detection_range && distance < min_distance) {
                min_distance = distance;
                nearest_food = race->position;
            }
        }
        for (auto& wptr : self.cached_food_things) {
            auto thing = wptr.lock();
            if (!thing || !thing->alive) continue;
            if (std::find(self.food_types.begin(), self.food_types.end(), thing->species_name) == self.food_types.end()) continue;
            double distance = self.position.distance_to(thing->position);
            if (distance <= self.detection_range && distance < min_distance) {
                min_distance = distance;
                nearest_food = thing->position;
            }
        }

        if (nearest_food.has_value()) {
            self.current_target = nearest_food.value();
            self.plan_path_to_target(*world, self.current_target);
            const int ww = world->config.world_width;
            const int wh = world->config.world_height;
            const double base_mul = (ctx.blackboard && ctx.blackboard->doubles.count("current_speed_multiplier")) ? ctx.blackboard->doubles["current_speed_multiplier"] : 1.0;
            const double speed_mul = (ctx.blackboard && ctx.blackboard->doubles.count("chase_speed_multiplier")) ? ctx.blackboard->doubles["chase_speed_multiplier"] : 1.0;
            const double energy_mul = (ctx.blackboard && ctx.blackboard->doubles.count("chase_energy_multiplier")) ? ctx.blackboard->doubles["chase_energy_multiplier"] : 1.0;
            self.perform_step_move_path(ww, wh, base_mul * speed_mul, energy_mul);
            self.forage_intent_lock_ticks = self.forage_intent_lock_duration; // 追食过程中应用短锁避免立即切换到交配
            return Status::Running;
        } else {
            self.current_target.reset();
            self.planned_path.clear();
            self.planned_path_index = 0;
        }
        return Status::Failure;
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
    // 行为：计算安全点并移动；清理繁殖相关黑板键
    seq_flee->add_child(std::make_shared<Action>([&self](TickContext& ctx){
        auto* world = static_cast<EcosystemState*>(ctx.world);
        if (!world || !self.alive) return Status::Failure;
        if (self.skip_movement) return Status::Failure;
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

        // 逃离状态：提高移速与能量消耗（可由黑板配置），乘以基础速度倍率
        const double base_mul = (bb.doubles.find("current_speed_multiplier") != bb.doubles.end()) ? bb.doubles["current_speed_multiplier"] : 1.0;
        const double speed_mul = (bb.doubles.find("flee_speed_multiplier") != bb.doubles.end()) ? bb.doubles["flee_speed_multiplier"] : 1.5;
        const double energy_mul = (bb.doubles.find("flee_energy_multiplier") != bb.doubles.end()) ? bb.doubles["flee_energy_multiplier"] : 1.5;
        self.perform_step_move_to(safe_spot, world->config.world_width, world->config.world_height, base_mul * speed_mul, energy_mul);

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
        if (self.skip_movement) return Status::Failure; // 本 tick 不应移动

        const int world_width = world->config.world_width;
        const int world_height = world->config.world_height;

        // 若已有游荡目标，直接推进一步
        if (self.wander_target.has_value()) {
            const int ww = world->config.world_width;
            const int wh = world->config.world_height;
            const Position target = self.wander_target.value();
            // 游荡倍率：基础速度乘以游荡倍率；能耗可配置
            const double base_mul = (ctx.blackboard && ctx.blackboard->doubles.count("current_speed_multiplier")) ? ctx.blackboard->doubles["current_speed_multiplier"] : 1.0;
            const double speed_mul = (ctx.blackboard && ctx.blackboard->doubles.count("wander_speed_multiplier")) ? ctx.blackboard->doubles["wander_speed_multiplier"] : 0.8;
            const double energy_mul = (ctx.blackboard && ctx.blackboard->doubles.count("wander_energy_multiplier")) ? ctx.blackboard->doubles["wander_energy_multiplier"] : 0.6;
            self.perform_step_move_to(target, ww, wh, base_mul * speed_mul, energy_mul);
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
        self.perform_step_move_to(target2, ww2, wh2);
        const double arrival_threshold2 = std::max(0.2, self.current_step_distance * 0.5);
        if (self.position.distance_to(target2) <= arrival_threshold2) {
            self.wander_target.reset();
        }
        return Status::Running;
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
        if (self.skip_movement) {
            self.current_target.reset();
            self.planned_path.clear();
            self.planned_path_index = 0;
            self.wander_target.reset();
            self.mating_target.reset();
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
        self.skip_movement = false;
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